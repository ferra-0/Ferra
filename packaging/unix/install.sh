#!/usr/bin/env sh
set -eu

# An installer must validate the package it just unpacked, never a standard
# library inherited from a previously moved checkout or an older release.
unset FERRA_PATH

PACKAGE_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
INSTALL_DIR=${FERRA_INSTALL_DIR:-"$HOME/.local/opt/ferra"}
BIN_LINK_DIR=${FERRA_BIN_DIR:-"$HOME/.local/bin"}
PROFILE_START="# >>> Ferra PATH >>>"
PROFILE_END="# <<< Ferra PATH <<<"

fail() {
  echo "Ferra install error: $*" >&2
  exit 1
}

case "$INSTALL_DIR" in
  ""|/|"$HOME") fail "unsafe install directory: $INSTALL_DIR" ;;
esac
case "$BIN_LINK_DIR" in
  ""|/) fail "unsafe bin directory: $BIN_LINK_DIR" ;;
esac

for required in \
  bin/ferra \
  bin/efe \
  bin/iron \
  share/ferra \
  share/ferra/lang.sh \
  share/ferra/icons/ferra-dark.png \
  share/ferra/icons/ferra-light.png \
  share/ferra/ferralang/lsp/ferra_lsp.py \
  share/ferra/eferra/lsp/eferra_lsp.py \
  share/ferra/ferralang/lsp/client/node_modules/vscode-languageclient \
  lib/ferra \
  uninstall.sh
do
  [ -e "$PACKAGE_DIR/$required" ] || fail "package is incomplete: missing $required"
done

for command_name in ferra efe iron; do
  command_path="$BIN_LINK_DIR/$command_name"
  if [ -e "$command_path" ] && [ ! -L "$command_path" ]; then
    fail "$command_path already exists and is not a symlink"
  fi
done

install_parent=$(dirname "$INSTALL_DIR")
staging_dir="$install_parent/.ferra-install-new-$$"
backup_dir="$install_parent/.ferra-install-old-$$"

cleanup() {
  [ ! -d "$staging_dir" ] || rm -rf "$staging_dir"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$install_parent" "$BIN_LINK_DIR" "$staging_dir"
cp -R "$PACKAGE_DIR/bin" "$staging_dir/bin"
cp -R "$PACKAGE_DIR/lib" "$staging_dir/lib"
cp -R "$PACKAGE_DIR/share" "$staging_dir/share"
cp "$PACKAGE_DIR/uninstall.sh" "$staging_dir/uninstall.sh"
[ ! -f "$PACKAGE_DIR/README.md" ] || cp "$PACKAGE_DIR/README.md" "$staging_dir/README.md"
chmod +x "$staging_dir/bin/ferra" "$staging_dir/bin/efe" \
  "$staging_dir/bin/iron" "$staging_dir/uninstall.sh"

if [ -e "$INSTALL_DIR" ]; then
  mv "$INSTALL_DIR" "$backup_dir"
fi
if ! mv "$staging_dir" "$INSTALL_DIR"; then
  [ ! -e "$backup_dir" ] || mv "$backup_dir" "$INSTALL_DIR"
  fail "cannot activate $INSTALL_DIR"
fi

for command_name in ferra efe iron; do
  ln -sfn "$INSTALL_DIR/bin/$command_name" "$BIN_LINK_DIR/$command_name"
done
[ ! -e "$backup_dir" ] || rm -rf "$backup_dir"

profile_file=""
if [ "${FERRA_NO_PATH_UPDATE:-0}" != "1" ]; then
  if [ -n "${FERRA_SHELL_PROFILE:-}" ]; then
    profile_file=$FERRA_SHELL_PROFILE
  else
    case "${SHELL:-}" in
      */zsh) profile_file="$HOME/.zshrc" ;;
      */bash) profile_file="$HOME/.bashrc" ;;
      *) profile_file="$HOME/.profile" ;;
    esac
  fi

  touch "$profile_file"
  if ! grep -Fqx "$PROFILE_START" "$profile_file"; then
    {
      printf '\n%s\n' "$PROFILE_START"
      printf 'export PATH="%s:$PATH"\n' "$BIN_LINK_DIR"
      printf '%s\n' "$PROFILE_END"
    } >> "$profile_file"
  fi
fi

smoke_dir=$(mktemp -d "${TMPDIR:-/tmp}/ferra-install-test.XXXXXX")
printf 'take "fe/math.fe"\nfn main(): i64 { ret 0 }\n' > "$smoke_dir/package.fe"
printf 'log("package-ok")\n' > "$smoke_dir/package.efe"
"$INSTALL_DIR/bin/ferra" "$smoke_dir/package.fe" -o "$smoke_dir/package.ll" >/dev/null
efe_output=$("$INSTALL_DIR/bin/efe" "$smoke_dir/package.efe")
mkdir "$smoke_dir/iron-project"
(
  cd "$smoke_dir/iron-project"
  PATH="$BIN_LINK_DIR:$PATH" "$INSTALL_DIR/bin/iron" new
)
[ -f "$smoke_dir/iron-project/ferra.json" ] || fail "Iron smoke test failed"
[ -f "$smoke_dir/iron-project/main.fe" ] || fail "Iron smoke test failed"
rm -rf "$smoke_dir"
[ "$efe_output" = "package-ok" ] || fail "eFerra smoke test failed"

lsp_installed=0
if [ "${FERRA_NO_LSP_INSTALL:-0}" != "1" ]; then
  "$INSTALL_DIR/share/ferra/lang.sh"
  lsp_installed=1
fi

trap - EXIT HUP INT TERM
echo "Ferra installed successfully."
echo "  Files: $INSTALL_DIR"
echo "  Commands: $BIN_LINK_DIR/{ferra,efe,iron}"
if [ -n "$profile_file" ]; then
  echo "  PATH updated in: $profile_file"
  echo "Open a new terminal or run: . \"$profile_file\""
fi
if ! command -v clang >/dev/null 2>&1; then
  echo "Note: install Clang to let Iron turn generated LLVM IR into executables." >&2
fi
if [ "$lsp_installed" = "1" ]; then
  echo "  LSP: Ferra/eFerra VS Code support installed"
  if ! command -v python3 >/dev/null 2>&1; then
    echo "Note: install Python 3 to run the Ferra language servers." >&2
  fi
fi
echo "Uninstall with: $INSTALL_DIR/uninstall.sh"
