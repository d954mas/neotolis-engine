#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <paired-preset>"
    echo "Example: $0 wasm-debug-paired"
    exit 1
fi

PAIRED_PRESET="$1"
case "${PAIRED_PRESET}" in
    wasm-debug-paired) SIMD_PRESET="wasm-debug-simd" ;;
    wasm-release-paired) SIMD_PRESET="wasm-release-simd" ;;
    wasm-analysis-paired) SIMD_PRESET="wasm-analysis-simd" ;;
    *)
        echo "Unsupported paired preset: ${PAIRED_PRESET}"
        exit 1
        ;;
esac

emcmake cmake --preset "${PAIRED_PRESET}"
emcmake cmake --preset "${SIMD_PRESET}"
cmake --build --preset "${PAIRED_PRESET}"
cmake --build --preset "${SIMD_PRESET}"

source "$(dirname "$0")/wasm_examples.sh"

for example in "${WASM_EXAMPLES[@]}"; do
    "$(dirname "$0")/package_wasm_simd.sh" "${example}" "${PAIRED_PRESET}" "${SIMD_PRESET}"
done
