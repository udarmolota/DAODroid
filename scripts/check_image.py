#!/usr/bin/env python3
"""Checks a Dragon Age: Origins (Xbox 360) disc image against what DAORecomp expects, without
extracting it: the XDVDFS partition, default.xex's size and SHA-1 (deps.GAME_XEX_SHA1), and the
XEX header's title and media IDs. Run it before the 30-60 minute install.

    python -I scripts/check_image.py "<path to .iso>"

Exit code 0 when the image is the supported edition, 1 otherwise. Read-only.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RECOMP = ROOT / "recomp"
sys.path.insert(0, str(RECOMP / "scripts"))
sys.path.insert(0, str(RECOMP))
import xiso  # noqa: E402  (recomp/scripts/xiso.py)
from installer import deps  # noqa: E402  (recomp/installer/deps.py: the pinned hashes and IDs)

XEX_MAGIC = b"XEX2"
XEX_EXECUTION_ID_KEY = 0x00040006  # XEX header key: media ID, version, base version, title ID, ...


def xex_ids(f, base, sector, size):
    """The title ID and media ID from a XEX2 header read in place."""
    f.seek(base + sector * xiso.SECTOR)
    header = f.read(min(size, 64 * 1024))
    if header[:4] != XEX_MAGIC:
        return None, None
    header_count = struct.unpack(">I", header[0x14:0x18])[0]
    pos = 0x18
    for _ in range(header_count):
        key, value = struct.unpack(">II", header[pos:pos + 8])
        pos += 8
        if key == XEX_EXECUTION_ID_KEY:
            media_id, _version, _base_version, title_id = struct.unpack(">IIII", header[value:value + 16])
            return title_id, media_id
    return None, None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    iso = Path(sys.argv[1])
    print(f"Image: {iso} ({iso.stat().st_size:,} bytes)")
    with open(iso, "rb") as f:
        base, entries = xiso.read_entries(f)
        print(f"XDVDFS partition at 0x{base:X}, {len(entries)} entries")
        xex = [(p, d, s, z) for p, d, s, z in entries if p.lower() == "default.xex" and not d]
        if not xex:
            print("FAIL: no default.xex in the image root")
            return 1
        path, _, sector, size = xex[0]
        title_id, media_id = xex_ids(f, base, sector, size)
        print(f"default.xex: {size:,} bytes, title ID {title_id:08X}, media ID {media_id:08X}")
        print("Hashing default.xex (SHA-1)...")
        sha1 = xiso.hash_file(f, base, sector, size, "sha1")
    ok = True
    for label, got, want in (("title ID", title_id, deps.GAME_TITLE_ID),
                             ("media ID", media_id, deps.GAME_MEDIA_ID)):
        state = "ok" if got == want else "MISMATCH"
        ok &= got == want
        print(f"  {label}: {got:08X} (expected {want:08X}) {state}")
    state = "ok" if sha1 == deps.GAME_XEX_SHA1 else "MISMATCH"
    ok &= sha1 == deps.GAME_XEX_SHA1
    print(f"  SHA-1: {sha1} (expected {deps.GAME_XEX_SHA1}) {state}")
    print("RESULT: supported edition, the installer will accept it" if ok
          else "RESULT: not the edition DAORecomp supports; do not start the install")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
