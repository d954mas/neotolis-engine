# Pinned tool versions (.toolchain-versions, .emsdk-version). Sourcing this puts the
# repo-local LLVM first on PATH; the require_* functions reject any other version of a
# tool before a check uses it, so a local run cannot pass with tools CI does not have.

NT_TOOLCHAIN_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../../.toolchain-versions
source "$NT_TOOLCHAIN_ROOT/.toolchain-versions"
# Per-user installs shared by every checkout and worktree (scripts/setup.sh).
NT_TOOLCHAIN_DIR="${USERPROFILE:-$HOME}/.neotolis/toolchain"
command -v cygpath > /dev/null 2>&1 && NT_TOOLCHAIN_DIR="$(cygpath -u "$NT_TOOLCHAIN_DIR")"
NT_LLVM_BIN="$NT_TOOLCHAIN_DIR/llvm-$LLVM_VERSION/bin"
NT_BUILDTOOLS_DIR="$NT_TOOLCHAIN_DIR/buildtools-cmake-$CMAKE_VERSION-ninja-$NINJA_VERSION"
if [ -d "$NT_BUILDTOOLS_DIR/Scripts" ]; then
    NT_BUILDTOOLS_BIN="$NT_BUILDTOOLS_DIR/Scripts"
else
    NT_BUILDTOOLS_BIN="$NT_BUILDTOOLS_DIR/bin"
fi
export PATH="$NT_LLVM_BIN:$NT_BUILDTOOLS_BIN:$PATH"

toolchain_fail() {
    echo "toolchain: $1" >&2
    echo "  install the pinned versions: bash scripts/setup.sh (Python and Node: docs/build.md#pinned-toolchain)" >&2
    exit 1
}

# require_llvm clang clang-format ...: each tool resolves to the pinned LLVM release.
require_llvm() {
    local tool found
    for tool in "$@"; do
        found="$("$tool" --version 2> /dev/null | grep -o 'version [0-9][0-9.]*' | head -1 || true)"
        found="${found#version }"
        [ "$found" = "$LLVM_VERSION" ] || toolchain_fail "$tool: expected $LLVM_VERSION, found ${found:-none}"
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

require_buildtools() {
    local found
    found="$(cmake --version 2> /dev/null | sed -n 's/^cmake version //p' || true)"
    [ "$found" = "$CMAKE_VERSION" ] || toolchain_fail "cmake: expected $CMAKE_VERSION, found ${found:-none}"
    # The PyPI build reports 1.13.2.git.kitware...; the pin is its release number.
    found="$(ninja --version 2> /dev/null | sed -E -n 's/^([0-9]+[.][0-9]+[.][0-9]+).*/\1/p' || true)"
    [ "$found" = "$NINJA_VERSION" ] || toolchain_fail "ninja: expected $NINJA_VERSION, found ${found:-none}"
}

require_emcc() {
    local expected found
    expected="$(tr -d '[:space:]' < "$NT_TOOLCHAIN_ROOT/.emsdk-version")"
    found="$(emcc --version 2> /dev/null | head -1 | grep -o ') [0-9][0-9.]*' | tr -d ') ' || true)"
    [ "$found" = "$expected" ] || toolchain_fail "emcc: expected $expected (.emsdk-version), found ${found:-none}"
}

# require_build_compiler <build dir>: the tree was configured by the pinned CMake and Ninja and
# its C and C++ compilers are the pinned LLVM, so a tree configured before a version change
# cannot keep building with the old tools.
require_build_compiler() {
    local dir="$1" lang found cmake_version
    found="$(sed -n 's/^CMAKE_COMMAND:INTERNAL=//p' "$dir/CMakeCache.txt" 2> /dev/null)"
    case "$found" in
        */buildtools-cmake-"$CMAKE_VERSION"-ninja-"$NINJA_VERSION"/*) ;;
        *) toolchain_fail "$dir: configured by ${found:-unknown cmake}, expected CMake $CMAKE_VERSION from ~/.neotolis/toolchain; reconfigure the tree with --fresh" ;;
    esac
    found="$(sed -n 's/^CMAKE_MAKE_PROGRAM:[A-Z]*=//p' "$dir/CMakeCache.txt" 2> /dev/null)"
    case "$found" in
        */buildtools-cmake-"$CMAKE_VERSION"-ninja-"$NINJA_VERSION"/*) ;;
        *) toolchain_fail "$dir: builds with ${found:-unknown ninja}, expected Ninja $NINJA_VERSION from ~/.neotolis/toolchain; reconfigure the tree with --fresh" ;;
    esac
    cmake_version="$(cmake --version | sed -n 's/^cmake version //p')"
    for lang in C CXX; do
        found="$(sed -n "s/^set(CMAKE_${lang}_COMPILER \"\(.*\)\")/\1/p" "$dir/CMakeFiles/$cmake_version/CMake${lang}Compiler.cmake" 2> /dev/null)"
        case "$found" in
            */.neotolis/toolchain/llvm-"$LLVM_VERSION"/bin/*) ;;
            *) toolchain_fail "$dir: $lang compiler is ${found:-unset}, expected LLVM $LLVM_VERSION from ~/.neotolis/toolchain; reconfigure the tree with --fresh" ;;
        esac
    done
}
