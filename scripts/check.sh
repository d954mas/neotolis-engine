#!/usr/bin/env bash
# Unified pre-commit check (read-only — the mutating formatter is scripts/fmt.sh,
# combined entry: scripts/format_and_check.sh). Modes:
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
    # A format/tidy failure exits while ctest still runs — reap it or it holds test exes locked.
    if [ -n "${CTEST_PID:-}" ]; then
        kill "$CTEST_PID" 2> /dev/null || true
        wait "$CTEST_PID" 2> /dev/null || true
        rm -f "${CTEST_LOG:-}"
    fi
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
# shellcheck source=lib/changed_files.sh
source "$SCRIPT_DIR/lib/changed_files.sh"
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
    local db="$TIDY_CI_DIR/compile_commands.json"
    if [ ! -f "$db" ]; then
        echo "Configuring $TIDY_CI_DIR (one-time; devapi groups ON to match CI lint)..."
        cmake --preset native-debug -B "$TIDY_CI_DIR" "${TIDY_CI_FLAGS[@]}"
        return
    fi
    # Stale DB silently SKIPS new TUs in tidy (fail-open) — reconfigure when any
    # build file is newer than the DB. Warm reconfigure costs ~1 s.
    local stale
    stale="$(find CMakeLists.txt cmake engine shared tools examples tests \
        \( -name CMakeLists.txt -o -name '*.cmake' \) -newer "$db" -print -quit 2>/dev/null || true)"
    if [ -n "$stale" ]; then
        echo "tidy-ci DB stale (newer: $stale) — reconfiguring..."
        cmake --preset native-debug -B "$TIDY_CI_DIR" "${TIDY_CI_FLAGS[@]}" > /dev/null
    fi
}

# Exact TU set including the changed headers, from ninja's dep log (the build
# step ran just before, so the log is fresh). Prints repo-relative .c paths;
# prints NOTHING when resolution is incomplete — a changed header unseen in ANY
# dep block (devapi-only TU, deleted, or graph gap) fails closed to full tidy.
resolve_header_scoped_tus() {
    local root_win="$ROOT_DIR"
    command -v cygpath > /dev/null 2>&1 && root_win="$(cygpath -m "$ROOT_DIR")"
    ninja -C "$NATIVE_BUILD_DIR" -t deps 2> /dev/null | awk -v root="$root_win/" -v headers="$CHANGED_HEADERS" '
        BEGIN {
            nh = split(headers, harr, "\n")
            for (i = 1; i <= nh; i++) if (harr[i] != "") want[harr[i]] = 1
        }
        function flush_block() {
            if (hit && src != "") tus[src] = 1
            src = ""; hit = 0
        }
        /^[^ \t]/ { flush_block(); next }
        /^[ \t]/ {
            p = $0
            gsub(/^[ \t]+|[ \t\r]+$/, "", p)
            gsub(/\\/, "/", p)
            if (index(p, root) == 1) {
                rel = substr(p, length(root) + 1)
                if (src == "" && rel ~ /\.c$/) src = rel
                if (rel in want) { seen[rel] = 1; hit = 1 }
            }
            next
        }
        { flush_block() }
        END {
            flush_block()
            for (h in want) if (!(h in seen)) exit 1
            for (t in tus) print t
        }
    '
}

# Runs tidy on changed .c files; changed headers add their exact including-TU
# set (fail-closed to the full tree).
run_tidy_gate() {
    local build_dir="$1"
    local scoped=""
    if [ -n "$CHANGED_HEADERS" ]; then
        scoped="$(resolve_header_scoped_tus)" || scoped=""
        if [ -z "$scoped" ]; then
            echo "Changed headers, deps resolution incomplete — running FULL clang-tidy (fail-closed):"
            printf '  %s\n' $CHANGED_HEADERS
            bash scripts/tidy.sh "$build_dir"
            return
        fi
        # Keep only TUs tidy would ever lint, then union with changed .c.
        scoped="$(printf '%s\n' "$scoped" \
            | grep -E '^(engine|shared|tools|examples|tests)/.*\.c$' \
            | grep -v 'deps/\|/web/\|_web\.c\|tools/research/' || true)"
    fi
    local files
    files="$(printf '%s\n%s\n' "$TIDY_FILES" "$scoped" | grep -v '^$' | sort -u || true)"
    if [ -n "$files" ]; then
        if [ -n "$scoped" ]; then
            echo "Changed .c + TUs including the changed headers ($(printf '%s\n' "$files" | grep -c .) files):"
        else
            echo "Changed .c files:"
        fi
        printf '  %s\n' $files
        # shellcheck disable=SC2086 — newline-split into file args; repo paths have no spaces
        bash scripts/tidy.sh "$build_dir" $files
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

# ctest runs in the BACKGROUND while format+tidy use the idle cores — its
# critical path is one single-threaded test. The wait + report happens after
# the tidy step; a format/tidy failure kills the orphan so no exe stays locked.
step "ctest (native-debug, parallel with format+tidy)"
CTEST_LOG="$(mktemp)"
ctest --test-dir "$NATIVE_BUILD_DIR" -j "$(nproc)" --output-on-failure > "$CTEST_LOG" 2>&1 &
CTEST_PID=$!
echo "(backgrounded, pid $CTEST_PID)"

collect_ctest() {
    CURRENT_STEP="ctest (native-debug)"
    local rc=0
    wait "$CTEST_PID" || rc=$?
    CTEST_PID=""
    if [ "$rc" -ne 0 ]; then
        cat "$CTEST_LOG"
        rm -f "$CTEST_LOG"
        return "$rc"
    fi
    tail -n 3 "$CTEST_LOG"
    rm -f "$CTEST_LOG"
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
