#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${FERRA_BUILD_DIR:-$PROJECT_ROOT/build/macos}"
RELEASE_DIR="${FERRA_RELEASE_DIR:-$PROJECT_ROOT/release}"

"$SCRIPT_DIR/build.sh" "$@"
ctest --test-dir "$BUILD_DIR" --output-on-failure
mkdir -p "$RELEASE_DIR"
cpack --config "$BUILD_DIR/CPackConfig.cmake" -G ZIP -B "$RELEASE_DIR"

archive=$(find "$RELEASE_DIR" -maxdepth 1 -type f \
  -name 'ferra-*-macos-*.zip' -print | sort | tail -n 1)
[ -n "$archive" ] || { echo "macOS ZIP was not created" >&2; exit 1; }
cmake -E tar tf "$archive" >/dev/null
echo "Ready to publish: $archive"
echo "Checksum: $archive.sha256"

