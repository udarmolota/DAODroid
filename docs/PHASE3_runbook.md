# Phase 3 runbook: the runtime on Android, headless

Goal (plan §3, phase 3): DAORecomp's runtime (kernel, XMA audio, the null GPU command
processor, hooks, settings) built for ARM64 and running the game on the phone with no window,
reaching the steady frame loop. Done 2026-10-22; results and the open items at the end.

## What was built

`android/runtime/CMakeLists.txt` makes `daodroid_headless`, a command-line executable for adb:

- From the submodule (branch `daodroid`): `kernel/*.cpp`, `apu/{audio,xma_decoder,xma_trace}.cpp`,
  `gpu/{command_processor,video,camera_fov,d3d_queries,effect_params,held_shot}.cpp`,
  `content/{installed,save_backups}.cpp`, `patches/patches.cpp`, `input/input.cpp`,
  `settings.cpp`, `files.cpp`, `log.cpp`, `hash.cpp`, `program.cpp`; XenonUtils (XEX loading,
  LZX, AES); the FFmpeg subset with its aarch64 NEON files (the XMA decoder); the generated
  game code (`android/ppc-probe`, phase 2) and its build descriptor.
- Ours: `main_headless.cpp` (DAORecomp's `main.cpp` without launcher, window, mods and UI;
  `DAO_DUMP_AFTER`, `DAO_INPUT_SCRIPT`, `DAO_TRACE`, `DAO_TRACE_AFTER`), `stubs.cpp` (null GPU
  with a real `CallInterruptCallback`; UI, audio output and launcher stubs),
  `shim/SDL3/SDL.h` (no gamepad, keeps `input.cpp` and its scripted presses),
  `shim/xenia/base/cvar.h` (no Xenia cvars), `build_info_android.cpp`.

The Win32 → POSIX port lives in the submodule, all guarded by `#ifdef _WIN32` so the Windows
build still compiles (checked: `recomp/build/installer/release` rebuilds clean):

| File | What changed |
|---|---|
| `DAORecomp/platform.h` (new) | POSIX stand-ins: `Sleep`, `SwitchToThread`, thread names/ids, `TerminateProcess`, `FILETIME`/`SYSTEMTIME`, `_aligned_malloc`, `_stricmp`, `_byteswap_*`, `_mm_pause`, keyboard polling no-ops, an `std::atomic_ref` for libc++ 18 |
| `stdafx.h`, `text.h` | `windows.h` only on Windows; UTF-8/UTF-32 helpers on POSIX |
| `kernel/host_file.h` (new), `xboxkrnl_io.cpp`, `vfs.cpp` | one file layer over a Win32 HANDLE or a POSIX descriptor (positioned reads/writes, truncate, flush); `stat` for attributes |
| `kernel/vfs.cpp` | POSIX: `\` → `/` in guest paths, and case-insensitive lookup of each component (the console's file system is case-insensitive) |
| `kernel/memory.cpp` | `mmap` 4 GiB `MAP_NORESERVE`; the 512 MB bank as a `memfd` mapped at `0xA`, `0xC` and `0xE` (+0x1000); needs 4 KB host pages |
| `kernel/thread.cpp` | `pthread_create` with 16 MB stacks; host stack sampling with a signal and the unwinder (offsets for `llvm-symbolizer`) |
| `kernel/objects.cpp` | `clock_nanosleep` for the short sleeps |
| `hash.cpp`, `program.cpp`, `files.cpp`, `content/*`, `kernel/xam.cpp`, `log.cpp`, `input/input.cpp` | small POSIX branches; `DAO_TRACE_AFTER`; an input-state trace line |
| `xenia/ffmpeg.cmake` | target variables overridable (aarch64 NEON files) |

## Commands

```bash
SDK="$LOCALAPPDATA/Android/Sdk"; NDK="$SDK/ndk/27.3.13750724"; CM="$SDK/cmake/3.22.1/bin"
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"; ADB="$SDK/platform-tools/adb.exe"; export MSYS_NO_PATHCONV=1
"$CM/cmake.exe" -G Ninja -S android/runtime -B build/runtime -DCMAKE_MAKE_PROGRAM="$CM/ninja.exe" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
"$CM/ninja.exe" -C build/runtime -j 6 daodroid_headless
"$BIN/llvm-strip.exe" -o build/runtime/daodroid_headless.stripped build/runtime/daodroid_headless
"$ADB" push install/game /data/local/tmp/dao/game            # once, 6.4 GB, a few minutes
"$ADB" push build/runtime/daodroid_headless.stripped /data/local/tmp/daodroid_headless
"$ADB" shell 'chmod 755 /data/local/tmp/daodroid_headless; cd /data/local/tmp/dao && DAO_DUMP_AFTER=30 DAO_INPUT_SCRIPT="every 2 START 0.2 from 30 until 120" timeout 150 /data/local/tmp/daodroid_headless /data/local/tmp/dao'
"$ADB" pull /data/local/tmp/dao/userdata/dao.log build/phone.log
```

Crashes: the handler logs the fault's guest address, the host `pc`/`lr` as executable offsets and
the registers; `llvm-symbolizer -e build/runtime/daodroid_headless --functions=short 0x<offset>`
names the generated function. Thread dumps (`DAO_DUMP_AFTER`) carry sampled host stacks the same
way. Full import tracing (`DAO_TRACE=1`) is too heavy on the phone (5 M lines a minute, and it
shifts timing); `DAO_TRACE_AFTER=<s>` traces from a moment on.

## Results (2026-10-22, Galaxy S25 Ultra, Android 17, 4 KB pages)

- Executable 62 MB stripped (264 MB with symbols). Guest memory: 4 GiB reserved, the bank
  mapped at the three windows, 51,949 functions in the table, XEX loaded, kernel up; 22 guest
  threads start; the null command processor runs the ring at 60 swaps/s; the intro logos' Bink
  thread runs 24 s and exits; the XMA decoder decodes the title music (streamed from
  `music_bank_strm_new.fsb`); **a stable 60 fps loop for 150 s with no errors** beyond the known
  benign warnings (`xam.xex` lookups, `data\sku.ini`). Scripted START reaches the game:
  `XamContentCreateEnumerator` (saves) fires; with the PC's `userdata/` copied over, the
  storage notice is dismissed and the DLC check (type 2 enumeration) completes, as on the PC.
- Fixed on the way: (1) the renderer-init crash was guest paths with `\` reaching a POSIX file
  system (and case), not the data; (2) `fsqrts` etc. are fine, the harness was right.
- **Open: first-run state.** A fresh `userdata/` stalls on the title screen after the save
  enumeration, on the phone *and* on the PC in null-GPU mode: `ui/menu_movies.cpp` (called from
  `InitializeGraphicsMenu`, not in the headless build) generates patched Scaleform menu movies
  into `userdata/generated/packages/core/override/`, which the menu flow needs. Phase 4 brings
  `graphics_menu`/`menu_movies` in; until then copy `install/userdata/generated` from a PC run.
- Noted for phase 4: thread 16/17 (job workers) spin on `KeQueryPerformanceFrequency`
  20 k/s on both hosts (the game's own timed poll); the update thread's idle loop is
  `WaitForSingleObject(exitEvent, 0) + Sleep(0)` and costs a core at 13 k iterations/s on the
  phone (power/thermal item: the `Sleep(0)` policy in `kernel/objects.cpp`).
- 16 KB pages: the `0xE0000000` alias refuses to map (logged) on such a device; untested.
