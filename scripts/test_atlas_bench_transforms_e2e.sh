#!/usr/bin/env bash
set -euo pipefail
PATH="/usr/bin:/bin:$PATH"
cd "$(git rev-parse --show-toplevel)"

EXE="${NT_ATLAS_BENCH_EXE:-build/tools/research/native-debug/atlas_bench}"
TMP_DIR="build/tests/tmp"
IDENTITY_OUT="${TMP_DIR}/atlas_bench_identity_e2e.json"
ALL_OUT="${TMP_DIR}/atlas_bench_all_e2e.json"

cleanup() {
    rm -f "${IDENTITY_OUT}"* "${ALL_OUT}"*
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
NT_BUILDER_THREADS=1 "${EXE}" "${ALL_OUT}" "assets/bench/rect_only/*.png" transform_e2e rect 1024 32 --transforms all >/dev/null

grep -Eq '"allowed_transforms":[[:space:]]*1([,}])' "${IDENTITY_OUT}"
grep -Eq '"allowed_transforms":[[:space:]]*255([,}])' "${ALL_OUT}"

if cmp -s "${IDENTITY_OUT}.ntpack" "${ALL_OUT}.ntpack"; then
    echo "atlas_bench --transforms did not affect the production pack" >&2
    exit 1
fi

echo "atlas_bench transforms e2e guard passed"
