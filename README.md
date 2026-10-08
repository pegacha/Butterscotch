# AM2R for New 3DS

**AM2R** (Another Metroid 2 Remake) running on the **New Nintendo 3DS / New 2DS XL**, installed as a HOME Menu app.
It runs the game's own code, so it plays like the PC version: the **AM2R 1.5.5 Community Updates** are recommended,
the original 1.1 works too.

![Samus at the landing site](docs/screenshots/landing-site.png)
![Samus over lava](docs/screenshots/lava.png)
![The pause screen's map on the touch screen](docs/screenshots/pause-map.png)

> [!IMPORTANT]
> You need your own copy of AM2R. No game files are included or downloaded for you.

## What you need

- A **New 3DS, New 3DS XL or New 2DS XL** with custom firmware and **FBI** (see [3ds.hacks.guide](https://3ds.hacks.guide)).
- Your **AM2R 1.1** zip, and a PC (Windows or Linux) to prepare the SD card files.
- About **700 MB** free on the SD card.

## Install

1. **Get AM2R 1.5.5** (recommended): on your PC, open the
   [AM2R Launcher](https://github.com/AM2R-Community-Developers/AM2RLauncher) with your 1.1 zip and install the
   "Community Updates (Latest)" profile.
2. **Make the SD card files**: download `am2r-sd.bat` (Windows) or `am2r-sd.sh` (Linux) from the
   [Releases](../../releases) page and run it. Pick the game's `data.win` (for 1.5.5, the one in the launcher's
   `Profiles/Community Updates (Latest)` folder) and a folder; copy the `3ds` folder it makes to the root of your SD card.
3. **Install the app**: scan the release's QR code with FBI (Remote Install → Scan QR Code), or copy `am2r.cia` to
   the SD card and install it with FBI.

Saves and settings stay on the SD card (`sdmc:/3ds/am2r/`) and survive updates. 1.1 and 1.5.5 saves are separate.

## Playing

| 3DS | |
|---|---|
| B | Jump |
| Y | Fire |
| A | Morph |
| L / ZL | Aim |
| R | Missiles |
| ZR | Aim lock |
| Select | Weapon select |
| Start | Pause |

- **Touch screen:** the map follows you while you play; tap it to pause. The pause screen (map, inventory, logs,
  options) works by touch.
- **Options → Display** has the 3DS settings: screen size, frameskip (a steady 30 fps), Fast scripts (on by default,
  keep it on), an FPS counter, and cheats.
- Buttons can be changed in the game's own Joypad options.

## Problems?

Open an issue here with what happened and, if you can, the `log.txt` from `sdmc:/3ds/am2r/`.

## Credits

AM2R by DoctorM64 and team, and the Community Updates by the AM2R Community Developers. Runs on
[Butterscotch](https://github.com/ButterscotchRunner/Butterscotch) (MrPowerGamerBR and contributors), with 3DS code
that started from [Cinnamon](https://github.com/Project-Sunshine-Native/cinnamon). Developer documentation:
[.github/README.md](.github/README.md) and [PORTING_NOTES.md](PORTING_NOTES.md).

This project has no association with Nintendo, the AM2R team or the AM2R Community Developers, and provides none of
their files. Licensed under the GNU AGPL v3.0 ([LICENSE](LICENSE)).
