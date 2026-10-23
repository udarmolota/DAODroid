# DAODroid status

One short file, updated at the end of every working session (DAORecomp playbook rule). Newest
entry first. Plan: `PLAN_dao_android_native_port.md`; phase 1 steps: `PHASE1_runbook.md`.

## 2026-10-22 (night 5): PHASE 5 FIRST RUN — the game runs in the app on the S25: videos, menu, sound

- `android/app/`: Gradle project (AGP 8.13.2, wrapper from RimDroid, minSdk 29, NDK 27.3),
  SDL3's Java (`org.libsdl.app`, from recomp/thirdparty/SDL 3.4.18) + `GameActivity`
  (libraries SDL3 + daodroid; game at `Android/data/com.daodroid/files/dao/{game,userdata}`,
  a Turnip .so in `files/dao/driver/`), manifest (landscape, fullscreen, extractNativeLibs).
- Native (`android/app/src/main/cpp`): `app_main.cpp` (SDL_main: main.cpp's startup, SDL
  window + gamepads + audio, guest main on a 16 MB-stack pthread, the SDL thread as Xenia's UI
  thread), `sdl_window.{h,cpp}` (xe::ui::Window + WindowedAppContext over SDL: ANativeWindow
  surface, paint requests, background/foreground), `vulkan_private_loader.c` + `vkdriverhook.c`
  + `linkernsbypass/` (ValDroid's private loader: a private libvulkan.so copy on Turnip,
  BSD-2 helper by Billy Laws), `app_stubs.cpp` (overlays/keyboard/photo/cheats not ported).
- `android/runtime` restructured: `daodroid-runtime` static library shared by the headless exe
  and the app; `ui/erf,menu_movies,graphics_menu` now built (fixes the first-run stall);
  `xenia_host_android.h` window hooks; Xenia: `dao_icd::SetLoaderLibrary`; input.cpp on
  Android reads a pad the UI thread opens (`OnSdlEvent`).
- APK built and installed (her command); the game runs in the app: intro videos, main menu,
  sound; 60 swaps/s; `PHASE5_runbook.md`. Fixed on the way: files pushed by adb are unreadable
  by the app (copy as the app with run-as); a driver .so can't be loaded from the shared
  storage (copied to internal files); the runtime library must be linked whole-archive
  (static-initializer import registrations); the Zomdroid Turnip 8xx build breaks the
  swapchain after the first frames (`VK_ERROR_INVALID_EXTERNAL_HANDLE` on acquire) while
  the newer Gen8 V32/V36 and v26_b10 builds work — V36 chosen.
- On-screen gamepad added (`VirtualPadView`: Zomdroid's default gamepad layout, merged with a
  physical pad in `input.cpp`): she drove the menu with it; a drag-to-arrange edit mode (hold
  the top switch) keeps positions in the app's preferences. No icons yet.
- Heat: thermal status 2 after long sessions, ~52 swaps/s at the menu; power work open.
- 2026-10-23: **pause menu fixed** — the app's `InitializePhotoMode` stub now installs the patched
  START menu movie (`InstallPauseMenuMovie`), as photo_mode.cpp does on Windows; without the
  override the engine drew no menu at all. She played: HUD, map, inventory, pause, saves work.
  Orientation pinned to landscape. (The earlier "no HUD" reading was a capture with the menu
  open, and a day-old log: a locked phone keeps SDL from starting the game.)
- 2026-10-23: screen shape — filling the screen by telling the game a wider display mode
  (`DAO_FILL_SCREEN=1`) only stretches the 16:9 projection (round menus turn into ellipses),
  so the app keeps 16:9 with side bars; a true wider field of view needs the engine's own
  aspect handling (open). Textures need no recompression: the disc's are console DXT already.
