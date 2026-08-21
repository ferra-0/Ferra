#!/usr/bin/env sh
set -eu

INSTALL_DIR=${FERRA_INSTALL_DIR:-"$HOME/.local/opt/ferra"}
BIN_LINK_DIR=${FERRA_BIN_DIR:-"$HOME/.local/bin"}
PROFILE_START="# >>> Ferra PATH >>>"
PROFILE_END="# <<< Ferra PATH <<<"

case "$INSTALL_DIR" in
  ""|/|"$HOME")
    echo "Refusing unsafe uninstall directory: $INSTALL_DIR" >&2
    exit 1
    ;;
esac

for command_name in ferra efe iron; do
  command_path="$BIN_LINK_DIR/$command_name"
  if [ -L "$command_path" ]; then
    link_target=$(readlink "$command_path")
    case "$link_target" in
      "$INSTALL_DIR"/*) rm "$command_path" ;;
    esac
  fi
done

remove_path_block() {
  profile_file=$1
  [ -f "$profile_file" ] || return 0
  temp_file="$profile_file.ferra-$$"
  awk -v start="$PROFILE_START" -v end="$PROFILE_END" '
    $0 == start { hidden = 1; next }
    $0 == end { hidden = 0; next }
    !hidden { print }
  ' "$profile_file" > "$temp_file"
  mv "$temp_file" "$profile_file"
}

remove_lsp_extension() {
  extensions_root=$1
  extension_dir="$extensions_root/local.fe-0.0.1"
  if [ -f "$extension_dir/server/ferra_lsp.py" ] ||
      [ -f "$extension_dir/server/eferra_lsp.py" ]; then
    rm -rf "$extension_dir"
  fi
}

if [ "${FERRA_KEEP_PATH:-0}" != "1" ]; then
  if [ -n "${FERRA_SHELL_PROFILE:-}" ]; then
    remove_path_block "$FERRA_SHELL_PROFILE"
  else
    remove_path_block "$HOME/.profile"
    remove_path_block "$HOME/.bashrc"
    remove_path_block "$HOME/.zshrc"
  fi
fi

if [ "${FERRA_KEEP_LSP:-0}" != "1" ]; then
  if [ -n "${VSCODE_EXTENSIONS_DIR:-}" ]; then
    remove_lsp_extension "$VSCODE_EXTENSIONS_DIR"
  else
    remove_lsp_extension "$HOME/.vscode/extensions"
    remove_lsp_extension \
      "$HOME/.var/app/com.visualstudio.code/data/vscode/extensions"
  fi
fi

if [ -d "$INSTALL_DIR" ]; then
  rm -rf "$INSTALL_DIR"
fi

echo "Ferra was removed. Open a new terminal to refresh PATH."
