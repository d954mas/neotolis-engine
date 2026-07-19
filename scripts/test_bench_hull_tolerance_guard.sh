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
if hull_repeat_required 0 0 || ! hull_repeat_required 1 0 || ! hull_repeat_required 0 1; then
    echo "repeat verification policy was classified incorrectly" >&2
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

PUBLISH_TEST_ROOT="build/tests/tmp/hull-publication-${$}"
PUBLISH_STAGE="${PUBLISH_TEST_ROOT}/stage"
PUBLISH_TARGET_A="${PUBLISH_TEST_ROOT}/tracked/a.json"
PUBLISH_TARGET_B="${PUBLISH_TEST_ROOT}/tracked/b.json"
mkdir -p "$PUBLISH_STAGE/$(dirname "$PUBLISH_TARGET_A")" "$(dirname "$PUBLISH_TARGET_A")"
printf 'old-a\n' > "$PUBLISH_TARGET_A"
printf 'old-b\n' > "$PUBLISH_TARGET_B"
printf 'new-a\n' > "$PUBLISH_STAGE/$PUBLISH_TARGET_A"
printf 'new-b\n' > "$PUBLISH_STAGE/$PUBLISH_TARGET_B"
if (
    install_count=0
    hull_publish_install_file() {
        install_count=$((install_count + 1))
        [[ $install_count -lt 2 ]] || return 1
        mv -- "$1" "$2"
    }
    hull_publish_staged_set "$PUBLISH_STAGE" "$PUBLISH_TARGET_A" "$PUBLISH_TARGET_B" 2>/dev/null
); then
    echo "injected publication failure unexpectedly succeeded" >&2
    exit 1
fi
if [[ "$(<"$PUBLISH_TARGET_A")" != new-a || "$(<"$PUBLISH_TARGET_B")" != old-b ]]; then
    echo "failed publication did not leave an explicit partial worktree update" >&2
    exit 1
fi
rm -rf -- "$PUBLISH_STAGE"
mkdir -p "$PUBLISH_STAGE/$(dirname "$PUBLISH_TARGET_A")"
printf 'retry-a\n' > "$PUBLISH_STAGE/$PUBLISH_TARGET_A"
printf 'retry-b\n' > "$PUBLISH_STAGE/$PUBLISH_TARGET_B"
hull_publish_staged_set "$PUBLISH_STAGE" "$PUBLISH_TARGET_A" "$PUBLISH_TARGET_B"
if [[ "$(<"$PUBLISH_TARGET_A")" != retry-a || "$(<"$PUBLISH_TARGET_B")" != retry-b ]]; then
    echo "clean retry did not publish the complete tracked set" >&2
    exit 1
fi
rm -rf -- "$PUBLISH_TEST_ROOT"

NT_HULL_VISUAL_GUARD_LIB_ONLY=1 source scripts/generate_hull_visual_acceptance.sh
FRONTIER_TEST_ROOT="build/tests/tmp/hull-frontier-${$}"
VALID_FRONTIER="${FRONTIER_TEST_ROOT}/valid.json"
EXPECTED_BUILDER_SHA="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
mkdir -p "$FRONTIER_TEST_ROOT"
sed \
    -e 's/"schema_version": [0-9][0-9]*/"schema_version": 3/' \
    -e '/"builder_binary_sha256":/d' \
    -e "/\"builder_threads\": 1/a\\  \"builder_binary_sha256\": \"${EXPECTED_BUILDER_SHA}\"," \
    tools/research/atlas_bench/hull_area_frontier.json > "$VALID_FRONTIER"
FRONTIER="$VALID_FRONTIER"
if ! frontier_valid; then
    echo "valid schema-3 frontier was rejected" >&2
    exit 1
fi
SCHEMA2_FRONTIER="${FRONTIER_TEST_ROOT}/schema2.json"
sed 's/"schema_version": 3/"schema_version": 2/' "$VALID_FRONTIER" > "$SCHEMA2_FRONTIER"
FRONTIER="$SCHEMA2_FRONTIER"
if frontier_valid; then
    echo "legacy schema-2 frontier was accepted" >&2
    exit 1
