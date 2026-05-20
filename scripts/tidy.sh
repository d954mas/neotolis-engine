#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${1:-build/_cmake/native-debug}"

if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
    echo "ERROR: compile_commands.json not found in $BUILD_DIR"
    echo "Run: cmake --preset native-debug"
    exit 1
fi

# Find all engine .c source files (exclude vendored deps/ and web-only files).
SOURCES=$(find engine shared tools examples tests \
    -name '*.c' | grep -v 'deps/\|/web/\|_web\.c\|tools/research/')

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
)

EXTRA_ARGS=""
for dep in "${SYSTEM_DEPS[@]}"; do
    EXTRA_ARGS+=" --extra-arg=-isystem$dep"
done

# Always invoke clang-tidy directly. The LLVM run-clang-tidy Python wrapper
# silently swallows diagnostics on Windows + LLVM 19, causing the script to
# report PASS while real errors exist. Direct per-file invocation surfaces
# errors reliably; parallelism via xargs -P keeps wall time comparable.
TIDY_OUTPUT=$(mktemp)
TIDY_RC=0

PARALLEL_JOBS="${TIDY_JOBS:-$(nproc 2>/dev/null || echo 4)}"

echo "$SOURCES" | tr ' ' '\n' | xargs -n 1 -P "$PARALLEL_JOBS" clang-tidy -p "$BUILD_DIR" $EXTRA_ARGS > "$TIDY_OUTPUT" 2>&1 || TIDY_RC=$?

# Filter: show only errors from project files, not vendored deps
PROJECT_ERRORS=$(grep "error:" "$TIDY_OUTPUT" | grep -v "deps/" || true)

if [ -n "$PROJECT_ERRORS" ]; then
    echo "$PROJECT_ERRORS"
    echo ""
    echo "clang-tidy: FAILED — errors in project files"
    rm -f "$TIDY_OUTPUT"
    exit 1
fi

# Count suppressed deps warnings for info
DEPS_WARNINGS=$(grep -c "error:" "$TIDY_OUTPUT" || true)
if [ "$DEPS_WARNINGS" -gt 0 ]; then
    echo "($DEPS_WARNINGS warnings from vendored deps/ suppressed)"
fi

rm -f "$TIDY_OUTPUT"
echo "clang-tidy: all project checks passed"
