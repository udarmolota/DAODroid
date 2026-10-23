// The app's entry point (phase 5): DAORecomp's main.cpp as an SDL3 Android application.
// SDL's Java side starts this on its own thread, which becomes Xenia's UI thread: the
// window is SDL's, the presenter paints into its surface, gamepads and audio are SDL's,
// and the recompiled main thread runs on a thread of its own with a stack as deep as the
// guest's. No launcher, no mods, no ImGui overlays yet (app_stubs.cpp).
//
// Arguments from GameActivity: the install folder (game/ and userdata/ in it) and the
// options below; environment (DAO_*) as the headless build takes it.
//   --vulkan-driver=<abs path>   a Vulkan HAL driver (a Turnip build) through the private
//                                loader instead of the phone's own
//   --native-lib-dir=<dir>       the app's native library directory (libvkdriverhook.so)
#include "stdafx.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <android/log.h>
#include <csignal>
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <ucontext.h>
#include <unistd.h>

#include "apu/audio_output.h"
#include "gpu/video.h"
#include "gpu/xenia_host.h"
#include "xenia/gpu/d3d12/dao_frame_trace.h"
#include "input/input.h"
#include "kernel/function.h"
#include "kernel/game_build.h"
#include "kernel/game_exit.h"
#include "kernel/memory.h"
#include "kernel/module.h"
#include "kernel/thread.h"
#include "kernel/title_update.h"
#include "kernel/vfs.h"
#include "profiler.h"
#include "settings.h"
#include "text.h"
#include "ui/graphics_menu.h"
#include "ui/photo_mode.h"

#include "sdl_window.h"
#include "vulkan_private_loader.h"
#include "xenia_host_android.h"

using namespace dao::kernel;
namespace fs = std::filesystem;

// mftb: the 360 timebase runs at 49.875 MHz.
uint64_t PPCQueryTimebase() noexcept
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t ns = uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
    return (ns / 1000000000ull) * 49875000ull + (ns % 1000000000ull) * 49875000ull / 1000000000ull;
}

namespace dao
{
    void ReportStub(const char* name, PPCContext& ctx)
    {
        static std::mutex mutex;
        static std::unordered_map<std::string, uint32_t> counts;
        uint32_t count;
        {
            std::lock_guard lock(mutex);
            count = ++counts[name];
        }
        if (count <= 3 || log::TraceEnabled())
        {
            LOGW("Unimplemented import {} ({:08X}, {:08X}, {:08X}, {:08X}) lr {:08X}{}", name, ctx.r3.u32, ctx.r4.u32,
                ctx.r5.u32, ctx.r6.u32, uint32_t(ctx.lr), count == 3 ? " (further calls muted)" : "");
        }
    }
}

namespace
{
    // Indirect calls to addresses without a recompiled function land here.
    PPC_FUNC(UnknownIndirectCall)
    {
        LOGE("Indirect call to unrecompiled address {:08X} (lr {:08X})", ctx.ctr.u32, uint32_t(ctx.lr));
        _exit(2);
    }

