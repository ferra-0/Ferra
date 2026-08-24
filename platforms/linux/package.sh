#!/usr/bin/env bash
set -euo pipefail

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
export FERRA_PACKAGE_PLATFORM="$PACKAGE_PLATFORM"
BUILD_DIR="${FERRA_BUILD_DIR:-$PROJECT_ROOT/build/$PACKAGE_PLATFORM}"
INSTALL_DIR="${FERRA_INSTALL_DIR:-$PROJECT_ROOT/dist/$PACKAGE_PLATFORM}"
RELEASE_DIR="${FERRA_RELEASE_DIR:-$PROJECT_ROOT/release}"

"$SCRIPT_DIR/build.sh" "$@"
ctest --test-dir "$BUILD_DIR" --output-on-failure

iron_smoke_dir=$(mktemp -d "${TMPDIR:-/tmp}/ferra-iron-package.XXXXXX")
trap 'rm -rf "$iron_smoke_dir"' EXIT
printf 'func main(): i64 { ret 0 }\n' > "$iron_smoke_dir/main.fe"
printf '{"entry":"main.fe","cpp":false,"objects":["runtime"],"libraries":[]}\n' \
  > "$iron_smoke_dir/ferra.json"
(
  cd "$iron_smoke_dir"
  FERRA_PATH="$PROJECT_ROOT" PATH="$INSTALL_DIR/bin:$PATH" \
    "$INSTALL_DIR/bin/iron"
)
lsp_smoke_root="$iron_smoke_dir/vscode-extensions"
VSCODE_EXTENSIONS_DIR="$lsp_smoke_root" \
  "$INSTALL_DIR/share/ferra/lang.sh"
lsp_extension="$lsp_smoke_root/local.ferra-0.0.3"
test -f "$lsp_extension/server/ferra_lsp.py"
test -f "$lsp_extension/server/eferra_lsp.py"
test -f "$lsp_extension/node_modules/vscode-languageclient/package.json"
test -f "$lsp_extension/server/ferra-root.txt"
test -d "$(cat "$lsp_extension/server/ferra-root.txt")/fe"
rm -rf "$iron_smoke_dir"
trap - EXIT

mkdir -p "$RELEASE_DIR"
cpack --config "$BUILD_DIR/CPackConfig.cmake" -G ZIP -B "$RELEASE_DIR"

archive=$(find "$RELEASE_DIR" -maxdepth 1 -type f \
  -name "ferra-*-$PACKAGE_PLATFORM-*.zip" -print | sort | tail -n 1)
[ -n "$archive" ] || {
  echo "$PACKAGE_PLATFORM ZIP was not created" >&2
  exit 1
}
cmake -E tar tf "$archive" >/dev/null
if [[ "${FERRA_NO_LSP_INSTALL:-0}" != "1" ]]; then
  "$INSTALL_DIR/share/ferra/lang.sh"
fi
echo "Ready to publish: $archive"
echo "Checksum: $archive.sha256"
