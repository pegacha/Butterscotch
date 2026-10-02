#!/usr/bin/env bash
# Builds the 3DS program (<name>.cia and <name>.3dsx): tools/n3ds/build.sh [build-dir] [extra cmake args...]
# BS_N3DS_GAME picks the game profile (default am2r); BS_N3DS_GAME=generic builds a plain Butterscotch runner.
set -euo pipefail
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
src="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$src/../build-n3ds}"
shift || true

# One WAD version per build lets the compiler fold the interpreter's version checks, as upstream does for its
# console builds.
case "${BS_N3DS_GAME:-am2r}" in
    am2r)
        profile=(
            -DENABLE_WAD14=ON -DENABLE_WAD16=OFF -DENABLE_WAD17=OFF
            "-DN3DS_APP_NAME=AM2R"
            "-DN3DS_APP_DESCRIPTION=Another Metroid 2 Remake"
            "-DN3DS_APP_AUTHOR=DoctorM64"
            -DN3DS_OUTPUT_NAME=am2r
            -DN3DS_SD_FOLDER=am2r
            -DN3DS_UNIQUE_ID=0xA2E21
            -DN3DS_PRODUCT_CODE=CTR-P-AM2R
        )
        ;;
    *)
        profile=(-DENABLE_WAD14=ON -DENABLE_WAD16=ON -DENABLE_WAD17=ON)
        ;;
esac

cmake -S "$src" -B "$build" -G Ninja -DPLATFORM=n3ds -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DENABLE_LEGACY_GL=OFF -DENABLE_MODERN_GL=OFF "${profile[@]}" "$@" >/dev/null
cmake --build "$build"
