#!/usr/bin/env bash

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

NT_HULL_AREA_GUARD_LIB_ONLY=1 source scripts/bench_hull_tolerance.sh

BASELINE="tools/research/atlas_bench/baseline"
PLATFORM="MSYS_NT-10.0-22631"

if ! hull_is_canonical_publication "mixed_aa" "0,2,5,10,15,25" "native-release" "1"; then
    echo "canonical mixed-AA sweep was not recognized" >&2
    exit 1
fi
if hull_is_canonical_publication "anim_heavy" "0,2,5,10,15,25" "native-release" "1" || hull_is_canonical_publication "mixed_aa" "0,5,10" "native-release" "1" ||
    hull_is_canonical_publication "mixed_aa" "0,2,5,10,15,25" "native-debug" "1" || hull_is_canonical_publication "mixed_aa" "0,2,5,10,15,25" "native-release" "2"; then
    echo "non-canonical sweep was accepted for publication" >&2
    exit 1
fi
if ! hull_status_is_clean "" || hull_status_is_clean $' M tools/builder/nt_builder_atlas.c\n?? local.txt'; then
    echo "dirty-tree publication status was classified incorrectly" >&2
    exit 1
fi

PORTABLE_SOURCE="build/tests/tmp/hull-area-portable-source-${$}.json"
PORTABLE_PROOF="build/tests/tmp/hull-area-portable-proof-${$}.json"
mkdir -p "$(dirname "$PORTABLE_SOURCE")"
printf '{\n  "os": "volatile",\n  "cpu": "volatile",\n  "pack_ms": 1.25,\n  "selected_geometry_proof": {"valid": true}\n}\n' > "$PORTABLE_SOURCE"
hull_write_portable_proof "$PORTABLE_SOURCE" "$PORTABLE_PROOF"
if grep -Eq '"(os|cpu|pack_ms)"' "$PORTABLE_PROOF" || ! grep -q 'selected_geometry_proof' "$PORTABLE_PROOF"; then
    echo "portable proof retained volatile metadata or lost proof data" >&2
    exit 1
fi
rm -f "$PORTABLE_SOURCE" "$PORTABLE_PROOF"

NT_HULL_VISUAL_GUARD_LIB_ONLY=1 source scripts/generate_hull_visual_acceptance.sh
FINAL_DIR="build/tests/tmp/hull-visual-final-${$}"
TEMP_DIR="${FINAL_DIR}.tmp"
REPEAT_DIR="${FINAL_DIR}.repeat"
PREVIOUS_DIR="${FINAL_DIR}.previous"
mkdir -p "$FINAL_DIR" "$TEMP_DIR" "$REPEAT_DIR"
printf 'accepted\n' > "$FINAL_DIR/marker.txt"
mv -- "$FINAL_DIR" "$PREVIOUS_DIR"
if hull_visual_cleanup 130; then
    echo "interrupted visual cleanup lost its failure status" >&2
    exit 1
fi
if [[ ! -f "$FINAL_DIR/marker.txt" || -e "$TEMP_DIR" || -e "$REPEAT_DIR" || -e "$PREVIOUS_DIR" ]]; then
    echo "interrupted visual cleanup did not restore the accepted report" >&2
    exit 1
fi
rm -rf -- "$FINAL_DIR"

mkdir -p "$PREVIOUS_DIR" "$TEMP_DIR" "$REPEAT_DIR"
printf 'accepted-after-crash\n' > "$PREVIOUS_DIR/marker.txt"
hull_visual_prepare_paths
if [[ ! -f "$FINAL_DIR/marker.txt" || -e "$TEMP_DIR" || -e "$REPEAT_DIR" || -e "$PREVIOUS_DIR" ]]; then
    echo "visual startup did not recover the accepted report after a hard interruption" >&2
    exit 1
fi
rm -rf -- "$FINAL_DIR"

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
expect_allowed "build/bench/hull-area"
if hull_path_is_protected "$BASELINE" "TOOLS/RESEARCH/ATLAS_BENCH/BASELINE" "Linux"; then
    echo "POSIX path comparison unexpectedly ignored case" >&2
    exit 1
fi
if ! hull_path_is_protected "$BASELINE" "TOOLS/RESEARCH/ATLAS_BENCH/BASELINE" "Darwin"; then
    echo "Darwin path comparison did not protect a case-only alias" >&2
    exit 1
fi
if ! hull_path_is_protected "build/space baseline" "build/space baseline/run" "Linux"; then
    echo "quoted path containment failed" >&2
    exit 1
fi

OUT_ALIAS="TOOLS/RESEARCH/ATLAS_BENCH/BASELINE/guard-no-write-${$}"
GUARD_LOG="build/tests/tmp/hull-area-protected-guard-${$}.log"
mkdir -p "$(dirname "$GUARD_LOG")"
if NT_HULL_AREA_PLATFORM="$PLATFORM" NT_BUILDER_THREADS=1 scripts/bench_hull_tolerance.sh --out "$OUT_ALIAS" --samples 0 >"$GUARD_LOG" 2>&1; then
    echo "mixed-case protected output was accepted" >&2
    exit 1
fi
if ! grep -qi "baseline directory" "$GUARD_LOG"; then
    echo "protected output failed for the wrong reason" >&2
    cat "$GUARD_LOG" >&2
    exit 1
fi
if [[ -e "$OUT_ALIAS" ]]; then
    echo "protected output was created before rejection: $OUT_ALIAS" >&2
    exit 1
fi
rm -f "$GUARD_LOG"

BENCH_EXE="build/tools/research/native-debug/atlas_bench"
if [[ -x "${BENCH_EXE}.exe" ]]; then
    BENCH_EXE="${BENCH_EXE}.exe"
elif [[ ! -x "$BENCH_EXE" ]]; then
    echo "atlas_bench must be built before the guard" >&2
    exit 1
fi
for invalid in -1 nan inf 1px ''; do
    if "$BENCH_EXE" "build/bench/hull-area-invalid-${$}.json" 'missing/*.png' guard concave 64 1 --max-added-area-percent "$invalid" >/dev/null 2>&1; then
        echo "invalid area percentage was accepted: '${invalid}'" >&2
        exit 1
    fi
done
if [[ -e "build/bench/hull-area-invalid-${$}.json" || -e "build/bench/hull-area-invalid-${$}.json.ntpack" ]]; then
    echo "invalid CLI input wrote evidence" >&2
    exit 1
fi

echo "test_bench_hull_tolerance_guard: passed"
