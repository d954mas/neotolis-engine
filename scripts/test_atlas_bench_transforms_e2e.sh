#!/usr/bin/env bash
set -euo pipefail
PATH="/usr/bin:/bin:$PATH"
cd "$(git rev-parse --show-toplevel)"

EXE="${NT_ATLAS_BENCH_EXE:-build/tools/research/native-debug/atlas_bench}"
TMP_DIR="build/tests/tmp"
IDENTITY_OUT="${TMP_DIR}/atlas_bench_identity_e2e.json"
IDENTITY_ROT90_OUT="${TMP_DIR}/atlas_bench_identity_rot90_e2e.json"
ALL_OUT="${TMP_DIR}/atlas_bench_all_e2e.json"
DEDUP_OUT="${TMP_DIR}/atlas_bench_dedup_e2e.json"

cleanup() {
    rm -f "${IDENTITY_OUT}"* "${IDENTITY_ROT90_OUT}"* "${ALL_OUT}"* "${DEDUP_OUT}"*
}
trap cleanup EXIT

if [[ ! -x "${EXE}" && ! -f "${EXE}.exe" ]]; then
    echo "atlas_bench executable not found: ${EXE}" >&2
    exit 1
fi
if [[ -f "${EXE}.exe" ]]; then
    EXE="${EXE}.exe"
fi

mkdir -p "${TMP_DIR}"
cleanup

NT_BUILDER_THREADS=1 "${EXE}" "${IDENTITY_OUT}" "assets/bench/rect_only/*.png" transform_e2e rect 1024 32 --transforms identity >/dev/null
NT_BUILDER_THREADS=1 "${EXE}" "${IDENTITY_ROT90_OUT}" "assets/bench/rect_only/*.png" transform_e2e rect 1024 32 --transforms identity-rot90 >/dev/null
NT_BUILDER_THREADS=1 "${EXE}" "${ALL_OUT}" "assets/bench/rect_only/*.png" transform_e2e rect 1024 32 --transforms all >/dev/null

grep -Eq '"allowed_transforms":[[:space:]]*1([,}])' "${IDENTITY_OUT}"
grep -Eq '"allowed_transforms":[[:space:]]*33([,}])' "${IDENTITY_ROT90_OUT}"
grep -Eq '"allowed_transforms":[[:space:]]*255([,}])' "${ALL_OUT}"

if cmp -s "${IDENTITY_OUT}.ntpack" "${IDENTITY_ROT90_OUT}.ntpack" || cmp -s "${IDENTITY_OUT}.ntpack" "${ALL_OUT}.ntpack"; then
    echo "atlas_bench --transforms did not affect the production pack" >&2
    exit 1
fi

# Dedup stats plumbing (builder -> CLI -> JSON): the anim_trim corpus has pinned,
# pairwise-distinct counters, so a swapped field mapping in main.c fails here.
NT_BUILDER_THREADS=1 "${EXE}" "${DEDUP_OUT}" "assets/bench/anim_trim/*.png" dedup_e2e concave 2048 0 >/dev/null
grep -Eq '"sprites":[[:space:]]*28([,}])' "${DEDUP_OUT}"
# unique carries stats.placements; without this pin a dropped mapping stays green.
grep -Eq '"unique":[[:space:]]*7([,}])' "${DEDUP_OUT}"
grep -Eq '"folds_exact":[[:space:]]*12([,}])' "${DEDUP_OUT}"
grep -Eq '"folds_d4":[[:space:]]*9([,}])' "${DEDUP_OUT}"
grep -Eq '"area_saved_px":[[:space:]]*3876([,}])' "${DEDUP_OUT}"
grep -Eq '"vertex_blocks_shared":[[:space:]]*0([,}])' "${DEDUP_OUT}"

echo "atlas_bench transforms e2e guard passed"
