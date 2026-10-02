#!/usr/bin/env bash
# Builds the game folder for the SD card (sdmc:/3ds/<folder>/) from a game's install directory:
#   tools/n3ds/make_sd.sh <game dir with data.win> <out dir, e.g. sd/3ds/am2r>
# It holds the game's own files (data.win, lang/, music, ...; not the Windows .exe/.dll), the textures converted by
# n3ds-preprocess (gfx/) and sd_data_rev.txt. It only needs redoing when tools/n3ds/sd_data_rev.txt goes up.
set -euo pipefail
src="$(cd "$(dirname "$0")/../.." && pwd)"
game="$1"
out="$2"
pre="${BS_PREPROCESS_BUILD:-$src/../build-preprocess}"

if [ ! -x "$pre/n3ds-preprocess" ]; then
    cmake -S "$src/tools/n3ds-preprocess" -B "$pre" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$pre"
fi

mkdir -p "$out"
rsync -a --exclude '*.exe' --exclude '*.dll' --exclude 'gfx/' --exclude 'audio/' "$game"/ "$out"/
"$pre/n3ds-preprocess" "$game/data.win" "$out"
rm -rf "$out/audio" # the preprocessor's sound bank isn't used by this port's audio
cp "$src/tools/n3ds/sd_data_rev.txt" "$out/sd_data_rev.txt"
du -sh "$out"
