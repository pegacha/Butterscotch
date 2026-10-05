# AM2R 3DS

AM2R (Another Metroid 2 Remake) 1.1 on the New Nintendo 3DS.

This is a game-specific fork of [Butterscotch](https://github.com/ButterscotchRunner/Butterscotch), an open source
re-implementation of the GameMaker: Studio runner in C. Butterscotch runs the game's own bytecode (`data.win`), so the
3DS build runs AM2R's real game code; this repository adds the 3DS platform layer and the changes AM2R needs on it. The
3DS rendering, audio and preprocessing code started from
[Cinnamon](https://github.com/Project-Sunshine-Native/cinnamon)'s Undertale 3DS port.

> [!IMPORTANT]
> You need your own copy of AM2R 1.1. This repository does not include any game files.

## Install

1. **SD card files.** On a PC (Linux or WSL, with [devkitPro](https://devkitpro.org/wiki/Getting_Started)'s 3DS
   tools for `tex3ds`), build the game folder from your AM2R 1.1 install and copy it to the card:

   ```bash
   tools/n3ds/make_sd.sh <AM2R 1.1 folder with data.win> sd/3ds/am2r
   # then copy sd/3ds/am2r to sdmc:/3ds/am2r/
   ```

   The folder only needs rebuilding when a release says it needs a newer SD data revision; the game tells you at
   startup if the card's folder is out of date.

2. **The program.** Open the [latest release](../../releases/latest) and scan its QR code with FBI
   (Remote Install → Scan QR Code), or download `am2r.cia` and install it with FBI from the SD card.
   Every passing build is published as a release; builds from branches other than `main` are marked pre-release.

Saves and `log.txt` go in `sdmc:/3ds/am2r/`. The 3DS button layout is in [res/n3ds/am2r/README.md](res/n3ds/am2r/README.md);
the game's own Joypad menu can rebind it. The pause screen opens on the bottom screen and is driven by touch.

## Building

```bash
tools/n3ds/build.sh ../build-n3ds    # -> am2r.cia + am2r.3dsx (needs devkitARM, and makerom for the .cia)
```

[.github/workflows/build-n3ds.yml](.github/workflows/build-n3ds.yml) is the reference build: the `devkitpro/devkitarm`
container, with makerom built from [3DSGuy/Project_CTR](https://github.com/3DSGuy/Project_CTR).

[PORTING_NOTES.md](PORTING_NOTES.md) covers how the port works: the SD layout, the emulator test loop (Azahar), the
renderer, audio and file system, the pause screen, diagnostics, performance numbers and known issues.

## Credits

* AM2R by DoctorM64 and team.
* [Butterscotch](https://github.com/ButterscotchRunner/Butterscotch) by MrPowerGamerBR and contributors.
* [Cinnamon](https://github.com/Project-Sunshine-Native/cinnamon) (3DS, Wii and Wii U ports of Butterscotch) by
  @casrielasriel and @grayforz24682.

## License

GNU Affero General Public License v3.0, as Butterscotch: see [LICENSE](LICENSE).

## Disclaimer

This project has no association, endorsement, or any connection whatsoever with Nintendo, the AM2R team, YoYo Games or
any of the software it runs, and does not provide any of it. You need to provide your own game files.
