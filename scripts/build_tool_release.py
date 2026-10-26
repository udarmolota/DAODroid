"""Builds what players download for the PC: dist/DAODroid-Tool-<version>.zip, a folder that runs
without installing anything: unzip, double-click "DAODroid Tool.cmd", pick the disc image.

    python scripts/build_tool_release.py [--version 0.2.0]

Inside:
  python/   python.org's embeddable Python (signed by the Python Software Foundation), with Tk
            for the window copied from this machine's Python of the same version, and
            truststore (as DAORecomp's installer bundles it)
  src/      the files git tracks in this repository and in recomp/ (DAORecomp, the daodroid
            branch): the tool, the runtime and Xenia sources it compiles. No game code or data,
            no build output; the launcher's APK-only files (jniLibs) are left out.
  DAODroid Tool.cmd
The tools the tool needs (LLVM, the Windows SDK headers, XenonRecomp, the NDK, portable git)
are downloaded by it on its first run, pinned and checked, as DAORecomp's installer does.
(Not PyInstaller: DAORecomp moved away from it after antivirus false positives.)
"""
import argparse
import hashlib
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build" / "tool-release"
DIST = ROOT / "dist"

# The embeddable Python matching this machine's (its Tk is copied in), from python.org.
PY_VERSION = f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}"
EMBED_URL = f"https://www.python.org/ftp/python/{PY_VERSION}/python-{PY_VERSION}-embed-amd64.zip"

CMD = r"""@echo off
rem DAODroid's PC tool: your own Dragon Age: Origins Xbox 360 disc image in, a game package
rem for the DAODroid launcher out. Nothing to install: this folder carries its own Python.
cd /d "%~dp0"
start "" "%~dp0python\pythonw.exe" -I "%~dp0src\scripts\daodroid_tool_gui.py"
"""

README = """DAODroid's PC tool
==================

1. Unzip this folder somewhere with about 30 GB free (the game, the tools, the build).
2. Double-click "DAODroid Tool.cmd".
3. Pick your Dragon Age: Origins Xbox 360 disc image (.iso) and a folder for the package.
4. Wait (about an hour the first time: it downloads and sets up its tools).
5. Copy the .daopkg file to your phone and add it in the DAODroid launcher (+).

The package is your own copy of the game: don't share it.
Source and help: https://github.com/udarmolota/DAODroid
"""


def tracked(repo):
    out = subprocess.run(["git", "-C", str(repo), "ls-files", "-z"], capture_output=True, check=True, text=True).stdout
    return [f for f in out.split("\0") if f]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", default="dev")
    args = parser.parse_args()
    if sys.platform != "win32":
        sys.exit("Built on Windows (the folder runs on Windows).")

    stage = WORK / "DAODroid-Tool"
    if stage.exists():
        shutil.rmtree(stage)
    (stage / "python").mkdir(parents=True)

    # The embeddable Python.
    downloads = WORK / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    embed = downloads / EMBED_URL.rsplit("/", 1)[1]
    if not embed.is_file():
        print(f"Downloading {EMBED_URL}")
        with urllib.request.urlopen(EMBED_URL, timeout=60) as r, open(embed, "wb") as f:
            shutil.copyfileobj(r, f)
    print(f"{embed.name}: SHA-256 {hashlib.sha256(embed.read_bytes()).hexdigest()}")
    with zipfile.ZipFile(embed) as z:
        z.extractall(stage / "python")

    # Tk for the window: the embeddable package has none; this Python's, same version.
    base = Path(sys.base_prefix)
    for name in ("_tkinter.pyd", "tcl86t.dll", "tk86t.dll", "zlib1.dll"):
        src = base / "DLLs" / name
        if src.is_file():
            shutil.copy2(src, stage / "python" / name)
    shutil.copytree(base / "tcl", stage / "python" / "tcl",
                    ignore=shutil.ignore_patterns("*.lib", "*.sh", "nmake", "tix*"))
    shutil.copytree(base / "Lib" / "tkinter", stage / "python" / "Lib" / "tkinter",
                    ignore=shutil.ignore_patterns("__pycache__", "test"))
    # truststore (downloads checked with Windows' certificate verifier, so a fresh PC doesn't
    # fail on GitHub's), pinned by hash in DAORecomp's requirements, unpacked into Lib.
    wheels = WORK / "wheels"
    if wheels.exists():
        shutil.rmtree(wheels)
    subprocess.run([sys.executable, "-m", "pip", "download", "--quiet", "--require-hashes", "--no-deps",
                    "--only-binary", ":all:", "-r", str(ROOT / "recomp" / "scripts" / "installer-requirements.txt"),
                    "-d", str(wheels)], check=True)
    for wheel in wheels.glob("*.whl"):
        with zipfile.ZipFile(wheel) as z:
            z.extractall(stage / "python" / "Lib")
    # The ._pth lists the search path: add Lib (tkinter, truststore) and keep site off.
    pth = next((stage / "python").glob("python3*._pth"))
    lines = pth.read_text(encoding="utf-8").splitlines()
    if "Lib" not in lines:
        lines.insert(1, "Lib")
    pth.write_text("\n".join(lines) + "\n", encoding="utf-8")

    # The source: what git tracks here and in recomp/.
    count = 0
    for repo, prefix in ((ROOT, ""), (ROOT / "recomp", "recomp/")):
        for rel in tracked(repo):
            if repo == ROOT and rel == "recomp":
                continue  # the submodule itself, listed next
            if rel.startswith((".github/", "android/app/src/main/jniLibs/")) or rel.endswith(".mp4"):
                continue
            src = repo / rel
            if not src.is_file():
                continue
            dst = stage / "src" / (prefix + rel)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            count += 1
    (stage / "src" / "recomp" / "VERSION").write_text(
        subprocess.run(["git", "-C", str(ROOT / "recomp"), "describe", "--tags", "--always"], capture_output=True,
                       text=True).stdout.strip() or "daodroid", encoding="utf-8")
    (stage / "DAODroid Tool.cmd").write_text(CMD.replace("\n", "\r\n"), encoding="utf-8")
    (stage / "README.txt").write_text(README.replace("\n", "\r\n"), encoding="utf-8")
    print(f"Source: {count} files")

    DIST.mkdir(exist_ok=True)
    out = DIST / f"DAODroid-Tool-{args.version}.zip"
    if out.exists():
        out.unlink()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in sorted(stage.rglob("*")):
            if p.is_file():
                z.write(p, Path("DAODroid-Tool") / p.relative_to(stage))
    print(f"Built {out} ({out.stat().st_size / 2**20:.1f} MB)")


if __name__ == "__main__":
    main()
