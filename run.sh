#!/usr/bin/env bash
# Configure (first run only), build, and run Blossom in Debug.
# Arguments are passed through to the executable.
set -euo pipefail

cd "$(dirname "$0")"

BUILD_DIR=build

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Debug
fi

cmake --build "$BUILD_DIR"
exec "./$BUILD_DIR/blossom" "$@"
