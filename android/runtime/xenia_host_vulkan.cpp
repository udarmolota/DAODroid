// Host side of the Xenia GPU integration on Android (phase 4): DAORecomp's
// gpu/xenia_host.cpp with Xenia's Vulkan graphics system instead of Direct3D 12, and
// no window of its own. Headless, the presenter has no surface: the game's frames
// are drawn into the guest output images, which CaptureFrame reads back (the first
// picture over adb); the app's surface is connected in phase 5. The Direct3D 12
// build's extras (temporal upscaling, HDR, frame generation, low latency, resolves
// into textures, GPU memory read-back) have no Vulkan counterpart yet.
#include "stdafx.h"

#include <condition_variable>

#include "gpu/xenia_host.h"
#include "kernel/memory.h"
#include "kernel/module.h"
#include "kernel/thread.h"
#include "kernel/types.h"
#include "settings.h"
#include "text.h"

#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/dao_host.h"
#include "xenia/dao_live_settings.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/gpu/xenos.h"
#include "xenia/memory.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/vulkan/dao_vulkan_icd.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"

#include "xenia_host_android.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "third_party/stb/stb_image_write.h"

DECLARE_int32(anisotropic_override);

using namespace dao::kernel;

namespace dao::gpu
{
    namespace
    {
        std::unique_ptr<xe::Memory> s_memory;
        std::unique_ptr<xe::gpu::vulkan::VulkanGraphicsSystem> s_graphics;
        std::recursive_mutex s_interruptMutex;
        // The app's window (android::SetWindowHost); none headless.
        android::WindowHost s_windowHost;
        std::unique_ptr<xe::ui::Window> s_window;

        // How the guest output is scaled to the surface: the upscaler and its sharpness.
        xe::ui::Presenter::GuestOutputPaintConfig OutputPaintConfig(Upscaler upscaler, float sharpness)
        {
            using PaintConfig = xe::ui::Presenter::GuestOutputPaintConfig;
            PaintConfig config;
            switch (upscaler)
            {
            case Upscaler::Bilinear:
                config.SetEffect(PaintConfig::Effect::kBilinear);
                break;
            case Upscaler::Cas:
                config.SetEffect(PaintConfig::Effect::kCas);
                config.SetCasAdditionalSharpness(sharpness);
                break;
            case Upscaler::Fsr:
                config.SetEffect(PaintConfig::Effect::kFsr);
                config.SetFsrSharpnessReduction(sharpness);
                break;
            }
            config.SetDither(true);
            return config;
        }

        void WritePngFile(void* context, void* data, int size)
        {
            fwrite(data, 1, size_t(size), static_cast<FILE*>(context));
        }

        bool SavePng(const std::filesystem::path& path, const uint8_t* rgb, uint32_t width, uint32_t height)
        {
            FILE* file = fopen(Utf8(path).c_str(), "wb");
            if (!file)
                return false;
            const int ok = stbi_write_png_to_func(WritePngFile, file, int(width), int(height), 3, rgb, int(width) * 3);
            fclose(file);
            return ok != 0;
        }
    }

    namespace android
    {
        void SetWindowHost(const WindowHost& host)
        {
            s_windowHost = host;
        }
    }

    void LoadXeniaLiveSettings()
    {
        // Of the Direct3D 12 build's live settings only anisotropic filtering applies.
        xe::dao_live::values.anisotropic_override = cvars::anisotropic_override;
    }

    void XeniaLowLatencyFrameStart() {}
    void XeniaLowLatencyFrameSwapped() {}
    bool XeniaLowLatencyNeedsRestart() { return false; }

