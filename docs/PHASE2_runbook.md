# Phase 2 runbook: the generated game code on ARM64

Goal (plan §3, phase 2): prove that DAORecomp's generated code (`recomp/ppc`, XenonRecomp output
from our disc) compiles for ARM64 Android with the NDK and computes the same thing as the x86-64
build that plays the game on the PC. Done 2026-10-22; results at the end.

## What is built

`android/ppc-probe/CMakeLists.txt` builds two things from the same sources:

- `daoppc`, the generated code as an ARM64 static library (NDK only). It mirrors
  `recomp/DAORecompLib/CMakeLists.txt` with the clang-cl flags translated:
  `/fp:strict` → `-ffp-model=strict`, `/clang:-ffp-contract=off` → `-ffp-contract=off`,
  `/FI mmio_hook.h` → `-include`, no `-march` (arm64-v8a baseline). SIMDe comes from the
  installer's XenonRecomp checkout (`recomp/tools/XenonRecomp/thirdparty/simde`).
- `daoppc_harness` (`harness.cpp`), on both hosts: runs call-free functions of the game on a
  deterministic `PPCContext` and prints two FNV-1a hashes of the resulting context per function
  and input seed (strict, and with every NaN canonicalised to the positive quiet NaN). On ARM64
  it links `daoppc`; on x86-64 Windows it links the `DAORecompLib.lib` the installer built in
  phase 1, so the comparison is against the exact code that plays the game.

The function table is generated at configure time into `build/<dir>/gen/` (it lists game
function addresses, so it is not committed):

- `scripts/find_leaf_funcs.py` scans `recomp/ppc` for functions with no calls, no kernel
  imports, no `__builtin_unreachable`/trap, and no integer division by a register (a zero
  divisor is undefined on the console and a crash here). It records whether a function touches
  guest memory, uses VMX/FP, or loops (a backward `goto`).
- `scripts/gen_leaf_table.py` picks the largest ones: up to 300 pure VMX/FP, 100 pure
  integer-only, 200 memory-touching VMX/FP without loops, and writes the table plus stubs for
  the 251 kernel imports every generated object links against.

Memory-touching functions run against a zeroed 4 GiB guest space (`--memory`: `mmap
MAP_NORESERVE` on Android, `VirtualAlloc` on Windows), reset to zero before each call so no
function sees another's writes. A crash inside a function (SIGSEGV/SEH) is caught and printed
as `CRASH` on both sides.

## Commands (Git Bash, from the repository root)

ARM64 library and harness (NDK 27.3, CMake 3.22.1 from the SDK, `-j 6`):

```bash
SDK="$LOCALAPPDATA/Android/Sdk"; NDK="$SDK/ndk/27.3.13750724"; CM="$SDK/cmake/3.22.1/bin"
"$CM/cmake.exe" -G Ninja -S android/ppc-probe -B build/ppc-probe -DCMAKE_MAKE_PROGRAM="$CM/ninja.exe" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
"$CM/ninja.exe" -C build/ppc-probe -j 6 daoppc daoppc_harness
```

