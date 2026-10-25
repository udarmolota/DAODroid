"""Makes a DAODroid game package (.daopkg, docs/PACKAGE_FORMAT.md) from the extracted game files
and a built game library, or checks one.

    make_package.py build --game <install/game> --library <libdaodroid.so> [--title-update <dir>] --out <dir>
    make_package.py check <package.daopkg>

The package holds the game's translated code and data: it is the player's own copy, never to be
shared. This is the packaging step of the PC tool; for now the library comes from the app's dev
build (android/app/build/intermediates/cxx/.../libdaodroid.so).
"""
import argparse
import datetime
import hashlib
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "recomp"))
from installer import deps  # noqa: E402  (recomp/installer/deps.py: the disc and title update pins)

FORMAT = 1
TOOL = "make_package.py 1"
LIBRARY_ENTRY = "lib/arm64-v8a/libdaodroid.so"
TITLE_ID = "454108C0"
MEDIA_ID = "186A1115"


def abi_version():
    """DAO_ABI_VERSION from android/app/src/main/cpp/dao_abi.h."""
    text = (ROOT / "android" / "app" / "src" / "main" / "cpp" / "dao_abi.h").read_text(encoding="utf-8")
    m = re.search(r"#define\s+DAO_ABI_VERSION\s+(\d+)", text)
    if not m:
        sys.exit("dao_abi.h has no DAO_ABI_VERSION")
    return int(m.group(1))


def git_commit(path):
    try:
        out = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True, text=True, check=True)
        dirty = subprocess.run(["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"],
                               capture_output=True, text=True, check=True).stdout.strip()
        return out.stdout.strip() + ("-dirty" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def file_hash(path, algo):
    h = hashlib.new(algo)
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def title_update_version(folder):
    if not folder:
        return None
    xexp = Path(folder) / "default.xexp"
    if not xexp.is_file():
        sys.exit(f"{folder} has no default.xexp")
    version = deps.TU_XEXP_SHA1S.get(file_hash(xexp, "sha1"))
    if not version:
        sys.exit(f"{xexp} is not Title Update 6 or 7 for this disc")
    return version


def build(args):
    game = Path(args.game).resolve()
    library = Path(args.library).resolve()
    xex = game / "default.xex"
    if not xex.is_file():
        sys.exit(f"No default.xex in {game}: point --game at DAORecomp's install/game")
    xex_sha1 = file_hash(xex, "sha1")
    if xex_sha1 != deps.GAME_XEX_SHA1:
        sys.exit(f"{xex} isn't the supported disc's (SHA-1 {xex_sha1})")
    if not library.is_file():
        sys.exit(f"No game library at {library}")
    title_update = title_update_version(args.title_update)

    files = sorted(p for p in game.rglob("*") if p.is_file())
    game_bytes = sum(p.stat().st_size for p in files)
    library_sha256 = file_hash(library, "sha256")
    daodroid = git_commit(ROOT)
    manifest = {
        "format": FORMAT,
        "abi": abi_version(),
        "title_id": TITLE_ID,
        "media_id": MEDIA_ID,
        "xex_sha1": xex_sha1,
        "title_update": title_update,
        "build": {
            "daodroid": daodroid,
            "daorecomp": git_commit(ROOT / "recomp"),
            "created": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "tool": TOOL,
        },
        "library_sha256": library_sha256,
        "game_bytes": game_bytes,
        "game_files": len(files),
    }

    out_dir = Path(args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"DAO-{TITLE_ID}-{daodroid[:8]}.daopkg"
    partial = out.with_suffix(".daopkg.partial")
    print(f"Writing {out} ({len(files)} game files, {game_bytes / 2**30:.1f} GB)")
    with zipfile.ZipFile(partial, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2) + "\n")
        z.write(library, LIBRARY_ENTRY)
        done = 0
        for i, p in enumerate(files):
            z.write(p, "game/" + p.relative_to(game).as_posix())
            done += p.stat().st_size
            if i % 200 == 0:
                print(f"  {done * 100 // max(game_bytes, 1)}%", flush=True)
        if args.title_update:
            z.write(Path(args.title_update) / "default.xexp", "title_update/default.xexp")
    partial.replace(out)
    print(f"Done: {out} ({out.stat().st_size / 2**30:.2f} GB)")
    print("This package is your own copy of the game: don't share it.")


def check(args):
    path = Path(args.package)
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        if not names or names[0] != "manifest.json":
            sys.exit("manifest.json isn't the first entry")
        m = json.loads(z.read("manifest.json"))
        problems = []
        if m.get("format") != FORMAT:
            problems.append(f"format {m.get('format')} (this tool knows {FORMAT})")
        if m.get("abi") != abi_version():
            problems.append(f"abi {m.get('abi')}, this tree's is {abi_version()}")
        if m.get("xex_sha1") != deps.GAME_XEX_SHA1:
            problems.append("an unknown disc (xex_sha1)")
        if LIBRARY_ENTRY not in names:
            problems.append(f"no {LIBRARY_ENTRY}")
        else:
            h = hashlib.sha256()
            with z.open(LIBRARY_ENTRY) as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            if h.hexdigest() != m.get("library_sha256"):
                problems.append("the library doesn't match library_sha256")
        game = [i for i in z.infolist() if i.filename.startswith("game/") and not i.is_dir()]
        if len(game) != m.get("game_files") or sum(i.file_size for i in game) != m.get("game_bytes"):
            problems.append("the game files don't match game_files/game_bytes")
        if "game/default.xex" not in names:
            problems.append("no game/default.xex")
        compressed = [i.filename for i in z.infolist() if i.compress_type != zipfile.ZIP_STORED]
        if compressed:
            problems.append(f"{len(compressed)} compressed entries (must be stored)")
    print(json.dumps({k: m[k] for k in ("format", "abi", "title_id", "media_id", "title_update", "build")}, indent=2))
    if problems:
        sys.exit("Not a valid package: " + "; ".join(problems))
    print(f"OK: {len(game)} game files, {m['game_bytes'] / 2**30:.1f} GB")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build")
    b.add_argument("--game", required=True)
    b.add_argument("--library", required=True)
    b.add_argument("--title-update")
    b.add_argument("--out", required=True)
    c = sub.add_parser("check")
    c.add_argument("package")
    args = parser.parse_args()
    build(args) if args.command == "build" else check(args)


if __name__ == "__main__":
    main()
