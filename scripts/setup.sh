#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

VERSION_FILE="$PROJECT_ROOT/.emsdk-version"
if [ ! -f "$VERSION_FILE" ]; then
    echo "Error: .emsdk-version not found at $VERSION_FILE"
    exit 1
fi

EMSDK_VERSION=$(cat "$VERSION_FILE" | tr -d '[:space:]')
echo "Emscripten SDK version: $EMSDK_VERSION"

EMSDK_DIR="$PROJECT_ROOT/emsdk"

if [ ! -d "$EMSDK_DIR" ]; then
    echo "Cloning emsdk..."
    git clone https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
else
    echo "emsdk directory already exists, updating..."
    cd "$EMSDK_DIR" && git pull && cd "$PROJECT_ROOT"
fi

echo "Installing emsdk $EMSDK_VERSION..."
"$EMSDK_DIR/emsdk" install "$EMSDK_VERSION"

echo "Activating emsdk $EMSDK_VERSION..."
"$EMSDK_DIR/emsdk" activate "$EMSDK_VERSION"

# shellcheck source=/dev/null
source "$EMSDK_DIR/emsdk_env.sh"

echo ""
echo "Emscripten SDK $EMSDK_VERSION is ready."
echo ""
echo "In future sessions, activate it with:"
echo "  source emsdk/emsdk_env.sh"
echo ""
echo "Then build with:"
echo "  emcmake cmake --preset wasm-debug"
echo "  cmake --build --preset wasm-debug"
