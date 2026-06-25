#!/usr/bin/env bash
# Guard: an EM_JS / EM_ASM body that references a Closure-strippable JS runtime helper MUST self-declare
# it via EM_JS_DEPS in the SAME translation unit. Otherwise the helper is invisible to Closure (the EM_JS
# body is an opaque string), gets stripped in --closure release builds, and the call throws
# "X is not defined" at runtime. The risk is release/Closure-only -> invisible to wasm-debug and to native,
# so a plain build never catches it. This guard does, at lint time.
#
# Trigger list: JS-side runtime helpers that only ever appear inside EM_JS/EM_ASM bodies (never in C),
# so a match is a reliable signal the body marshals strings / reaches wasm exports.
set -euo pipefail

HELPERS='UTF8ToString|stringToNewUTF8|stringToUTF8|lengthBytesUTF8|allocateUTF8|intArrayFromString|wasmExports\['

fail=0
while IFS= read -r -d '' f; do
    grep -Eq 'EM_JS\(|EM_ASM' "$f" 2>/dev/null || continue   # only TUs with an EM_JS/EM_ASM body
    grep -Eq "$HELPERS" "$f" 2>/dev/null || continue          # ...that reference a strippable JS helper
    if ! grep -q 'EM_JS_DEPS(' "$f" 2>/dev/null; then
        echo "EM_JS_DEPS GUARD FAIL: $f"
        echo "  uses a Closure-strippable JS runtime helper inside an EM_JS/EM_ASM body but declares no EM_JS_DEPS(...)."
        echo "  Add e.g.  EM_JS_DEPS(<tu_name>, \"\$UTF8ToString,...\")  so Closure keeps it in wasm-release."
        grep -nE "$HELPERS" "$f" | head -5 | sed 's/^/     /'
        fail=1
    fi
done < <(find engine examples -name '*.c' -not -path '*/deps/*' -print0)

if [ "$fail" -ne 0 ]; then
    echo "EM_JS_DEPS GUARD: FAILED (see above)."
    exit 1
fi
echo "EM_JS_DEPS GUARD: ok -- every EM_JS/EM_ASM body using a JS runtime helper self-declares EM_JS_DEPS."
