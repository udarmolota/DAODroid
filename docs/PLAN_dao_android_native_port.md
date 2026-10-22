# Dragon Age: Origins (Xbox 360) → native ARM64 Android: engineering plan

Date: 2026-10-22. Status: plan, nothing built yet. Follows the feasibility brief
`RimDroid/docs/BRIEF_dragon_age_origins_android_research.md` (Codex, same date) and adds a
source-level audit of DAORecomp, mw2-android and our four launchers. Everything below was
read-only; no game file, repository or launcher was modified.

## 0. Decision in one paragraph

Build **DAODroid**: an Android app whose game code is DAORecomp's XenonRecomp output compiled
for ARM64 (native CPU, no interpreter/JIT), whose console-service runtime is DAORecomp's kernel
ported from Win32 to POSIX/Android, whose GPU is Xenia's **Vulkan** backend (the only part that
stays "emulation": the Xenos command stream is executed on the host GPU), and whose Android
shell (process model, data import, Turnip driver loading, touch pad, logs) is lifted from
ValDroid/RimDroid/Zomdroid. The game keeps its Xbox controller interface end to end: SDL3 feeds
the same `XINPUT_STATE` the console game reads; the touch overlay is a virtual Xbox pad.
A DAO-specific "seam" renderer (PM4 → Vulkan without EDRAM emulation, as mw2-android and
Rayman Origins Recomp do) is stage 2, started only after stage 1 measurements on the S25.

Why this and not the alternatives:

| Option | Verdict |
|---|---|
| DAORecomp runtime + Xenia Vulkan (stage 1) | Least new code to first picture. The runtime already implements DAO's exact kernel/GPU contract; Xenia's Vulkan backend implements the same `GraphicsSystem`/`CommandProcessor` interfaces as the D3D12 one DAO vendors. |
| Transplant DAO code onto ReXGlue/ShiftGlue | ShiftGlue/ReXGlue publish only linux-arm64, not Android/bionic; the DAO hooks, memory layout and `dao_host` contract would all have to be re-expressed. Not a drop-in. Rejected for stage 1. |
| Copy mw2-android's runtime and renderer | Best Android precedent, but GPL-3.0-only and MW2-tuned (no EDRAM aliasing, no tiling). Use as a **design reference** only unless the app is deliberately made GPL-3 (owner decision, see §7). |
| Win64 build under Wine/Box64/FEX | Not native; kept only as a comparison experiment if ever needed. |
| Full Xbox 360 emulator on the phone | Not the requested architecture. |

## 1. Hard facts established today

### 1.1 The game image
- `C:\Users\innaz\Downloads\Dragon Age - Origins (World) (En,Es,It).7z`, 6.72 GB; contains an
  XISO of 7.84 GB. `default.xex`: Title ID `454108C0`, Media ID `186A1115`, XEX2, SHA-256
  `70a3d469…9042c9fdba8391`. This is exactly the edition DAORecomp supports (disc build; Title
  Update 6/7 optional, needed only for Awakening/DLC).

### 1.2 The target phone (read over adb today)
- SM-S938B (Galaxy S25 Ultra), Android 17, Qualcomm, **page size 4096**, 11.4 GB RAM,
  `vulkan.adreno.so` present. Consequence: the 16 KB-page alias problem (§4.3) does not block
  the first prototype; it is still handled before any wider release.

### 1.3 DAORecomp (MIT, `96dot/Dragon-Age-Origins-Recomp`, main @ `1a6c668a`, 2026-10-21)
- 1034 files: `xenia/` 672 (vendored xenia-canary @ `1c81b867`, BSD), `DAORecomp/` 149 (runtime),
  `scripts/` 53, `thirdparty/` (antilag2, lmxxf-nr: desktop only), `DAORecompLib/` 4, `config/` 4.
- Runtime core that matters on Android: `kernel/` 7.4 k lines, `apu/` 2.2 k, `gpu/` 2.2 k,
  `input/`, `content/`, `patches/`, `main.cpp`, `program.cpp`, `settings.cpp`: ~15 k lines.
  Desktop-only and **not** ported: `launcher/`, `ui/` (except `keyboard.cpp` and `erf.cpp`),
  `mods/`, `neural/`: ~14 k lines.
