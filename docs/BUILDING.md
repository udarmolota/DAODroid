# Building DAODroid yourself

For developers, and for anyone who wants the development build (the game built into the APK)
instead of the launcher plus a game package. Players: see the README's four steps.

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

## The game package (the PC tool, from the command line)

```bash
python -I scripts/daodroid_tool.py --iso "<the disc image>" --out "<folder>" [--accept-msvc-license] [--jobs N]
```

The same steps as the window (`DAODroid Tool.cmd`, `scripts/daodroid_tool_gui.py`): DAORecomp's
own installer steps check the disc, extract the game and translate its code (recomp/ppc); the
NDK r27d (the Android SDK's, or a download checked against Google's SHA-1) builds
`libdaodroid.so` with CMake and Ninja (no Gradle, no Android Studio); `scripts/make_package.py`
packs it ([PACKAGE_FORMAT.md](PACKAGE_FORMAT.md)). Its work folder is `build/tool/`. A full run
from the disc image on a 4-core laptop with the tools already set up: about 14 minutes.

The launcher APK: `cd android && ./gradlew assembleLauncherDebug` (no game code in it; it builds
without recomp/ppc).

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