    bool InitializeXeniaGraphics(const std::filesystem::path& userRoot)
    {
        // On Android Xenia's log goes to logcat (tag dao-xenia), not a file.
        xe::InitializeLogging("dao-xenia");

        if (s_windowHost.loaderLibrary)
            xe::ui::vulkan::dao_icd::SetLoaderLibrary(s_windowHost.loaderLibrary);

        s_memory = std::make_unique<xe::Memory>();
        s_graphics = std::make_unique<xe::gpu::vulkan::VulkanGraphicsSystem>();
        // With the app's window: its app context (the SDL thread is the UI thread) makes
        // the presenter there; headless: no app context, the presenter is created on this
        // thread, without a surface.
        if (s_graphics->Setup(s_memory.get(), s_windowHost.appContext, true) != 0)
        {
            LOGE("Failed to set up the Vulkan graphics system (see logcat, tag dao-xenia)");
            s_graphics.reset();
            s_memory.reset();
            return false;
        }
        if (s_windowHost.appContext && s_windowHost.createWindow)
        {
            bool opened = false;
            s_windowHost.appContext->CallInUIThreadSynchronous([&opened] {
                s_window = s_windowHost.createWindow();
                opened = s_window && s_window->Open();
                if (opened)
                    s_window->SetPresenter(s_graphics->presenter());
            });
            if (!opened)
            {
                LOGE("Failed to open the game window");
                s_window.reset();
                s_graphics.reset();
                s_memory.reset();
                return false;
            }
        }
        if (s_windowHost.aspectWidth && s_windowHost.aspectHeight)
            s_graphics->SetScaledAspectRatio(s_windowHost.aspectWidth, s_windowHost.aspectHeight);
        // The shaders and pipelines of earlier runs, translated and created in the
        // background while the game starts; new ones are added as they're seen.
        s_graphics->InitializeShaderStorage(userRoot / "shader_cache", g_module.titleId, false);
        if (xe::ui::Presenter* presenter = s_graphics->presenter())
        {
            const Settings& settings = GetSettings();
            presenter->SetGuestOutputPaintConfigFromUIThread(OutputPaintConfig(settings.upscaler, settings.sharpness));
        }

        // Registers the game reads directly (the rest are only written).
        *g_memory.Translate<u32>(0x7FC80000 + 0x1951 * 4) = 1; // interrupt status: vblank

        LOGI("Xenia Vulkan graphics initialized ({})", s_graphics->name());
        return true;
    }

    bool XeniaActive()
    {
        return s_graphics != nullptr;
    }

    XeniaWindow GetXeniaWindow()
    {
        if (!s_graphics || !s_graphics->presenter())
            return {};
        return { s_window.get(), s_windowHost.appContext, s_graphics->presenter(), s_graphics->provider() };
    }

    void XeniaSetFullscreen(bool) {}

    void XeniaSetOutputScaling(Upscaler upscaler, float sharpness)
    {
        if (!s_graphics || !s_graphics->presenter())
            return;
        const auto apply = [upscaler, sharpness] {
            s_graphics->presenter()->SetGuestOutputPaintConfigFromUIThread(OutputPaintConfig(upscaler, sharpness));
        };
        if (s_windowHost.appContext)
            s_windowHost.appContext->CallInUIThread(apply);
        else
            apply();
    }

    void XeniaInitializeRingBuffer(uint32_t physicalAddress, uint32_t sizeLog2)
    {
        s_graphics->InitializeRingBuffer(physicalAddress, sizeLog2);
    }

    void XeniaEnableReadPointerWriteBack(uint32_t address, uint32_t blockSizeLog2)
    {
        s_graphics->EnableReadPointerWriteBack(address, blockSizeLog2);
    }

    void XeniaSetInterruptCallback(uint32_t callback, uint32_t userData)
    {
        s_graphics->SetInterruptCallback(callback, userData);
    }

    void XeniaWriteRegister(uint32_t address, uint32_t value)
    {
        s_graphics->WriteRegister(address, value);
    }

