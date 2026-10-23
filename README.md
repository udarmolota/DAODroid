# DAODroid

A native ARM64 Android port of *Dragon Age: Origins* (the Xbox 360 edition), built on
[DAORecomp](https://github.com/96dot/Dragon-Age-Origins-Recomp): the game's PowerPC code is
translated to C++ ahead of time (XenonRecomp) and compiled for the phone, the console services
are provided by DAORecomp's runtime ported to Android, and the GPU command stream runs on Vulkan
through Xenia's backend. The game keeps its original controller interface: a physical gamepad
or a touch overlay feeds the same Xbox controller state the console game reads.

**This repository contains no game code and no game data.** Everything derived from the disc
(the translated code in `ppc/`, the extracted game files, saves, the disc image) stays on the
build machine and is ignored by git (`.gitignore`). You need your own copy of the game: the
Xbox 360 disc with title ID `454108C0` and media ID `186A1115` (English/Italian/Spanish), the
edition DAORecomp supports. No build of the app is published here: as with DAORecomp, you build
it yourself on a PC from your own disc (the translated game code ends up inside your APK, so it
is yours alone), then install that APK on your phone. The instructions for that are the
runbooks in `docs/`, one per phase as the port progresses.

Status: early. Done so far: the PC baseline (phase 1), the translated game code compiled and
checked on ARM64 (phase 2), the runtime running the game headless on a phone (phase 3), and
the game's frames rendered by the phone's GPU through Xenia's Vulkan backend with Turnip, still
off-screen (phase 4, first half). Next: the app itself. See
[docs/status.md](docs/status.md) for the current state and
[docs/PLAN_dao_android_native_port.md](docs/PLAN_dao_android_native_port.md) for the plan.

## Layout

| Path | What |
|---|---|
| `docs/` | plan, phase runbooks, status log |
| `recomp/` | DAORecomp as a git submodule, pinned; our changes to it (the Win32 to POSIX port, Android guards) come as a branch |
| `xenia-vulkan/` | Xenia's Vulkan backend and SPIR-V translator, imported from the same xenia-canary commit as DAORecomp's vendored Xenia, adapted to the runtime; the CMake that builds Xenia for Android |
| `android/ppc-probe/` | NDK build of the translated game code and the ARM64-vs-x86 correctness harness |
| `android/runtime/` | the runtime for Android, headless for now: kernel, audio decoding, Xenia's Vulkan GPU off-screen, a command-line entry point for adb |
| `scripts/` | tooling: disc image check, `run_game.sh` input recipes, the leaf-function harness tables, the SPIR-V shader build |
| `launcher-java/` | the Android app (to come), with parts reused from the author's other launchers |

## Building the PC baseline

```
git clone --recurse-submodules <this repository> DAO
cd DAO
python -I scripts/check_image.py "<path to the ISO>"          # must say "supported edition"
cd recomp
python install.py --iso "<path to the ISO>" --dest "<DAO>/install" --accept-msvc-license --llvm download
```

The details, what the installer downloads and the acceptance checks are in
[docs/PHASE1_runbook.md](docs/PHASE1_runbook.md).

## License

MIT (see `LICENSE`) for this repository's own files. Third-party components and their licenses
are listed in `NOTICE.md`. *Dragon Age* is a trademark of Electronic Arts; this project is not
affiliated with or endorsed by EA or BioWare.
