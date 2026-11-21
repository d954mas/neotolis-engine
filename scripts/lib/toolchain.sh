# The pinned toolchain (.toolchain-versions) for a shell. Sourcing this puts the installs of
# scripts/setup_toolchain.py first on PATH and activates the pinned emsdk, as CI does. CMake rejects
# a tree built with anything else (cmake/nt_toolchain_check.cmake); the require_* functions cover
# what CMake does not run: clang-format, clang-tidy, Python and Node.

NT_TOOLCHAIN_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../../.toolchain-versions
source "$NT_TOOLCHAIN_ROOT/.toolchain-versions"
# Python's Path.home(): USERPROFILE on Windows, HOME elsewhere.
if command -v cygpath > /dev/null 2>&1; then
    NT_TOOLCHAIN_DIR="$(cygpath -u "$USERPROFILE")/.neotolis/toolchain"
    EMSDK="$(cygpath -m "$NT_TOOLCHAIN_DIR/emsdk-$EMSDK_VERSION")"
else
    NT_TOOLCHAIN_DIR="$HOME/.neotolis/toolchain"
    EMSDK="$NT_TOOLCHAIN_DIR/emsdk-$EMSDK_VERSION"
fi
export EMSDK EM_CONFIG="$EMSDK/.emscripten"
# A globally activated emsdk (emsdk activate --permanent) exports these; emcc would run on that
# SDK's Python instead of the pinned one and fail its minimum-version check.
unset EMSDK_PYTHON EMSDK_NODE
export PATH="$NT_TOOLCHAIN_DIR/llvm-$LLVM_VERSION/bin:$NT_TOOLCHAIN_DIR/cmake-$CMAKE_VERSION/bin:$NT_TOOLCHAIN_DIR/ninja-$NINJA_VERSION:$NT_TOOLCHAIN_DIR/emsdk-$EMSDK_VERSION:$NT_TOOLCHAIN_DIR/emsdk-$EMSDK_VERSION/upstream/emscripten:$PATH"

toolchain_fail() {
    echo "toolchain: $1" >&2
    echo "  install the pinned versions: python scripts/setup_toolchain.py (Python and Node: docs/build.md#pinned-toolchain)" >&2
    exit 1
}

# require_installed llvm cmake ninja emsdk: setup_toolchain.py writes each marker last, and the
# directories above come first on PATH, so a marker means the pinned binary is the one that runs.
require_installed() {
    local tool var
    for tool in "$@"; do
        var="${tool^^}_VERSION"
        [ -f "$NT_TOOLCHAIN_DIR/$tool-${!var}/.installed" ] || toolchain_fail "$tool ${!var} is not installed in $NT_TOOLCHAIN_DIR"
    done
}

require_python() {
    local found
    found="$(python --version 2>&1 | grep -o '[0-9]*\.[0-9]*' | head -1 || true)"
    [ "$found" = "$PYTHON_VERSION" ] || toolchain_fail "python: expected $PYTHON_VERSION.x, found ${found:-none}"
}

require_node() {
    local found
    found="$(node --version 2> /dev/null | sed -n 's/^v\([0-9]*\)\..*/\1/p' || true)"
    [ "$found" = "$NODE_VERSION" ] || toolchain_fail "node: expected $NODE_VERSION.x, found ${found:-none}"
}
