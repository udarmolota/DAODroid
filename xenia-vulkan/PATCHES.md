# The Vulkan backend: what was imported and what was changed

`src/xenia` holds Xenia's Vulkan GPU backend, imported from
[xenia-canary](https://github.com/xenia-canary/xenia-canary) at commit
`1c81b867473806b2845a1946b2d185c9d888a319`, the one DAORecomp's vendored Xenia
(`recomp/xenia`, Direct3D 12 only) comes from. Xenia is BSD-licensed
(`recomp/xenia/LICENSE`). The shared GPU, UI/presenter and base code is not
duplicated here: it is DAORecomp's patched copy, built for Android by
`CMakeLists.txt` next to this file. The DAO changes to that shared code and the
interfaces the backends implement are listed in `recomp/xenia/PATCHES.md`.

To see a file's changes against upstream:

```bash
git -C recomp/tools/xenia-canary show 1c81b867:src/xenia/gpu/vulkan/vulkan_command_processor.cc | diff -u - xenia-vulkan/src/xenia/gpu/vulkan/vulkan_command_processor.cc
```

## Imported files

- `gpu/vulkan/`: the backend (command processor, pipeline cache, primitive processor,
  query pool, render target cache, shader, shared memory, texture cache, deferred
  command buffer, graphics system). Not the trace viewer/dump mains.
- `gpu/spirv_*`: the SPIR-V shader translator and builder.
- `ui/vulkan/`: the instance, device, provider, presenter, immediate drawer, memory
  allocator and the function tables. Not the window demo.

## Added files

This project's (MIT, the repository's `LICENSE`), not Xenia's:

- `gpu/vulkan/dao_vulkan_command_processor.cc`: `DaoReadGpuWrittenMemory`, the GPU's
  data of a range it wrote (resolves) copied back for the CPU, as the Direct3D 12
  backend's `dao_command_processor.cc` does; `ReadBackGpuWrittenMemory` in the Android
  host calls it.
- `ui/vulkan/dao_vulkan_icd.*`: a driver loaded directly with `DAO_VULKAN_ICD=<path>`,
  without the system loader: a plain ICD (`vk_icdGetInstanceProcAddr`) or an Android
  Vulkan HAL module (`HMI`, the Turnip builds for Android). No layers, and on Android
  no surfaces: for off-screen runs.

## Changed upstream files (marked `DAO`)

The same changes DAORecomp made for its Direct3D 12 backend, applied to the Vulkan
one, plus what the Android runs needed:

- `gpu/vulkan/vulkan_graphics_system.*`: `Setup` takes the runtime's `Memory` instead
  of the emulator's processor and kernel; the command processor is made without a
  kernel state; no Vulkan device is a failure, not a crash.
- `gpu/vulkan/vulkan_command_processor.*`: no kernel state, emulator or audio system
  (the window title text has no audio backend name); PM4 implementation included by
  its `xenia/gpu/` path (the shared file lives in the other tree); DAO's frame
  statistics and `DAO_GPU_FRAME_TRACE` (`dao_trace::OnSwap`, `LogDraw`, `LogResolve`,
  as in the Direct3D 12 command processor).
- `ui/vulkan/vulkan_presenter.*`: `CaptureGuestOutput(image, dao_sdr)` (the game's
  HDR is not done on this backend, the flag is unused) and
  `RefreshGuestOutputImpl(..., dao_generated_ref)` (no frame generation: always
  false), as `ui/presenter.h` declares them.
- `ui/vulkan/vulkan_instance.cc`: `DAO_VULKAN_ICD` (see `dao_vulkan_icd.*`); the layer
  enumeration stubbed when the driver has none.
- `ui/vulkan/vulkan_provider.cc`: `DAO_VULKAN_NO_SURFACE=1`: the instance and device
  without surface and swapchain support, the presenter still created (it draws the
  guest output into its images; nothing can be painted to a window).

## Not ported

DAORecomp's Direct3D 12 extras have no Vulkan counterpart yet: temporal upscaling
(FSR/XeSS/DLSS), frame generation, true HDR output, low latency, resolves straight
into textures (`dao_resolve_to_textures`), the frame rate guard and neural
rendering. The Android host (`android/runtime/xenia_host_vulkan.cpp`) stubs their
entry points.

## Shaders

The SPIR-V bytecode is generated at build time from `recomp/xenia/src/xenia/{gpu,ui}/shaders`
by `scripts/build_xenia_shaders_spirv.py` with the NDK's shader tools (glslc,
spirv-opt, spirv-dis) into the build tree, not committed. The output shaders include
AMD FidelityFX CAS and FSR 1 headers from the xenia-canary checkout's submodules.
DAORecomp's own `dao_*` shaders are HLSL for Direct3D 12 and are skipped.

## Texture LOD bias switch (2026-10-23)

`gpu/spirv_shader_translator_fetch.cc`: `dao_texture_lod_bias` (int cvar, default 0, set in
config.toml's `[xenia]`) is added to the LOD of every mip-mapped fetch with a computed LOD,
after the fetch constant's and the instruction's biases. 1 = one mip smaller everywhere (the
effect of halving the textures, with no data changed). Explicit-LOD fetches are untouched.
Baked into the translated SPIR-V, so it applies from the next start.

## Pre-rotation on Android (2026-10-24)

The Android compositor was rotating every frame on the GPU (SurfaceFlinger: the game's layer
CLIENT-composed, ROT_90) because the swapchain was created with preTransform IDENTITY in the
landscape window size. `ui/vulkan/vulkan_presenter.{h,cc}`: with `dao_present_prerotate`
(default on, Android only) and a surface whose currentTransform is ROTATE_90/270, the swapchain
takes the surface's natural (portrait) extent and preTransform = currentTransform; the guest
output flow is laid out in the logical (swapped) extent, forced to the bilinear effect, and its
final pass draws with `ui/shaders/guest_output_bilinear_rotate{90,270}.ps.xesl` (DAO's own, in
`xenia-vulkan/src/xenia/ui/shaders/`, compiled by the same SPIR-V step) into the rotated
rectangle; letterbox clears are rotated too. `dao_present_prerotate_flip` turns the other way
if a device shows the frame upside down. The presenter's UI drawers (ImGui) don't know about
the rotation (none are used on Android yet).
