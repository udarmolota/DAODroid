# Third-party components

This repository's own files are MIT-licensed (`LICENSE`). It builds on, or will include
code from, the following projects, each under its own license:

| Component | Where | License |
|---|---|---|
| DAORecomp (Dragon Age: Origins — Recompiled) by 96dot | `recomp/` (git submodule) | MIT, except its `xenia/` directory |
| Xenia / xenia-canary (GPU, UI and base code vendored by DAORecomp; the Vulkan backend and SPIR-V translator imported from the same pinned commit `1c81b867`) | `recomp/xenia/`, `xenia-vulkan/src/` | BSD 3-Clause (`recomp/xenia/LICENSE`) |
| Vulkan-Headers, VulkanMemoryAllocator, glslang (SPIR-V builder), fmt, xxHash, Snappy, Dear ImGui, stb, cxxopts, toml++, utfcpp (Xenia's third-party libraries) | `recomp/tools/xenia-canary/third_party/` (fetched, not in the repository) | Apache-2.0 / MIT, MIT, BSD-3-Clause and others per library, MIT, BSD-2-Clause, BSD-3-Clause, MIT, MIT, MIT, MIT, Boost |
| AMD FidelityFX CAS and FSR 1 (shader headers included by Xenia's output shaders) | `recomp/tools/xenia-canary/third_party/FidelityFX-*` (fetched) | MIT |
| XenonRecomp and XenonUtils (hedge-dev) | downloaded by DAORecomp's installer into `recomp/tools/` | MIT |
| SIMD Everywhere (SIMDe) | via XenonRecomp's `thirdparty/` | MIT |
| SDL 3 | `recomp/thirdparty/SDL` (downloaded) | zlib |
| FFmpeg (has207/xenia fork, XMA decoding) | downloaded into `recomp/tools/xenia-canary/third_party/` | LGPL 2.1+ |
| Mesa Turnip (Vulkan driver for Adreno): StevenMXZ's Turnip Gen8 V38 build (Mesa 27.0), bundled in the APK as the built-in driver | `android/app/src/main/jniLibs/arm64-v8a/libvulkan_turnip_gen8.so` (not in the repository; from github.com/StevenMXZ/Adreno-Tools-Drivers, release v27.0.0-R1) | MIT |
| liblinkernsbypass (Billy Laws): linker namespaces for the private Vulkan loader | `android/app/src/main/cpp/linkernsbypass/` | BSD 2-Clause |
| AndroidX AppCompat, Material Components for Android (the launcher's screens) | Gradle dependencies | Apache-2.0 |
| Material Design icons (Google): the launcher's icons | `android/app/src/main/res/drawable/ic_*.xml` | Apache-2.0 |
| Khronos Vulkan validation layer (debugging, dev builds only) | `android/app/src/dev/jniLibs/` (not in the repository) | Apache-2.0 |
| Code reused from the author's own launchers (Zomdroid, RimDroid, ValDroid): the performance bar, the on-screen gamepad's layout, the private Vulkan loader, the launcher's structure | `android/app/` | as in those repositories |

Design references that contributed no code: mw2-android / MW2 Recompiled (GPL-3.0),
Rayman Origins Recomp, Pinyon Shift.

No game code, game data or anything derived from the Dragon Age: Origins disc is part of
this repository. *Dragon Age* is a trademark of Electronic Arts Inc.
