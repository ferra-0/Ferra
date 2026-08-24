#!/usr/bin/env bash
set -e

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

for EXTENSIONS_ROOT in "${EXTENSION_ROOTS[@]}"; do
EXT_DIR="$EXTENSIONS_ROOT/local.ferra-0.0.3"
LEGACY_FERRA_DIRS=(
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
  "version": "0.0.3",
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
          "match": "\\b[a-zA-Z_][a-zA-Z0-9_]*\\s*(?=\\()"
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
          "match": "\\b(log|logl|take|ftake|sizeof|typeof|volatile_store|volatile_load|atomic_load|atomic_store|atomic_add|atomic_exchange|atomic_compare_exchange)\\b"
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

echo "Ferra language installed successfully."
echo "Restart VS Code completely."
done
