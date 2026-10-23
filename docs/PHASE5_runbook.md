# Phase 5 runbook: the app

Goal (plan §3, phase 5): the game as an Android app with a window, a gamepad and sound:
SDL3 owns the activity, the window, the gamepads and the audio device; Xenia's Vulkan GPU
paints into the window; Turnip runs the GPU through a private Vulkan loader. First run
2026-10-22 on the Galaxy S25 Ultra: the intro videos and the main menu on screen with sound.
Results and the open items at the end.

## What was built

- `android/`: the Gradle project (AGP 8.13.2, Gradle 8.13 wrapper, NDK 27.3, minSdk 29,
  arm64 only). `android/app/src/main/java/org/libsdl/app` is SDL3's Java side (zlib, from
  `recomp/thirdparty/SDL`, 3.4.18); `com.daodroid.GameActivity` extends it: loads `SDL3`
  and `daodroid`, passes the install folder and the driver to the native side.
- `android/app/src/main/cpp`: `libdaodroid.so` (the runtime library from `android/runtime`,
  linked whole-archive because the kernel imports register through static initializers;
  SDL's `input.cpp` and `audio_output.cpp`; the app's own files) and `libvkdriverhook.so`.
  - `app_main.cpp`: `SDL_main` on SDL's thread, which becomes Xenia's UI thread; DAORecomp's
    startup order; the guest main thread on a pthread with a 16 MB stack; the event loop.
  - `sdl_window.*`: `xe::ui::Window` and `WindowedAppContext` over the SDL window: the
    presenter paints into the activity's `ANativeWindow`; paint requests and UI-thread calls
    are SDL user events; background/foreground disconnect and reconnect the surface.
  - `vulkan_private_loader.c`, `vkdriverhook.c`, `linkernsbypass/` (BSD-2, Billy Laws):
    ValDroid's way of running a Turnip build: a private copy of the system `libvulkan.so` in
    a linker namespace, with the hook handing it our driver instead of the phone's. Xenia
    gets that library as its loader (`dao_icd::SetLoaderLibrary`).
  - `app_stubs.cpp`: the ImGui overlays (keyboard, notices, photo mode, cheats) as no-ops.
  - `VirtualPadView.java`, `VirtualPad.java`, `virtual_pad_jni.cpp`: the on-screen Xbox pad over
    SDL's surface, its state merged into the game's controller (`input.cpp` on Android).
- `android/runtime`: one static library `daodroid-runtime` for both front ends; the menu
  units `ui/erf.cpp`, `menu_movies.cpp`, `graphics_menu.cpp` are built (the first-run
  stall of phase 3 is gone: the menu movies are generated).
- Submodule: `input/input.cpp` on Android reads a gamepad the UI thread opens
  (`OnSdlEvent`), since only one thread may pump SDL's events.
- Xenia: the acquire semaphore created exportable as a sync file on Android
  (`vulkan_presenter.cc`; harmless, kept), the failed-acquire result logged.

## Files on the phone

```
Android/data/com.daodroid/files/dao/game/      DAORecomp's install/game (6.4 GB)
Android/data/com.daodroid/files/dao/userdata/  settings, saves, generated menus, dao.log
Android/data/com.daodroid/files/dao/driver/    one Turnip .so (copied to the app's internal
                                               files at start: code can't run from the shared storage)
```

