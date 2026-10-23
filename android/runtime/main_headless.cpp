// Headless Android entry point (phases 3-4): DAORecomp's main.cpp without the launcher,
// the game window, mods, the UI overlays and the Windows diagnostics. It brings the kernel
// up, loads default.xex and runs the guest's main thread with Xenia's Vulkan GPU drawing
// into off-screen images (xenia_host_vulkan.cpp), or the null command processor, so the
// game can be exercised on the phone from adb.
//
//   daodroid_headless <install folder with game/ and userdata/>   (or DAO_GAME=<game folder>)
//   DAO_GPU=null            no Vulkan: the null command processor
//   DAO_CAPTURE_EVERY=<s>   save the presented frame to userdata/capture_N.png every <s> seconds
//   DAO_DUMP_AFTER=<s>      periodic thread dumps     DAO_AUDIO=null  no XMA output (decoding stays)
#include "stdafx.h"

#include "apu/audio_output.h"
#include "gpu/xenia_host.h"
#include "ui/graphics_menu.h"
#include "xenia/gpu/d3d12/dao_frame_trace.h"
#include "input/input.h"
#include "kernel/function.h"
#include "kernel/game_build.h"
#include "kernel/memory.h"
#include "kernel/module.h"
#include "kernel/thread.h"
#include "kernel/title_update.h"
#include "kernel/vfs.h"
#include "settings.h"
#include "text.h"

#include <csignal>
#include <fstream>
#include <sstream>
#include <link.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>

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

// Indirect calls to addresses without a recompiled function land here.
static PPC_FUNC(UnknownIndirectCall)
{
    LOGE("Indirect call to unrecompiled address {:08X} (lr {:08X})", ctx.ctr.u32, uint32_t(ctx.lr));
    _exit(2);
}

// The executable's load address, for offsets llvm-symbolizer can take against the unstripped file.
static uintptr_t ExeBase()
{
    uintptr_t base = 0;
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void* out) {
        *static_cast<uintptr_t*>(out) = uintptr_t(info->dlpi_addr);
        return 1;  // the first entry is the executable
    }, &base);
    return base;
}

// A fault: log where the guest was, then die with the default action (a tombstone).
static void CrashHandler(int signal, siginfo_t* info, void* uctx)
{
    const auto* address = static_cast<const uint8_t*>(info ? info->si_addr : nullptr);
    // Writes to physical pages the GPU watches (to invalidate its copies): the page is
    // made writable again and the write retried. (Reads never fault on them.)
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
        const uintptr_t base = ExeBase();
        LOGE("  host pc +{:X} lr +{:X} (exe offsets); x0 {:X} x1 {:X} x19 {:X} x20 {:X} x21 {:X} x22 {:X}",
            uintptr_t(mc.pc) - base, uintptr_t(mc.regs[30]) - base, uintptr_t(mc.regs[0]), uintptr_t(mc.regs[1]),
            uintptr_t(mc.regs[19]), uintptr_t(mc.regs[20]), uintptr_t(mc.regs[21]), uintptr_t(mc.regs[22]));
        // Generated functions keep ctx in x19 and base in x20 after their prologue.
        const auto* ctx = reinterpret_cast<const PPCContext*>(mc.regs[19]);
        if (ctx && (uintptr_t(ctx) & 0x3F) == 0 && !g_memory.IsGuest(reinterpret_cast<const uint8_t*>(ctx)))
            LOGE("  x19 as PPCContext: lr {:08X} ctr {:08X} r1 {:08X} r3 {:08X} r4 {:08X} r5 {:08X} r31 {:08X}",
                uint32_t(ctx->lr), ctx->ctr.u32, ctx->r1.u32, ctx->r3.u32, ctx->r4.u32, ctx->r5.u32, ctx->r31.u32);
    }
#endif
    if (PPCContext* ctx = CurrentContext())
    {
        LOGE("  guest lr {:08X} ctr {:08X} r1 {:08X} r3 {:08X} r4 {:08X} r5 {:08X} r31 {:08X}", uint32_t(ctx->lr),
            ctx->ctr.u32, ctx->r1.u32, ctx->r3.u32, ctx->r4.u32, ctx->r5.u32, ctx->r31.u32);
    }
    if (GuestThread* thread = GuestThread::Current())
        LOGE("  thread {} last import {} lr {:08X}", thread->threadId, thread->lastImport ? thread->lastImport : "-",
            thread->lastImportLr);
    ::signal(signal, SIG_DFL);
    raise(signal);
}

