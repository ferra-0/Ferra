#!/usr/bin/env bash
set -e

EXTENSION_VERSION="0.0.4"
EXTENSION_ID="local.ferra"

# Keep this selection aligned with the Windows installer. VS Code itself
# honors VSCODE_EXTENSIONS, while VSCODE_EXTENSIONS_DIR is the old Ferra
# override retained for existing scripts. A portable VS Code installation
# keeps extensions below its portable data directory.
EXTENSION_ROOTS=()
if [[ -n "${VSCODE_EXTENSIONS:-}" ]]; then
  EXTENSION_ROOTS=("$VSCODE_EXTENSIONS")
elif [[ -n "${VSCODE_EXTENSIONS_DIR:-}" ]]; then
  EXTENSION_ROOTS=("$VSCODE_EXTENSIONS_DIR")
elif [[ -n "${VSCODE_PORTABLE:-}" ]]; then
  EXTENSION_ROOTS=("$VSCODE_PORTABLE/extensions")
else
  EXTENSION_ROOT_CANDIDATES=(
    "$HOME/.vscode/extensions"
    "$HOME/.vscode-insiders/extensions"
    "$HOME/.vscode-oss/extensions"
    "$HOME/.cursor/extensions"
    "$HOME/.var/app/com.visualstudio.code/data/vscode/extensions"
    "$HOME/.local/share/code-server/extensions"
  )

  for extension_root_candidate in "${EXTENSION_ROOT_CANDIDATES[@]}"; do
    if [[ -d "$extension_root_candidate" ]]; then
      EXTENSION_ROOTS+=("$extension_root_candidate")
    fi
  done

  # A first-time install normally has no extensions directory yet.
  if (( ${#EXTENSION_ROOTS[@]} == 0 )); then
    EXTENSION_ROOTS=("$HOME/.vscode/extensions")
  fi
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRST_EXTENSION_DIR=""

for EXTENSIONS_ROOT in "${EXTENSION_ROOTS[@]}"; do
EXT_DIR="$EXTENSIONS_ROOT/$EXTENSION_ID-$EXTENSION_VERSION"
LEGACY_FERRA_DIRS=(
  "$EXTENSIONS_ROOT/local.ferra-0.0.3"
  "$EXTENSIONS_ROOT/local.ferra-0.0.2"
  "$EXTENSIONS_ROOT/local.ferra-0.0.1"
  "$EXTENSIONS_ROOT/local.fe-0.0.2"
  "$EXTENSIONS_ROOT/local.fe-0.0.1"
  "$EXTENSIONS_ROOT/local.fe-0.0.3"
  "$EXTENSIONS_ROOT/local.efe-0.0.1"
  "$EXTENSIONS_ROOT/local.eferra-0.0.1"
)
SYNTAX_DIR="$EXT_DIR/syntaxes"
ICON_DIR="$EXT_DIR/icons"
SERVER_DIR="$EXT_DIR/server"

echo "Installing Ferra language support to: $EXTENSIONS_ROOT"

# Remove every old identity used by earlier installers. Otherwise VS Code can
# load an outdated grammar for the same .fe files instead of this release.
rm -rf "$EXT_DIR" "${LEGACY_FERRA_DIRS[@]}"

mkdir -p "$SYNTAX_DIR" "$ICON_DIR" "$SERVER_DIR"

cp "$SCRIPT_DIR/icons/ferra-dark.png" \
  "$ICON_DIR/ferra-dark.png"

cp "$SCRIPT_DIR/icons/ferra-light.png" \
  "$ICON_DIR/ferra-light.png"

cp "$SCRIPT_DIR/ferralang/lsp/ferra_lsp.py" \
  "$SERVER_DIR/ferra_lsp.py"

cp "$SCRIPT_DIR/eferra/lsp/eferra_lsp.py" \
  "$SERVER_DIR/eferra_lsp.py"

cp "$SCRIPT_DIR/eferra/lsp/eferra.tmLanguage.json" \
  "$SYNTAX_DIR/eferra.tmLanguage.json"

cp "$SCRIPT_DIR/ferralang/lsp/client/extension.js" \
  "$EXT_DIR/extension.js"

printf '%s\n' "$SCRIPT_DIR" > "$SERVER_DIR/ferra-root.txt"

cat > "$EXT_DIR/package.json" <<'EOF'
{
  "name": "ferra",
  "displayName": "ferra",
  "version": "0.0.4",
  "publisher": "local",

  "main": "./extension.js",

  "activationEvents": [
    "onLanguage:ferra",
    "onLanguage:eferra"
  ],

  "engines": {
    "vscode": "^1.91.0"
  },

  "categories": [
    "Programming Languages"
  ],

  "dependencies": {
    "vscode-languageclient": "^10.1.0"
  },

  "contributes": {
    "configurationDefaults": {
      "[ferra]": {
        "editor.inlayHints.enabled": "on"
      },
      "[eferra]": {
        "editor.inlayHints.enabled": "on"
      }
    },

    "configuration": {
      "title": "Ferra LSP",
      "properties": {
        "ferra.lsp.pythonPath": {
          "type": "string",
          "default": "python3",
          "description": "Python executable used to start the Ferra language server."
        },
        "eferra.lsp.pythonPath": {
          "type": "string",
          "default": "python3",
          "description": "Python executable used to start the eFerra language server."
        }
      }
    },

    "languages": [
      {
        "id": "ferra",
        "aliases": ["Ferra", "The Ferra Language"],
        "extensions": [".fe"],
        "configuration": "./language-configuration.json",

        "icon": {
          "light": "./icons/ferra-light.png",
          "dark": "./icons/ferra-dark.png"
        }
      },
      {
        "id": "eferra",
        "aliases": ["eFerra", "Dynamic Ferra"],
        "extensions": [".efe"],
        "configuration": "./language-configuration.json",

        "icon": {
          "light": "./icons/ferra-light.png",
          "dark": "./icons/ferra-dark.png"
        }
      }
    ],

    "grammars": [
      {
        "language": "ferra",
        "scopeName": "source.fe",
        "path": "./syntaxes/ferra.tmLanguage.json"
      },
      {
        "language": "eferra",
        "scopeName": "source.eferra",
        "path": "./syntaxes/eferra.tmLanguage.json"
      }
    ]
  }
}
EOF

cat > "$EXT_DIR/language-configuration.json" <<'EOF'
{
  "comments": {
    "blockComment": ["/*", "*/"]
  },

  "brackets": [
    ["{", "}"],
    ["(", ")"],
    ["[", "]"]
  ],

  "autoClosingPairs": [
    { "open": "{", "close": "}" },
    { "open": "(", "close": ")" },
    { "open": "[", "close": "]" },
    { "open": "\"", "close": "\"", "notIn": ["string"] },
    { "open": "'", "close": "'", "notIn": ["string"] }
  ],

  "surroundingPairs": [
    { "open": "{", "close": "}" },
    { "open": "(", "close": ")" },
    { "open": "[", "close": "]" },
    { "open": "\"", "close": "\"" },
    { "open": "'", "close": "'" }
  ]
}
EOF

cat > "$SYNTAX_DIR/ferra.tmLanguage.json" <<'EOF'
{
  "name": "ferra",
  "scopeName": "source.fe",
  "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",

  "patterns": [
    { "include": "#comments" },
    { "include": "#llvmInline" },
    { "include": "#strings" },
    { "include": "#functionDeclarations" },
    { "include": "#variableDeclarations" },
    { "include": "#keywords" },
    { "include": "#builtinFunctions" },
    { "include": "#booleans" },
    { "include": "#numbers" },
    { "include": "#operators" },
    { "include": "#punctuation" }
  ],

  "repository": {

    "variableDeclarations": {
      "patterns": [
        {
          "name": "meta.constant.declaration.grouped.fe",
          "begin": "\\b(const)\\b\\s+([a-zA-Z_][a-zA-Z0-9_]*(?:\\s*<[^;\\r\\n()]+>)?(?:\\s*\\*)*(?:\\s*\\[\\])*)\\s*(\\()",
          "beginCaptures": {
            "1": { "name": "storage.modifier.fe" },
            "2": { "name": "storage.type.fe" },
            "3": { "name": "punctuation.section.parens.begin.fe" }
          },
          "end": "\\)",
          "endCaptures": { "0": { "name": "punctuation.section.parens.end.fe" } },
          "patterns": [
            {
              "match": "(?:\\G|,)\\s*([a-zA-Z_][a-zA-Z0-9_]*)(?=\\s*(?:=|,|\\)|\\bpass\\b))",
              "captures": {
                "1": { "name": "constant.other.definition.fe" }
              }
            },
            { "include": "#comments" },
            { "include": "#strings" },
            { "include": "#keywords" },
            { "include": "#booleans" },
            { "include": "#numbers" },
            { "include": "#operators" },
            { "include": "#punctuation" }
          ]
        },
        {
          "name": "meta.variable.declaration.fe",
          "begin": "\\b(var|let|const)\\b\\s+(?=[a-zA-Z_][a-zA-Z0-9_]*(?:\\s*,\\s*[a-zA-Z_][a-zA-Z0-9_]*)+\\s*=)",
          "beginCaptures": {
            "1": { "name": "storage.modifier.fe" }
          },
          "end": "(?=\\s*=)",
          "patterns": [
            { "name": "variable.other.definition.fe", "match": "\\b[a-zA-Z_][a-zA-Z0-9_]*\\b" },
            { "name": "punctuation.separator.comma.fe", "match": "," }
          ]
        },
        {
          "name": "meta.variable.declaration.fe",
          "match": "\\b(var|let|const)\\b\\s+([a-zA-Z_][a-zA-Z0-9_]*)(\\s*\\[[^\\]\\r\\n]*\\])?\\s*(?=[:=])",
          "captures": {
            "1": { "name": "storage.modifier.fe" },
            "2": { "name": "variable.other.definition.fe" },
            "3": { "name": "meta.brackets.fe" }
          }
        }
      ]
    },

    "comments": {
    "patterns": [
        {
          "name": "comment.line.double-slash.fe",
          "match": "//.*$"
        },
        {
          "name": "comment.block.fe",
          "begin": "/\\*",
          "end": "\\*/"
        }
      ]
    },

    "strings": {
      "patterns": [
        {
          "name": "string.interpolated.fe",
          "begin": "\\$\"",
          "end": "\"",

          "beginCaptures": {
            "0": {
              "name": "storage.type.interpolated-string.fe"
            }
          },

          "patterns": [
            {
              "include": "#interpolation"
            },

            {
              "name": "constant.character.escape.fe",
              "match": "\\\\[nrt\"\\\\]"
            }
          ]
        },

        {
          "name": "string.quoted.double.fe",
          "begin": "\"",
          "end": "\"",
          "beginCaptures": {
            "0": { "name": "punctuation.definition.string.begin.fe" }
          },
          "endCaptures": {
            "0": { "name": "punctuation.definition.string.end.fe" }
          },
          "patterns": [
            {
              "name": "constant.character.escape.fe",
              "match": "\\\\[nrt\"\\\\]"
            }
          ]
        },

        {
          "name": "string.quoted.single.fe",
          "begin": "'",
          "end": "'",
          "beginCaptures": {
            "0": { "name": "punctuation.definition.string.begin.fe" }
          },
          "endCaptures": {
            "0": { "name": "punctuation.definition.string.end.fe" }
          },
          "patterns": [
            {
              "name": "constant.character.escape.fe",
              "match": "\\\\[nrt\"\\\\]"
            }
          ]
        }
      ]
    },

    "interpolation": {
      "patterns": [
        {
          "name": "meta.interpolation.fe",
          "begin": "\\{",
          "end": "\\}",

          "beginCaptures": {
            "0": {
              "name": "punctuation.section.interpolation.begin.fe"
            }
          },

          "endCaptures": {
            "0": {
              "name": "punctuation.section.interpolation.end.fe"
            }
          },

          "patterns": [
            {
              "name": "variable.other.fe",
              "match": "[a-zA-Z_][a-zA-Z0-9_]*"
            },
            {
              "name": "entity.name.function.fe",
              "match": "\\b[a-zA-Z_][a-zA-Z0-9_]*\\s*(?=\\()"
            },
            {
              "include": "#keywords"
            },
            {
              "include": "#numbers"
            },
            {
              "include": "#operators"
            }
          ]
        }
      ]
    },

    "llvmInline": {
      "patterns": [
        {
          "begin": "\\b(__ll|__llh)\\b\\s+\"",
          "beginCaptures": {
            "1": {
              "name": "keyword.control.llvm.fe"
            }
          },

          "end": "\"",

          "name": "source.llvm.embedded.fe",

          "patterns": [
            {
              "name": "entity.name.function.llvm",
              "match": "@[A-Za-z_.$][A-Za-z0-9_.$]*"
            },
            {
              "name": "storage.type.llvm",
              "match": "\\b(i1|i8|i16|i32|i64|float|double|ptr|void)\\b"
            },
            {
              "name": "keyword.control.llvm",
              "match": "\\b(declare|define|ret|call|alloca|load|store|br)\\b"
            },
            {
              "name": "constant.numeric.llvm",
              "match": "\\b\\d+\\b"
            }
          ]
        }
      ]
    },

    "builtinFunctions": {
      "patterns": [
        {
          "name": "storage.type.function.fe",
          "match": "\\b(func|fn)\\b"
        }
      ]
    },

    "functionDeclarations": {
      "patterns": [
        {
          "name": "meta.function.declaration.fe",
          "match": "\\b(func|fn)\\b\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?=\\()",
          "captures": {
            "1": {
              "name": "storage.type.function.fe"
            },
            "2": {
              "name": "entity.name.function.fe"
            }
          }
        },
        {
          "name": "entity.name.function.fe",
          "match": "\\b(?!(?:func|fn)\\b)[a-zA-Z_][a-zA-Z0-9_]*\\s*(?=\\()"
        }
      ]
    },

    "keywords": {
      "patterns": [
        {
          "name": "keyword.control.fe",
          "match": "\\b(if|elif|else|for|match|ret|stop|pass)\\b"
        },
        {
          "name": "keyword.control.fe",
          "match": "\\b(stct|impl|drop|nodrop|dropnow|oper|var|let|const|extern)\\b"
        },
        {
          "name": "storage.type.primitive.fe",
          "match": "\\b(int|str|nul|bol|usize|isize|hex|f32|f64|ptr|i1|i8|i16|i32|i64|u8|u16|u32|u64|tup)\\b"
        },
        {
          "name": "keyword.other.fe",
          "match": "\\b(log|logl|take|ftake|sizeof|typeis|len|typeof|volatile_store|volatile_load|atomic_load|atomic_store|atomic_add|atomic_exchange|atomic_compare_exchange)\\b"
        },
        {
          "name": "keyword.operator.word.fe",
          "match": "\\b(is|not|this|in|and|or|as)\\b"
        }
      ]
    },

    "booleans": {
      "patterns": [
        {
          "name": "constant.language.boolean.fe",
          "match": "\\b(true|false)\\b"
        }
      ]
    },

    "numbers": {
      "patterns": [
        {
          "name": "constant.numeric.hex.fe",
          "match": "\\b0[xX][0-9a-fA-F]+\\b"
        },
        {
          "name": "constant.numeric.float.fe",
          "match": "\\b\\d+\\.\\d+([eE][+-]?\\d+)?\\b"
        },
        {
          "name": "constant.numeric.integer.fe",
          "match": "\\b\\d+\\b"
        },
        {
          "name": "constant.numeric.null.fe",
          "match": "null"
        }
      ]
    },

    "operators": {
      "patterns": [
        {
          "name": "keyword.operator.assignment.fe",
          "match": "(<<=|>>=|\\+=|-=|\\*=|/=|%=|&=|\\|=|#=|=)"
        },
        {
          "name": "keyword.operator.comparison.fe",
          "match": "(<=|>=|==|!=|<|>)"
        },
        {
          "name": "keyword.operator.arithmetic.fe",
          "match": "(\\+|\\-|\\*|/|%)"
        },
        {
          "name": "keyword.operator.logical.fe",
          "match": "(!|\\?|\\:)"
        },
        {
          "name": "keyword.operator.reference.fe",
          "match": "(\\^)"
        },
        {
          "name": "keyword.operator.bitwise.fe",
          "match": "(&|\\||#|~)"
        },
        {
          "name": "keyword.operator.shift.fe",
          "match": "(<<|>>)"
        }
      ]
    },

    "punctuation": {
      "patterns": [
        {
          "name": "punctuation.definition.block.fe",
          "match": "[{}]"
        },
        {
          "name": "punctuation.definition.brackets.fe",
          "match": "[\\[\\]]"
        },
        {
          "name": "punctuation.definition.parens.fe",
          "match": "[()]"
        },
        {
          "name": "punctuation.separator.fe",
          "match": "[,;]"
        }
      ]
    }

  }
}
EOF

if [ -d "$SCRIPT_DIR/ferralang/lsp/client/node_modules/vscode-languageclient" ]; then
  cp -a "$SCRIPT_DIR/ferralang/lsp/client/node_modules" "$EXT_DIR/node_modules"
elif [ -d "$SCRIPT_DIR/ferralang/tests/node_modules/vscode-languageclient" ]; then
  cp -a "$SCRIPT_DIR/ferralang/tests/node_modules" "$EXT_DIR/node_modules"
elif command -v npm >/dev/null 2>&1; then
  echo "Installing the local Ferra LSP client dependency..."
  npm install --omit=dev --ignore-scripts --prefix "$EXT_DIR"
else
  echo "ERROR: npm is required to install vscode-languageclient." >&2
  exit 1
fi

if [[ -z "$FIRST_EXTENSION_DIR" ]]; then
  FIRST_EXTENSION_DIR="$EXT_DIR"
fi

echo "Ferra extension files prepared successfully."
done

VSIX_BUILDER="$SCRIPT_DIR/packaging/vscode/make-vsix.py"
PYTHON_COMMAND=""
if command -v python3 >/dev/null 2>&1; then
  PYTHON_COMMAND="$(command -v python3)"
elif command -v python >/dev/null 2>&1; then
  PYTHON_COMMAND="$(command -v python)"
fi

if [[ -n "$PYTHON_COMMAND" && -f "$VSIX_BUILDER" ]]; then
  VSIX_WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ferra-vsix-install.XXXXXX")"
  trap 'rm -rf "$VSIX_WORK_DIR"' EXIT HUP INT TERM
  VSIX_PATH="$VSIX_WORK_DIR/ferra-$EXTENSION_VERSION.vsix"
  "$PYTHON_COMMAND" "$VSIX_BUILDER" "$FIRST_EXTENSION_DIR" "$VSIX_PATH" >/dev/null

  EDITOR_EXTENSION_ARGS=()
  if [[ -n "${VSCODE_EXTENSIONS:-}" ]]; then
    EDITOR_EXTENSION_ARGS=(--extensions-dir "$VSCODE_EXTENSIONS")
  elif [[ -n "${VSCODE_EXTENSIONS_DIR:-}" ]]; then
    EDITOR_EXTENSION_ARGS=(--extensions-dir "$VSCODE_EXTENSIONS_DIR")
  elif [[ -n "${VSCODE_PORTABLE:-}" ]]; then
    EDITOR_EXTENSION_ARGS=(--extensions-dir "$VSCODE_PORTABLE/extensions")
  fi

  EDITOR_CANDIDATES=(code code-insiders code-oss codium cursor code-server)
  if [[ "$(uname -s)" == "Darwin" ]]; then
    EDITOR_CANDIDATES+=(
      "/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code"
      "$HOME/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code"
      "/Applications/Visual Studio Code - Insiders.app/Contents/Resources/app/bin/code-insiders"
      "/Applications/Cursor.app/Contents/Resources/app/bin/cursor"
      "/Applications/VSCodium.app/Contents/Resources/app/bin/codium"
    )
  fi

  EDITOR_FOUND=0
  EDITOR_INSTALLED=0
  SEEN_EDITOR_PATHS=$'\n'
  for editor_candidate in "${EDITOR_CANDIDATES[@]}"; do
    if [[ "$editor_candidate" == */* ]]; then
      [[ -x "$editor_candidate" ]] || continue
      editor_path="$editor_candidate"
    else
      command -v "$editor_candidate" >/dev/null 2>&1 || continue
      editor_path="$(command -v "$editor_candidate")"
    fi
    if [[ "$SEEN_EDITOR_PATHS" == *$'\n'"$editor_path"$'\n'* ]]; then
      continue
    fi
    SEEN_EDITOR_PATHS+="$editor_path"$'\n'
    EDITOR_FOUND=$((EDITOR_FOUND + 1))

    echo "Registering Ferra VSIX with: $editor_path"
    if install_output=$("$editor_path" "${EDITOR_EXTENSION_ARGS[@]}" \
        --install-extension "$VSIX_PATH" --force 2>&1); then
      if extension_list=$("$editor_path" "${EDITOR_EXTENSION_ARGS[@]}" \
          --list-extensions --show-versions 2>&1) && \
          printf '%s\n' "$extension_list" | grep -Fqx "$EXTENSION_ID@$EXTENSION_VERSION"; then
        EDITOR_INSTALLED=$((EDITOR_INSTALLED + 1))
      else
        echo "WARNING: $editor_path did not report $EXTENSION_ID@$EXTENSION_VERSION after installation." >&2
        printf '%s\n' "$extension_list" >&2
      fi
    else
      echo "WARNING: VSIX installation failed in $editor_path:" >&2
      printf '%s\n' "$install_output" >&2
    fi
  done

  if (( EDITOR_FOUND > 0 && EDITOR_INSTALLED == 0 )); then
    echo "ERROR: an editor CLI was found, but none registered the Ferra extension." >&2
    echo "VS Code 1.91 or newer is required." >&2
    exit 1
  fi
  if (( EDITOR_FOUND == 0 )); then
    echo "No VS Code-compatible CLI was found; installed the extension directory directly." >&2
  fi
  rm -rf "$VSIX_WORK_DIR"
  trap - EXIT HUP INT TERM
else
  echo "WARNING: Python 3 is unavailable, so VSIX registration was skipped." >&2
  echo "The extension directory was installed, but hover and inlay hints also require Python 3." >&2
fi

echo "Ferra/eFerra syntax, hover, diagnostics and inlay hints are installed."
echo "Restart the editor completely."
