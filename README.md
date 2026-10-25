# DAODroid

*Dragon Age: Origins* (the Xbox 360 edition) on Android phones, built on
[DAORecomp](https://github.com/96dot/Dragon-Age-Origins-Recomp) by 96dot: the game's code is
translated ahead of time and compiled for the phone, and its graphics run on Vulkan through
Xenia's backend. The game keeps its console interface (radial menus, tactical pause, party
control), played with a gamepad or the on-screen one.

Made with the help of AI.

**No game code, game data or game package is shared here.** You make your own game package on
your PC from your own disc: the Xbox 360 disc with title ID `454108C0` and media ID `186A1115`
(the English/Italian/Spanish edition, also disc 1 of its Ultimate Edition).

## How to play

1. **On your PC**, get this repository (`git clone --recurse-submodules
   https://github.com/udarmolota/DAODroid`; it holds both the PC tool and the launcher's
   source), double-click **`DAODroid Tool.cmd`** in its folder, pick your disc image and wait.
   It checks the disc, extracts the game, translates its code and makes a game package, a
   `.daopkg` file (about 7 GB, about an hour: it sets up its own tools on the way). You need
   [Python 3.10+](https://www.python.org/downloads/). You make the package once; run the tool
   again only when a new DAODroid version brings fixes to the game itself.
2. **On your phone**, install the DAODroid launcher APK from this repository's
   [Releases](https://github.com/udarmolota/DAODroid/releases).
3. **Copy the `.daopkg`** to the phone (a cable, a cloud drive, a messenger).
4. In the launcher, tap **+**, pick the file, and once it's added, tap **Play**.

The package is your own copy of the game: don't share it.

The launcher keeps each game's settings (Vulkan driver, graphics, environment variables),
saves (export and import), shader cache and a bug report screen. To build everything yourself,
see [docs/BUILDING.md](docs/BUILDING.md).

Status: playable on a Galaxy S25 Ultra (Snapdragon 8 Elite, Adreno 830): 50–60 fps in the
first camp on a cool phone, 30 with the game's frame cap for long sessions. See
[docs/status.md](docs/status.md).

## Drivers

The launcher comes with Turnip Gen8 V38 (Mesa) for the Adreno 8xx; any other Vulkan driver in
Android's format (a `.so`, or an Adreno Tools `.zip`) can be installed per game from the side
menu, without rebuilding anything.

| GPU | Driver | State |
|---|---|---|
| Adreno 8xx (Snapdragon 8 Elite) | the built-in Turnip | tested on the S25 Ultra. The phone's own Adreno driver shows a black picture for now |
| Adreno 6xx/7xx | a current Turnip build for 6xx/7xx, e.g. StevenMXZ's `Turnip_v27.0.0-R1` ([releases](https://github.com/StevenMXZ/Adreno-Tools-Drivers/releases)) | untested |
| Mali, Xclipse, PowerVR | the phone's own driver, or PanVK builds for Mali | untested |

If you try another phone, a report from the launcher's **Report a bug** is very welcome.

## Playing

- **Controls**: any Bluetooth or USB gamepad, or the on-screen one (its layout: the side menu,
  or hold the switch at the top of the screen in the game).
- **Graphics**: the game's own Graphics tab (Options, in the title and pause menus) has the
  frame cap: 30 keeps a phone cool through long sessions. The launcher's settings have the
  texture detail, the Vulkan driver and, under Advanced, Turnip's mode and environment variables.
- **Text** (the character's name): Android's keyboard opens in a dialog; Latin letters only
  for now (the game's fonts have no others).

## Known issues

- Heat: the phone's GPU gets throttled after some minutes at 60 fps; the 30 fps cap or a
  cooler helps.
- No DLC or title update yet (DAORecomp supports them; untested on Android).
- The text and voices are English, Italian or Spanish (what the disc has).

## License

MIT (see `LICENSE`) for this repository's own files. Third-party components and their
licenses are listed in `NOTICE.md`. *Dragon Age* is a trademark of Electronic Arts; this
project is not affiliated with or endorsed by EA or BioWare.
