#!/usr/bin/env bash
# Unified pre-commit check. Modes:
#   scripts/check.sh          gates + build + ctest + format/tidy on changed files
#   scripts/check.sh --full   default + whole-tree format + full tidy
#   scripts/check.sh --push   default + wasm-debug + wasm-release + submodule test
# The cheap gates (module composition, EM_JS_DEPS, doc links, CRT pins) run in EVERY mode —
# they cost seconds and previously CI-only failures came exactly from skipping them.
# Remaining known CI-only class: GNU ld link order (Linux-specific, see AGENTS.md).
# All tidy runs use the tidy-ci DB (native-debug + devapi groups ON) so the lint
# matches the CI lint job exactly — devapi TUs are absent from the plain
# native-debug compile DB and would otherwise be silently skipped.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

MODE="default"
case "${1:-}" in
    "") ;;
    --full) MODE="full" ;;
    --push) MODE="push" ;;
    *)
        echo "usage: scripts/check.sh [--full|--push]"
        exit 2
        ;;
esac

NATIVE_BUILD_DIR="build/_cmake/native-debug"
CURRENT_STEP="startup"
RESULTS=()

step() {
    CURRENT_STEP="$1"
    echo ""
    echo "== $1 =="
}

ok() {
    RESULTS+=("PASS  $CURRENT_STEP")
}

print_summary() {
    local status=$?
    echo ""
    echo "=================================================="
    echo "check.sh summary (mode: $MODE)"
    if [ "${#RESULTS[@]}" -gt 0 ]; then
        printf '  %s\n' "${RESULTS[@]}"
    fi
    if [ "$status" -eq 0 ]; then
        echo "RESULT: PASS"
    else
        echo "  FAIL  $CURRENT_STEP"
        echo "RESULT: FAIL"
    fi
}
trap print_summary EXIT

# #region changed files
# Union of: commits ahead of origin/master, staged+unstaged edits, untracked files.
compute_changed_files() {
    local base_ref=""
    if git rev-parse --verify --quiet origin/master > /dev/null; then
        base_ref="origin/master"
    elif git rev-parse --verify --quiet master > /dev/null; then
        base_ref="master"
    fi
    {
        if [ -n "$base_ref" ]; then
            git diff --name-only "$base_ref...HEAD"
        fi
        git diff --name-only HEAD
        git ls-files --others --exclude-standard
    } | sort -u | while IFS= read -r f; do
        [ -f "$f" ] && printf '%s\n' "$f" || true
    done
}
CHANGED_FILES="$(compute_changed_files)"

# Format set: changed .c/.h outside vendored deps/.
FORMAT_FILES="$(printf '%s\n' "$CHANGED_FILES" | grep -E '\.(c|h)$' | grep -v 'deps/' || true)"
# Tidy set: changed .c under tidy.sh's find roots, minus its exclusions.
TIDY_FILES="$(printf '%s\n' "$CHANGED_FILES" \
    | grep -E '^(engine|shared|tools|examples|tests)/.*\.c$' \
    | grep -v 'deps/\|/web/\|_web\.c\|tools/research/' || true)"
# Any changed header invalidates unchanged .c files -> full tidy.
CHANGED_HEADERS="$(printf '%s\n' "$CHANGED_FILES" | grep '\.h$' | grep -v 'deps/' || true)"
# #endregion

# tidy-ci DB = native-debug preset + devapi groups ON. Mirrors the ci.yml "lint"
# job (Format & Tidy -> Configure step) so devapi TUs land in compile_commands.json
# with correct flags. Keep the flag list in sync with .github/workflows/ci.yml.
TIDY_CI_DIR="build/_cmake/tidy-ci"
TIDY_CI_FLAGS=(
    -DNT_DEVAPI_ENABLED=ON
    -DNT_DEVAPI_GROUP_UI=ON
    -DNT_DEVAPI_GROUP_OBS=ON
    -DNT_DEVAPI_GROUP_ENTITY_WRITE=ON
    -DNT_DEVAPI_GROUP_CAPTURE=ON
)
ensure_tidy_ci() {
    if [ ! -f "$TIDY_CI_DIR/compile_commands.json" ]; then
        echo "Configuring $TIDY_CI_DIR (one-time; devapi groups ON to match CI lint)..."
        cmake --preset native-debug -B "$TIDY_CI_DIR" "${TIDY_CI_FLAGS[@]}"
    fi
}