    bool ReadBackGpuWrittenMemory(uint32_t address, uint32_t length, uint32_t pixelSizeLog2)
    {
        if (!s_graphics || !IsPhysicalAddress(address) || !length)
            return false;
        // Within the 512 MB of physical memory (the 0xE view is offset by a page).
        constexpr uint32_t kPhysicalSize = 0x20000000;
        const uint32_t physical = PhysicalAddressOf(address);
        if (physical >= kPhysicalSize || length > kPhysicalSize - physical)
            return false;
        auto* processor = static_cast<xe::gpu::vulkan::VulkanCommandProcessor*>(s_graphics->command_processor());
        // Shared with the GPU thread, which may finish after this has given up waiting.
        struct Readback
        {
            std::mutex mutex;
            std::condition_variable cv;
            bool done = false;
            bool read = false;
            std::vector<uint8_t> data;
        };
        const auto readback = std::make_shared<Readback>();
        processor->CallInThread([readback, processor, physical, length, pixelSizeLog2] {
            std::vector<uint8_t> data;
            const bool read = processor->DaoReadGpuWrittenMemory(physical, length, pixelSizeLog2, data);
            std::lock_guard lock(readback->mutex);
            readback->read = read;
            readback->data = std::move(data);
            readback->done = true;
            readback->cv.notify_all();
        });
        // A GPU that has stopped leaves the caller, a game thread, without the data
        // rather than stuck. A readback takes milliseconds; 5 s is only reached if
        // the GPU thread is gone or hung.
        constexpr auto kReadbackTimeout = std::chrono::seconds(5);
        std::unique_lock lock(readback->mutex);
        if (!readback->cv.wait_for(lock, kReadbackTimeout, [&] { return readback->done; }))
        {
            LOGW("GPU: reading back {:08X} (+{}) took too long", address, length);
            return false;
        }
        const std::vector<uint8_t>& data = readback->data;
        if (!readback->read || data.size() > length)
            return false;
        // Through the write watches, so the GPU's copies are reloaded from it.
        PrepareGuestWrite(address, uint32_t(data.size()));
        memcpy(g_memory.Translate<uint8_t>(address), data.data(), data.size());
        return true;
    }

    bool XeniaWantsSingleSampleScene()
    {
        return false;
    }

    void XeniaWriteSwapPacket(uint32_t buffer, uint32_t fetchConstant, uint32_t frontBufferVirtual,
        uint32_t width, uint32_t height)
    {
        namespace xenos = xe::gpu::xenos;

        // The fetch constant comes from the D3D texture header, which holds a virtual
        // address; the GPU wants the physical one.
        xenos::xe_gpu_texture_fetch_t fetch;
        xe::copy_and_swap_32_unaligned(&fetch, g_memory.Translate<uint32_t>(fetchConstant), 6);
        const uint32_t frontBufferPhysical = PhysicalAddressOf(frontBufferVirtual);
        fetch.base_address = frontBufferPhysical >> 12;

        auto* dwords = g_memory.Translate<xe::be<uint32_t>>(buffer);
        uint32_t offset = 0;
        dwords[offset++] = xenos::MakePacketType0(xe::gpu::XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0, 6);
        dwords[offset++] = fetch.dword_0;
        dwords[offset++] = fetch.dword_1;
        dwords[offset++] = fetch.dword_2;
        dwords[offset++] = fetch.dword_3;
        dwords[offset++] = fetch.dword_4;
        dwords[offset++] = fetch.dword_5;
        dwords[offset++] = xenos::MakePacketType3(xenos::PM4_XE_SWAP, 4);
        dwords[offset++] = xe::gpu::xenos::kSwapSignature;
        dwords[offset++] = frontBufferPhysical;
        dwords[offset++] = width;
        dwords[offset++] = height;
        while (offset < 64)
            dwords[offset++] = xenos::MakePacketType2();
    }

    bool CaptureFrame(const std::filesystem::path& path, double aspect, bool sdr)
    {
        if (!s_graphics || !s_graphics->presenter())
            return false;
        xe::ui::RawImage image;
        if (!s_graphics->presenter()->CaptureGuestOutput(image, sdr) || !image.width || !image.height)
            return false;
        // The middle of it at `aspect`.
        const auto [frameWidth, frameHeight] = FrameSize(image.width, image.height, aspect);
        const uint32_t width = std::clamp(uint32_t(std::lround(frameWidth)), 1u, image.width);
        const uint32_t height = std::clamp(uint32_t(std::lround(frameHeight)), 1u, image.height);
        const uint32_t left = (image.width - width) / 2, top = (image.height - height) / 2;
        // RGBX -> RGB
        std::vector<uint8_t> rgb(size_t(width) * height * 3);
        for (uint32_t y = 0; y < height; y++)
        {
            const uint8_t* row = image.data.data() + size_t(top + y) * image.stride + size_t(left) * 4;
            for (uint32_t x = 0; x < width; x++)
                memcpy(&rgb[(size_t(y) * width + x) * 3], row + x * 4, 3);
        }
        return SavePng(path, rgb.data(), width, height);
    }