- Win32 usage is concentrated (grep, ~150 call sites): `kernel/xboxkrnl_threading.cpp`,
  `kernel/import_stubs.cpp`, `gpu/xenia_host.cpp` (Win32 window), `main.cpp` (VEH crash handler,
  `timeBeginPeriod`), `kernel/xboxkrnl_io.cpp` (`CreateFileW`), `kernel/memory.cpp`
  (`VirtualAlloc2`/`MapViewOfFile3` placeholders), `kernel/objects.cpp` (`NtDelayExecution`
  high-resolution sleep), `kernel/thread.cpp` (`CreateThread`, `RtlVirtualUnwind` diagnostics).
- Already portable: `input/input.cpp` (SDL3 gamepad → Xbox state, rumble), `apu/audio_output.cpp`
  (SDL3 audio stream, 48 kHz F32), XMA decoder on Xenia's FFmpeg fork, `content/xiso.cpp`,
  `content/stfs.cpp`.
- The generated code is built as a separate static library (`DAORecompLib`) with SIMDe on the
  include path and `mmio_hook.h` force-included (`PPC_MM_STORE_U32` → fake command processor).
  Its clang-cl flags (`/fp:strict`, `/FI`, `-march=sandybridge`, `/clang:-ffp-contract=off`) need
  GCC-style equivalents for NDK clang (`-ffp-model=strict`, `-include`, `-ffp-contract=off`,
  no `-march`).
- **The vendored Xenia has no Vulkan at all**: only `gpu/d3d12` (39 files) and `ui/d3d12` (25),
  the DXBC shader translator, and DXBC shader bytecode. `xenia/CMakeLists.txt` filters out every
  `_android/_posix/_linux/_arm64` file and defines `WIN32 _AMD64`. The Android/POSIX base and UI
  files *are* present in the tree (`base/*_posix.cc`, `base/*_android.cc`, `ui/window_android.*`,
  `ui/surface_android.*`), just not compiled.
- Upstream xenia-canary at the same pinned commit has what we need: `src/xenia/gpu/vulkan/`
  (23 files), `src/xenia/ui/vulkan/` (30 files), `gpu/spirv_shader_translator*` (11 files),
  `android/android_studio_project/`.
- DAO's changes to Xenia are listed in `xenia/PATCHES.md`. Shared-GPU files changed:
  `gpu/command_processor.*`, `pm4_command_processor_implement.h`, `graphics_system.*`,
  `gpu_flags.cc`, `draw_util.*`, `primitive_processor.cc`, `shader_storage.h`, `shared_memory.*`,
  `texture_cache.*`, `memory.h` (replaced by a shim over DAO guest memory), `ui/presenter.*`.
  D3D12-only additions that need Vulkan counterparts in stage 1: resolve straight from render
  target into texture, `ReadBackGpuWrittenMemory` (save thumbnails), the frame-latency wait.
  D3D12-only additions that are **dropped**: FSR/XeSS/DLSS, frame generation, HDR, neural
  rendering, Anti-Lag/Reflex/Streamline, photo mode.
- `docs/porting-playbook.md` and `docs/x360-recomp-notes.md` document the hooks, the order things
  broke, the env-var test harness (`DAO_GPU=null`, `DAO_XMA=fake`, `DAO_INPUT_SCRIPT`,
  `DAO_CAPTURE_EVERY`) and the scripted-input regression recipes. All reusable on Android.
