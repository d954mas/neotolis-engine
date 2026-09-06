#!/usr/bin/env bash
# Unified pre-commit check. Direct modes are read-only; format_and_check.sh
# opts into running the formatter under the same lock first. Modes:
#   scripts/check.sh                    gates + build + ctest + format/tidy on changed files
#   scripts/check.sh --full             default + whole-tree format + full tidy
#   scripts/check.sh --push             default + native-release + wasm-debug + wasm-release + submodule test
#   scripts/check.sh --format [--full|--push]  format changed files under the same run lock first
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

FORMAT_FIRST=0
if [ "${1:-}" = "--format" ]; then
    FORMAT_FIRST=1
    shift
fi

MODE="default"
case "${1:-}" in
    "") ;;
    --full) MODE="full" ;;
    --push) MODE="push" ;;
    *)
        echo "usage: scripts/check.sh [--format] [--full|--push]"
        exit 2
        ;;
esac

NATIVE_BUILD_DIR="build/_cmake/native-debug"
CURRENT_STEP="startup"
RESULTS=()

# Two checks in one tree corrupt each other (shared test outputs, exes relinked
# under a running ctest); mkdir is the atomic lock that works on every host.
LOCK_DIR="build/.check.lock"
mkdir -p build
if ! mkdir "$LOCK_DIR" 2> /dev/null; then
    echo "check.sh: another run holds $LOCK_DIR in this tree -- wait for it, or rmdir the lock if it is stale"
    exit 2
fi

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
    # A format/tidy failure exits while ctest still runs — reap the TREE (plain
    # kill leaves Windows grandchild test exes alive, holding their file locks).
    if [ -n "${CTEST_PID:-}" ]; then
        WINPID="$(ps -p "$CTEST_PID" 2> /dev/null | awk 'NR == 2 { print $4 }' || true)"
        { [ -n "$WINPID" ] && taskkill //PID "$WINPID" //T //F > /dev/null 2>&1; } || kill "$CTEST_PID" 2> /dev/null || true
        wait "$CTEST_PID" 2> /dev/null || true
    fi
    rmdir "$LOCK_DIR" 2> /dev/null || true
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
        if [ "$CURRENT_STEP" = "ctest (native-debug)" ] && [ -f "${CTEST_LOG:-}" ]; then
            print_ctest_failures "$CTEST_LOG"
            echo "  full ctest log: $CTEST_LOG"
        fi
        echo "RESULT: FAIL"
    fi
}
trap print_summary EXIT

# shellcheck source=lib/check_output.sh
source "$SCRIPT_DIR/lib/check_output.sh"

if [ "$FORMAT_FIRST" -eq 1 ]; then
    bash "$SCRIPT_DIR/fmt.sh"
fi

# #region changed files
# shellcheck source=lib/changed_files.sh
source "$SCRIPT_DIR/lib/changed_files.sh"
CHANGED_FILES="$(compute_changed_files)"
CHANGED_NAMES_ALL="$(compute_changed_names_all)"

# Format set: changed .c/.h outside vendored deps/ and generated/ (fmt.sh skips
# generated too — they are generator outputs, "revert don't commit" per AGENTS).
FORMAT_FILES="$(printf '%s\n' "$CHANGED_FILES" | grep -E '\.(c|h)$' | grep -v 'deps/\|/generated/' || true)"
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
    local db="$TIDY_CI_DIR/compile_commands.json"
    if [ ! -f "$db" ]; then
        echo "Configuring $TIDY_CI_DIR (one-time; devapi groups ON to match CI lint)..."
        cmake --preset native-debug -B "$TIDY_CI_DIR" "${TIDY_CI_FLAGS[@]}"
        return
    fi
    # Stale DB silently SKIPS new TUs in tidy (fail-open) — reconfigure when any
    # build file is newer than the DB. Warm reconfigure costs ~1 s.
    local stale
    stale="$(find CMakeLists.txt CMakePresets.json cmake engine shared tools examples tests deps \
        \( -name CMakeLists.txt -o -name '*.cmake' -o -name CMakePresets.json \) -newer "$db" -print -quit 2>/dev/null || true)"
    if [ -n "$stale" ]; then
        echo "tidy-ci DB stale (newer: $stale) — reconfiguring..."
        cmake --preset native-debug -B "$TIDY_CI_DIR" "${TIDY_CI_FLAGS[@]}" > /dev/null
    fi
}

