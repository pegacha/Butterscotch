#!/usr/bin/env bash
# Builds butterscotch.3dsx for the 3DS: tools/n3ds/build.sh [build-dir] [extra cmake args...]
set -euo pipefail
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
src="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$src/../build-n3ds}"
shift || true
# One WAD version per build lets the compiler fold the interpreter's version checks (upstream does the same for
# its console builds). AM2R is WAD 14; BS_N3DS_WADS=all builds a runner for any game.
case "${BS_N3DS_WADS:-14}" in
    14) wads="-DENABLE_WAD14=ON -DENABLE_WAD16=OFF -DENABLE_WAD17=OFF" ;;
    16) wads="-DENABLE_WAD14=OFF -DENABLE_WAD16=ON -DENABLE_WAD17=OFF" ;;
    17) wads="-DENABLE_WAD14=OFF -DENABLE_WAD16=OFF -DENABLE_WAD17=ON" ;;
    *) wads="-DENABLE_WAD14=ON -DENABLE_WAD16=ON -DENABLE_WAD17=ON" ;;
esac
cmake -S "$src" -B "$build" -G Ninja -DPLATFORM=n3ds -DCMAKE_BUILD_TYPE=RelWithDebInfo     $wads -DENABLE_LEGACY_GL=OFF -DENABLE_MODERN_GL=OFF "$@" >/dev/null
cmake --build "$build"
