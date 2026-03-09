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

# Find all engine .c source files (exclude vendored deps/).
# Headers are analyzed transitively; passing only .c avoids duplicated header work.
SOURCES=$(find engine shared tools examples tests \
    -name '*.c' | grep -v deps/)

FILE_COUNT=$(echo "$SOURCES" | wc -w)
echo "Running clang-tidy on $FILE_COUNT files..."

# Treat vendored deps as system includes to silence clang-tidy on their headers
EXTRA_ARGS="--extra-arg=-isystem$ROOT_DIR/deps/unity/src"

# Use run-clang-tidy if available and python3 is present, otherwise fall back
# Note: run-clang-tidy uses single-dash flags (-extra-arg), clang-tidy uses double-dash (--extra-arg)
if command -v run-clang-tidy &>/dev/null && command -v python3 &>/dev/null; then
    echo "$SOURCES" | xargs run-clang-tidy -p "$BUILD_DIR" -extra-arg="-isystem$ROOT_DIR/deps/unity/src"
else
    echo "$SOURCES" | xargs clang-tidy -p "$BUILD_DIR" $EXTRA_ARGS
fi

echo "clang-tidy: all checks passed"