# A changed header can affect any configured variant, so lint the full tree.
run_tidy_gate() {
    local build_dir="$1"
    if [ -n "$CHANGED_HEADERS" ]; then
        echo "Changed headers detected — running FULL clang-tidy:"
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

step "gates (module composition, EM_JS_DEPS, doc links, CRT pins, test registration)"
bash scripts/check_no_real_impl_links.sh
bash scripts/check_link_failure_loud.sh
bash scripts/check_emjs_deps.sh
bash scripts/check_doc_links.sh
bash scripts/check_crt_pins.sh
# Registration gate reads the tidy-ci compile DB + CTestTestfiles (devapi ON,
# so devapi-gated tests are visible) — keep the DB fresh first.
ensure_tidy_ci
bash scripts/check_tests_registered.sh "$TIDY_CI_DIR"
python -m unittest discover -s scripts/tests -p 'test_*.py'
bash scripts/tests/test_check.sh
bash scripts/tests/test_tidy.sh
ok

step "build (native-debug)"
if [ ! -f "$NATIVE_BUILD_DIR/CMakeCache.txt" ]; then
    echo "ERROR: $NATIVE_BUILD_DIR is not configured — configure the preset first:"
    echo "  cmake --preset native-debug"
    exit 1
fi
cmake --build "$NATIVE_BUILD_DIR"
ok

# ctest runs in the BACKGROUND while format+tidy use the idle cores — its
# critical path is one single-threaded test. The wait + report happens after
# the tidy step; a format/tidy failure kills the orphan so no exe stays locked.
#
# The three atlas-bench guard tests (~29 s CPU, the 18 s wall critical path)
# exercise builder geometry + research-bench provenance only. In default mode
# they run ONLY when the change set touches those areas; --push/--full (and CI)
# always run them, so nothing ships unchecked.
step "ctest (native-debug, parallel with format+tidy)"
# Everything the guards depend on: builder+bench sources, their deps libraries,
# fixtures/goldens, the scripts they source, and the test-registration infra.
# Matched against the UNFILTERED name union so deletions also trigger them.
GUARD_RELEVANT='^(tools/builder/|tools/research/|engine/atlas/|engine/hash/|shared/include/|deps/(clipper2|stb|miniz|cjson)/|scripts/(bench_|atlas/|test_atlas_|test_bench_|generate_hull_visual_|lib/hull_)|tests/fixtures/hull_visual_acceptance/|tests/unit/test_atlas_|tests/unit/test_helpers/|tests/CMakeLists|cmake/|CMakeLists\.txt$|CMakePresets\.json$)'
CTEST_ARGS=()
if [ "$MODE" = "default" ] && ! printf '%s\n' "$CHANGED_NAMES_ALL" | grep -qE "$GUARD_RELEVANT"; then
    echo "(no builder/atlas paths in the change set — the 3 bench-guard tests defer to --push/--full)"
    CTEST_ARGS=(-E '^(test_atlas_hull_visual_report|test_atlas_transform_sweep_guard|test_bench_hull_tolerance_guard)$')
fi
CTEST_LOG="$NATIVE_BUILD_DIR/check-ctest.log" # kept on disk; overwritten per run
ctest --test-dir "$NATIVE_BUILD_DIR" -j "$(nproc)" --output-on-failure "${CTEST_ARGS[@]}" > "$CTEST_LOG" 2>&1 &
CTEST_PID=$!
echo "(backgrounded, pid $CTEST_PID)"

collect_ctest() {
    CURRENT_STEP="ctest (native-debug)"
    local rc=0
    wait "$CTEST_PID" || rc=$?
    CTEST_PID=""
    if [ "$rc" -ne 0 ]; then
        cat "$CTEST_LOG"
        return "$rc"
    fi
    tail -n 3 "$CTEST_LOG"
    RESULTS+=("PASS  ctest (native-debug)")
}

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

collect_ctest

if [ "$MODE" = "push" ]; then
    # Release compiles the same TUs with NDEBUG (asserts -> TRAP, so NT_ASSERT_FULL-only code drops out)
    # and -O2: a test registered behind an assert-mode guard, or a variable only an assert reads, is
    # -Wunused under -Werror here and nowhere in the debug builds. Mirrors ci.yml's native-release job.
    step "build (native-release)"
    if [ ! -d "build/_cmake/native-release" ]; then
        echo "ERROR: build/_cmake/native-release missing — configure the preset first:"
        echo "  cmake --preset native-release"
        exit 1
    fi
    cmake --build build/_cmake/native-release
    ok

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