fi
MALFORMED_BUILDER_FRONTIER="${FRONTIER_TEST_ROOT}/malformed-builder-hash.json"
sed "s/${EXPECTED_BUILDER_SHA}/not-a-sha256/" "$VALID_FRONTIER" > "$MALFORMED_BUILDER_FRONTIER"
FRONTIER="$MALFORMED_BUILDER_FRONTIER"
if frontier_valid; then
    echo "frontier with malformed publisher builder hash was accepted" >&2
    exit 1
fi
TAMPERED_FRONTIER="${FRONTIER_TEST_ROOT}/tampered-metric.json"
sed '0,/"hull_vertices_total": 926/s//"hull_vertices_total": 927/' "$VALID_FRONTIER" > "$TAMPERED_FRONTIER"
FRONTIER="$TAMPERED_FRONTIER"
if frontier_valid; then
    echo "frontier metric inconsistent with its hashed proof was accepted" >&2
    exit 1
fi
rm -rf -- "$FRONTIER_TEST_ROOT"

FINAL_DIR="build/tests/tmp/hull-visual-final-${$}"
TEMP_DIR="${FINAL_DIR}.tmp"
REPEAT_DIR="${FINAL_DIR}.repeat"
mkdir -p "$TEMP_DIR" "$REPEAT_DIR"
if hull_visual_cleanup 130; then
    echo "interrupted visual cleanup lost its failure status" >&2
    exit 1
fi
if [[ -e "$TEMP_DIR" || -e "$REPEAT_DIR" ]]; then
    echo "interrupted visual cleanup retained temporary output" >&2
    exit 1
fi
rm -rf -- "$FINAL_DIR"

mkdir -p "$TEMP_DIR" "$REPEAT_DIR"
hull_visual_prepare_paths
if [[ -e "$TEMP_DIR" || -e "$REPEAT_DIR" ]]; then
    echo "visual startup did not clear temporary output" >&2
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

BENCH_EXE="${NT_ATLAS_BENCH_EXE:-build/tools/research/native-debug/atlas_bench}"
if [[ -x "${BENCH_EXE}.exe" ]]; then
    BENCH_EXE="${BENCH_EXE}.exe"
elif [[ ! -x "$BENCH_EXE" ]]; then
    echo "atlas_bench must be built before the guard" >&2
    exit 1
fi
for invalid in -1 nan inf 1px ''; do
    INVALID_LOG="build/tests/tmp/hull-area-invalid-${$}.log"
    if "$BENCH_EXE" "build/bench/hull-area-invalid-${$}.json" 'assets/bench/rect_only/rect_00.png' guard concave 64 1 --max-added-area-percent "$invalid" >"$INVALID_LOG" 2>&1; then
        echo "invalid area percentage was accepted: '${invalid}'" >&2
        exit 1
    fi
    if ! grep -q "atlas_bench: bad argument near" "$INVALID_LOG"; then
        echo "invalid area percentage failed for the wrong reason: '${invalid}'" >&2
        cat "$INVALID_LOG" >&2
        exit 1
    fi
done
rm -f "$INVALID_LOG"
if [[ -e "build/bench/hull-area-invalid-${$}.json" || -e "build/bench/hull-area-invalid-${$}.json.ntpack" ]]; then
    echo "invalid CLI input wrote evidence" >&2
    exit 1
fi

THREAD_JSON="build/bench/hull-area-invalid-threads-${$}.json"
THREAD_LOG="build/tests/tmp/hull-area-invalid-threads-${$}.log"
if NT_BUILDER_THREADS=1junk "$BENCH_EXE" "$THREAD_JSON" 'assets/bench/rect_only/rect_00.png' guard rect 64 1 >"$THREAD_LOG" 2>&1; then
    echo "malformed NT_BUILDER_THREADS was accepted" >&2
    exit 1
fi
if ! grep -q "NT_BUILDER_THREADS" "$THREAD_LOG"; then
    echo "malformed NT_BUILDER_THREADS failed for the wrong reason" >&2
    cat "$THREAD_LOG" >&2
    exit 1
fi
rm -f "$THREAD_LOG" "$THREAD_JSON" "${THREAD_JSON}.ntpack" "${THREAD_JSON}.h" "${THREAD_JSON}.baseline.ntpack" "${THREAD_JSON}.baseline.h"

echo "test_bench_hull_tolerance_guard: passed"