    // The game library's load address, for offsets llvm-symbolizer can take against the
    // unstripped libdaodroid.so.
    uintptr_t LibraryBase()
    {
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(&LibraryBase), &info) && info.dli_fbase)
            return uintptr_t(info.dli_fbase);
        return 0;
    }

    // A fault: writes to pages the GPU watches are retried; anything else is logged, then
    // the default action (a tombstone).
    void CrashHandler(int signal, siginfo_t* info, void* uctx)
    {
        const auto* address = static_cast<const uint8_t*>(info ? info->si_addr : nullptr);
        if (signal == SIGSEGV && address && g_memory.IsGuest(address) &&
            dao::gpu::HandleWatchedWrite(const_cast<uint8_t*>(address), true))
            return;
        if (g_memory.IsGuest(address))
            LOGE("Guest access violation (signal {}) at guest {:08X}", signal, g_memory.ToGuest(address));
        else
            LOGE("Host fault (signal {}) at {:p}", signal, static_cast<const void*>(address));
#if defined(__aarch64__)
        if (uctx)
        {
            const auto& mc = static_cast<ucontext_t*>(uctx)->uc_mcontext;
            const uintptr_t base = LibraryBase();
            LOGE("  host pc +{:X} lr +{:X} (libdaodroid.so offsets); x0 {:X} x1 {:X} x19 {:X} x20 {:X}",
                uintptr_t(mc.pc) - base, uintptr_t(mc.regs[30]) - base, uintptr_t(mc.regs[0]),
                uintptr_t(mc.regs[1]), uintptr_t(mc.regs[19]), uintptr_t(mc.regs[20]));
            const auto* ctx = reinterpret_cast<const PPCContext*>(mc.regs[19]);
            if (ctx && (uintptr_t(ctx) & 0x3F) == 0 && !g_memory.IsGuest(reinterpret_cast<const uint8_t*>(ctx)))
                LOGE("  x19 as PPCContext: lr {:08X} ctr {:08X} r1 {:08X} r3 {:08X} r4 {:08X}", uint32_t(ctx->lr),
                    ctx->ctr.u32, ctx->r1.u32, ctx->r3.u32, ctx->r4.u32);
        }
#endif
        if (PPCContext* ctx = CurrentContext())
            LOGE("  guest lr {:08X} ctr {:08X} r1 {:08X} r3 {:08X}", uint32_t(ctx->lr), ctx->ctr.u32, ctx->r1.u32,
                ctx->r3.u32);
        if (GuestThread* thread = GuestThread::Current())
            LOGE("  thread {} last import {} lr {:08X}", thread->threadId, thread->lastImport ? thread->lastImport : "-",
                thread->lastImportLr);
        ::signal(signal, SIG_DFL);
        raise(signal);
    }

    struct Options
    {
        fs::path installRoot;
        std::string vulkanDriver;
        std::string nativeLibDir;
    };

    Options ParseOptions(int argc, char** argv)
    {
        Options options;
        for (int i = 1; i < argc; i++)
        {
            const std::string_view arg = argv[i];
            if (arg.starts_with("--vulkan-driver="))
                options.vulkanDriver = arg.substr(16);
            else if (arg.starts_with("--native-lib-dir="))
                options.nativeLibDir = arg.substr(17);
            else if (!arg.starts_with("--"))
                options.installRoot = fs::path(std::string(arg));
        }
        if (options.installRoot.empty())
        {
            if (const char* external = SDL_GetAndroidExternalStoragePath())
                options.installRoot = fs::path(external) / "dao";
        }
        return options;
    }

    // The guest's main thread on a host thread with a 16 MB stack (SDL's thread has the
    // Java default).
    void StartGuestMain()
    {
        ThreadParams params;
        params.stackSize = g_module.stackSize;
        params.startAddress = g_module.entryPoint;
        params.isMain = true;
        params.name = "Guest main";
        auto mainThread = GuestThread::Create(params);
        if (!mainThread)
        {
            LOGE("Failed to create the guest main thread");
            ExitGame(1);
        }
        auto* holder = new std::shared_ptr<GuestThread>(mainThread);
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 16 * 1024 * 1024);
        pthread_t thread;
        const int result = pthread_create(&thread, &attr, [](void* arg) -> void* {
            auto* held = static_cast<std::shared_ptr<GuestThread>*>(arg);
            pthread_setname_np(pthread_self(), "Guest main"); // else it inherits SDL's name
            const uint32_t code = (*held)->RunOnCurrentHostThread();
            LOGI("Main thread returned {:#x}", code);
            delete held;
            ExitGame(code);
        }, holder);
        pthread_attr_destroy(&attr);
        if (result != 0)
        {
            LOGE("Failed to start the guest main thread ({})", result);
            ExitGame(1);
        }
        pthread_detach(thread);
    }
}

