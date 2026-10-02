#!/usr/bin/env bash
# Builds butterscotch.3dsx for the 3DS: tools/n3ds/build.sh [build-dir] [extra cmake args...]
set -euo pipefail
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
src="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$src/../build-n3ds}"
shift || true
cmake -S "$src" -B "$build" -G Ninja -DPLATFORM=n3ds -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@" >/dev/null
cmake --build "$build"