Files put there by `adb push` belong to the shell user and the app cannot read them
(EACCES through FUSE): copy as the app instead, e.g.
`adb shell run-as com.daodroid cp -r /data/local/tmp/dao/game /sdcard/Android/data/com.daodroid/files/dao/game`
(a debug build; a release build needs a file manager or the app's own importer, phase 6).

## Commands

```bash
cd android && JAVA_HOME="/c/Program Files/Android/Android Studio/jbr" ./gradlew :app:assembleDebug   # ~15 min the first time
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.daodroid/.GameActivity
adb logcat -s DAODroid dao-xenia SDL DEBUG            # the app, Xenia, SDL, crashes
adb shell run-as com.daodroid tail -50 /sdcard/Android/data/com.daodroid/files/dao/userdata/dao.log
```

Native compile checks without Gradle: `cmake -S android/app/src/main/cpp -B build/app` with
the NDK toolchain (as in PHASE4_runbook.md) and `ninja -C build/app daodroid`.

## Results (2026-10-22, Galaxy S25 Ultra)

- The game runs in the app: the intro videos (Bink, 60 swaps/s), then the main menu, with
  sound (AAudio through SDL). 2340x1080 swapchain, fullscreen landscape.
- **Turnip builds:** the Zomdroid set's `libvulkan_freedreno_8xx.so` (Mesa 26.1-devel) fails
  after the first frames: every `vkAcquireNextImageKHR` returns
  `VK_ERROR_INVALID_EXTERNAL_HANDLE` (-1000072003) once the swapchain's buffers come back
  with release fences; the picture freezes while the game runs on. The newer Gen8 builds
  (stevenmx's Turnip Gen8 V32, V36 = Mesa 26.3-devel, and `libvulkan_freedreno_v26_b10`)
  have no such failure. **V36 is the one to use.** The phone's proprietary driver still
  renders black (phase 4).
- CPU: the process at ~160% of a core at the menu (the game's threads plus the GPU thread);
  not profiled yet.

`userdata/env.txt` holds KEY=VALUE lines the activity puts in the game's environment
(`DAO_CAPTURE_EVERY=5`, `DAO_GPU_FRAME_TRACE=<swap>`, `DAO_DUMP_AFTER`, `DAO_GPU=null`); the
capture log line carries the draw counts of a recent frame. Pull files binary-safely with
`adb exec-out run-as com.daodroid cat <file> > local` (a plain `adb shell cat` mangles them).

Performance bar (2026-10-23, temporary): `PerfOverlayView` + `PerfSampler` from Zomdroid, drawn
at the top centre over the pad (not clickable). fps = presented frames (Xenia swaps, JNI
`nativeGetPresentedFrames`), CPU = the process's ticks over all cores, GPU = the kgsl `gpubusy`
node, RAM, thermal status; sampled once a second on a `PerfOverlay` thread. `DAO_PERF_OVERLAY=0`
in `env.txt` hides it.

In-process CPU profiler (`android/runtime/profiler.cpp`, `DAO_PROFILE=<seconds>` in `env.txt`):
simpleperf and debuggerd are unusable on this Samsung (perf events disabled, root needed), so
every thread gets a POSIX timer on its own CPU clock raising SIGPROF each millisecond of CPU
time; the handler records the PC, and a report every N seconds in `dao.log` lists the threads by
CPU time and the top functions of each (names from the dynamic symbol table: the recompiled
guest functions are `sub_<guest address>`). The guest main thread is now named `Guest main`
(it used to show as `SDLThread`, inherited from its creator). The handler also walks the
frame-pointer chain (8 frames), and each thread's report has an "inclusive" list: functions
by the samples anywhere under them (who calls the hot code).

Vulkan validation layer (debugging GPU faults / device loss): Khronos's Android binaries,
https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases (android-binaries-<ver>.zip,
built with NDK 27.3); `arm64-v8a/libVkLayer_khronos_validation.so` goes to
`android/app/src/main/jniLibs/arm64-v8a/` (27 MB, gitignored, debug builds only). The system
loader (also our private copy: the namespace is SHARED, so GraphicsEnv's layer paths apply)
injects it into a debuggable app when the phone's developer settings name it:

    adb shell settings put global enable_gpu_debug_layers 1
    adb shell settings put global gpu_debug_app com.daodroid
    adb shell settings put global gpu_debug_layers VK_LAYER_KHRONOS_validation

Messages go to logcat (tag `VALIDATION`). Turn it off afterwards
(`settings put global enable_gpu_debug_layers 0`): it costs a lot of CPU.

## Open items

- (Fixed 2026-10-23) The pause menu didn't draw: the START menu's patched movie
  (`InstallPauseMenuMovie`, installed by `InitializePhotoMode` on Windows) must be installed
  here too; the engine shows no menu without the override. Done in both front ends' stubs.
- Performance in the world: ~9-14 swaps/s with 2000+ draws per frame; the menu runs at 60.
- Screen shape: the game renders 1280x720 and the phone is 2340x1080, so the frame sits
  between black bars. Telling the game a 1560x720 display (`SetDisplayMode`) and fitting the
  frame to the screen (`DAO_FILL_SCREEN=1`) only stretches it: the engine projects at its
  buffers' 16:9 regardless, and the 2D UI stretches with it. A real fill needs a wider render
  target (and UI layout) in the engine: a later item.

- The on-screen pad (`VirtualPadView`: Zomdroid's default gamepad layout, its positions and
  sizes; `VirtualPad.nativeSetState` -> `input::SetVirtualPad`, merged with a physical pad)
  works; holding the top-centre switch for 0.6 s enters an edit mode where every element is
  dragged into place (saved in the app's preferences). No icons yet; the physical gamepad
  path is untested.
- Heat: after some twenty minutes the phone reports thermal status 2 (moderate) with the
  CPUs at 62-66 C and the game drops to ~52 swaps/s at the menu (the process ~140-160% of a
  core). Power work (the `Sleep(0)` idle loop, the job workers' timed polls) is still open.
- The ImGui overlays: the on-screen keyboard (the character's name at New Game!), notices,
  photo mode, cheats: `ui/overlay.cpp`, `keyboard.cpp`, `notice_overlay.cpp`,
  `launcher/game_art.cpp` over the presenter's UI drawers (the SDL window supports them).
- Backgrounding: the surface disconnect/reconnect on `WILL_ENTER_BACKGROUND` /
  `DID_ENTER_FOREGROUND` is written but not exercised; SDL refreshes the native window
  pointer on `surfaceCreated`.
- Game files and driver import in the app (a launcher screen), the headless exe rebuilt
  after the CMake restructure, performance and thermal measurements, the `Sleep(0)` idle
  loop, save thumbnails (GPU read-back is in).
