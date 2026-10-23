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
| Mesa Turnip (Vulkan driver for Adreno): a build for the phone's GPU, pushed to the phone for the headless runs; to be bundled by the app | not in the repository (phase 5: `android/`) | MIT |
| Code reused from the author's own launchers (Zomdroid, RimDroid, ValDroid, PriDroid) | `launcher-java/` (to come) | as in those repositories |

Design references that contributed no code: mw2-android / MW2 Recompiled (GPL-3.0),
Rayman Origins Recomp, Pinyon Shift.

No game code, game data or anything derived from the Dragon Age: Origins disc is part of
this repository. *Dragon Age* is a trademark of Electronic Arts Inc.
