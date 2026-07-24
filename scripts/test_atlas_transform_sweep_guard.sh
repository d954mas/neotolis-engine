#!/usr/bin/env bash

set -euo pipefail
PATH="/usr/bin:/bin:$PATH"
cd "$(git rev-parse --show-toplevel)"

PRESET="native-sweep-guard-${$}"
FAKE_DIR="build/tools/research/${PRESET}"
OUT_DIR="build/tests/tmp/atlas-transform-sweep-guard-${$}"
cleanup() {
    rm -rf -- "$FAKE_DIR" "$OUT_DIR"
}
trap cleanup EXIT
mkdir -p "$FAKE_DIR" "$OUT_DIR"

cat > "${FAKE_DIR}/atlas_bench" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
out_json="$1"
sha="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
if [[ "${FAKE_DIFFER_SHAS:-0}" == 1 && "$out_json" == *_all2.json ]]; then
    sha="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
fi
texture="0.7239"
if [[ "$out_json" == *anim_heavy* ]]; then
    texture="0.5169"
elif [[ "$out_json" == *mixed_aa* ]]; then
    texture="0.6332"
fi
cat > "$out_json" <<JSON
{
  "atlases": [{
    "pages": 1,
    "density_fill_frontier": 0.5,
    "density_fill_texture": $texture,
    "pack_ms": 1
  }],
  "selected_geometry_proof": {
    "selected_pack_sha256": "$sha"
  }
}
JSON
EOF
chmod +x "${FAKE_DIR}/atlas_bench"

DIFFER_LOG="${OUT_DIR}/differ.log"
if FAKE_DIFFER_SHAS=1 bash tools/research/atlas_transform_sweep.sh --preset "$PRESET" --out "$OUT_DIR" --no-build >"$DIFFER_LOG" 2>&1; then
    echo "sweep accepted differing ALL-mask hashes" >&2
    exit 1
fi
grep -q "ALL-mask determinism DIFFER" "$DIFFER_LOG"

BASELINE_DIR="${OUT_DIR}/baselines"
mkdir -p "$BASELINE_DIR"
cp tools/research/atlas_bench/baseline/anim_heavy.json "$BASELINE_DIR/"
cp tools/research/atlas_bench/baseline/mixed_aa.json "$BASELINE_DIR/"
MISSING_LOG="${OUT_DIR}/missing.log"
if NT_ATLAS_TRANSFORM_BASELINE_DIR="$BASELINE_DIR" bash tools/research/atlas_transform_sweep.sh --preset "$PRESET" --out "$OUT_DIR" --no-build >"$MISSING_LOG" 2>&1; then
    echo "sweep accepted missing baseline evidence" >&2
    exit 1
fi
grep -q "missing baseline evidence" "$MISSING_LOG"
