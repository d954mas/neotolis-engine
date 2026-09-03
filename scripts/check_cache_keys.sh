#!/usr/bin/env bash
# Gate: renderer/material cache keys are exact packs or whole-struct nt_hash64, never a
# golden-ratio linear fold. Pool handles are sequential, so `handle*K + field` aliases
# neighbouring resources (#392). The constant is legitimate elsewhere (nt_hash internals,
# RNGs, vendored deps); only the identity-producing modules are policed.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR"

HITS="$(git ls-files -- 'engine/renderers/*.c' 'engine/renderers/*.h' 'engine/material/*.c' 'engine/material/*.h' |
    xargs -r grep -nIH -E '0x9E3779B9' 2>/dev/null || true)"
if [ -n "$HITS" ]; then
    echo "ERROR: golden-ratio fold in a cache-key module — pack the identity exactly (nt_gfx_pipeline_key / bit lanes) or nt_hash64 the whole struct:"
    printf '%s\n' "$HITS"
    echo "check_cache_keys: FAILED"
    exit 1
fi
echo "check_cache_keys: passed (no linear folds in renderer/material cache keys)"
