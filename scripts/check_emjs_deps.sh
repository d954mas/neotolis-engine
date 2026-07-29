#!/usr/bin/env bash
# Guard: an EM_JS / EM_ASM body that references a Closure-strippable JS runtime helper MUST declare
# THAT helper in an EM_JS_DEPS of the SAME translation unit. Presence of some EM_JS_DEPS is not
# enough: a helper kept only via another dep's transitive __deps (an emscripten implementation
# detail) breaks silently in --closure wasm-release on an emsdk upgrade or a deps refactor.
# The risk is release/Closure-only -> invisible to wasm-debug and native builds.
#
# Method: strip C/JS comments (heuristic; also trims '//' inside string literals, which only ever
# hides text after it on that line), collect helper references, and require each one, by name, in
# the union of the TU's EM_JS_DEPS strings. JS-library helpers are declared with a '$' prefix
# ($UTF8ToString); wasm exports referenced as wasmExports['x'] are declared bare (malloc).
#
# Trigger list: JS-side runtime helpers that only ever appear inside EM_JS/EM_ASM bodies (never in C),
# so a match is a reliable signal the body marshals strings / reaches wasm exports.
set -euo pipefail

HELPERS='UTF8ToString|stringToNewUTF8|stringToUTF8|lengthBytesUTF8|allocateUTF8|intArrayFromString'

fail=0
# One recursive grep pre-filters the EM_JS/EM_ASM TUs; a per-file spawn loop over
# every .c cost seconds in Windows process spawns for a handful of real hits.
while IFS= read -r f; do
    stripped=$(perl -0777 -pe 's{/\*.*?\*/}{}gs; s{//[^\n]*}{}g' "$f")
    required=$( { grep -oE "\b($HELPERS)\b" <<<"$stripped" || true;
                  grep -oE "wasmExports\['[A-Za-z0-9_]+'\]" <<<"$stripped" | sed -E "s/wasmExports\['([A-Za-z0-9_]+)'\]/\1/" || true; } | sort -u)
    [ -n "$required" ] || continue
    declared=$(grep -oE 'EM_JS_DEPS\([A-Za-z0-9_]+[[:space:]]*,[[:space:]]*"[^"]*"' "$f" \
               | sed -E 's/^[^"]*"//; s/"$//' | tr ',' '\n' | sed -E 's/^[[:space:]]*\$?//; s/[[:space:]]*$//' | sort -u)
    missing=$(comm -23 <(echo "$required") <(echo "$declared"))
    if [ -n "$missing" ]; then
        echo "EM_JS_DEPS GUARD FAIL: $f"
        echo "  EM_JS/EM_ASM body references helpers missing from this TU's EM_JS_DEPS:"
        echo "$missing" | sed 's/^/     /'
        echo "  Declare each one: JS-library helpers with '\$' (\$UTF8ToString), wasmExports['x'] bare (x)."
        fail=1
    fi
done < <(grep -RlE 'EM_JS\(|EM_ASM' engine examples --include='*.c' --exclude-dir=deps 2>/dev/null || true)

if [ "$fail" -ne 0 ]; then
    echo "EM_JS_DEPS GUARD: FAILED (see above)."
    exit 1
fi
echo "EM_JS_DEPS GUARD: ok -- every JS runtime helper used in an EM_JS/EM_ASM body is declared in its TU's EM_JS_DEPS."