- 2026-10-23: temporary **performance bar** (Zomdroid's `PerfOverlayView`/`PerfSampler`: fps
  from Xenia's swap count over JNI, CPU, kgsl GPU busy, RAM, thermal) added at the top of the
  screen; `DAO_PERF_OVERLAY=0` hides it. Her reading in the camp hall: 25 fps, GPU 87 %,
  CPU 23 %, RAM 88 % (menu 60 fps). kgsl: the GPU was **capped at 443 MHz of 1200** (devfreq
  max_freq; thermal status 1, skin 40 °C; earlier in the run it sat at 525–832 MHz), so the
  GPU is the first bottleneck under throttling. Threads: one at 96 % of a core (shown as
  `SDLThread`: the guest main thread inherits SDL's name; to be named), Eclipse::Update 25 %,
  DragonAge::Render 14 %, Xenia GPU Commands 14 %. Phone has 4 GB swap full, 490 MB free: memory
  pressure. simpleperf and debuggerd don't work on this Samsung (perf events off, root needed):
  profiling needs our own in-app sampler (SIGPROF on the guest thread → function histogram).
- 2026-10-23: **in-process profiler** written (`android/runtime/profiler.cpp`, `DAO_PROFILE=<s>`,
  per-thread CPU-clock timers → SIGPROF → PC histogram, report in dao.log with `sub_` names);
  guest main thread named. Native build compiles; APK not built yet (her command). The crash
  while she changed graphics settings: Turnip `submit failed: Resource deadlock would occur` →
  VK_ERROR_DEVICE_LOST, 15 kgsl GPU faults for the process, 8 s after "settings saved"
  (sharpness 8→5, shadows off, post-processing off); not reproduced yet.
  Second run with the profiler: **the same device loss again**, ~90 s in, right when the
  world starts rendering, with her saved settings (shadows off, post-processing off, FSR
  sharpness 1, vsync off) — the graphics settings are the prime suspect; config.toml.bak holds
  the defaults for a retest. Profile at the menu: `SDLThread` (the SDL UI loop, not the guest:
  the guest main is now named) at 49 % of the process CPU, all inside SDL's 1 ms
  WaitEvent/pump polling (atomics, mutexes, clock_gettime, joystick update): a core burnt for
  nothing → replace with a real wait (own semaphore + poll). Guest threads at the menu are idle.
  Third run, default settings: **no crash**, the camp hall at ~21 fps. kgsl: `thermal_pwrlevel`
  10 = 443 MHz as soon as the SKIN sensor reads 40 °C (Samsung thresholds 38/40/42/45 °C);
  GPU busy 88 %. In-world profile: `SDLThread` (SDL's own loop, SDL_WaitEvent on Android)
  at a whole core (top 96 %; ~50 % of the process samples) in SDL atomics/mutexes/
  clock_gettime/joystick pump — the game's threads are small (Eclipse::Update 13 %, GPU
  Commands 11 %, DragonAge::Render 8 %, guest main idle), the 5 Vulkan Pipeline threads
  compile shaders for the first minute. So: GPU-bound at a throttled clock, and the SDL loop
  wastes a core that heats the phone. Profiler now walks frame pointers (inclusive view) to
  see who wakes the SDL loop; the POSIX CPU-clock timers tick every ~4 ms, not 1 ms.
  Her report: the round party portraits (HUD, left) don't show the hero and the dog —
  render-to-texture portraits; to compare with a PC frame trace later.
  Settings bisected one at a time (her runs): **post-processing off is safe and helps**
  (~21 → ~25 fps while the GPU clock fell 443 → 389 MHz); **dynamic shadows off crashes**
  (3 of 3: fps drops, then VK_ERROR_DEVICE_LOST 10–15 s later, kgsl gpufaults +12). The
  engine's shadows-off path makes Turnip fault — to investigate with a frame trace (PC trace
  of the same state for comparison); until then shadows stay on. config.toml on the phone:
  shadows = true, post_processing = false. Upscaler "bilinear" vs FSR still to test.
  Her decisions: no frame-rate cap for now (see the maximum); try halving textures; shader
  precision as a last resort. Ideas: shader-side LOD bias +1 (textures sampled one mip smaller,
  no data changes), fp16 (RelaxedPrecision) in the SPIR-V translator, FSR/CAS off.
- She loaded her save in the app: the 3D world renders correctly (capture: the camp room, the
  dog, rats), Back opens the map/inventory, but **the in-world UI layer is missing: no HUD,
  and Start hides the HUD without showing the pause menu** (the game does open it: the same
  user-language query as on the PC follows each Start). A DAO_GPU_FRAME_TRACE of an in-world
  frame with the menu "open" shows ~2000 scene draws, the post-process chain and the
  front-buffer resolve, but no 2D UI draws at all: the game isn't submitting them, so it's a
  game-side state, not a dropped draw. Suspects for next time: the patched START menu movie
  (`mainmenu.gfx` override from `menu_movies.cpp`; the phone log shows no VFS mapping line
  for it while the PC's does), or something the app's UI stubs answer differently
  (`CheatsAvailable`, photo mode hooks that the START menu's rows call).
- Performance in the world: ~9-14 swaps/s (2000+ draws per frame on the Vulkan backend);
  the menu runs at 60. Profiling is the next big item after the UI.
- `userdata/env.txt` (KEY=VALUE) sets the game's environment in the app (DAO_CAPTURE_EVERY,
  DAO_GPU_FRAME_TRACE, ...); the capture line reports a recent frame's draw counts.
- Not done: the ImGui overlays (keyboard for the character name!), physical gamepad
  untested, backgrounding untested, headless exe not rebuilt after the restructure,
  performance not measured.
  Uncommitted everywhere (waiting for the GitHub URL).

## 2026-10-22 (night 4): PHASE 4, FIRST HALF DONE — the game renders on the S25's GPU (Turnip), off-screen

- `xenia-vulkan/`: Xenia's Vulkan backend + SPIR-V translator imported from the pinned
  xenia-canary commit and adapted like DAORecomp's D3D12 one (`xenia-vulkan/PATCHES.md`); a
  CMake file building DAORecomp's patched shared Xenia (base/ui/gpu) and the backend for
  Android; shaders compiled to SPIR-V at build time with the NDK tools
  (`scripts/build_xenia_shaders_spirv.py`, 119 shaders). Host: `android/runtime/xenia_host_vulkan.cpp`
  (no window; capture to PNG, GPU memory read-back, `DAO_DUMP_MEM`). Details, commands, results:
  `PHASE4_runbook.md`.
- **Result:** with Turnip (Mesa 26.1, Adreno 8xx build from the Zomdroid set, loaded directly
  through its HAL entry point: `DAO_VULKAN_ICD`, `DAO_VULKAN_NO_SURFACE`) the intro logos and
  the main menu render correctly on the phone (`build/phone_turnip_capture*.png`), 60 swaps/s,
  no failed draws, no Xenia errors.
- **The phone's proprietary Adreno driver renders black**: everything runs (shaders, pipelines,
  texture uploads, resolves, swap), the CPU textures hold the video, but the front buffer read
  back from the GPU is RGB 0 / A 255, i.e. the draw stage. Not chased: Turnip is the plan.
- Submodule (branch `daodroid`): Android guards in the shared Xenia code (thread priority,
  frame pacer, Bionic quirks: robust mutexes, `pthread_t`, fontconfig/GTK, a duplicate
  `SetAttributes`), `xenia_memory.cpp` write watches with `mprotect`. Windows build re-checked.
- Phase 5 next: the app — SDL3 window/pad/audio, Turnip with a surface (ValDroid's private
  loader way, or a blit to the window), the UI/menu units (`graphics_menu`, `menu_movies`),
  then performance. Decision needed on how the app gets Turnip a surface (runbook "Open items").
- Uncommitted everywhere (waiting for the GitHub URL).

## 2026-10-22 (night 3): PHASE 3 DONE — the game runs headless on the S25 at a steady 60 fps loop

- `daodroid_headless` (62 MB stripped) runs on the phone: 4 GiB guest space + memfd bank at the
  three windows, XEX loaded, 22 guest threads, null GPU ring at 60 swaps/s for 150 s, intro
  logos' Bink thread and the XMA decoder working, scripted START reaching the title. Details,
  commands and findings: `PHASE3_runbook.md`.
- Bugs fixed: guest `\` paths and case on a POSIX file system (`kernel/vfs.cpp`); FFmpeg's aarch64
  NEON sources (`XE_HAS_GAS_ASM` + `xe_add_gas_sources` in our CMake); libc++ 18 lacks
  `std::atomic_ref` (shim in `platform.h`). The Windows build still compiles with the port.
- Diagnostics added: crash handler with symbolizable offsets, POSIX host stack sampling for the
  thread dumps, `DAO_TRACE_AFTER`, an input-state trace line.
- Open: first-run `userdata/` stalls at the title (missing `generated/` menu movies, made by
  `ui/menu_movies.cpp`, not built here yet) — same on the PC in null mode; copy the PC's
  `userdata/generated` meanwhile. Phase 4 next: SDL3 + Xenia Vulkan, and the UI/menu units.
- Uncommitted everywhere (waiting for the GitHub URL): submodule branch `daodroid`
  (`git -C recomp status`), `android/runtime/`, `android/ppc-probe/`, scripts, docs, LICENSE,
  README, NOTICE.

## 2026-10-22 (night 2): PHASE 3 IN PROGRESS — runtime compiles for ARM64, link blocked on FFmpeg NEON

- Submodule branch `daodroid` (uncommitted): `DAORecomp/platform.h` (POSIX stand-ins for the
  Win32 calls: sleeps, thread names/ids, FILETIME/SYSTEMTIME, `_aligned_malloc`, `_stricmp`,
  `_byteswap_*`, an `std::atomic_ref` shim for libc++ 18), `stdafx.h` (windows.h only on
  Windows), `text.h` (UTF-8/UTF-32 helpers on POSIX), `program.cpp`, `files.cpp`, `hash.cpp`
  (TinySHA1 + a SHA-256), `kernel/host_file.h` (new: HANDLE-or-fd file layer used by
  `xboxkrnl_io.cpp` and `vfs.cpp`), `kernel/memory.cpp` (mmap 4 GiB + memfd bank at the three
  windows, `0xE` at +0x1000: needs 4 KB pages), `kernel/thread.cpp` (pthread, 16 MB stacks, no
  host stack walking yet), `kernel/objects.cpp` (`clock_nanosleep`), `content/save_backups.cpp`
  and `installed.cpp`, `kernel/xam.cpp` (rename), `xenia/ffmpeg.cmake` (target guards).
- Our side: `android/runtime/` — CMake target `daodroid_headless`, `main_headless.cpp`,
  `stubs.cpp` (null GPU incl. a real `CallInterruptCallback`, UI/input/audio-output stubs,
  `DefaultUpdateChannel`), `shim/xenia/base/cvar.h`, `build_info_android.cpp`.
- State: every runtime object compiles for ARM64 (kernel, apu, gpu null path, content, settings,
  XenonUtils, FFmpeg subset). **Link fails on three FFmpeg aarch64 symbols**
  (`ff_tx_codelet_list_float_aarch64`, `ff_float_dsp_init_aarch64`, `ff_mpadsp_init_aarch64`):
  the `.S` NEON files in `recomp/xenia/ffmpeg.cmake`'s aarch64 blocks are not getting compiled
  (check the conditions around lines 143–160 and 247–258, the ASM language, and `HAVE_NEON`
  in `ffmpeg-xenia/config.h`). Next: fix that, link, strip, push the exe + `install/game` to
  `/data/local/tmp/dao/` on the S25 and run `DAO_DUMP_AFTER=30 ./daodroid_headless /data/local/tmp/dao`.
- Not done: `DAO_INPUT_SCRIPT` support in the input stub (the game will sit at the title screen),
  host stack sampling on POSIX, the 16 KB-page fallback, PHASE3 runbook.

## 2026-10-22 (late): PHASE 2 DONE — generated code builds and computes correctly on ARM64

- `android/ppc-probe/`: NDK CMake for the generated code (`daoppc`, 203 units, 0 warnings,
  8.6 min at -j 6, 376 MB .a) and a correctness harness built for both ARM64 (phone) and x86-64
  (against the installer's own `DAORecompLib.lib`). Details and commands: `PHASE2_runbook.md`.
- Harness tooling: `scripts/find_leaf_funcs.py` (call-free functions, with memory/VMX/loop
  flags), `scripts/gen_leaf_table.py` (table + 251 kernel-import stubs, generated into build/).
- Result on the S25 Ultra vs the PC, 304 functions × 4 seeds: 1180 of 1216 pairs identical bit
  for bit; 1 identical crash (store past 4 GiB); 30 pairs differ only by the NaN sign
  convention (x86 negative default NaN, ARM64/PowerPC positive); 2 functions beyond that, one
  of them NaN-sign shifted through integer ops, the other a real find: `fctidz` of NaN gives
  0 on ARM64 (SIMDe fallback cast) where x86 and the console give `0x8000000000000000`.
  Phase 3 item: patch the recompiler's `fctidz`/`fctiwz` emission for ARM64 and add a NaN seed.
- Lessons that cost time: Git Bash rewrites `/data/local/tmp` unless `MSYS_NO_PATHCONV=1`;
  Windows harness output is CRLF (strip before diffing); a `;` in an input-script comment is a
  statement; leaf detection must exclude register division, loops over zeroed memory, and
  treat `simde_mm_load_si128((base + …))` as memory access.
- Uncommitted: everything from LICENSE/README/NOTICE on, plus `android/ppc-probe/` and the two
  scripts (waiting for the GitHub repository URL).

## 2026-10-22 (night, later): distribution model settled, repository going public

- Decision: a proper repository on GitHub (`udarmolota`), MIT, with LICENSE/README/NOTICE.
  Distribution model = DAORecomp's: the repository explains how to build the APK at home on a
  PC from one's own disc; no APK is ever published (it contains the translated game code).
  The owner installs her own build on her phone. This supersedes the earlier "personal only"
  note: the project is personal in use, public in source.
- Consequence for the plan: the per-user PC build pipeline (ISO → ppc → NDK → APK) is a real
  deliverable (phase 8 in DAORecomp's playbook terms), not an afterthought.

## 2026-10-22 (night): PHASE 1 DONE — manual PC session passes incl. pad, naming, save/load

- 22-minute manual session through the launcher (Play), logs and 247 captures in
  `install/testruns/manual2/` (`sheet.png` 33 MB, `dao.log`, `xenia.log`, `thumb_*.png`):
  New Game → intro movie (81–290 s) → character creation → on-screen keyboard "Enter Name"
  (11 chars) → Castle Cousland: walking, camera, dialogue (Duncan, Bryce), a fight with the
  rats in the storeroom, autosave, a named user save (keyboard again, 5 chars), quit to menu,
  Load Game listing 2 saves, load, play on, quit. Clean exit ("Window closed, exiting").
- Pads: STK-7024X first (SDL name), later "Xbox 360 Controller (2)" hot-plugged mid-session;
  both drove the game; keyboard merged in. The pad/keyboard overlay works as documented.
- Saves in `install/userdata/content/454108C0/00000001/0x4eb25201_1_{a,u}!0/Saves/`
  (`savegame.das` ~225 KB, `savegame.met`, `scrnshot.bin` 448x256 tiled ARGB). The **user
  save's thumbnail is a real image** (storeroom, dog, rat: `thumb_u.png`); the **autosave's is
  black** (`thumb_a.png`, made at 868 s during the area transition). Worth checking on the
  Xbox behaviour before calling it a bug; the user save proves the GPU readback path works.
  Keep these saves: DAORecomp's `load_latest`, `walk`, `camera` recipes need one.
- One hang, not reproduced: on her first manual attempt the game froze on the loading/quote
  screen after New Game (no file opens, no new threads, F7 still reached the game, 60 fps
  swaps continued for 10 min, pad disconnect did not help). The quote screen normally
  auto-advances in 3–5 s. A scripted run with intro logos on did not reproduce it, nor did
  the second manual run with the same launcher path and pad. Logged as a one-off in
  `install/testruns/manual_stuck_dao.log`; `DAO_DUMP_AFTER=30` is the tool if it recurs.
- Log lines this session, all benign: `ExGetXConfigSetting(0x3, 0x2) unknown` ×12 (character
  creation), `NtCreateFile: unmapped path 'hrt201d.bin'` / `'oth999d.bin'` (area probes),
  `xam.xex` ×7. xenia.log: FSR upscaling active by default (`temporal_upscaler = "auto"`).
- Phase 1 "done" criteria met: PC build from our disc, playable scene, pad menus, naming,
  save/reload. Next: phase 2 (ARM64 compile probe of `recomp/ppc/` with the NDK).

## 2026-10-22 (evening): scripted baseline runs on the PC pass; pad/save checks by hand remain

- Harness: DAORecomp's `scripts/test/run_game.sh` works against our install through a junction
  `recomp/userdata` → `install/userdata` (the exe finds `install/game` from its own folder; the
  harness edits `userdata/config.toml` from the repo root). `EXE=../install/DAORecomp.exe`.
  Input scripts take absolute paths; a `;` inside a comment is parsed as a statement separator.
- Our input recipes (no save needed, DAORecomp's all need one): `scripts/test_inputs/
  new_game_smoke.txt` (90 s) and `new_game_creator.txt` (200 s). Outputs in
  `install/testruns/{smoke,newgame,creator}/` (captures, `sheet.png`, `dao.log`, `xenia.log`).
- Results: BioWare/DA logos, title screen ("Press START"), storage-device notice, DLC check,
  main menu (New Game, Downloadable Content, Options, Extras, "Sign in to Dragon Age Servers"),
  New Game → Canticle quote → intro Bink movie (clean, no block artifacts) → the in-engine
  prologue cinematic: 3D characters, fire, the Archdemon, Duncan's fight. XMA audio decoding
  ran (`XMA decoder worker started`, FFmpeg xmaframes). Scripted START/A reach every step, so
  the XInput path works end to end (the physical-pad check is still manual).
- Performance (RTX 3050 Laptop, 720p, default settings): title 60.3 fps flat; menu + movie
  59.5 avg / 49.8 min; 200 s run incl. the 3D cinematic 58.7 avg / 43.0 min / 61.2 max.
  CPU per thread in the cinematic: Eclipse::Update 20 %, WorkerThread0 15 %, DragonAge::Render
  13 %, GPU Commands 11 %.
- Log lines to know: `XexGetModuleHandle('xam.xex'): module not found` (×7, benign),
  `NtCreateFile: unmapped path 'data\sku.ini'` / `'oth999d.bin'` (benign), six
  `XAudio: the worker was 22–26 ms late, its callbacks were dropped` on the 4-core laptop,
  and `xenia.log`: 28× `Failed to write pipeline description to storage` (pipeline cache
  write; shaders load fine, 21 from storage on the 2nd run). None stopped the game.
- Not reached by script: character creation (the intro cinematic is longer than 200 s and
  A/START/B do not skip it), naming through the on-screen keyboard, save/load. These are the
  manual pad checks of `PHASE1_runbook.md`.
- The ISO in `image/` can be deleted now that the install is verified (7.8 GB).

## 2026-10-22 (later): phase 1 build done, baseline play checks pending

- `python install.py --iso ... --dest install --accept-msvc-license --llvm download --jobs 6`
  ran clean, exit 0, 13:30 → 13:57 (27 min total; the game build itself ~14 min at `-j 6` on
  the 4-core i7-11370H). Log: `install/install_log.txt`.
- Recompiler: "206 files updated, 0 removed"; no missing-instruction or switch warnings in the
  log. `recomp/ppc/`: 206 files, 253 MB, **51,949 functions** in `ppc_func_mapping.cpp`;
  `ppc_config.h`: `PPC_IMAGE_BASE 0x82000000`, `PPC_IMAGE_SIZE 0x12E0000`,
  `PPC_CODE_BASE 0x821F0000`, `PPC_CODE_SIZE 0xF4A808`. Largest unit 5.5 MB
  (`ppc_recomp.132.cpp`). This is phase 2's input.
- Built for x86-64-v3 (the installer's choice for this CPU). Compiler warnings: only Xenia's
  `WIN32_LEAN_AND_MEAN`/`NOMINMAX` redefinitions and one `-Wnontrivial-memcall`; none from the
  generated code.
- Installed: `install/DAORecomp.exe` (77 MB) + upscaler DLLs (desktop extras), `install/game/`
  6.5 GB (9450 files), `install/userdata/`. Neural rendering skipped (no RDNA 4), expected.
- Disk now used by phase 1: `recomp/tools` 5.2 GB, `recomp/build` 1.0 GB, `recomp/ppc` 253 MB,
  `install/game` 6.5 GB, `image/` 7.8 GB (the ISO can go once the baseline is confirmed).
- Not done yet: the baseline play checks from `PHASE1_runbook.md` (launcher, pad, new game,
  naming, dialogue, fight, save/load) and the scripted recipes.

## 2026-10-22: phase 0 done, phase 1 prepared, nothing built

- Decisions: personal project, one APK with the translated game code inside, game data imported
  separately; no distribution, so no license question; mw2-android stays a design reference
  (no code taken). Russian language deferred to a data mod after phase 6 (the disc is En/Es/It;
  a Russian 360 disc would be a different XEX and would not work with DAORecomp).
- Repository initialised (no commits yet). `recomp/` = DAORecomp submodule at `1a6c668a`.
  `.gitignore` keeps `ppc/`, game data, `image/`, `install/`, builds and binaries out.
- Disc image extracted from the 7z to `image/` (7.84 GB ISO). `scripts/check_image.py` run on
  it: title ID 454108C0, media ID 186A1115, `default.xex` SHA-1 `532c0ec1…` all match the
  installer's pins. The installer will accept this disc.
- PC checked: i7-11370H (AVX2), 15.7 GB, RTX 3050, 232 GB free, Python 3.13 + Tkinter, git.
- Phone checked over adb: SM-S938B (S25 Ultra), Android 17, 4 KB pages, 12 GB, Adreno.
- Open: the owner's command to start the phase 1 install (30–60+ min, ~25 GB).

Key addresses and hooks: none of ours yet; DAORecomp's are in `recomp/docs/porting-playbook.md`.
