#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

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
