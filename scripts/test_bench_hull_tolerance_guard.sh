#!/usr/bin/env bash

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

NT_HULL_TOLERANCE_GUARD_LIB_ONLY=1 source scripts/bench_hull_tolerance.sh

BASELINE="tools/research/atlas_bench/baseline"
PLATFORM="MSYS_NT-10.0-22631"

expect_protected() {
    if ! hull_path_is_protected "$BASELINE" "$1" "$PLATFORM"; then
        echo "expected protected path: $1" >&2
        exit 1
    fi
}

expect_allowed() {
    if hull_path_is_protected "$BASELINE" "$1" "$PLATFORM"; then
        echo "expected allowed path: $1" >&2
        exit 1
    fi
}

expect_protected "$BASELINE"
expect_protected "$BASELINE/run"
expect_protected "TOOLS/RESEARCH/ATLAS_BENCH/BASELINE"
expect_protected "TOOLS\\RESEARCH\\ATLAS_BENCH\\BASELINE\\mixed-case"
expect_protected "$BASELINE/../baseline/dot-dot"
expect_allowed "tools/research/atlas_bench/baseline-copy"
expect_allowed "build/bench/hull-tolerance"
if hull_path_is_protected "$BASELINE" "TOOLS/RESEARCH/ATLAS_BENCH/BASELINE" "Linux"; then
    echo "POSIX path comparison unexpectedly ignored case" >&2
    exit 1
fi
if ! hull_path_is_protected "build/space baseline" "build/space baseline/run" "Linux"; then
    echo "quoted path containment failed" >&2
    exit 1
fi

OUT_ALIAS="TOOLS/RESEARCH/ATLAS_BENCH/BASELINE/guard-no-write-${$}"
if NT_HULL_TOLERANCE_PLATFORM="$PLATFORM" scripts/bench_hull_tolerance.sh --out "$OUT_ALIAS" --samples 0 >/dev/null 2>&1; then
    echo "mixed-case protected output was accepted" >&2
    exit 1
fi
if [[ -e "$OUT_ALIAS" ]]; then
    echo "protected output was created before rejection: $OUT_ALIAS" >&2
    exit 1
fi

echo "test_bench_hull_tolerance_guard: passed"
