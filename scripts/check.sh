#!/usr/bin/env bash
# Unified pre-commit check. Modes:
#   scripts/check.sh          build + ctest + format/tidy on changed files
#   scripts/check.sh --full   build + ctest + whole-tree format + full tidy
#   scripts/check.sh --push   default + wasm-debug build + CI-parity tidy
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

step "build (native-debug)"
cmake --build "$NATIVE_BUILD_DIR"
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
    bash scripts/tidy.sh "$NATIVE_BUILD_DIR"
    ok

    # The remaining CI lint-job gates (module-composition + EM_JS/Closure).
    step "module-composition & EM_JS gates"
    bash scripts/check_no_real_impl_links.sh
    bash scripts/check_link_failure_loud.sh
    bash scripts/check_emjs_deps.sh
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
    run_tidy_gate "$NATIVE_BUILD_DIR"
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

    step "clang-tidy (CI parity)"
    # Flag list mirrors the ci.yml "lint" job (Format & Tidy -> Configure step):
    # devapi groups ON so devapi TUs land in compile_commands.json with correct flags.
    # Keep in sync with .github/workflows/ci.yml when the lint job changes.
    TIDY_CI_DIR="build/_cmake/tidy-ci"
    TIDY_CI_FLAGS=(
        -DNT_DEVAPI_ENABLED=ON
        -DNT_DEVAPI_GROUP_UI=ON
        -DNT_DEVAPI_GROUP_OBS=ON
        -DNT_DEVAPI_GROUP_ENTITY_WRITE=ON
        -DNT_DEVAPI_GROUP_CAPTURE=ON
    )
    if [ ! -f "$TIDY_CI_DIR/compile_commands.json" ]; then
        echo "Configuring $TIDY_CI_DIR (one-time)..."
        cmake --preset native-debug -B "$TIDY_CI_DIR" "${TIDY_CI_FLAGS[@]}"
    fi
    run_tidy_gate "$TIDY_CI_DIR"
    ok
fi
