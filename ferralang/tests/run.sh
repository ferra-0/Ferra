#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
FERRALANG_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

PROJECT_ROOT="$(cd -- "$FERRALANG_DIR/.." && pwd)"
COMPILER="${FERRA_COMPILER:-ferra}"
NATIVE_OPT_LEVEL="${FERRA_NATIVE_OPT_LEVEL:--O2}"
BUILD_DIR="$(mktemp -d /tmp/ferra-regression.XXXXXX)"

if command -v timeout >/dev/null 2>&1; then
  TIMEOUT_PROGRAM=timeout
elif command -v gtimeout >/dev/null 2>&1; then
  TIMEOUT_PROGRAM=gtimeout
else
  TIMEOUT_PROGRAM=""
fi

if [ ! -x "$COMPILER" ]; then
  echo "Ferra compiler is not executable: $COMPILER" >&2
  exit 2
fi

if [ -z "${KEEP_TEST_ARTIFACTS:-}" ]; then
  trap 'rm -rf -- "$BUILD_DIR"' EXIT
else
  echo "Artifacts: $BUILD_DIR"
fi

export FERRA_PATH="$PROJECT_ROOT"

passed=0
failed=0

pass() {
  passed=$((passed + 1))
  echo "PASS $1"
}

fail() {
  failed=$((failed + 1))
  echo "FAIL $1: $2"
}

while IFS= read -r source; do
  relative="${source#"$SCRIPT_DIR/positive/"}"
  stem="${source%.fe}"
  artifact_name="${relative//\//__}"
  artifact_name="${artifact_name%.fe}"
  ir="$BUILD_DIR/$artifact_name.ll"
  executable="$BUILD_DIR/$artifact_name"
  compiler_log="$BUILD_DIR/$artifact_name.compiler.log"
  runtime_output="$BUILD_DIR/$artifact_name.out"
  run_command=("$executable")

  if [ -f "$stem.args" ]; then
    while IFS= read -r argument || [ -n "$argument" ]; do
      run_command+=("$argument")
    done < "$stem.args"
  fi

  if ! "$COMPILER" "$source" -o "$ir" >"$compiler_log" 2>&1; then
    fail "$relative" "Ferra compilation failed"
    sed -n '1,20p' "$compiler_log"
    continue
  fi

  if [ -f "$stem.ir_contains" ]; then
    ir_ok=1
    while IFS= read -r fragment; do
      if [ -n "$fragment" ] && ! grep -Fq -- "$fragment" "$ir"; then
        fail "$relative" "missing IR fragment: $fragment"
        ir_ok=0
      fi
    done < "$stem.ir_contains"
    if [ "$ir_ok" -eq 0 ]; then
      continue
    fi
  fi

  if [ -f "$stem.ir_count" ]; then
    ir_count_ok=1
    while IFS='|' read -r expected_count fragment; do
      if [ -z "$expected_count" ] || [ -z "$fragment" ]; then
        continue
      fi
      actual_count="$(grep -F -c -- "$fragment" "$ir" || true)"
      if [ "$actual_count" -ne "$expected_count" ]; then
        fail "$relative" "IR count for '$fragment': expected $expected_count, got $actual_count"
        ir_count_ok=0
      fi
    done < "$stem.ir_count"
    if [ "$ir_count_ok" -eq 0 ]; then
      continue
    fi
  fi

  if [ -f "$stem.compile_only" ]; then
    if clang "$NATIVE_OPT_LEVEL" -Wno-override-module -c "$ir" \
        -o "$executable.o" >"$runtime_output" 2>&1; then
      pass "$relative"
    else
      fail "$relative" "clang rejected generated IR"
      sed -n '1,20p' "$runtime_output"
    fi
    continue
  fi

  link_command=(clang++ "$NATIVE_OPT_LEVEL" -Wno-override-module "$ir")
  link_ready=1
  if [ -f "$stem.link" ]; then
    while IFS= read -r item; do
      if [ -z "$item" ]; then
        continue
      elif [[ "$item" == -* ]]; then
        link_command+=("$item")
      elif [ "$item" = "@ferra-runtime" ]; then
        if [ -z "${FERRA_RUNTIME_LIBRARY:-}" ]; then
          fail "$relative" "FERRA_RUNTIME_LIBRARY is required by $stem.link"
          link_ready=0
          break
        fi
        link_command+=("$FERRA_RUNTIME_LIBRARY")
      else
        link_command+=("$PROJECT_ROOT/$item")
      fi
    done < "$stem.link"
  fi
  if [ "$link_ready" -eq 0 ]; then
    continue
  fi
  link_command+=(-o "$executable")

  if ! "${link_command[@]}" >"$runtime_output" 2>&1; then
    fail "$relative" "native linking failed"
    sed -n '1,20p' "$runtime_output"
    continue
  fi

  if [ -n "$TIMEOUT_PROGRAM" ]; then
    "$TIMEOUT_PROGRAM" 5s "${run_command[@]}" >"$runtime_output" 2>&1
  else
    "${run_command[@]}" >"$runtime_output" 2>&1
  fi
  status=$?
  if [ "$status" -ne 0 ]; then
    fail "$relative" "program exited with status $status"
    sed -n '1,20p' "$runtime_output"
    continue
  fi

  if [ -f "$stem.out" ] && ! diff -u "$stem.out" "$runtime_output"; then
    fail "$relative" "stdout differs"
    continue
  fi

  pass "$relative"
done < <(find "$SCRIPT_DIR/positive" -type f -name '*.fe' | sort)

while IFS= read -r source; do
  relative="${source#"$SCRIPT_DIR/negative/"}"
  stem="${source%.fe}"
  artifact_name="negative__${relative//\//__}"
  artifact_name="${artifact_name%.fe}"
  ir="$BUILD_DIR/$artifact_name.ll"
  compiler_log="$BUILD_DIR/$artifact_name.compiler.log"

  if "$COMPILER" "$source" -o "$ir" >"$compiler_log" 2>&1; then
    fail "$relative" "negative test unexpectedly compiled"
    continue
  fi

  diagnostics_ok=1
  while IFS= read -r fragment; do
    if [ -n "$fragment" ] && ! grep -Fq -- "$fragment" "$compiler_log"; then
      fail "$relative" "missing diagnostic: $fragment"
      diagnostics_ok=0
    fi
  done < "$stem.error"

  if [ "$diagnostics_ok" -eq 1 ]; then
    pass "negative/$relative"
  fi
done < <(find "$SCRIPT_DIR/negative" -type f -name '*.fe' | sort)

echo
echo
echo "--- R.E.S.U.L.T passed=$passed failed=$failed ---"
if [ "$failed" -ne 0 ]; then
  exit 1
fi
