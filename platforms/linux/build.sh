#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This script builds the native Linux package; run it on Linux." >&2
  exit 2
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
PACKAGE_PLATFORM="${FERRA_PACKAGE_PLATFORM:-}"
if [[ -z "$PACKAGE_PLATFORM" ]]; then
  if [[ -n "${ANDROID_ROOT:-}" || -n "${TERMUX_VERSION:-}" ||
        "${PREFIX:-}" == */com.termux*/files/usr ]]; then
    PACKAGE_PLATFORM="android"
  else
    PACKAGE_PLATFORM="linux"
  fi
fi
BUILD_DIR="${FERRA_BUILD_DIR:-$PROJECT_ROOT/build/$PACKAGE_PLATFORM}"
INSTALL_DIR="${FERRA_INSTALL_DIR:-$PROJECT_ROOT/dist/$PACKAGE_PLATFORM}"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="${FERRA_BUILD_TYPE:-Release}" \
  -DCMAKE_C_COMPILER="${CC:-clang}" \
  -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
  -DFERRA_PACKAGE_PLATFORM="$PACKAGE_PLATFORM" \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  "$@"
cmake --build "$BUILD_DIR" --parallel
cmake --install "$BUILD_DIR"

echo "$PACKAGE_PLATFORM package: $INSTALL_DIR"
