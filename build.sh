#!/usr/bin/env bash
# Builds Tessera on macOS/Linux. Usage: ./build.sh [Release|Debug]
# Set QTDIR to the Qt kit (e.g. ~/Qt/6.10.3/macos); extra CMake args go in TESSERA_CMAKE_ARGS.
set -euo pipefail

CONFIG=${1:-Release}
BUILD="$(dirname "$0")/build/$CONFIG"
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)

# shellcheck disable=SC2086
cmake -S "$(dirname "$0")" -B "$BUILD" "${GEN[@]}" -DCMAKE_BUILD_TYPE="$CONFIG" \
    ${QTDIR:+-DCMAKE_PREFIX_PATH="$QTDIR"} ${TESSERA_CMAKE_ARGS:-}
cmake --build "$BUILD" --parallel
