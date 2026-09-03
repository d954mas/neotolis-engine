#!/usr/bin/env bash
# Tripwire: gfx/renderer/material cache keys are exact packs or whole-struct nt_hash64,
# never a golden-ratio linear fold. Pool handles are sequential, so `handle*K + field`
# aliases neighbouring resources. This greps for the one constant that pattern was
# written with; the neighbour tests are the real guard. The constant is legitimate
# elsewhere (nt_hash internals, RNGs, vendored deps), so only the identity-producing
# modules are policed.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

PATHS=('engine/graphics/*.c' 'engine/graphics/*.h' 'engine/renderers/*.c' 'engine/renderers/*.h' 'engine/material/*.c' 'engine/material/*.h')
HITS="$( { git ls-files -- "${PATHS[@]}"; git ls-files --others --exclude-standard -- "${PATHS[@]}"; } | sort -u |
    xargs -r grep -niIH -E '0x9e3779b9' 2>/dev/null || true)"
if [ -n "$HITS" ]; then
    echo "ERROR: golden-ratio fold in a cache-key module — pack the identity exactly (nt_gfx_pipeline_key / bit lanes) or nt_hash64 the whole struct:"
    printf '%s\n' "$HITS"
    echo "check_cache_keys: FAILED"
    exit 1
fi
echo "check_cache_keys: passed (no linear folds in gfx/renderer/material cache keys)"