static fs::path FindGameRoot(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
        if (strncmp(argv[i], "--", 2) != 0)
        {
            const fs::path given = argv[i];
            return fs::exists(given / "game" / "default.xex") ? given / "game" : given;
        }
    if (const char* env = getenv("DAO_GAME"))
        return fs::path(env);
    return fs::current_path() / "game";
}

int main(int argc, char** argv)
{
    fs::path gameRoot = fs::absolute(FindGameRoot(argc, argv)).lexically_normal();
    if (!gameRoot.has_filename())
        gameRoot = gameRoot.parent_path();
    const fs::path userRoot = gameRoot.parent_path() / "userdata";
    fs::create_directories(userRoot);
    dao::log::Init(userRoot / "dao.log");

    LOGI("Dragon Age: Origins recomp (headless Android) - game {}, user data {}", dao::Utf8(gameRoot),
        dao::Utf8(userRoot));
    LOGI("Host: {} byte pages", sysconf(_SC_PAGESIZE));
    if (!fs::exists(gameRoot / "default.xex"))
    {
        LOGE("default.xex not found under {}", dao::Utf8(gameRoot));
        return 1;
    }

    struct sigaction sa{};
    sa.sa_sigaction = CrashHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE })
        sigaction(sig, &sa, nullptr);

    SelectBuild(gameRoot);
    if (!g_memory.Init())
    {
        LOGE("Failed to reserve the 4 GB guest address space");
        return 1;
    }

    // Every code address without a recompiled function gets a reporting trampoline.
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
    // The Options menu's Graphics tab is patched into the menu; makes userdata/generated.
    dao::ui::InitializeGraphicsMenu();
    if (const char* audio = getenv("DAO_AUDIO"); !audio || strcmp(audio, "null") != 0)
        dao::apu::InitializeAudioOutput();
    dao::input::Initialize();

    // DAO_GPU=null runs without rendering (null command processor).
    const char* gpuMode = getenv("DAO_GPU");
    if (!gpuMode || strcmp(gpuMode, "null") != 0)
        dao::gpu::InitializeXeniaGraphics(userRoot);
    else
        LOGI("DAO_GPU=null: null command processor");

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

    // DAO_DUMP_MEM=<hex addr>:<hex len>[,...]@<seconds>: a frame capture and the guest
    // memory ranges (read back from the GPU first, where it wrote them) to userdata/.
    if (const char* spec = getenv("DAO_DUMP_MEM"))
    {
        std::string text = spec;
        const size_t at = text.find('@');
        const int seconds = at != std::string::npos ? atoi(text.c_str() + at + 1) : 10;
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        std::stringstream list(text.substr(0, at));
        for (std::string item; std::getline(list, item, ',');)
        {
            const size_t colon = item.find(':');
            if (colon != std::string::npos)
                ranges.emplace_back(uint32_t(strtoul(item.c_str(), nullptr, 16)),
                    uint32_t(strtoul(item.c_str() + colon + 1, nullptr, 16)));
        }
        std::thread([=] {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            LOGI("Memory dump capture: {}", dao::gpu::CaptureFrame(userRoot / "mem_capture.png") ? "ok" : "failed");
            for (auto [address, length] : ranges)
            {
                const uint32_t physicalView = 0xA0000000 + (address & 0x1FFFFFFF);
                const bool fromGpu = dao::gpu::ReadBackGpuWrittenMemory(physicalView, length, 2);
                std::ofstream file(userRoot / fmt::format("mem_{:08X}.bin", address), std::ios::binary);
                file.write(reinterpret_cast<const char*>(g_memory.Translate(physicalView)), length);
                LOGI("Dumped {:08X} +{:X} ({})", address, length, fromGpu ? "read back from the GPU" : "guest memory");
            }
        }).detach();
    }

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

    ThreadParams params;
    params.stackSize = g_module.stackSize;
    params.startAddress = g_module.entryPoint;
    params.isMain = true;
    params.name = "Guest main";
    auto mainThread = GuestThread::Create(params);
    if (!mainThread)
        return 1;

    const uint32_t code = mainThread->RunOnCurrentHostThread();
    LOGI("Main thread returned {:#x}", code);
    return int(code);
}