int main(int argc, char* argv[])
{
    const Options options = ParseOptions(argc, argv);
    const fs::path gameRoot = options.installRoot / "game";
    const fs::path userRoot = options.installRoot / "userdata";
    std::error_code ec;
    fs::create_directories(userRoot, ec);
    dao::log::Init(userRoot / "dao.log");
    __android_log_print(ANDROID_LOG_INFO, "DAODroid", "install folder %s, log in userdata/dao.log",
        dao::Utf8(options.installRoot).c_str());

    LOGI("Dragon Age: Origins recomp (Android app) - game {}, user data {}", dao::Utf8(gameRoot), dao::Utf8(userRoot));
    LOGI("Host: {} byte pages", sysconf(_SC_PAGESIZE));
    if (!fs::exists(gameRoot / "default.xex", ec))
    {
        LOGE("default.xex not found under {}: put the game's files (DAORecomp's install/game) there", dao::Utf8(gameRoot));
        return 1;
    }

    struct sigaction sa{};
    sa.sa_sigaction = CrashHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE })
        sigaction(sig, &sa, nullptr);

    // SDL: the window (Vulkan, fullscreen), gamepads, audio. The event loop keeps running
    // in the background so the surface's loss reaches the presenter (RunUILoop).
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC))
    {
        LOGE("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }
    SDL_Window* sdlWindow = SDL_CreateWindow("Dragon Age: Origins", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
    if (!sdlWindow)
    {
        LOGE("SDL_CreateWindow failed: {}", SDL_GetError());
        return 1;
    }

    // DAO_FILL_SCREEN=1 (an experiment): the game is told a display as wide as the screen
    // (720 tall) and the presenter shows the frame at the screen's aspect ratio instead of
    // 16:9 with bars. The game keeps projecting at 16:9 (its buffers' shape), so the picture
    // is stretched; a true fill needs a wider render target in the engine.
    int screenWidth = 0, screenHeight = 0;
    SDL_GetWindowSizeInPixels(sdlWindow, &screenWidth, &screenHeight);
    bool fillScreen = false;
    if (const char* fill = getenv("DAO_FILL_SCREEN"); fill && strcmp(fill, "0") != 0 && screenWidth > 0 && screenHeight > 0)
    {
        if (screenWidth < screenHeight)
            std::swap(screenWidth, screenHeight); // SDL's first report can be portrait
        const uint32_t displayWidth = uint32_t((720 * screenWidth / screenHeight + 4) / 8 * 8);
        if (displayWidth > 1280)
        {
            dao::gpu::SetDisplayMode(displayWidth, 720);
            fillScreen = true;
        }
    }

    SelectBuild(gameRoot);
    if (!g_memory.Init())
    {
        LOGE("Failed to reserve the 4 GB guest address space");
        return 1;
    }
    PPCFunc** const slots = g_memory.FunctionSlot(g_build.info.codeBase);
    for (uint32_t i = 0; i < g_build.info.codeSize / 4; i++)
    {
        if (!slots[i])
            slots[i] = UnknownIndirectCall;
    }

    g_processBlock = KernelAlloc(0x60);
    VfsInit(gameRoot, userRoot);
    if (IsTitleUpdate())
    {
        VfsMountDevice("\\Device\\Update", UpdateRoot(gameRoot));
        VfsCreateSymlink("update:", "\\Device\\Update");
    }
    if (!LoadXex(gameRoot / "default.xex", IsTitleUpdate() ? UpdateRoot(gameRoot) / "default.xexp" : fs::path()))
        return 1;
    StartTimeStampThread();

    dao::LoadSettings(userRoot);
    dao::gpu::LoadXeniaLiveSettings();
    // The Options menu's Graphics tab is patched into the menu (and the menu movies made).
    dao::ui::InitializeGraphicsMenu();
    dao::ui::InitializePhotoMode();
    if (const char* audio = getenv("DAO_AUDIO"); !audio || strcmp(audio, "null") != 0)
        dao::apu::InitializeAudioOutput();
    dao::input::Initialize();

    // The GPU: Xenia's Vulkan backend painting into SDL's window; on another driver
    // through the private loader when one is given.
    dao::app::SdlWindowedAppContext appContext;
    dao::gpu::android::WindowHost windowHost;
    windowHost.appContext = &appContext;
    windowHost.createWindow = [&appContext, sdlWindow] {
        return std::unique_ptr<xe::ui::Window>(new dao::app::SdlWindow(appContext, sdlWindow));
    };
    if (fillScreen)
    {
        windowHost.aspectWidth = uint32_t(screenWidth);
        windowHost.aspectHeight = uint32_t(screenHeight);
    }
    if (!options.vulkanDriver.empty())
    {
        windowHost.loaderLibrary = dao_open_private_vulkan_loader(options.vulkanDriver.c_str(), options.nativeLibDir.c_str());
        LOGI("Vulkan driver {}: {}", options.vulkanDriver, windowHost.loaderLibrary ? "through the private loader" : "not usable, the phone's own driver runs the game");
    }
    dao::gpu::android::SetWindowHost(windowHost);
    const char* gpuMode = getenv("DAO_GPU");
    if (!gpuMode || strcmp(gpuMode, "null") != 0)
        dao::gpu::InitializeXeniaGraphics(userRoot);
    else
        LOGI("DAO_GPU=null: null command processor");

    // DAO_PROFILE=<seconds>: the in-process sampling profiler, a report every N seconds.
    if (const char* profile = getenv("DAO_PROFILE"))
        dao::profiler::Start(std::max(1, atoi(profile)));
    // DAO_DUMP_AFTER=<seconds>: periodically log what every guest thread is doing.
    if (const char* dump = getenv("DAO_DUMP_AFTER"))
    {
        const int seconds = std::max(1, atoi(dump));
        std::thread([seconds] {
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(seconds));
                LOGI("---- thread dump ----");
                DumpThreads();
            }
        }).detach();
    }
    // DAO_CAPTURE_EVERY=<seconds>: save the presented frame to userdata/capture_N.png.
    if (const char* every = getenv("DAO_CAPTURE_EVERY"))
    {
        const int seconds = std::max(1, atoi(every));
        std::thread([seconds, userRoot] {
            for (int n = 1;; n++)
            {
                std::this_thread::sleep_for(std::chrono::seconds(seconds));
                const auto path = userRoot / fmt::format("capture_{}.png", n);
                // With the draws of a recent frame: submitted by the game, skipped by
                // VIZ queries, failed in the backend, issued to the GPU.
                xe::gpu::dao_trace::FrameStats stats;
                const uint32_t swaps = xe::gpu::dao_trace::SwapCount();
                const bool haveStats = swaps >= 4 && xe::gpu::dao_trace::GetFrameStats(swaps - 3, stats);
                LOGI("Capture {}: {} (swap {}: {} draws submitted, {} viz-skipped, {} failed, {} issued)",
                    dao::Utf8(path), dao::gpu::CaptureFrame(path) ? "ok" : "failed", haveStats ? stats.swap : 0,
                    stats.submitted, stats.viz_skipped, stats.failed, stats.issued);
            }
        }).detach();
    }

    StartGuestMain();

    // This thread is the UI thread for the rest of the run.
    const dao::gpu::XeniaWindow host = dao::gpu::GetXeniaWindow();
    appContext.RunUILoop(sdlWindow, static_cast<dao::app::SdlWindow*>(host.window));
    // Gone at once: the game's threads don't need a clean shutdown, and the activity is
    // finished by the process ending.
    LOGI("UI loop ended, exiting");
    dao::kernel::ExitGame(0);
}
