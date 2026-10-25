# DAODroid game package (`.daopkg`)

What the PC tool makes from a player's own disc and the launcher imports: the game files and
the game library built for that disc. **A package holds the game's translated code and data:
it is that player's own copy and must never be shared.** The launcher refuses nothing based on
where a package came from (it can't tell), so this is a rule for people, stated in the README
and in the launcher.

## Container

A ZIP file (ZIP64: the game is 6.5 GB), entries **stored, not compressed** (the game's files are
already compressed; storing makes building and importing a plain copy). The file name is free;
the tool writes `DAO-<title id>-<short build id>.daopkg`.

```
manifest.json                    first entry, so a launcher can read it without scanning
lib/arm64-v8a/libdaodroid.so     the game library: DAORecomp's runtime, Xenia, the translated code
game/default.xex                 the game files as DAORecomp's installer extracts them
game/...
title_update/default.xexp        optional: Title Update 6 or 7 (when the code was built with it)
```

Nothing else: no saves, no settings, no drivers (those live in the launcher's instance).

## manifest.json

```json
{
  "format": 1,
  "abi": 1,
  "title_id": "454108C0",
  "media_id": "186A1115",
  "xex_sha1": "532c0ec164f44bc8784cc0e8f6f67f72c1ed3751",
  "title_update": null,
  "build": {
    "daodroid": "<git commit of this repository>",
    "daorecomp": "<git commit of recomp/, branch daodroid>",
    "created": "2026-10-25T14:00:00Z",
    "tool": "make_package.py 1"
  },
  "library_sha256": "<sha256 of lib/arm64-v8a/libdaodroid.so>",
  "game_bytes": 6957432832,
  "game_files": 1234
}
```

| Field | Meaning | The launcher |
|---|---|---|
| `format` | this layout's version | refuses a newer one ("update the launcher") |
| `abi` | `DAO_ABI_VERSION` the library was built with (`android/app/src/main/cpp/dao_abi.h`) | refuses a mismatch ("build the package again with tools matching this launcher" / "update the launcher") |
| `title_id`, `media_id`, `xex_sha1` | the disc the code was translated from (DAORecomp's pins) | shows them; refuses an unknown `xex_sha1` |
| `title_update` | `null`, `6` or `7` | shows it |
| `build` | where the package came from, for bug reports | shows it in the instance's details and logs |
| `library_sha256` | the game library's hash | checks it after copying |
| `game_bytes`, `game_files` | totals of `game/` | free-space check before importing, progress bar |

## Importing (the launcher)

1. The player picks the `.daopkg` (the system file picker; any folder, a USB drive, a cloud).
2. Read `manifest.json`, check `format`, `abi`, `xex_sha1`, the free space (`game_bytes` plus
   a margin).
3. Create an instance: `files/dao/instances/<id>/` (shared storage: `game/`, `userdata/`) and
   `<internal files>/instances/<id>/libdaodroid.so` (internal: code can't be mapped from
   shared storage). Copy the entries; check `library_sha256`.
4. The instance keeps its own settings (driver, graphics preset, pad layout, `env.txt`) and its
   own saves.

## Versions

- `format` changes when this layout changes. Old launchers refuse new packages.
- `abi` changes when the interface between the launcher (its Java side, its SDL build, the
  command-line options it passes) and the game library changes. Both sides carry the number;
  a package must be rebuilt (or the launcher updated) when they differ.
- A new DAORecomp or DAODroid commit with the same `abi` doesn't need a new launcher: rebuild
  the package to get the game library's fixes.
