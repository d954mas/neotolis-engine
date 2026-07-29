#!/usr/bin/env bash
# Gate: CRT pinning goes only through nt_set_static_crt(_cxx) (cmake/warnings.cmake).
# A raw -U_DLL / MSVC_RUNTIME_LIBRARY / _ITERATOR_DEBUG_LEVEL anywhere else survives
# NT_STATIC_CRT=OFF and re-creates the mixed-CRT /failifmismatch class inside a
# consumer exe that embeds builder + runtime.
#
# Also: deps/basisu's transcoder TU may be compiled ONLY by engine/basisu. A second
# compile of it in another target duplicates every basist:: symbol as soon as one
# exe links both sides.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

FAIL=0

# Upstream-vendored deps keep their own build files (see AGENTS.md); our thin
# wrapper CMakeLists under deps/ (cjson, stb, ...) ARE checked.
UPSTREAM_DEPS='^(\./)?deps/(clay|cglm|unity|basisu|glfw)/'
# git ls-files (tracked + untracked-unignored) replaces a tree walk that
# sniffed .git packfiles and node_modules: same file set the repo can build from.
CMAKE_FILES="$( { git ls-files -- 'CMakeLists.txt' '**/CMakeLists.txt' '*.cmake' '**/*.cmake';
                  git ls-files --others --exclude-standard -- 'CMakeLists.txt' '**/CMakeLists.txt' '*.cmake' '**/*.cmake'; } |
    sort -u | while IFS= read -r f; do [ -f "$f" ] && printf '%s\n' "$f" || true; done)"

grep_cmake_files() {
    # shellcheck disable=SC2086 — newline-split into file args; repo paths have no spaces
    printf '%s\n' "$CMAKE_FILES" | xargs -r grep -nIH "$@" 2>/dev/null
}

CRT_HITS="$(grep_cmake_files -E -e '-U_DLL|MSVC_RUNTIME_LIBRARY|_ITERATOR_DEBUG_LEVEL' |
    grep -vE "$UPSTREAM_DEPS" |
    grep -v '^\(\./\)\?cmake/warnings\.cmake:' || true)"
if [ -n "$CRT_HITS" ]; then
    echo "ERROR: raw CRT pin outside cmake/warnings.cmake — use nt_set_static_crt(_cxx):"
    printf '%s\n' "$CRT_HITS"
    FAIL=1
fi

TU_HITS="$(grep_cmake_files -e 'basisu_transcoder\.cpp' |
    grep -vE "$UPSTREAM_DEPS" |
    grep -v '^\(\./\)\?engine/basisu/CMakeLists\.txt:' || true)"
if [ -n "$TU_HITS" ]; then
    echo "ERROR: basisu_transcoder.cpp compiled outside engine/basisu (duplicate-TU class):"
    printf '%s\n' "$TU_HITS"
    FAIL=1
fi

if [ "$FAIL" -ne 0 ]; then
    echo "check_crt_pins: FAILED"
    exit 1
fi
echo "check_crt_pins: passed (CRT pins centralized; single basisu transcoder TU)"
