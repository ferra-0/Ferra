#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This script builds the native Linux package; run it on Linux." >&2
  exit 2
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${FERRA_BUILD_DIR:-$PROJECT_ROOT/build/linux}"
INSTALL_DIR="${FERRA_INSTALL_DIR:-$PROJECT_ROOT/dist/linux}"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="${FERRA_BUILD_TYPE:-Release}" \
  -DCMAKE_C_COMPILER="${CC:-clang}" \
  -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  "$@"
cmake --build "$BUILD_DIR" --parallel
cmake --install "$BUILD_DIR"

echo "Linux package: $INSTALL_DIR"
