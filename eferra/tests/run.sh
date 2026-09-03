#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
EFERRA_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd -- "$EFERRA_DIR/.." && pwd)"
COMPILER="${FERRA_COMPILER:-$(command -v ferra)}"
EFE="${EFERRA_BIN:-}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/eferra-tests.XXXXXX")"
HTTP_SERVER_PID=""

cleanup() {
  if [[ -n "$HTTP_SERVER_PID" ]]; then
    kill "$HTTP_SERVER_PID" 2>/dev/null || true
    wait "$HTTP_SERVER_PID" 2>/dev/null || true
  fi
  rm -rf -- "$BUILD_DIR"
}
trap cleanup EXIT

print_failure() {
  local name="$1"
  local source="$2"
  local actual="$3"
  local status="$4"

  printf 'FAIL %s (exit code %s)\n' "$name" "$status" >&2
  printf '%s\n' '--- source (with line numbers) ---' >&2
  nl -ba "$source" >&2
  printf '%s\n' '--- eFerra output ---' >&2
  if [[ -s "$actual" ]]; then
    cat "$actual" >&2
  else
    printf '(no output)\n' >&2
  fi
}

run_success_case() {
  local name="$1"
  local source="${2:-$SCRIPT_DIR/$name.efe}"
  local expected="${3:-$SCRIPT_DIR/$name.out}"
  local actual="$BUILD_DIR/$name.actual"
  local status

  set +e
  "$EFE" "$source" >"$actual" 2>&1
  status=$?
  set -e

  if (( status != 0 )); then
    print_failure "$name" "$source" "$actual" "$status"
    return "$status"
  fi

  if ! diff -u "$expected" "$actual"; then
    print_failure "$name" "$source" "$actual" 0
    return 1
  fi
}

# Always use this checkout's standard library.  In particular, do not inherit
# a FERRA_PATH left over from a checkout that has since been moved.
export FERRA_PATH="$PROJECT_ROOT"
cd "$PROJECT_ROOT"

if [[ -z "$EFE" ]]; then
  EFE="$BUILD_DIR/efe"
  "$COMPILER" "$EFERRA_DIR/efe.fe" -o "$BUILD_DIR/efe.ll" >/dev/null
  if [[ -z "${FERRA_RUNTIME_LIBRARY:-}" ]]; then
    echo "FERRA_RUNTIME_LIBRARY is required when EFERRA_BIN is not set" >&2
    exit 2
  fi
  clang++ -O2 -Wno-override-module "$BUILD_DIR/efe.ll" \
    "$FERRA_RUNTIME_LIBRARY" -lcurl -pthread -lm -o "$EFE"
fi

for name in \
  native_timer \
  native_json \
  primitive_methods \
  function_values \
  func_var_keywords \
  language_features \
  native_thread \
  native_thread_many \
  native_thread_autojoin \
  native_buffer \
  native_file_stream
do
  run_success_case "$name"
done

run_success_case imports \
  "$EFERRA_DIR/testdata/imports/main.efe" \
  "$EFERRA_DIR/testdata/imports/main.out"

PRINTF_PROGRAM="$(type -P printf || true)"
if [[ -z "$PRINTF_PROGRAM" ]]; then
  echo "An external printf executable is required by native_process" >&2
  exit 2
fi
sed "s|@PRINTF_PROGRAM@|$PRINTF_PROGRAM|g" \
  "$SCRIPT_DIR/native_process.efe.in" >"$BUILD_DIR/native_process.efe"
run_success_case native_process \
  "$BUILD_DIR/native_process.efe" "$SCRIPT_DIR/native_process.out"

python3 "$SCRIPT_DIR/http_stream_server.py" "$BUILD_DIR/http-port" &
HTTP_SERVER_PID=$!
for _ in {1..100}; do
  [[ -s "$BUILD_DIR/http-port" ]] && break
  sleep 0.02
done
if [[ ! -s "$BUILD_DIR/http-port" ]]; then
  printf 'FAIL native_http_stream: local HTTP server did not start\n' >&2
  exit 1
fi
HTTP_PORT="$(<"$BUILD_DIR/http-port")"
sed "s/__PORT__/$HTTP_PORT/g" "$SCRIPT_DIR/native_http_stream.efe.in" \
  >"$BUILD_DIR/native_http_stream.efe"
run_success_case native_http_stream \
  "$BUILD_DIR/native_http_stream.efe" "$SCRIPT_DIR/native_http_stream.out"
kill "$HTTP_SERVER_PID" 2>/dev/null || true
wait "$HTTP_SERVER_PID" 2>/dev/null || true
HTTP_SERVER_PID=""

# The source loader must not depend on a trailing newline for its NUL byte.
python3 -c \
  'import pathlib, sys; pathlib.Path(sys.argv[2]).write_bytes(pathlib.Path(sys.argv[1]).read_bytes()[:-1])' \
  "$SCRIPT_DIR/native_math.efe" "$BUILD_DIR/native_math_no_newline.efe"
run_success_case native_math_no_newline \
  "$BUILD_DIR/native_math_no_newline.efe" "$SCRIPT_DIR/native_math.out"

for name in \
  native_timer_wrong_arity \
  native_timer_unknown_method \
  native_json_invalid \
  native_json_wrong_type \
  native_json_unsupported \
  primitive_wrong_arity \
  primitive_unknown_method \
  primitive_map_wrong_callback \
  primitive_map_wrong_arity \
  missing_import \
  native_buffer_bad_capacity \
  native_stream_bad_buffer \
  native_thread_bad_function \
  native_thread_bad_args
do

  case_root="$SCRIPT_DIR"
  if [[ ! -f "$case_root/$name.efe" ]]; then
    case_root="$EFERRA_DIR/testdata/negative"
  fi
  set +e
  "$EFE" "$case_root/$name.efe" >"$BUILD_DIR/$name.actual" 2>&1
  status=$?
  set -e

  if (( status == 0 )); then
    printf 'FAIL %s: expected failure\n' "$name" >&2
    exit 1
  fi
  if grep -Ev '^[0-9]+: ' "$BUILD_DIR/$name.actual" >/dev/null; then
    print_failure "$name" "$case_root/$name.efe" \
      "$BUILD_DIR/$name.actual" "$status"
    printf '%s\n' 'Every diagnostic must start with a source line.' >&2
    exit 1
  fi
  if ! grep -F -f "$case_root/$name.error" "$BUILD_DIR/$name.actual" >/dev/null; then
    print_failure "$name" "$case_root/$name.efe" \
      "$BUILD_DIR/$name.actual" "$status"
    printf '%s\n' '--- expected diagnostic fragment(s) ---' >&2
    cat "$case_root/$name.error" >&2
    exit 1
  fi
done

"$COMPILER" "$SCRIPT_DIR/native_bindings.fe" \
  -o "$BUILD_DIR/native_bindings.ll" >/dev/null
if [[ -z "${FERRA_RUNTIME_LIBRARY:-}" ]]; then
  echo "FERRA_RUNTIME_LIBRARY is required for native binding tests" >&2
  exit 2
fi
clang++ -O2 -Wno-override-module "$BUILD_DIR/native_bindings.ll" \
  "$FERRA_RUNTIME_LIBRARY" -lcurl -pthread -lm \
  -o "$BUILD_DIR/native_bindings"
"$BUILD_DIR/native_bindings" >"$BUILD_DIR/native_bindings.out"
diff -u "$SCRIPT_DIR/native_bindings.out" "$BUILD_DIR/native_bindings.out"

echo "EFerra native binding tests passed"
