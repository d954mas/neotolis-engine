#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
# Match compile_commands.json's path style (Windows C:/... when on Git Bash);
# clang treats /c/... and C:/... as distinct, breaking -isystem suppression.
if command -v cygpath > /dev/null 2>&1; then
    ROOT_DIR="$(cygpath -m "$ROOT_DIR")"
fi
BUILD_DIR="${1:-build/_cmake/native-debug}"
if [ "$#" -gt 0 ]; then shift; fi

if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
    echo "ERROR: compile_commands.json not found in $BUILD_DIR"
    echo "Run: cmake --preset native-debug"
    exit 1
fi

if [ "$#" -gt 0 ]; then
    # Explicit file list mode: lint only the given .c files (non-.c args skipped).
    SOURCES=""
    for f in "$@"; do
        case "$f" in
            *.c) SOURCES="$SOURCES$f"$'\n' ;;
        esac
    done
    SOURCES="${SOURCES%$'\n'}"
    if [ -z "$SOURCES" ]; then
        echo "clang-tidy: no .c files in argument list — nothing to check"
        exit 0
    fi
else
    # Find all engine .c source files (exclude vendored deps/ and web-only files).
    SOURCES=$(find engine shared tools examples tests \
        -name '*.c' | grep -v 'deps/\|/web/\|_web\.c\|tools/research/')
fi

# Lint only TUs present in compile_commands.json. A file absent from the DB
# (e.g. devapi tests when NT_DEVAPI_GROUP_* options are off) gets guessed
# flags and bogus diagnostics. check.sh --push lints those via tidy-ci.
FILTER_RESULT=$(printf '%s\n' "$SOURCES" | python -c "
import json, sys
build_dir, root = sys.argv[1], sys.argv[2]
db = {e['file'].replace(chr(92), '/').lower() for e in json.load(open(build_dir + '/compile_commands.json'))}
kept, skipped = [], 0
for line in sys.stdin:
    f = line.strip()
    if not f:
        continue
    if (root + '/' + f).lower() in db:
        kept.append(f)
    else:
        skipped += 1
print(skipped)
print('\n'.join(kept))
" "$BUILD_DIR" "$ROOT_DIR" | tr -d '\r')
SKIPPED=$(printf '%s\n' "$FILTER_RESULT" | head -1)
SOURCES=$(printf '%s\n' "$FILTER_RESULT" | tail -n +2)
if [ "$SKIPPED" -gt 0 ]; then
    echo "clang-tidy: skipped $SKIPPED file(s) absent from $BUILD_DIR/compile_commands.json (build options off)"
fi
if [ -z "$SOURCES" ]; then
    echo "clang-tidy: nothing to check in this build dir"
    exit 0
fi

FILE_COUNT=$(echo "$SOURCES" | wc -w)
echo "Running clang-tidy on $FILE_COUNT files..."

# Treat vendored deps as system includes to silence clang-tidy on their headers
SYSTEM_DEPS=(
    "$ROOT_DIR/deps/unity/src"
    "$ROOT_DIR/deps/cglm/include"
    "$ROOT_DIR/deps/glad/include"
    "$ROOT_DIR/deps/xxhash"
    "$ROOT_DIR/deps/cgltf"
    "$ROOT_DIR/deps/mikktspace"
    "$ROOT_DIR/deps/stb"
    "$ROOT_DIR/deps/clay"
    "$ROOT_DIR/deps/clipper2/CPP"
    "$ROOT_DIR/deps/glfw/include"
    "$ROOT_DIR/deps/miniz"
    "$ROOT_DIR/deps/tinycthread"
    "$ROOT_DIR/deps/basisu/transcoder"
    "$ROOT_DIR/deps/basisu/encoder"
)

EXTRA_ARGS=()
for dep in "${SYSTEM_DEPS[@]}"; do
    EXTRA_ARGS+=("--extra-arg=-isystem$dep")
done

# Invoke one TU per clang-tidy process. Any tool failure fails the gate; reruns
# are explicit so a transient error cannot hide an unchecked file.
TIDY_OUTPUT=$(mktemp)
TIDY_RC=0

PARALLEL_JOBS="${TIDY_JOBS:-$(nproc 2>/dev/null || echo 4)}"

printf '%s\n' "$SOURCES" | xargs -n 1 -P "$PARALLEL_JOBS" clang-tidy -p "$BUILD_DIR" "${EXTRA_ARGS[@]}" > "$TIDY_OUTPUT" 2>&1 || TIDY_RC=$?

if [ "$TIDY_RC" -ne 0 ]; then
    echo "clang-tidy: invocation failed with exit $TIDY_RC"
    echo "--- full output ---"
    cat "$TIDY_OUTPUT"
    rm -f "$TIDY_OUTPUT"
    exit "$TIDY_RC"
fi

rm -f "$TIDY_OUTPUT"
echo "clang-tidy: all project checks passed"
