// Phase 2 harness: runs leaf functions of the recompiled game (no memory, no calls) on a
// deterministic PPCContext and prints a hash of the resulting context per function and
// seed. Built for x86-64 Windows (against DAORecomp's own DAORecompLib) and for ARM64 Android
// (against the probe's libdaoppc.a); identical output on both means the ARM64 build of the
// generated code computes the same thing as the x64 build that plays the game.
//
// Usage: daoppc_harness [seeds=4] [--verbose]
// Output lines: "<function> <seed> <hash>" and a final "TOTAL <n functions> <combined hash>".
#include "ppc_recomp_shared.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// mmio_hook.h (force-included into the generated code) declares this; leaf functions never
// store to the GPU window, so it is never called.
void DaoGpuRegisterWrite(uint32_t, uint32_t) { std::abort(); }
// The time base (mftb); the runtime's counter in the game. Fixed here: the output must not
// depend on the clock.
uint64_t PPCQueryTimebase() noexcept { return 0x123456789ull; }

struct LeafFunc
{
    const char* name;
    PPCFunc* fn;
    int instructions;
    int vmx;
    int memory;  // touches guest memory: runs only with a mapped guest space (--memory)
};

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <sys/mman.h>
#endif

// A zeroed 4 GiB guest address space, so that functions reading and writing through `base`
// see the same (zero, then their own) contents on both hosts. Pages are backed on first
// touch (MAP_NORESERVE / Windows demand-zero commit).
static uint8_t* MapGuestSpace()
{
    const size_t size = 1ull << 32;
#ifdef _WIN32
    void* p = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return static_cast<uint8_t*>(p);
#else
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? nullptr : static_cast<uint8_t*>(p);
#endif
}

#include "leaf_funcs.inc"
// Every object of the generated code references the kernel imports; the runtime provides
// them in the game, these stubs do here (they are never reached by leaf functions).
#include "import_stubs.inc"

static uint64_t Fnv1a(const void* data, size_t size, uint64_t h = 0xcbf29ce484222325ull)
{
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++)
    {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

struct XorShift
{
    uint64_t s;
    uint64_t next()
    {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return s;
    }
};

// Fills the context with a deterministic pattern. Registers get a mix of plain random bits,
// small integers, and floats in sane ranges, so that integer and FP paths both see
// representative values; the vector registers get random bytes.
static void Fill(PPCContext& ctx, uint64_t seed)
{
    XorShift rng{ seed * 0x9E3779B97F4A7C15ull + 0x1234567ull };
    std::memset(&ctx, 0, sizeof(ctx));
    PPCRegister* gprs[] = { &ctx.r0, &ctx.r1, &ctx.r2, &ctx.r3, &ctx.r4, &ctx.r5, &ctx.r6, &ctx.r7, &ctx.r8, &ctx.r9,
        &ctx.r10, &ctx.r11, &ctx.r12, &ctx.r13, &ctx.r14, &ctx.r15, &ctx.r16, &ctx.r17, &ctx.r18, &ctx.r19,
        &ctx.r20, &ctx.r21, &ctx.r22, &ctx.r23, &ctx.r24, &ctx.r25, &ctx.r26, &ctx.r27, &ctx.r28, &ctx.r29,
        &ctx.r30, &ctx.r31 };
    for (PPCRegister* r : gprs)
    {
        const uint64_t v = rng.next();
        switch (v & 3)
        {
        case 0: r->u64 = v >> 2; break;
        case 1: r->u64 = (v >> 40) & 0xFFFF; break;      // small positive
        case 2: r->s64 = -int64_t((v >> 44) & 0xFFF); break; // small negative
        default: r->u64 = uint32_t(v >> 8); break;       // 32-bit
        }
    }
    PPCRegister* fprs[] = { &ctx.f0, &ctx.f1, &ctx.f2, &ctx.f3, &ctx.f4, &ctx.f5, &ctx.f6, &ctx.f7, &ctx.f8, &ctx.f9,
        &ctx.f10, &ctx.f11, &ctx.f12, &ctx.f13, &ctx.f14, &ctx.f15, &ctx.f16, &ctx.f17, &ctx.f18, &ctx.f19,
        &ctx.f20, &ctx.f21, &ctx.f22, &ctx.f23, &ctx.f24, &ctx.f25, &ctx.f26, &ctx.f27, &ctx.f28, &ctx.f29,
        &ctx.f30, &ctx.f31 };
    for (PPCRegister* r : fprs)
    {
        const uint64_t v = rng.next();
        // Doubles in about [-1000, 1000] with a fraction, occasionally 0 or a whole number.
        const double d = (double(int64_t(v >> 20) % 2000000) / 1000.0) - 1000.0;
        r->f64 = (v & 7) == 0 ? 0.0 : (v & 7) == 1 ? double(int(d)) : d;
    }
    PPCVRegister* vrs = &ctx.v0;
    for (int i = 0; i < 128; i++)
    {
        for (int j = 0; j < 2; j++)
            vrs[i].u64[j] = rng.next();
        // Every other vector: four floats in [-100, 100] (otherwise random bits are NaNs).
        if (i & 1)
            for (int j = 0; j < 4; j++)
                vrs[i].f32[j] = float(int64_t(rng.next() >> 20) % 200000) / 1000.0f - 100.0f;
    }
    ctx.lr = 0x82000000 + (rng.next() & 0xFFFFF0);
    ctx.ctr.u64 = rng.next() & 0xFF;
    ctx.xer.so = 0;
    ctx.xer.ov = 0;
    ctx.xer.ca = uint8_t(rng.next() & 1);
    ctx.msr = 0x200A000;
    // The host's own FP control word: it differs between x86 (MXCSR) and ARM64 (FPCR), so the
    // fpscr field is left out of the hash below.
    ctx.fpscr.csr = ctx.fpscr.getcsr();
}

static uint64_t HashContext(const PPCContext& ctx)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(&ctx);
    const size_t fpscrAt = reinterpret_cast<const uint8_t*>(&ctx.fpscr) - bytes;
    uint64_t h = Fnv1a(bytes, fpscrAt);
    return Fnv1a(bytes + fpscrAt + sizeof(ctx.fpscr), sizeof(ctx) - fpscrAt - sizeof(ctx.fpscr), h);
}

