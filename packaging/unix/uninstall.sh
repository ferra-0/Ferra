#!/usr/bin/env sh
set -eu

TERMUX_PREFIX=${PREFIX:-}
IS_TERMUX=0
case "$TERMUX_PREFIX" in
  */com.termux/files/usr) IS_TERMUX=1 ;;
esac
if [ -n "${TERMUX_VERSION:-}" ] && [ -n "$TERMUX_PREFIX" ]; then
  IS_TERMUX=1
fi

if [ -n "${FERRA_INSTALL_DIR:-}" ]; then
  INSTALL_DIR=$FERRA_INSTALL_DIR
elif [ "$IS_TERMUX" = "1" ]; then
  INSTALL_DIR="$TERMUX_PREFIX/opt/ferra"
else
  INSTALL_DIR="$HOME/.local/opt/ferra"
fi
if [ -n "${FERRA_BIN_DIR:-}" ]; then
  BIN_LINK_DIR=$FERRA_BIN_DIR
elif [ "$IS_TERMUX" = "1" ]; then
  BIN_LINK_DIR="$TERMUX_PREFIX/bin"
else
  BIN_LINK_DIR="$HOME/.local/bin"
fi
PROFILE_START="# >>> Ferra PATH >>>"
PROFILE_END="# <<< Ferra PATH <<<"

case "$INSTALL_DIR" in
  ""|/|"$HOME"|"$TERMUX_PREFIX"|"$TERMUX_PREFIX/"|\
  "$TERMUX_PREFIX/opt"|"$TERMUX_PREFIX/opt/")
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
  for extension_dir in \
    "$extensions_root/local.ferra-0.0.4" \
    "$extensions_root/local.ferra-0.0.3" \
    "$extensions_root/local.ferra-0.0.2" \
    "$extensions_root/local.ferra-0.0.1" \
    "$extensions_root/local.fe-0.0.3" \
    "$extensions_root/local.fe-0.0.2" \
    "$extensions_root/local.fe-0.0.1" \
    "$extensions_root/local.efe-0.0.1" \
    "$extensions_root/local.eferra-0.0.1"; do
    if [ -f "$extension_dir/server/ferra_lsp.py" ] ||
        [ -f "$extension_dir/server/eferra_lsp.py" ]; then
      rm -rf "$extension_dir"
    fi
  done
}

remove_lsp_extensions() {
  if [ -n "${VSCODE_EXTENSIONS:-}" ]; then
    remove_lsp_extension "$VSCODE_EXTENSIONS"
  elif [ -n "${VSCODE_EXTENSIONS_DIR:-}" ]; then
    # Compatibility with older Ferra installer scripts.
    remove_lsp_extension "$VSCODE_EXTENSIONS_DIR"
  elif [ -n "${VSCODE_PORTABLE:-}" ]; then
    remove_lsp_extension "$VSCODE_PORTABLE/extensions"
  else
    found_extensions_root=0
    for extensions_root in \
      "$HOME/.vscode/extensions" \
      "$HOME/.vscode-insiders/extensions" \
      "$HOME/.vscode-oss/extensions" \
      "$HOME/.cursor/extensions" \
      "$HOME/.var/app/com.visualstudio.code/data/vscode/extensions" \
      "$HOME/.local/share/code-server/extensions"; do
      if [ -d "$extensions_root" ]; then
        remove_lsp_extension "$extensions_root"
        found_extensions_root=1
      fi
    done

    # Match the installer: this is the target for a first-time standard Code
    # installation even when the directory does not exist yet.
    if [ "$found_extensions_root" -eq 0 ]; then
      remove_lsp_extension "$HOME/.vscode/extensions"
    fi
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
  remove_lsp_extensions
fi

if [ -d "$INSTALL_DIR" ]; then
  rm -rf "$INSTALL_DIR"
fi

echo "Ferra was removed. Open a new terminal to refresh PATH."
