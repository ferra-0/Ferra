#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

# Always compile against the standard library that belongs to this checkout.
# This remains correct when the checkout is moved, and deliberately overrides
# an inherited FERRA_PATH that may point to an older installed release.
export FERRA_PATH="$PROJECT_ROOT"

case "$(uname -s)" in
  Linux)
    exec "$PROJECT_ROOT/platforms/linux/build.sh" "$@"
    ;;
  Darwin)
    exec "$PROJECT_ROOT/platforms/macos/build.sh" "$@"
    ;;
  *)
    echo "Use platforms/windows/build.ps1 from PowerShell on Windows." >&2
    exit 2
    ;;
esac
