#!/usr/bin/env bash
set -euo pipefail

# Rebuild the native distributable, unpack it, and run its own installer.
# This exercises exactly the same installation path that downloaded ZIP users
# receive, including PATH links and the bundled Ferra/eFerra LSP extension.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
RELEASE_DIR="${FERRA_RELEASE_DIR:-$SCRIPT_DIR/release}"

case "$(uname -s)" in
  Linux)
    if [[ -n "${FERRA_PACKAGE_PLATFORM:-}" ]]; then
      PACKAGE_PLATFORM="$FERRA_PACKAGE_PLATFORM"
    elif [[ -n "${ANDROID_ROOT:-}" || -n "${TERMUX_VERSION:-}" ||
            "${PREFIX:-}" == */com.termux*/files/usr ]]; then
      PACKAGE_PLATFORM="android"
    else
      PACKAGE_PLATFORM="linux"
    fi
    ;;
  Darwin) PACKAGE_PLATFORM="macos" ;;
  *)
    echo "sync.sh is for Linux, macOS, and Termux. On Windows use sync-ferra-path.ps1." >&2
    exit 2
    ;;
esac

if [[ -n "${FERRA_ARCHIVE:-}" ]]; then
  ARCHIVE="$FERRA_ARCHIVE"
  [[ -f "$ARCHIVE" ]] || {
    echo "FERRA_ARCHIVE does not exist: $ARCHIVE" >&2
    exit 1
  }
else
  FERRA_NO_LSP_INSTALL=1 "$SCRIPT_DIR/package.sh" "$@"
  ARCHIVE="$(find "$RELEASE_DIR" -maxdepth 1 -type f \
    -name "ferra-*-${PACKAGE_PLATFORM}-*.zip" -print \
    | sort | tail -n 1)"
  [[ -n "$ARCHIVE" ]] || {
    echo "No ${PACKAGE_PLATFORM} ZIP was produced in: $RELEASE_DIR" >&2
    exit 1
  }
fi

cmake -E tar tf "$ARCHIVE" >/dev/null || {
  echo "Cannot read Ferra ZIP: $ARCHIVE" >&2
  exit 1
}

UNPACK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ferra-sync.XXXXXX")"
cleanup() {
  rm -rf "$UNPACK_DIR"
}
trap cleanup EXIT HUP INT TERM

(
  cd -- "$UNPACK_DIR"
  cmake -E tar xvf "$ARCHIVE" >/dev/null
)
INSTALL_SCRIPT="$(find "$UNPACK_DIR" -mindepth 2 -maxdepth 2 \
  -type f -name install.sh -print -quit)"
[[ -n "$INSTALL_SCRIPT" ]] || {
  echo "The ZIP does not contain install.sh: $ARCHIVE" >&2
  exit 1
}

echo "Installing fresh package: $ARCHIVE"
(
  cd -- "$(dirname -- "$INSTALL_SCRIPT")"
  chmod +x ./install.sh
  exec ./install.sh
)

echo "Ferra was rebuilt and reinstalled from its ZIP."
