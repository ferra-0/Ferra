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

export FERRA_PATH="${FERRA_PATH:-$PROJECT_ROOT}"
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

"$EFE" "$SCRIPT_DIR/native_timer.efe" >"$BUILD_DIR/native_timer.out"
diff -u "$SCRIPT_DIR/native_timer.out" "$BUILD_DIR/native_timer.out"

"$EFE" "$SCRIPT_DIR/native_json.efe" >"$BUILD_DIR/native_json.out"
diff -u "$SCRIPT_DIR/native_json.out" "$BUILD_DIR/native_json.out"

"$EFE" "$SCRIPT_DIR/primitive_methods.efe" >"$BUILD_DIR/primitive_methods.out"
diff -u "$SCRIPT_DIR/primitive_methods.out" "$BUILD_DIR/primitive_methods.out"

"$EFE" "$SCRIPT_DIR/function_values.efe" >"$BUILD_DIR/function_values.out"
diff -u "$SCRIPT_DIR/function_values.out" "$BUILD_DIR/function_values.out"

"$EFE" "$SCRIPT_DIR/language_features.efe" >"$BUILD_DIR/language_features.out"
diff -u "$SCRIPT_DIR/language_features.out" "$BUILD_DIR/language_features.out"

"$EFE" "$SCRIPT_DIR/native_thread.efe" >"$BUILD_DIR/native_thread.out"
diff -u "$SCRIPT_DIR/native_thread.out" "$BUILD_DIR/native_thread.out"

"$EFE" "$SCRIPT_DIR/native_thread_many.efe" >"$BUILD_DIR/native_thread_many.out"
diff -u "$SCRIPT_DIR/native_thread_many.out" "$BUILD_DIR/native_thread_many.out"

"$EFE" "$SCRIPT_DIR/native_thread_autojoin.efe" >"$BUILD_DIR/native_thread_autojoin.out"
diff -u "$SCRIPT_DIR/native_thread_autojoin.out" "$BUILD_DIR/native_thread_autojoin.out"

"$EFE" "$SCRIPT_DIR/native_buffer.efe" >"$BUILD_DIR/native_buffer.out"
diff -u "$SCRIPT_DIR/native_buffer.out" "$BUILD_DIR/native_buffer.out"

"$EFE" "$SCRIPT_DIR/native_file_stream.efe" >"$BUILD_DIR/native_file_stream.out"
diff -u "$SCRIPT_DIR/native_file_stream.out" "$BUILD_DIR/native_file_stream.out"

PRINTF_PROGRAM="$(type -P printf || true)"
if [[ -z "$PRINTF_PROGRAM" ]]; then
  echo "An external printf executable is required by native_process" >&2
  exit 2
fi
sed "s|@PRINTF_PROGRAM@|$PRINTF_PROGRAM|g" \
  "$SCRIPT_DIR/native_process.efe.in" >"$BUILD_DIR/native_process.efe"
"$EFE" "$BUILD_DIR/native_process.efe" >"$BUILD_DIR/native_process.out"
diff -u "$SCRIPT_DIR/native_process.out" "$BUILD_DIR/native_process.out"

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
"$EFE" "$BUILD_DIR/native_http_stream.efe" >"$BUILD_DIR/native_http_stream.out"
diff -u "$SCRIPT_DIR/native_http_stream.out" "$BUILD_DIR/native_http_stream.out"
kill "$HTTP_SERVER_PID" 2>/dev/null || true
wait "$HTTP_SERVER_PID" 2>/dev/null || true
HTTP_SERVER_PID=""

# The source loader must not depend on a trailing newline for its NUL byte.
python3 -c \
  'import pathlib, sys; pathlib.Path(sys.argv[2]).write_bytes(pathlib.Path(sys.argv[1]).read_bytes()[:-1])' \
  "$SCRIPT_DIR/native_math.efe" "$BUILD_DIR/native_math_no_newline.efe"
"$EFE" "$BUILD_DIR/native_math_no_newline.efe" >"$BUILD_DIR/native_math.out"
diff -u "$SCRIPT_DIR/native_math.out" "$BUILD_DIR/native_math.out"

for name in \
  native_timer_wrong_arity \
  native_timer_unknown_method \
  native_json_invalid \
  native_json_wrong_type \
  native_json_unsupported \
  primitive_wrong_arity \
  primitive_unknown_method \
  native_buffer_bad_capacity \
  native_stream_bad_buffer \
  native_thread_bad_function \
  native_thread_bad_args
do
  if "$EFE" "$SCRIPT_DIR/$name.efe" >"$BUILD_DIR/$name.actual" 2>&1; then
    printf 'FAIL %s: expected failure\n' "$name" >&2
    exit 1
  fi
  grep -F -f "$SCRIPT_DIR/$name.error" "$BUILD_DIR/$name.actual" >/dev/null
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
