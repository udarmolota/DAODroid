# DAODroid status

One short file, updated at the end of every working session (DAORecomp playbook rule). Newest
entry first. Plan: `PLAN_dao_android_native_port.md`; phase 1 steps: `PHASE1_runbook.md`.

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
