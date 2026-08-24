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
if [[ ! "$PACKAGE_PLATFORM" =~ ^[a-z0-9_-]+$ ]]; then
  echo "Invalid FERRA_PACKAGE_PLATFORM: $PACKAGE_PLATFORM" >&2
  exit 2
fi

DEFAULT_BUILD_DIR="$PROJECT_ROOT/build/$PACKAGE_PLATFORM"
BUILD_DIR="${FERRA_BUILD_DIR:-$DEFAULT_BUILD_DIR}"
INSTALL_DIR="${FERRA_INSTALL_DIR:-$PROJECT_ROOT/dist/$PACKAGE_PLATFORM}"

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
  -DFERRA_PACKAGE_PLATFORM="$PACKAGE_PLATFORM" \
  -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  "$@"
cmake --build "$BUILD_DIR" --parallel
cmake --install "$BUILD_DIR"

echo "$PACKAGE_PLATFORM package: $INSTALL_DIR"
