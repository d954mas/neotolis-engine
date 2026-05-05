#!/usr/bin/env bash
# POSIX alt for tools/fetch_bunnymark_art.ps1. Same semantics; idempotent.
set -euo pipefail
out="examples/bunnymark/raw/sd"
mkdir -p "$out"

pairs=(
    "rabbitv3.png:bunny_red.png"
    "rabbitv3_ash.png:bunny_green.png"
    "rabbitv3_batman.png:bunny_blue.png"
    "rabbitv3_neo.png:bunny_yellow.png"
    "rabbitv3_spidey.png:bunny_purple.png"
)
base="https://raw.githubusercontent.com/britzl/defold-bunnymark/master/assets/images"
fetched=0; skipped=0
for entry in "${pairs[@]}"; do
    src="${entry%%:*}"; dst_file="${entry##*:}"; dst="$out/$dst_file"
    if [ -f "$dst" ] && [ "$(head -c 4 "$dst" | od -An -tx1 | tr -d ' \n')" = "89504e47" ]; then
        echo "skip  $dst (already valid)"; skipped=$((skipped+1)); continue
    fi
    echo "fetch $base/$src -> $dst"
    curl -fsSL -o "$dst" "$base/$src"
    fetched=$((fetched+1))
done
echo "Fetched: $fetched, Skipped: $skipped (out of 5). Done."