x86-64 harness (the installer's LLVM 22 + xwin sysroot, CMake 4.4 from `recomp/tools`):

```bash
R=recomp; "$R/tools/cmake/bin/cmake.exe" -G Ninja -S android/ppc-probe -B build/ppc-probe-x64 -DCMAKE_MAKE_PROGRAM="$PWD/$R/tools/ninja/ninja.exe" -DCMAKE_TOOLCHAIN_FILE="$PWD/$R/cmake/clang-cl-xwin.cmake" -DDAO_LLVM_BIN="$PWD/$R/tools/llvm/bin" -DDAO_WINSYSROOT="$PWD/$R/tools/winsysroot" -DCMAKE_BUILD_TYPE=Release
"$R/tools/ninja/ninja.exe" -C build/ppc-probe-x64 daoppc_harness
```

Run and compare (the phone over adb; `MSYS_NO_PATHCONV=1` keeps Git Bash from rewriting
`/data/local/tmp`; Windows output is CRLF, strip it before diffing):

```bash
export MSYS_NO_PATHCONV=1; ADB="$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
./build/ppc-probe-x64/daoppc_harness.exe 4 --memory --verbose | tr -d '\r' > build/harness_x64_all.txt
"$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe" -o build/ppc-probe/daoppc_harness.stripped build/ppc-probe/daoppc_harness
"$ADB" push build/ppc-probe/daoppc_harness.stripped /data/local/tmp/daoppc_harness
"$ADB" shell 'cd /data/local/tmp && chmod 755 daoppc_harness && ./daoppc_harness 4 --memory --verbose' | tr -d '\r' > build/harness_arm64_all.txt
```

`--dump <function>` prints every register after each seed, for a register-level diff of one
function. The classification script used for the results below is in the session log; it
compares line by line and splits mismatches into "strict hash only" (NaN sign) and "canonical
hash too".

## Results (2026-10-22)

**Build.** All 203 generated units compile for ARM64 with zero warnings (the only warning in
the whole build is XenonRecomp's own `msr fpcr` inline asm operand width in `ppc_context.h`,
harmless). Objects are real AArch64 (`elf64-littleaarch64`, `-O3`, `-ffp-model=strict`,
NDK defaults `-fstack-protector-strong -D_FORTIFY_SOURCE=2 -ffunction-sections`). Library build:
8.6 minutes at `-j 6` on the i7-11370H; `libdaoppc.a` 376 MB (with symbols), the harness 155 MB
unstripped / 0.5 MB stripped (the symbol and `.debug_line` tables, not code: `.text` 737 KB
for 304 functions).

**Correctness, 304 functions × 4 input seeds = 1216 (function, seed) pairs, S25 Ultra vs PC:**

| Outcome | Pairs | Functions | Meaning |
|---|---|---|---|
| Identical context, bit for bit | 1180 | 294 | integer, FP and VMX paths agree, incl. `vmsum`, `vmadd`, `vperm`, `vcmp`, rounding modes, `fsel`, `fsqrt`, memory loads/stores through `base` |
| Crash on both, same function and seed | 1 | 1 | `sub_82754FB8` seed 2: a 4-byte store at `base + 0xFFFFFFFE`, past the 4 GiB space (the console wraps; never happens with real addresses) |
| Differ only by NaN sign | 30 | 9 | x86 makes its default NaN negative (`0xFFF8…`/`0xFFC00000`), ARM64 and the PowerPC positive (`0x7FF8…`/`0x7FC00000`); comparisons and arithmetic treat them alike |
| Differ beyond NaN sign | 5 | 2 | see below |

The two remaining functions:

- `sub_82CEB100` (4 seeds): `vrsqrtefp128` of negative lanes gives a NaN whose sign differs
  between hosts; the function then shifts and permutes those lanes as integers (`vslw128`,
  `vrlimi128`, `vperm128`), so the sign bit lands in a non-NaN position. Root cause is still the
  NaN sign convention.
- `sub_82CE27D8` (1 seed): `fctidz` of a NaN. The generated code is
  `(f > double(LLONG_MAX)) ? LLONG_MAX : simde_mm_cvttsd_si64(...)`: overflow upwards is
  saturated like the console, NaN and overflow downwards fall through to the intrinsic. On x86
  `cvttsd2si` returns `0x8000000000000000`, which is what the PowerPC gives for both; on ARM64
  SIMDe's fallback is a plain C cast, and `fcvtzs` turns a NaN into 0. **This is the one genuine
  semantic difference found**, and the ARM64 side is the wrong one. Fix for phase 3: extend
  `scripts/patch_recompiler.py` so `fctidz`/`fctiwz` (and the VMX `vcfpsxws` family, to be
  checked) emit an explicit `isnan ? INT_MIN : saturate` sequence; add a NaN seed to the harness
  as the regression test.

What the harness does not cover: functions that call others (the call graph, `PPC_CALL_INDIRECT`
through the function table), the kernel imports, guest threads/TLS, memory ordering between
threads, and the `0xE0000000` alias (phase 3). It also does not time anything: performance is
phase 6.

## Notes for phase 3

- NDK defaults add `-fstack-protector-strong` and `_FORTIFY_SOURCE=2` to every generated
  function; measure, and consider turning them off for `daoppc` (mw2-android does).
- `-ffp-model=strict` on AArch64 works but forbids FMA contraction and some NEON FP folding; it
  is what keeps results identical to the PC build. Keep it until a measured reason not to.
- The weak `sub_X` aliases resolve fine on both linkers; the harness calls `__imp__sub_X`
  directly only to be independent of that.
- `PPCQueryTimebase()` and `DaoGpuRegisterWrite()` are the only runtime symbols the generated
  code needs besides the 251 kernel imports.
