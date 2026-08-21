#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
case "$(uname -s)" in
  Linux) exec "$SCRIPT_DIR/platforms/linux/package.sh" "$@" ;;
  Darwin) exec "$SCRIPT_DIR/platforms/macos/package.sh" "$@" ;;
  *)
    echo "On Windows run .\\package.ps1 from PowerShell." >&2
    exit 2
    ;;
esac

