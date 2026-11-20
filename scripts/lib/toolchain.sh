# Pinned tool versions (.toolchain-versions, .emsdk-version). Sourcing this puts the
# repo-local LLVM first on PATH; the require_* functions reject any other version of a
# tool before a check uses it, so a local run cannot pass with tools CI does not have.

NT_TOOLCHAIN_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../../.toolchain-versions
source "$NT_TOOLCHAIN_ROOT/.toolchain-versions"
# Per-user install shared by every checkout and worktree (scripts/setup_llvm.py).
NT_LLVM_BIN="${USERPROFILE:-$HOME}/.neotolis/toolchain/llvm-$LLVM_VERSION/bin"
command -v cygpath > /dev/null 2>&1 && NT_LLVM_BIN="$(cygpath -u "$NT_LLVM_BIN")"
export PATH="$NT_LLVM_BIN:$PATH"

toolchain_fail() {
    echo "toolchain: $1" >&2
    echo "  install the pinned versions: see docs/build.md#setup (python scripts/setup_llvm.py for LLVM)" >&2
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

require_emcc() {
    local expected found
    expected="$(tr -d '[:space:]' < "$NT_TOOLCHAIN_ROOT/.emsdk-version")"
    found="$(emcc --version 2> /dev/null | head -1 | grep -o ') [0-9][0-9.]*' | tr -d ') ' || true)"
    [ "$found" = "$expected" ] || toolchain_fail "emcc: expected $expected (.emsdk-version), found ${found:-none}"
}

# require_build_compiler <build dir>: the configured C and C++ compilers are the pinned LLVM,
# so a tree configured before a version change cannot keep building with the old compiler.
require_build_compiler() {
    local dir="$1" lang found cmake_version
    cmake_version="$(cmake --version | sed -n 's/^cmake version //p')"
    for lang in C CXX; do
        found="$(sed -n "s/^set(CMAKE_${lang}_COMPILER \"\(.*\)\")/\1/p" "$dir/CMakeFiles/$cmake_version/CMake${lang}Compiler.cmake" 2> /dev/null)"
        case "$found" in
            */.neotolis/toolchain/llvm-"$LLVM_VERSION"/bin/*) ;;
            *) toolchain_fail "$dir: $lang compiler is ${found:-unset}, expected LLVM $LLVM_VERSION from ~/.neotolis/toolchain; reconfigure the tree with --fresh" ;;
        esac
    done
}
