#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script builds the native macOS package; run it on macOS." >&2
  exit 2
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
DEFAULT_BUILD_DIR="$PROJECT_ROOT/build/macos"
BUILD_DIR="${FERRA_BUILD_DIR:-$DEFAULT_BUILD_DIR}"
INSTALL_DIR="${FERRA_INSTALL_DIR:-$PROJECT_ROOT/dist/macos}"

# A checkout can be moved after CMake has generated this directory. Its cache
# then points to the old source path and CMake refuses to proceed. Recreate
# only the default disposable build directory; a caller-owned FERRA_BUILD_DIR
# is never removed automatically.
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  CACHE_SOURCE="$(sed -n 's|^CMAKE_HOME_DIRECTORY:INTERNAL=||p' \
    "$BUILD_DIR/CMakeCache.txt")"
  if [[ -n "$CACHE_SOURCE" && "$CACHE_SOURCE" != "$PROJECT_ROOT" ]]; then
    if [[ -n "${FERRA_BUILD_DIR:-}" ]]; then
      echo "Build cache belongs to: $CACHE_SOURCE" >&2
      echo "Remove or choose a new FERRA_BUILD_DIR before rebuilding." >&2
      exit 2
    fi
    echo "Recreating moved-checkout build cache: $BUILD_DIR"
    rm -rf -- "$BUILD_DIR"
  fi
fi

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="${FERRA_BUILD_TYPE:-Release}" \
  -DCMAKE_C_COMPILER="${CC:-clang}" \
  -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  "$@"
cmake --build "$BUILD_DIR" --parallel
cmake --install "$BUILD_DIR"

echo "macOS package: $INSTALL_DIR"