# Runs tidy on changed .c files, or the full tree when headers changed.
run_tidy_gate() {
    local build_dir="$1"
    if [ -n "$CHANGED_HEADERS" ]; then
        echo "Changed headers detected — they affect unchanged .c files, running FULL clang-tidy:"
        printf '  %s\n' $CHANGED_HEADERS
        bash scripts/tidy.sh "$build_dir"
    elif [ -n "$TIDY_FILES" ]; then
        echo "Changed .c files:"
        printf '  %s\n' $TIDY_FILES
        # shellcheck disable=SC2086 — newline-split into file args; repo paths have no spaces
        bash scripts/tidy.sh "$build_dir" $TIDY_FILES
    else
        echo "tidy: nothing to check"
    fi
}

step "gates (module composition, EM_JS_DEPS, doc links, CRT pins)"
bash scripts/check_no_real_impl_links.sh
bash scripts/check_link_failure_loud.sh
bash scripts/check_emjs_deps.sh
bash scripts/check_doc_links.sh
bash scripts/check_crt_pins.sh
ok

step "build (native-debug)"
if [ ! -f "$NATIVE_BUILD_DIR/CMakeCache.txt" ]; then
    echo "ERROR: $NATIVE_BUILD_DIR is not configured — configure the preset first:"
    echo "  cmake --preset native-debug"
    exit 1
fi
cmake --build "$NATIVE_BUILD_DIR"
ok

step "hull tolerance guard"
bash scripts/test_bench_hull_tolerance_guard.sh
ok

step "ctest (native-debug)"
ctest --test-dir "$NATIVE_BUILD_DIR" --output-on-failure
ok

if [ "$MODE" = "full" ]; then
    step "clang-format (whole tree)"
    # Same file set as the CI lint job's format check, minus vendored deps/.
    find engine shared tools examples \( -name '*.c' -o -name '*.h' \) \
        | grep -v 'deps/' \
        | xargs clang-format --dry-run --Werror
    ok

    step "clang-tidy (full)"
    ensure_tidy_ci
    bash scripts/tidy.sh "$TIDY_CI_DIR"
    ok
else
    step "clang-format (changed files)"
    if [ -n "$FORMAT_FILES" ]; then
        printf '  %s\n' $FORMAT_FILES
        # shellcheck disable=SC2086
        clang-format --dry-run --Werror $FORMAT_FILES
    else
        echo "format: nothing to check"
    fi
    ok

    step "clang-tidy (changed files)"
    ensure_tidy_ci
    run_tidy_gate "$TIDY_CI_DIR"
    ok
fi

if [ "$MODE" = "push" ]; then
    step "build (wasm-debug)"
    if ! command -v emcc > /dev/null 2>&1; then
        echo "ERROR: emcc not found in PATH — the wasm-debug build is required before push."
        echo "Activate emsdk version $(cat .emsdk-version 2> /dev/null || echo '(see .emsdk-version)'):"
        echo "  source <emsdk>/emsdk_env.sh   (or emsdk_env.bat / emsdk_env.ps1)"
        echo "Then configure once: emcmake cmake --preset wasm-debug"
        exit 1
    fi
    if [ ! -d "build/_cmake/wasm-debug" ]; then
        echo "ERROR: build/_cmake/wasm-debug missing — configure the preset first:"
        echo "  emcmake cmake --preset wasm-debug"
        exit 1
    fi
    cmake --build build/_cmake/wasm-debug
    ok

    # Release catches the Closure-only class (EM_JS helpers stripped by minification)
    # that wasm-debug can never see. Measured warm cost: ~25 s.
    step "build (wasm-release)"
    if [ ! -d "build/_cmake/wasm-release" ]; then
        echo "ERROR: build/_cmake/wasm-release missing — configure the preset first:"
        echo "  emcmake cmake --preset wasm-release"
        exit 1
    fi
    cmake --build build/_cmake/wasm-release
    ok

    # Mirrors the ci.yml "Submodule consumption test": the engine must stay
    # consumable via add_subdirectory with the documented link order.
    step "submodule consumption test"
    SUBMODULE_DIR="build/submodule-test"
    if [ ! -f "$SUBMODULE_DIR/CMakeCache.txt" ]; then
        cmake -S tests/submodule -B "$SUBMODULE_DIR" -DENGINE_ROOT=../.. -DCMAKE_C_COMPILER=clang -G Ninja
    fi
    cmake --build "$SUBMODULE_DIR"
    "./$SUBMODULE_DIR/submodule_test"
    ok
fi
