#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <example> <baseline-preset> <simd-preset>"
    echo "Example: $0 hello wasm-release-paired wasm-release-simd"
    exit 1
fi

EXAMPLE="$1"
BASE_PRESET="$2"
SIMD_PRESET="$3"
BASE_DIR="build/examples/${EXAMPLE}/${BASE_PRESET}"
SIMD_DIR="build/examples/${EXAMPLE}/${SIMD_PRESET}"
BASE_WASM="${BASE_DIR}/index.wasm"
SIMD_WASM="${SIMD_DIR}/index.wasm"
SIMD_JS="${SIMD_DIR}/index.js"
OUT_WASM="${BASE_DIR}/index_simd.wasm"
OUT_JS="${BASE_DIR}/index_simd.js"

if [ ! -f "${BASE_WASM}" ]; then
    echo "Missing baseline wasm: ${BASE_WASM}"
    exit 1
fi

if [ ! -f "${SIMD_WASM}" ]; then
    echo "Missing SIMD wasm: ${SIMD_WASM}"
    exit 1
fi

if [ ! -f "${SIMD_JS}" ]; then
    echo "Missing SIMD JS: ${SIMD_JS}"
    exit 1
fi

cp "${SIMD_WASM}" "${OUT_WASM}"
cp "${SIMD_JS}" "${OUT_JS}"
echo "Packaged SIMD: ${OUT_WASM} + ${OUT_JS}"