    std::pair<double, double> FrameSize(double width, double height, double aspect)
    {
        if (aspect <= 0)
            return { width, height };
        return { std::min(width, height * aspect), std::min(height, width / aspect) };
    }

    uint64_t HashFrame()
    {
        if (!s_graphics || !s_graphics->presenter())
            return 0;
        xe::ui::RawImage image;
        if (!s_graphics->presenter()->CaptureGuestOutput(image) || !image.width)
            return 0;
        return std::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char*>(image.data.data()), image.data.size()));
    }

    bool CaptureHalfRes(std::vector<uint8_t>& rgb, uint32_t& width, uint32_t& height)
    {
        if (!s_graphics || !s_graphics->presenter())
            return false;
        xe::ui::RawImage image;
        if (!s_graphics->presenter()->CaptureGuestOutput(image) || image.width < 2 || image.height < 2)
            return false;
        width = image.width / 2;
        height = image.height / 2;
        rgb.resize(size_t(width) * height * 3);
        for (uint32_t y = 0; y < height; y++)
        {
            const uint8_t* row0 = image.data.data() + size_t(y * 2) * image.stride;
            const uint8_t* row1 = row0 + image.stride;
            uint8_t* out = &rgb[size_t(y) * width * 3];
            for (uint32_t x = 0; x < width; x++, out += 3)
            {
                for (int c = 0; c < 3; c++)
                    out[c] = uint8_t((row0[x * 8 + c] + row0[x * 8 + 4 + c] + row1[x * 8 + c] + row1[x * 8 + 4 + c] + 2) / 4);
            }
        }
        return true;
    }

    bool WritePng(const std::filesystem::path& path, const uint8_t* rgb, uint32_t width, uint32_t height)
    {
        return SavePng(path, rgb, width, height);
    }

    void CallInterruptCallback(uint32_t callback, uint32_t userData, uint32_t source, uint32_t cpu)
    {
        // Interrupts are serialised (the console delivers them one at a time) and run
        // with the PCR claiming the target hardware thread, as a DPC: at DISPATCH_LEVEL
        // with dpc_active set, as on the console (and Xenia's EmulateCPInterruptDPC),
        // for guest code that reads the IRQL or KeIsExecutingDpc. (Never nested, being
        // serialised: dpc_active is back to 0 after.)
        std::lock_guard lock(s_interruptMutex);
        GuestThread* thread = GuestThread::Current();
        if (!thread)
            thread = GuestThread::AttachHostThread("GPU interrupt");
        if (cpu > 5)
            cpu = 2;
        *g_memory.Translate<uint8_t>(thread->pcr + kPcrCurrentCpu) = uint8_t(cpu);
        uint8_t& irql = *g_memory.Translate<uint8_t>(thread->pcr + kPcrIrql);
        u32& dpcActive = *g_memory.Translate<u32>(thread->pcr + kPcrDpcActive);
        const uint8_t oldIrql = irql;
        irql = kDispatchLevel;
        dpcActive = 1;
        CallGuest(callback, { source, userData });
        dpcActive = 0;
        irql = oldIrql;
    }
}

namespace xe::dao_host
{
    void AttachGuestThread(const char* name)
    {
        dao::kernel::GuestThread::AttachHostThread(name);
    }

    void DispatchInterrupt(uint32_t callback, uint32_t user_data, uint32_t source, uint32_t cpu)
    {
        dao::gpu::CallInterruptCallback(callback, user_data, source, cpu);
    }

    uint32_t TitleId()
    {
        return dao::kernel::g_module.titleId;
    }

    void ShortSleep(uint32_t microseconds)
    {
        dao::kernel::ShortSleep(microseconds);
    }
}
