# DAODroid

*Dragon Age: Origins* (the Xbox 360 edition) on Android phones, built on
[DAORecomp](https://github.com/96dot/Dragon-Age-Origins-Recomp) by 96dot: the game's PowerPC
code is translated to C++ ahead of time (XenonRecomp) and compiled for ARM64, DAORecomp's
runtime is ported to Android, and the GPU command stream runs on Vulkan through Xenia's
backend. The game keeps its console interface (radial menus, tactical pause, party control),
played with a physical gamepad or an on-screen one.

Made with the help of AI.

**This repository contains no game code and no game data, and no APK is published.** The
APK you build holds the game's translated code, so it is yours alone: you build it on a PC
from your own disc, then install it on your own phone. You need the Xbox 360 disc with title
ID `454108C0` and media ID `186A1115` (the English/Italian/Spanish edition, also disc 1 of
its Ultimate Edition), the one DAORecomp supports.

Status: playable on a Galaxy S25 Ultra (Snapdragon 8 Elite, Adreno 830): 50–60 fps in the
first camp on a cool phone, 30 with the game's frame cap for long sessions; saves, sound,
videos, the in-game keyboard and both kinds of gamepad work. See
[docs/status.md](docs/status.md) for the details and the known issues below.

## What you need

- A PC with Windows 10/11 (DAORecomp's installer also runs on Linux), about 25 GB free, and
  Python 3.10 or newer. The first run takes 30–60 minutes.
- [Android Studio](https://developer.android.com/studio) (its JDK and SDK), and from its SDK
  Manager the **NDK 27.3.13750724** and **CMake 3.22.1**.
- An image of your disc (`.iso`).
- An arm64 phone with Android 10 or newer, USB debugging on, and about 7 GB free.
- A Vulkan driver for your GPU (see [Drivers](#drivers)).

## Building

### 1. The game code and files (on the PC)

```bash
git clone --recurse-submodules https://github.com/udarmolota/DAODroid DAO
cd DAO
python -I scripts/check_image.py "<path to the ISO>"     # must say "supported edition"
cd recomp
python install.py --iso "<path to the ISO>" --dest "<path to DAO>/install" --accept-msvc-license --llvm download
```

DAORecomp's installer checks the disc, extracts the game files to `install/game/`,
translates the game code into `recomp/ppc/` and builds the PC version too (you can play it
to check your disc). `recomp/` is DAORecomp on its `daodroid` branch, with the Android
changes. Details: [docs/PHASE1_runbook.md](docs/PHASE1_runbook.md).

### 2. The APK

Open `android/` in Android Studio once (it writes `android/local.properties` with your SDK's
path), or create that file yourself with `sdk.dir=<path to the Android SDK>`. Then:

```bash
cd android
./gradlew assembleDevDebug     # Windows: gradlew.bat assembleDevDebug
```

The first build takes about 15 minutes (52,000 translated functions). The APK is
`android/app/build/outputs/apk/dev/debug/app-dev-debug.apk`. It is a debug build on purpose:
that is what lets you copy the game into the app's folder over USB (step 3).

The app has two variants: `dev`, the one above, with the game built in, and `launcher`
(`assembleLauncherDebug`), without any game code, which imports game packages
([docs/PACKAGE_FORMAT.md](docs/PACKAGE_FORMAT.md)): Add game (+) picks a `.daopkg`, checks it and
copies it in; each game then has its own settings (driver, graphics, environment variables),
saves (export/import), shader cache and bug report. A package is made with
`scripts/make_package.py build --game <install/game> --library <the dev build's libdaodroid.so>`;
the one-step PC tool (disc image in, package out) is in progress.

### 3. Installing on the phone

Copy `app-dev-debug.apk` to the phone (a USB cable, a cloud drive, a messenger) and open it there;
Android asks to allow installing apps from that source once. Or, with adb:

```bash
adb install -r android/app/build/outputs/apk/dev/debug/app-dev-debug.apk
```

The app reads everything from `Android/data/com.daodroid/files/dao/` on the phone:

| Folder | What |
|---|---|
| `game/` | the game files (`install/game` from step 1, 6.4 GB) |
| `driver/` | one Vulkan driver `.so` (optional; without one the phone's own is used) |
| `userdata/` | settings (`config.toml`), saves, logs; created by the app |

Files put there with `adb push` belong to adb's user, and the app can't read them. Copy them
through the app instead (`run-as` works with the debug APK):

```bash
adb push "<path to DAO>/install/game" /data/local/tmp/dao_game
adb shell run-as com.daodroid mkdir -p /sdcard/Android/data/com.daodroid/files/dao/driver
adb shell run-as com.daodroid cp -r /data/local/tmp/dao_game /sdcard/Android/data/com.daodroid/files/dao/game
adb shell rm -r /data/local/tmp/dao_game
```

and the driver the same way:

```bash
adb push libvulkan_freedreno.so /data/local/tmp/
adb shell run-as com.daodroid cp /data/local/tmp/libvulkan_freedreno.so /sdcard/Android/data/com.daodroid/files/dao/driver/
```

Then start **DAODroid** from the launcher. To play on, save in the game; your saves are in
`userdata/content/`.

## Drivers

The app loads the first `.so` in `dao/driver/` through a private Vulkan loader (the way the
author's other launchers load Turnip), or the phone's own driver when the folder is empty.
You can swap drivers without rebuilding.

| GPU | Driver | State |
|---|---|---|
| Adreno 8xx (Snapdragon 8 Elite) | Turnip Gen8 V38 ([StevenMXZ](https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases), Mesa 27.0) | tested: the S25 Ultra. The phone's own Adreno driver renders a black picture |
| Adreno 6xx/7xx | Turnip builds for 6xx/7xx from the same authors | expected to work, untested |
| Mali, Xclipse, PowerVR | the phone's own driver | untested |

On the Adreno 830 Turnip's tiled rendering faults with some of the engine's passes; the app
sets `TU_DEBUG=sysmem` (render to system memory) on Adreno 8xx unless `userdata/env.txt` sets
`TU_DEBUG` itself. Other Turnip builds tried on the 830: V36 works (slower), V32 works,
V25.1/V22 are older (V22 shows a black picture), v26_b10 faults.

If you try another phone, a report with `userdata/dao.log` is very welcome.

## Playing

- **Controls**: any Bluetooth or USB gamepad, or the on-screen one. The switch at the top of
  the screen hides the on-screen pad; hold it for half a second to drag the buttons where you
  like (kept between runs).
- **Graphics**: the Graphics tab in the game's Options menu (title and pause menus). The Android defaults are the
  bilinear output, dynamic shadows and post-processing off, and the intro logos skipped. The
  frame cap (30/60) is there too: 30 keeps a phone cool through long sessions.
- **Text** (the character's name): Android's keyboard opens in a dialog.
- **More switches** in `userdata/config.toml`, section `[xenia]`:
  `dao_texture_lod_bias = 0` sharper textures (1 is the default, one mip smaller and faster),
  `dao_present_prerotate = false` lets Android rotate the frame (then the output upscaler
  setting applies again).

## Known issues

- The party portraits on the HUD are empty after the first load following a cold start; they
  come back after any conversation with a cutscene camera. Also on Windows: reported upstream
  ([DAORecomp #7](https://github.com/96dot/Dragon-Age-Origins-Recomp/issues/7)).
- Heat: the phone's GPU gets throttled after some minutes at 60 fps; the 30 fps cap or a
  cooler helps.
- No DLC or title update yet (DAORecomp supports them; untested on Android).
- The text and voices are English, Italian or Spanish (what the disc has).

## Layout

| Path | What |
|---|---|
| `recomp/` | DAORecomp as a git submodule ([fork](https://github.com/udarmolota/Dragon-Age-Origins-Recomp-Android), branch `daodroid`: the Win32-to-POSIX port and Android defaults) |
| `xenia-vulkan/` | Xenia's Vulkan backend and SPIR-V translator, imported from the same xenia-canary commit as DAORecomp's Xenia and adapted ([PATCHES.md](xenia-vulkan/PATCHES.md)) |
| `android/app/` | the app: SDL3 window and input, the private Vulkan loader, the on-screen pad, the keyboard dialog, the performance bar |
| `android/runtime/` | the runtime for Android (also as a headless binary for adb), the in-process profiler |
| `android/ppc-probe/` | the ARM64 build of the translated code and its correctness harness |
| `scripts/` | disc check, the SPIR-V shader build, harness tables |
| `docs/` | the plan, one runbook per phase, the status log |

## License

MIT (see `LICENSE`) for this repository's own files. Third-party components and their
licenses are listed in `NOTICE.md`. *Dragon Age* is a trademark of Electronic Arts; this
project is not affiliated with or endorsed by EA or BioWare.
