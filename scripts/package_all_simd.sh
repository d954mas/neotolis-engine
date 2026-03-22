#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <baseline-preset> <simd-preset>"
    echo "Example: $0 wasm-release-paired wasm-release-simd"
    exit 1
fi

source "$(dirname "$0")/wasm_examples.sh"

for example in "${WASM_EXAMPLES[@]}"; do
    "$(dirname "$0")/package_wasm_simd.sh" "${example}" "$1" "$2"
done