// The same hash with every NaN replaced by the positive quiet NaN. x86 makes its default NaN
// negative (0xFFF8...), ARM64 and the PowerPC positive (0x7FF8...); the sign of a NaN means
// nothing to the game, so a mismatch only in the strict hash is this convention, nothing else.
// (A vector lane holding an integer that looks like a NaN is canonicalised too: a difference
// hidden that way shows in the strict hash and is checked by hand.)
static uint64_t HashContextCanonicalNaN(const PPCContext& ctx)
{
    alignas(64) static PPCContext copy;
    std::memcpy(&copy, &ctx, sizeof(copy));
    PPCRegister* f = &copy.f0;
    for (int k = 0; k < 32; k++)
        if ((f[k].u64 & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (f[k].u64 & 0x000FFFFFFFFFFFFFull))
            f[k].u64 = 0x7FF8000000000000ull;
    PPCVRegister* v = &copy.v0;
    for (int k = 0; k < 128; k++)
        for (int j = 0; j < 4; j++)
            if ((v[k].u32[j] & 0x7F800000u) == 0x7F800000u && (v[k].u32[j] & 0x007FFFFFu))
                v[k].u32[j] = 0x7FC00000u;
    return HashContext(copy);
}

// Returns the guest space to all zeros between functions, so that no function sees what an
// earlier one wrote (NaN signs would otherwise leak from one host's results into the next
// function's inputs). Only touched pages cost anything.
static void ResetGuestSpace(uint8_t* base)
{
    const size_t size = 1ull << 32;
#ifdef _WIN32
    VirtualFree(base, size, MEM_DECOMMIT);
    VirtualAlloc(base, size, MEM_COMMIT, PAGE_READWRITE);
#else
    madvise(base, size, MADV_DONTNEED);
#endif
}

// Runs one function, catching a crash (a store past the 4 GiB space, a trap): both hosts then
// report "CRASH" for that seed instead of dying, and the run continues.
#ifdef _WIN32
static bool RunOne(PPCFunc* fn, PPCContext& ctx, uint8_t* base)
{
    __try { fn(ctx, base); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#else
#include <csignal>
#include <csetjmp>
static sigjmp_buf g_crashJump;
static void OnCrash(int) { siglongjmp(g_crashJump, 1); }
static bool RunOne(PPCFunc* fn, PPCContext& ctx, uint8_t* base)
{
    struct sigaction sa{}, oldSegv{}, oldBus{}, oldFpe{}, oldIll{};
    sa.sa_handler = OnCrash;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;
    sigaction(SIGSEGV, &sa, &oldSegv); sigaction(SIGBUS, &sa, &oldBus);
    sigaction(SIGFPE, &sa, &oldFpe); sigaction(SIGILL, &sa, &oldIll);
    bool ok;
    if (sigsetjmp(g_crashJump, 1) == 0) { fn(ctx, base); ok = true; }
    else ok = false;
    sigaction(SIGSEGV, &oldSegv, nullptr); sigaction(SIGBUS, &oldBus, nullptr);
    sigaction(SIGFPE, &oldFpe, nullptr); sigaction(SIGILL, &oldIll, nullptr);
    return ok;
}
#endif

int main(int argc, char** argv)
{
    // Unbuffered, so the last line before a crash names the function.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int seeds = 4;
    bool verbose = false, memory = false;
    const char* dump = nullptr;  // --dump <function>: print every register after each seed
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--verbose") == 0) verbose = true;
        else if (std::strcmp(argv[i], "--memory") == 0) memory = true;
        else if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) dump = argv[++i];
        else seeds = std::atoi(argv[i]);
    }
    // Without --memory: a base pointer the pure functions never dereference (32-byte aligned
    // as the prologue assumes). With it: a zeroed 4 GiB guest space, and the memory-touching
    // functions run too.
    alignas(64) static uint8_t dummy[256];
    uint8_t* base = dummy;
    if (memory)
    {
        base = MapGuestSpace();
        if (!base)
        {
            std::printf("ERROR: could not map a 4 GiB guest space\n");
            return 2;
        }
        // Touch both ends: a mapping that cannot be written shows up here, not inside a function.
        base[0] = 1; base[0] = 0;
        base[0xFFFFFFF0ull] = 1; base[0xFFFFFFF0ull] = 0;
        if (verbose)
            std::printf("guest space at %p\n", static_cast<void*>(base));
    }
    alignas(64) static PPCContext ctx;

    // Layout and input self-test: the two hosts must agree on these before any function
    // result can be compared.
    if (verbose)
    {
        Fill(ctx, 0);
        std::printf("LAYOUT sizeof=%zu xer=%zu cr0=%zu fpscr=%zu f0=%zu v0=%zu v127=%zu fill0=%016" PRIx64 "\n",
            sizeof(PPCContext), size_t(reinterpret_cast<const uint8_t*>(&ctx.xer) - reinterpret_cast<const uint8_t*>(&ctx)),
            size_t(reinterpret_cast<const uint8_t*>(&ctx.cr0) - reinterpret_cast<const uint8_t*>(&ctx)),
            size_t(reinterpret_cast<const uint8_t*>(&ctx.fpscr) - reinterpret_cast<const uint8_t*>(&ctx)),
            size_t(reinterpret_cast<const uint8_t*>(&ctx.f0) - reinterpret_cast<const uint8_t*>(&ctx)),
            size_t(reinterpret_cast<const uint8_t*>(&ctx.v0) - reinterpret_cast<const uint8_t*>(&ctx)),
            size_t(reinterpret_cast<const uint8_t*>(&ctx.v127) - reinterpret_cast<const uint8_t*>(&ctx)),
            HashContext(ctx));
    }

    const size_t count = sizeof(kLeafFuncs) / sizeof(kLeafFuncs[0]);
    size_t ran = 0;
    uint64_t total = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < count; i++)
    {
        const LeafFunc& f = kLeafFuncs[i];
        if (f.memory && !memory)
            continue;
        if (dump && std::strcmp(dump, f.name) != 0)
            continue;
        for (int seed = 0; seed < seeds; seed++)
        {
            Fill(ctx, uint64_t(seed) * 1000003ull + i);
            if (memory && f.memory)
                ResetGuestSpace(base);
            if (verbose)
                std::printf("%s %d ", f.name, seed);  // before the call: a crash names its function
            const bool ok = RunOne(f.fn, ctx, base);
            const uint64_t h = ok ? HashContext(ctx) : 0;
            const uint64_t hc = ok ? HashContextCanonicalNaN(ctx) : 0;
            total = Fnv1a(&hc, sizeof(hc), total);
            if (verbose)
            {
                if (ok) std::printf("%016" PRIx64 " %016" PRIx64 "\n", h, hc);
                else std::printf("CRASH CRASH\n");
            }
            if (dump && ok)
            {
                const PPCRegister* r = &ctx.r0;
                for (int k = 0; k < 32; k++) std::printf("%s seed%d r%-2d %016" PRIx64 "\n", f.name, seed, k, r[k].u64);
                const PPCRegister* fr = &ctx.f0;
                for (int k = 0; k < 32; k++) std::printf("%s seed%d f%-2d %016" PRIx64 "\n", f.name, seed, k, fr[k].u64);
                const PPCVRegister* v = &ctx.v0;
                for (int k = 0; k < 128; k++)
                    std::printf("%s seed%d v%-3d %016" PRIx64 " %016" PRIx64 "\n", f.name, seed, k, v[k].u64[1], v[k].u64[0]);
                const PPCCRRegister* cr = &ctx.cr0;
                for (int k = 0; k < 8; k++)
                    std::printf("%s seed%d cr%d %d%d%d%d\n", f.name, seed, k, cr[k].lt, cr[k].gt, cr[k].eq, cr[k].so);
                std::printf("%s seed%d xer so=%d ov=%d ca=%d ctr=%016" PRIx64 " lr=%016" PRIx64 "\n", f.name, seed,
                    ctx.xer.so, ctx.xer.ov, ctx.xer.ca, ctx.ctr.u64, ctx.lr);
            }
        }
        ran++;
    }
    std::printf("TOTAL %zu %016" PRIx64 "\n", ran, total);
    return 0;
}
