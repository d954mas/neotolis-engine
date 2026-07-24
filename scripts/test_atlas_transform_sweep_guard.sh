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
if [[ "$out_json" == *_all2.json ]]; then
    sha="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
fi
cat > "$out_json" <<JSON
{
  "atlases": [{
    "pages": 1,
    "density_fill_frontier": 0.5,
    "density_fill_texture": 0.5,
    "pack_ms": 1
  }],
  "selected_geometry_proof": {
    "selected_pack_sha256": "$sha"
  }
}
JSON
EOF
chmod +x "${FAKE_DIR}/atlas_bench"

if bash tools/research/atlas_transform_sweep.sh --preset "$PRESET" --out "$OUT_DIR" --no-build >/dev/null 2>&1; then
    echo "sweep accepted differing ALL-mask hashes" >&2
    exit 1
fi
