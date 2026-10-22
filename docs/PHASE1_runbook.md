# Phase 1 runbook: the PC baseline (DAORecomp built from our disc)

Purpose: generate the recompiled game code (`recomp/ppc/`) that every later phase compiles for
ARM64, and prove the disc, the recompiler fixes and the runtime work together before anything is
debugged on Android. Nothing here touches the phone.

Status of this runbook: prepared 2026-10-22, **not yet run**. The build is started only on the
owner's direct command (CLAUDE.md gate #4).

## What is already in place

| Item | State |
|---|---|
| Repository | `C:\Users\innaz\Desktop\DAO`, git initialised, no commits yet |
| DAORecomp | submodule `recomp/` pinned to `1a6c668a5764ef497175ca1e3b9e6f2508f1c9a4` (main, 2026-10-21) |
| Disc image | `image\Dragon Age - Origins (World) (En,Es,It).iso` (7,838,695,424 bytes), extracted from the 7z in Downloads with Windows `tar.exe` (bsdtar); folder is gitignored |
| Install target | `install\` (gitignored); the installer creates `install\game\` (~8 GB) and `install\DAORecomp.exe` |
| Image check | `scripts\check_image.py` compares `default.xex`'s title ID, media ID and SHA-1 with the installer's pins (`recomp/installer/deps.py`). **Run 2026-10-22: all three match** (SHA-1 `532c0ec164f44bc8784cc0e8f6f67f72c1ed3751`) |

## This PC (checked 2026-10-22)

| | |
|---|---|
| CPU | Intel i7-11370H, 4 cores / 8 threads, AVX2 → the installer picks `-march=x86-64-v3` |
| RAM | 15.7 GB |
| GPU | NVIDIA RTX 3050 Laptop (D3D12, driver 32.0.15.5597) + Intel Iris Xe |
| Disk C: | 232 GB free (needs ~25 GB: 8 GB game + ~12 GB tools and build + the ISO's 7.8 GB until the game files are extracted) |
| OS | Windows 11 26200 |
| Python | 3.13.15 with Tkinter (`C:\Users\innaz\AppData\Local\Programs\Python\Python313\python.exe`) |
| git | 2.56 |
| Not present | standalone LLVM, standalone CMake, 7-Zip (not needed: bsdtar handled the 7z) |

The 4-core CPU puts the first build at the long end of DAORecomp's "30–60 minutes".

## What the installer does (recomp/installer/pipeline.py, 7 steps)

1. **Checks the disc image** (partition, `default.xex` SHA-1 `532c0ec1…`, title `454108C0`,
   media `186A1115`) and the host (AVX).
2. **Extracts the game files** to `install\game\` (~8 GB). After this step the ISO is no longer
   needed; `install\game\dao-extracted.txt` marks it.
3. **Fetches sources** into `recomp\tools\` and `recomp\thirdparty\` (all pinned, `deps.py`):
   xenia-canary third-party (sparse), DirectX-Headers, DXC headers, FFmpeg (has207 fork),
   cxxopts, date, disruptorplus, fmt, imgui, rapidjson, snappy, tomlplusplus, utfcpp, xbyak,
   xxhash, XenonRecomp (+ submodules), SDL 3.4.18, FidelityFX SDK (sparse).
4. **Sets up the compiler**: LLVM 22.1.8 for Windows (862 MB download, `recomp\tools\llvm`),
   xwin 0.10.0 + MSVC CRT 14.44 + Windows SDK 10.0.26100 (~85 MB, `recomp\tools\winsysroot`,
   needs the Microsoft license accepted: `--accept-msvc-license`), CMake 4.4.3, Ninja 1.13.2.
   Also XeSS 3.0.2 (77 MB) and NVIDIA Streamline 2.14.1 (276 MB): desktop-only upscalers the
   Windows build links; unavoidable without patching the installer, harmless for us.
5. **Builds the recompiler** (XenonRecomp patched by `scripts/patch_recompiler.py`) and
   **recompiles the game** into `recomp\ppc\` (a few minutes). This folder is phase 2's input.
6. **Builds the game** (`recomp\build\`), the long step.
7. **Installs** to `install\`: `DAORecomp.exe`, upscaler DLLs, licenses, `mods\README.txt`.

Interrupting with Ctrl+C is safe: "running the installer again resumes where it stopped".
Everything it writes stays under `recomp\tools`, `recomp\thirdparty`, `recomp\build`,
`recomp\ppc`, `recomp\analysis` and `install\`; all gitignored (ours and the submodule's).

## Commands, in order

Step 0, before the build (read-only, a minute; hashes 19 MB inside the ISO):

```bash
python -I "C:\Users\innaz\Desktop\DAO\scripts\check_image.py" "C:\Users\innaz\Desktop\DAO\image\Dragon Age - Origins (World) (En,Es,It).iso"
```

Expected: `RESULT: supported edition`. Anything else: stop, do not install.

Step 1, the install (the long one; `--jobs 6` leaves two threads for the PC to stay usable; drop
it for all 8):

```bash
cd "C:\Users\innaz\Desktop\DAO\recomp" && python install.py --iso "C:\Users\innaz\Desktop\DAO\image\Dragon Age - Origins (World) (En,Es,It).iso" --dest "C:\Users\innaz\Desktop\DAO\install" --accept-msvc-license --llvm download --jobs 6
```

Notes on the flags: `--accept-msvc-license` answers the xwin license question
(https://go.microsoft.com/fwlink/?LinkId=2086102) so the run needs no keyboard; `--llvm download`
takes the pinned LLVM instead of searching the PC (there is none). No `--shortcuts`: nothing is
added to the Start menu or desktop. No `--title-update`: the disc build only (TU6/7 is for
Awakening and the DLC campaigns; it needs the `.xexp` package, which we do not have).

Optional, better certificate handling for the downloads (the installer uses it if present):

```bash
python -m pip install truststore==0.10.4
```

## After the build: the baseline checks (phase 1 "done")

Run from `install\` (settings in `install\userdata\config.toml`, logs in `install\userdata\*.log`):

1. `DAORecomp.exe` opens the launcher drawn with the disc's title art; **Play** starts the game.
   First run compiles shaders on use (objects may pop in).
2. With a pad (SDL3: Xbox or DualSense over USB/BT): menus, new game, character naming through the
   on-screen keyboard (`ui/keyboard.cpp`), a dialogue, a fight, inventory, save, quit, load.
   Keep these saves apart from any real ones (none exist yet).
3. Scripted regression, the recipes DAORecomp ships (`recomp\scripts\test\README.md`,
   `run_game.sh`, inputs in `scripts\test\inputs\`): load latest save, walk, camera, options
   menu, save game. They give us a repeatable PC reference for every Android comparison later.
4. Record in `docs\status.md`: build time, `recomp\ppc\` size and file count, fps at the title
   and in a scene, any warnings from XenonRecomp (`recomp\analysis\recomp_log.txt`).

Two PC experiments worth doing right after, because they de-risk Android phases (each is a run
with an environment variable, not a code change):

- `DAO_GPU=null` + `DAO_XMA=fake`/real: the headless loop phase 3 reproduces on the phone.
- `ViewBaseForPageSize()` never returning `0xE0000000` (this one is a one-line code experiment,
  to be approved separately): tells whether 16 KB-page phones can be supported at all.

## What this phase does not do

- No Android build, no NDK, no APK, nothing installed on the phone.
- No change to DAORecomp's sources (the submodule stays at the pinned commit; our patches come in
  phase 3 on a branch).
- No commit: the repository state is for review first.
