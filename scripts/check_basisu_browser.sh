#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
ROOT="$PWD"
PORT="${NT_BASIS_PORT:-8453}"

# The small producer is independent of every example pack, including Sponza.
cmake --preset native-debug
cmake --build --preset native-debug --target basis_fixture_packs
for preset in wasm-debug wasm-release; do
    cmake --preset "$preset"
    cmake --build --preset "$preset" --target browser_basis
    (
        cd tests/browser
        CI=1 NT_BASIS_PRESET="$preset" NT_SHOWCASE_PORT="$PORT" \
            NT_SHOWCASE_DIR="$ROOT/build/tests/browser/basis/$preset" \
            npx playwright test --config basis.config.ts
    )
    PORT=$((PORT + 1))
done
