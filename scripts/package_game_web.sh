#!/usr/bin/env bash
# Zip a built WASM game directory for jam submission (e.g. itch.io upload).
# Usage: scripts/package_game_web.sh [preset]   (default: wasm-release-paired)
set -euo pipefail

PRESET="${1:-wasm-release-paired}"
SRC="build/games/turkic-jam-2026/${PRESET}"
OUT="build/games/turkic-jam-2026/turkic_jam_${PRESET}.zip"

if [ ! -f "${SRC}/index.html" ]; then
    echo "ERROR: ${SRC}/index.html not found."
    echo "Build the game for ${PRESET} first (e.g. VS Code: Build wasm-release-paired)."
    exit 1
fi

# Python's zipfile is always available here and avoids depending on a `zip` binary.
python - "$SRC" "$OUT" <<'PY'
import os, sys, zipfile
src, out = sys.argv[1], sys.argv[2]
if os.path.exists(out):
    os.remove(out)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, _dirs, files in os.walk(src):
        for name in files:
            path = os.path.join(root, name)
            z.write(path, os.path.relpath(path, src))
size = os.path.getsize(out)
print(f"Wrote {out} ({size/1024:.1f} KB)")
PY
