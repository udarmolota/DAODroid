# Phase 4 runbook: Xenia's Vulkan GPU on the phone, off-screen

Goal (plan §3, phase 4, first half): the game's frames rendered by the phone's GPU through
Xenia's Vulkan backend, inside the headless runtime from phase 3, captured to PNG over adb.
Done 2026-10-22: the intro logos and the main menu render on the Galaxy S25 Ultra with
Turnip. Results and the open items at the end. The app (SDL3 window, pad, audio, Turnip
bundled) is phase 5.

## What was built

- `xenia-vulkan/`: Xenia's Vulkan backend and SPIR-V shader translator, imported from the
  xenia-canary commit DAORecomp's vendored Xenia comes from (`1c81b867`), adapted to the DAO
  runtime's interfaces the same way DAORecomp adapted its Direct3D 12 backend; and a CMake
  file that builds DAORecomp's patched shared Xenia code (base, UI/presenter, GPU) plus the
  backend for Android. What changed in the imported files: `xenia-vulkan/PATCHES.md`.
- `scripts/build_xenia_shaders_spirv.py`: compiles Xenia's `.xesl`/`.glsl` shaders to SPIR-V
  headers with the NDK's shader tools (glslc, spirv-opt, spirv-dis), at build time, into the
  build tree. 119 shaders; DAORecomp's own `dao_*` (HLSL) are skipped.
- `android/runtime/xenia_host_vulkan.cpp`: the host side (DAORecomp's `gpu/xenia_host.cpp`
  with the Vulkan graphics system, no window): guest memory, shader cache, the swap packet,
  frame capture, GPU memory read-back (`DaoReadGpuWrittenMemory`, new on Vulkan), interrupt
  delivery. `gpu/xenia_memory.cpp` (the write watches) runs on POSIX with `mprotect`; the
  headless main routes the faults to it.
- Headless main additions: `DAO_CAPTURE_EVERY=<s>` (PNG + draw statistics of a recent frame),
  `DAO_DUMP_MEM=<addr>:<len>[,...]@<s>` (a capture and guest memory ranges, read back from the
  GPU where it wrote them), `DAO_GPU=null`.
- Submodule (branch `daodroid`) additions, all guarded: Windows-only thread priority and the
  frame pacer's timer (`gpu/graphics_system.cc`, `ui/dao_frame_pacer.cc`), Bionic (no robust
  mutexes, integer `pthread_t`, no fontconfig/GTK, a duplicate `SetAttributes`) in
  `base/mutex.cc`, `base/threading_posix.cc`, `base/filesystem_android.cc`, `ui/imgui_drawer.cc`.
  Listed in `recomp/xenia/PATCHES.md` ("Android").

## Prerequisites

The installer's xenia-canary checkout is sparse; the Vulkan backend needs more of it:

```bash
cd recomp/tools/xenia-canary
git sparse-checkout add src/xenia/gpu src/xenia/ui src/xenia/base cmake third_party/CMakeLists.txt
git submodule update --init third_party/Vulkan-Headers third_party/VulkanMemoryAllocator third_party/glslang third_party/FidelityFX-CAS third_party/FidelityFX-FSR
```

A Turnip build for the phone's Adreno (the Mesa Vulkan driver): see "Results" for why. The
one used here is the Adreno 8xx build bundled by Zomdroid (`libvulkan_freedreno_8xx.so`,
Mesa 26.1.0-devel); pushed to `/data/local/tmp/vk/`.

## Commands