- Maintainer position (issue #2, 2026-10-17): no Android device, welcomes an Android build, no
  branch or plan of their own.

### 1.4 mw2-android (GPL-3.0-only, `OpenPort-Labs/mw2-android`, fork of PaulCombal/mw2-recompiled)
The closest working precedent: XenonRecomp title, ARM64 APK, Vulkan, Turnip loader, touch pad,
AAudio, 16 KB-page aware. What it teaches (design, not code, unless §7 decides otherwise):
- One CMake project builds both the PC exe and `libmw2.so`; the Gradle module (`android/app`)
  just points at it. NDK 28.2, minSdk 26, arm64-v8a only.
- Two processes: launcher, and `:game` hosting the runtime; the game is **killed**, never
  unwound. `GameService` (foreground, type `specialUse`) keeps `:game` out of the cached bucket.
- No SDL on Android: `ANativeWindow` from a `SurfaceView`, AAudio, `GamepadInput.kt` +
  `TouchOverlayView.kt` feeding the runtime's XInput state over JNI, game on its own pthread with
  a big stack (recompiled PPC nests deeply).
- Guest memory on Linux/Android: one 4 GiB `MAP_NORESERVE` mapping; the 512 MB physical bank is a
  `memfd` (fallback: unlinked file in the app cache when seccomp refuses `memfd_create`) mapped
  `MAP_FIXED|MAP_SHARED` at both windows. MW2 uses only the `0xA`/`0xC` windows, so it never hits
  DAO's `0xE0000000 + 0x1000` alias (§4.3).
- Renderer: their own PM4 → Vulkan "seam" renderer (`runtime/gpu/vulkan`, 16.7 k lines): image per
  EDRAM surface, no EDRAM emulation, Vulkan 1.3 dynamic rendering with a 1.1 render-pass fallback
  for Mali, software BC1–BC5 decode for GPUs without BC, shader cache shipped in assets.
  Performance claims: 1080p60 on Snapdragon 8-series. Not transferable to DAO numerically.
- Adreno: Turnip sideloaded through libadrenotools; thread pinning to prime/gold cores via
  `sched_setaffinity`; texture budget from device RAM; `onTrimMemory` quarters the cache.

### 1.5 Our launchers (all Java, our code): what is reused
Ranked by value; ValDroid/RimDroid copies are the newest.

| Piece | Where | Use in DAODroid |
|---|---|---|
| Turnip/custom ICD loading for a native Vulkan app | `RimDroid/unity/src/main/cpp/vkshim/{vkshim.c,driver.c,driver_hook.c}` + `app/src/main/cpp/liblinkernsbypass/` (same in ValDroid) | Load the chosen `libvulkan_freedreno*.so` in a private linker namespace, fall back to the system driver. Replaces mw2's libadrenotools. |
| Driver policy and import UI | `VulkanDriverPolicy.java` (+ unit test), `GpuInfo.java`, `GpuDriverAdvisor.java`, `CustomDriverInstaller.java` (AdrenoTools zip / raw .so), `fragments/DriverImportFragment.java`, Turnip `.so` set in `assets/bundles/libs.tar.xz` | As is. Also local `Desktop/termux_mesa/turnip_x/.../libvulkan_freedreno.so`. |
| Separate game process shell | `RimDroid/.../ClrActivity.java`, `ValDroid/.../NativeUnityActivity.java` + `NativeEngine.java` | Launcher in main process, `GameActivity` in `:game`; env via Intent `String[]` → `Os.setenv` before `System.loadLibrary`; immersive, keep-screen-on, `setFixedSize` render scale, `Surface.setFrameRate`. Add mw2's foreground `GameService` idea. |
| Vulkan present fixes | `RimDroid/app/src/main/cpp/vulkan_wrapper/vulkan_wrapper.c` | IDENTITY pre-transform, SUBOPTIMAL→SUCCESS, present counter; BC→ETC2 re-encode is the Mali fallback for Xbox DXT textures. |
| Virtual Xbox pad overlay + editor | Zomdroid `input/` (`StickControlElement`, `DpadControlElement`, `ButtonControlElement`, `RadialMenuControlElement`, `InputControlsView`, `ControlElementDescription` JSON, `IconPacks`, `ControlsEditorActivity`, `assets/default_controls.json`, `control_icon_packs/`) | Already emits `GAMEPAD_BUTTON_*`/axes/D-pad. Re-target `InputNativeInterface` to the runtime's XInput state (or SDL3 virtual joystick). |
| Physical pad hotplug + remap wizard | Zomdroid `input/GamepadManager.java`, `fragments/GamepadMapperFragment.java`; RimDroid `GamepadMapping.java`/`GamepadMapperActivity.java` | Optional if SDL3's Android gamepad layer is used; the wizard UI stays. |
| Data import | Zomdroid `InstallerService.java` (streams straight from the SAF Uri, FGS dataSync, progress broadcasts), `NewInstanceFragment`, `AppStorage`, `AppStorageProvider` (DocumentsProvider), `GameDataTransfer`/`DataTransferFragment` (save export/import) | Add an XISO/XEX/STFS extractor (port of DAORecomp `content/xiso.cpp`, `stfs.cpp`, `scripts/xiso.py`). |
| Logs and crash capture | Zomdroid `cpp/zomdroid.c` `install_crash_handler()` (altstack, unwind, dladdr, SIGSEGV left to debuggerd) + stdio→file thread; `CrashHandler.java`; RimDroid `LogExporter.java` (ApplicationExitInfo, ANR traces, zip via FileProvider), `LogcatReader.java`, `game/NativeLog.java` | As is. |
| Perf HUD | `PerfOverlayView.java` + `PerfSampler.java` | The S25 gate needs frame-time percentiles, thermal, RSS. |
| Build scaffolding | `RimDroid/app/build.gradle` (AGP 8.13.2, NDK 27.x, CMake 3.22.1, arm64 only, `c++_shared`, `ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON`, `pageSizeCompat`, git hash in BuildConfig, SHA-pinned fetch tasks), `game_mode_config.xml`, CI workflows | As is. `extractNativeLibs` not needed (nothing is exec'd). |
| Settings / instances / l10n | `LauncherPreferences`, `InstanceSettings`, `InstanceShortcuts`, `values-{es,pt,ru}`, wiki WebView | As is. |

Not carried over from any launcher: box64, Wine, glibc/x86_64 libs, the X server, Mono/.NET/
JVM, GL4ES/Zink/MobileGlues, the evdev virtual pad, audio shims (SDL3 or AAudio instead), Steam/
GOG downloaders, `MANAGE_EXTERNAL_STORAGE`.

## 2. Target architecture

```
PC (once per game build)                      Phone
───────────────────────────                   ─────────────────────────────────────────────
ISO ──xiso──► default.xex                     APK (arm64-v8a)
   ──XenonRecomp + DAO patches──► ppc/*.cpp     ├─ Launcher process: import, settings, driver pick, logs
   ──NDK clang (SIMDe)──► libdaorecomp_ppc.a    └─ :game process
                                                   ├─ libdaodroid.so = runtime + Xenia Vulkan + ppc code
                                                   ├─ guest memory: 4 GiB MAP_NORESERVE + memfd bank
                                                   ├─ kernel HLE (threads, objects, VFS, XAM, XMA)
                                                   ├─ SDL3 (Android): gamepad, haptics, audio stream
                                                   ├─ Xenia Vulkan: command processor, SPIR-V, presenter
                                                   └─ vkshim: Turnip or system vulkan.adreno.so
                                                 files/: game data (~8 GB), saves, shader cache, logs
```

- **Input contract**: `DAORecomp/input/input.cpp` keeps producing `XINPUT_STATE` (sticks,
  independent analog triggers, stick clicks, bumpers, face, D-pad, Start/Back, rumble). Two
  sources merge into it: SDL3's Android gamepad (USB/BT pads) and the touch overlay. The game's
  own console menus, radial selection, dialogue wheel and `XamShowKeyboardUI` replacement
  (`ui/keyboard.cpp`, pad-driven) stay as they are.
- **Window**: SDL3 owns the Android window/activity (it already provides Activity, JNI,
  lifecycle, gamepad, audio). Xenia's presenter is given the `ANativeWindow` behind the SDL window
  via `ui/surface_android.h` (`AndroidNativeWindowSurface`); Xenia's own `window_android`/
  `windowed_app_*android` are not used. This mirrors how `gpu/xenia_host.cpp` already bypasses
  Xenia's app loop on Windows.
- **Process model and lifetime**: as mw2/RimDroid: `:game` process, killed on exit; foreground
  service while the game is up; on `onStop` the guest is paused and the swapchain dropped,
  rebuilt on resume.

## 3. Phases with "done" criteria

Each phase ends with a short `docs/status.md` update (DAO playbook rule). Approval gates per
CLAUDE.md apply at every step: no edits, commits, pushes or builds without the explicit go-ahead.

| # | Phase | Done when | Risk retired |
|---|---|---|---|
| 0 | Decisions and repo | §7 answered; `DAO/` initialised as a repo with the layout in §5; license file chosen | scope |
| 1 | PC baseline | DAORecomp installed from *this* ISO on the PC: `ppc/` generated, game reaches a playable scene, save/reload, pad menus work; `scripts/test/run_game.sh` recipes pass. The generated `ppc/` is the input for everything after | "is this disc really supported" |
| 2 | ARM64 compile probe | `ppc/` + `DAORecompLib` compile with NDK clang (SIMDe) into a static lib, zero warnings promoted to errors as upstream requires; a tiny host harness calls a few leaf functions and compares results with x64 | VMX lane order, `-ffp-model=strict`, jump tables |
| 3 | Runtime on POSIX, headless | `kernel/`, `apu/`, `content/`, `main.cpp` build on Linux-ARM64/Android; `DAO_GPU=null` (fake command processor) reaches the steady frame loop with real XMA decode (`DAO_XMA=fake` is known to hang the game at loading). First run on the S25 via adb, no window yet | memory aliases, threads, TLS, sleeps, file I/O, XMA |
| 4 | First picture | Xenia Vulkan backend imported at the pinned commit with DAO's shared-GPU patches re-applied; `AndroidNativeWindowSurface` fed from SDL3; title screen and one repeatable scene render on the S25 (system driver and Turnip) | shader translation, EDRAM/tiling on Vulkan, resolves/readback |
| 5 | Playable | New game, naming via pad keyboard, dialogue, combat, inventory, save/load, 30 fps target at 1280×720; USB and BT pads, touch overlay; background/resume without stuck buttons | gameplay correctness |
| 6 | S25 gameplay gate | 20–30 min session with combat, dialogue, an area transition, save/reload; CPU/GPU frame times, p95/p99, RSS/PSS peak, skin temperature, hot vs cold fps recorded with the perf HUD | thermal, memory |
| 7 | Decide stage 2 | If GPU time dominates after Turnip and resolution scaling: design the DAO seam renderer (PM4 → Vulkan, no EDRAM, forced single tile without MSAA as the playbook's `sub_82661640` hook already allows). If CPU dominates: profile guest threads, affinity, spin-wait sleeps | performance |

Stop/reconsider conditions are the brief's: aliases not implementable, unsupported control flow
recurring, Vulkan correctness unreachable on Adreno, or stable play outside thermal/memory limits.

## 4. Technical notes per subsystem

### 4.1 Build system
- Keep DAORecomp's CMake shape: `xenia/` (now with `gpu/vulkan`, `ui/vulkan`, SPIR-V translator and
  the POSIX/Android base files un-filtered), `DAORecompLib/`, `DAORecomp/` → on Android a
  `SHARED` library instead of the exe, with `launcher/`, `ui/` (minus keyboard/erf), `mods/`,
  `neural/`, `cpu_check.cpp`, `res/` excluded. One CMake project drives both the PC exe and the
  `.so`, as mw2 does, so fixes land once.
- Shader pipeline: Xenia's `tools_build/compile_shader_spirv.py` (glslang) replaces
  `build_xenia_shaders.py` (FXC) for the Vulkan backend's own shaders; guest shaders are
  translated at run time by `spirv_shader_translator` and cached (Xenia's pipeline cache) in
  `files/shader_cache`.
- Toolchain on this PC today: Android SDK at `%LOCALAPPDATA%\Android\Sdk` with NDK 27.0/27.3,
  CMake 3.22.1, platforms 35/36, adb; Python 3.13; git. No standalone clang/cmake/java on PATH
  (Android Studio bundles its JDK). DAORecomp's own installer downloads portable LLVM + xwin for
  the PC build.

### 4.2 Kernel port (Win32 → POSIX), file by file
- `kernel/memory.cpp`: `VirtualAlloc2`/`MapViewOfFile3` placeholders → `mmap(4 GiB, MAP_NORESERVE)`
  + `memfd_create` (fallback: unlinked cache file) mapped `MAP_FIXED|MAP_SHARED` at `0xA`, `0xC`
  and `0xE`; null page `mprotect(PROT_NONE)`. Keep the `PageHeap` bookkeeping untouched.
- `kernel/thread.cpp`: `CreateThread` → `pthread_create` with a 16 MB stack (`/STACK:16777216` on
  Windows; mw2 confirms the default 1 MB is not enough); `SetThreadDescription` → `pthread_setname_np`;
  suspend/resume and `RtlVirtualUnwind` thread dumps → `_Unwind_Backtrace` + `dladdr` (Zomdroid's
  crash handler) or drop on Android.
- `kernel/objects.cpp`: `NtDelayExecution`/high-resolution timer → `clock_nanosleep`; keep the
  10 µs spin-wait sleep policy (it is what holds CPU at ~25 %).
- `kernel/xboxkrnl_io.cpp`: `CreateFileW` → `open`; paths UTF-8 already.
- `main.cpp`: VEH crash handler → `sigaction` on an alt stack; `timeBeginPeriod` dropped;
  `QueryPerformanceCounter` → `clock_gettime(CLOCK_MONOTONIC)`.
- `gpu/xenia_host.cpp`: the Win32 window/monitor code goes; the interrupt dispatch, guest-thread
  attach and `ShortSleep` stay (they are the whole `dao_host.h` contract: 4 functions).
- `content/install.cpp` and the STFS/XISO readers: portable; they become the phone-side importer.

### 4.3 Guest memory and page size
- DAO maps the `0xE0000000` window at bank offset `+0x1000` because the game's D3D computes GPU
  addresses that way. `mmap` needs the file offset page-aligned, so this alias is impossible on a
  16 KB-page kernel. **The S25 has 4 KB pages, so stage 1 is unaffected.**
- Before any wider release, test on the PC build: make `ViewBaseForPageSize()` never return
  `0xE0000000` (serve 4 KB-page physical requests from the `0xA` window). The inlined
  `MmGetPhysicalAddress` formula is window-agnostic, so if the game runs unchanged the alias is
  not needed and 16 KB devices are supported for free. If it does not, 16 KB devices stay
  unsupported (Android's `pageSizeCompat` cannot help a guest mapping).
- Xenia's shared-memory write-watch works at host page size; 4 KB on this device.

### 4.4 GPU, stage 1 (Xenia Vulkan)
- Import from xenia-canary `1c81b867`: `gpu/vulkan/*` (minus `*_main.cc`), `ui/vulkan/*`,
  `gpu/spirv_*`, `base/*_posix.cc`, `base/*_android.cc`, `ui/surface_android.*`, Vulkan
  third-party (Vulkan-Headers, VMA, glslang/SPIRV-Tools from the xenia-canary submodules).
- Re-apply DAO's shared-GPU diffs (PATCHES.md list) by `git show 1c81b867:<file> | diff` as the
  file documents. Port the four D3D12-only DAO behaviours that gameplay needs: resolve straight
  into a texture, `ReadBackGpuWrittenMemory` (save thumbnails, picking), present-interval clamp
  (already a `PPC_FUNC` hook, API-neutral), frame-latency wait.
- Expectation to state honestly: Xenia's Vulkan path on a phone emulates EDRAM and predicated
  tiling; Rayman Origins Recomp measured it "slow with stalls" on an S23 for a 2D game. Stage 1
  is a correctness baseline and a bring-up vehicle, not the performance answer. Scaling knobs
  available immediately: render at 1280×720 or lower, force single tile without MSAA (playbook
  hook), Turnip instead of the stock driver.

### 4.5 GPU, stage 2 (DAO seam renderer, optional)
- Same seam as mw2/Rayman: execute PM4 in our own command processor (DAO already has
  `gpu/command_processor.cpp` in "fake" form), key an image per EDRAM surface, translate guest
  shaders once with XenosRecomp to SPIR-V, no EDRAM emulation. Game-specific work: DAO's
  render-target usage (shadow maps, post effects, resolves for the UI), vertex layouts, the
  Scaleform UI pass. mw2's documents (`docs/rendering.md`, `d3d9-seam.md`, `shaders.md`,
  `textures.md`) are the reference for the design; copying their code is a license decision (§7).

### 4.6 Audio
- `apu/xma_decoder.cpp` + Xenia FFmpeg subset build for ARM64 as is; output stays SDL3 audio
  (`SDL_OpenAudioDeviceStream`, 48 kHz F32) on top of AAudio. Verify callback cadence and movie
  (Bink) sync early; the playbook says the game hangs at loading without real XMA.

### 4.7 Input (the "transfer the gamepad" requirement)
- Physical pads: SDL3 Android gamepad (`SDL_INIT_GAMEPAD|SDL_INIT_HAPTIC`), same code as PC,
  `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` kept, rumble via `SDL_RumbleGamepad`.
- Touch: Zomdroid's virtual Xbox pad overlay in the `:game` activity, delivered over JNI into
  the same `XINPUT_STATE` merge that `DAO_INPUT_SCRIPT` already uses for scripted input (one
  logical player, no duplicate presses).
- Acceptance list from the brief: USB and BT pads, reconnect, slot identity, movement/camera/
  targeting/radial/held buttons/trigger combos, naming, dialogue, inventory, party commands,
  pause, dead zones and axis signs, background/resume, pad + touch sharing one player.

### 4.8 Packaging and data
- APK: ~tens of MB of native code (`libdaodroid.so`), Turnip `.so` set, shaders. Game data
  (~8 GB extracted; measure) imported once from the ISO/7z through the SAF picker into
  `files/game/`; saves in `files/state/`; shader cache and logs beside them. `AppStorageProvider`
  makes them visible to file managers. Nothing from the disc is in the APK or the repo.
- First build on the PC takes 30–60 min (DAORecomp's own figure); the Android library build is
  comparable (2.4 M lines of generated C++ in mw2's case; DAO's size is known after phase 1).

## 5. Proposed repository layout (`C:\Users\innaz\Desktop\DAO`)

```
DAO/
  docs/            this plan, status.md, briefs
  recomp/          DAORecomp checkout as a git submodule (or vendored), with our patches on a branch
  xenia-vulkan/    the imported Vulkan backend files + the re-applied DAO patches
  android/         Gradle project (from RimDroid's build.gradle template), app + :game process
  launcher-java/   reused ValDroid/RimDroid/Zomdroid Java: driver policy, importer, controls, logs
  scripts/         xiso/xex tooling, NDK build driver, regression recipes (ported run_game.sh)
```
`ppc/` (generated game code) and any extracted game data stay out of git, as upstream does.

## 6. Effort (planning judgement, not a promise)
- Phase 1–2: days. Phase 3: one to three weeks. Phase 4: weeks to a couple of months (the
  Vulkan import and DAO patch re-application is the bulk). Phase 5–6: weeks. Stage 2 renderer:
  a separate multi-month effort, only if measurements demand it.

## 7. Decisions needed from the owner before phase 0
1. **License stance.** MIT (DAORecomp) + BSD (Xenia) + our Java is the clean default and keeps
   mw2-android as a design reference only. Taking mw2's runtime/renderer code makes the app
   GPL-3.0.
2. **Phase 1 on this PC.** DAORecomp's installer needs ~25 GB and 30–60 min of CPU, and produces
   the `ppc/` sources the whole port depends on. Who runs it, and when.
3. **Window layer.** SDL3-owned Android activity (recommended: DAO already depends on SDL3 for
   input and audio) versus an mw2-style hand-written `SurfaceView` + JNI shell.
4. **Repo bootstrap.** `git init` in `DAO/` with the layout of §5, DAORecomp as a submodule.

## 8. Sources read
- DAORecomp main @ 1a6c668a: `CMakeLists.txt` (root, `DAORecomp/`, `DAORecompLib/`, `xenia/`),
  `docs/porting-playbook.md`, `docs/x360-recomp-notes.md`, `xenia/PATCHES.md`, `xenia/dao_host.h`,
  `DAORecomp/kernel/memory.{cpp,h}`, `thread.cpp`, `objects.cpp`, `xboxkrnl_*.cpp`, `input/input.cpp`,
  `apu/audio_output.cpp`, `gpu/xenia_host.cpp`, `DAORecompLib/mmio_hook.h`; GitHub issue #2.
- xenia-canary @ 1c81b867: directory listings of `src/xenia/gpu/vulkan`, `ui/vulkan`, `gpu/spirv_*`, `android/`.
- mw2-android master: `README.md`, `docs/{rendering,d3d9-seam,runtime,android}.md`, `LICENSE`,
  `runtime/guest_memory.cpp`, `runtime/android/jni.cpp`, `android/app/build.gradle.kts`, tree.
- Rayman Origins Recomp `docs/NATIVE_RENDERER.md`; pinyon-shift `docs/ANDROID.md`; ShiftGlue README.
- Local: `RimDroid/docs/BRIEF_dragon_age_origins_android_research.md`, the four launchers
  (subagent surveys), `Desktop/termux_mesa` (Turnip build), the phone over adb.