```bash
SDK="$LOCALAPPDATA/Android/Sdk"; NDK="$SDK/ndk/27.3.13750724"; CM="$SDK/cmake/3.22.1/bin"
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"; ADB="$SDK/platform-tools/adb.exe"; export MSYS_NO_PATHCONV=1
"$CM/cmake.exe" -G Ninja -S android/runtime -B build/runtime -DCMAKE_MAKE_PROGRAM="$CM/ninja.exe" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release
"$CM/ninja.exe" -C build/runtime -j 6 daodroid_headless          # ~15 min the first time (Xenia + shaders)
"$BIN/llvm-strip.exe" -o build/runtime/daodroid_headless.stripped build/runtime/daodroid_headless
"$ADB" push build/runtime/daodroid_headless.stripped /data/local/tmp/daodroid_headless
"$ADB" shell chmod 755 /data/local/tmp/daodroid_headless
# The phone's own driver (renders black on the S25, see below):
"$ADB" shell 'cd /data/local/tmp/dao && DAO_AUDIO=null DAO_CAPTURE_EVERY=10 timeout 60 /data/local/tmp/daodroid_headless /data/local/tmp/dao'
# Turnip, loaded directly (no system loader: no surfaces, hence off-screen):
"$ADB" shell 'cd /data/local/tmp/dao && DAO_VULKAN_ICD=/data/local/tmp/vk/libvulkan_freedreno_8xx.so DAO_VULKAN_NO_SURFACE=1 DAO_AUDIO=null DAO_CAPTURE_EVERY=10 DAO_INPUT_SCRIPT="every 2 START 0.2 from 30 until 70" timeout 75 /data/local/tmp/daodroid_headless /data/local/tmp/dao'
"$ADB" pull /data/local/tmp/dao/userdata/capture_4.png build/
"$ADB" shell logcat -d -s dao-xenia                              # Xenia's own log (logcat, not a file, on Android)
```

Diagnostics: `DAO_GPU_FRAME_TRACE=<swap>` logs every draw, resolve and the swap of the frame
after that swap (as on Direct3D 12; now also on Vulkan); the capture line reports a recent
frame's draws submitted / VIZ-skipped / failed / issued. `[xenia] log_level = 3` in
`config.toml` turns Xenia's debug log on (texture loads, pipelines; heavy). `DAO_DUMP_MEM`
plus a hex dump tells whether a texture or a resolved front buffer holds data (how the
black output was localized to the draw stage).

## Results (2026-10-22, Galaxy S25 Ultra, Android 17, Adreno 830)

- **Turnip (Mesa 26.1, Adreno 8xx build): the logos and the main menu render correctly**
  (`build/phone_turnip_capture*.png`): Bink video via YUV textures, Scaleform UI, the scripted
  START reaching the menu; 52 draws per logo frame, 114–116 per menu frame, none failed; 60
  swaps/s; no errors in Xenia's log. Loaded directly through its Android HAL entry point
  (`DAO_VULKAN_ICD`, `ui/vulkan/dao_vulkan_icd.cc`), off-screen (`DAO_VULKAN_NO_SURFACE`).
- **The phone's proprietary Adreno driver renders black.** Everything else works there:
  instance and device creation, 119 shaders, the translated game shaders and pipelines, texture
  uploads every frame (the write watches fire), the resolve and the swap; the CPU-side video
  textures hold real data (Y 16–235), but the front buffer read back from the GPU is RGB 0 with
  alpha 255. So the draw itself produces nothing on that driver (June 2026 build,
  Vulkan 1.3.284). Not investigated further: the launchers bundle Turnip for Adreno anyway and
  Turnip is the plan for the app. No validation layers are available to a shell process.
- The Mesa 26.3 Turnip build in the Zomdroid set asserts on this GPU (built for a7xx only);
  the 8xx build is the one.
- Built: `daodroid_headless` 66 MB stripped (Xenia libraries 5, 3rd-party 6, shaders generated
  in `build/runtime/xenia-vulkan/gen`). The Windows DAORecomp rebuilds with the submodule's
  guards (checked with the installer's build directory).

## Open items

- A surface: with Turnip loaded directly there is no `VK_KHR_surface`/swapchain. The app
  (phase 5) needs either the launchers' way (ValDroid's `vkshim`/`driver.c`: a private copy of
  the system `libvulkan.so` in a linker namespace, hooked to open Turnip) or a blit from the
  presenter's guest output into an SDL/ANativeWindow surface (Turnip exposes
  `VK_EXT_headless_surface`). Decide in phase 5.
- Performance and power not measured yet (headless, the GPU thread runs 60 swaps/s at the menu).
- The proprietary driver's black output, if anyone needs that driver (a Xenia pipeline the
  Adreno driver dislikes, or a driver bug): compare pipelines/render passes, or a RenderDoc
  capture from the app.
- From phase 3, still open: the first-run `userdata/generated` menu movies (`ui/menu_movies.cpp`
  not built), the `Sleep(0)` idle loop, `fctidz` NaN on ARM64.
