#!/usr/bin/env bash
# Gate: no engine module may link a REAL swappable impl (re-poisons the link
# graph and makes stubs unreachable -- the #126 anti-pattern). Modules link the
# header-only nt_X_interface; the executable picks exactly one impl.
#
# All 7 swappable pairs are decoupled (61-01 keystone nt_log + 61-02 rest).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Swappable real impl targets that engine modules must NEVER link directly.
SWAPPABLE=(nt_log nt_input nt_http nt_gfx nt_window nt_app nt_fs nt_clipboard)

FAIL=0
while IFS= read -r cmake; do
    # Collapse every target_link_libraries(...) call to ONE logical line (file:line:text)
    # so a multi-line dep list is checked as a whole -- a line-by-line read silently skips
    # continuation lines (the bug this guards). awk strips comments + accumulates until the
    # call's matching close paren (depth-balanced), then emits the joined call.
    while IFS= read -r entry; do
        file="${entry%%:*}"
        rest="${entry#*:}"
        lineno="${rest%%:*}"
        text="${rest#*:}"
        # Strip the link TARGET (first token after the open paren) -- a module building
        # itself (e.g. target_link_libraries(nt_gfx PUBLIC ...) in engine/graphics)
        # legitimately names its own impl. Only the DEPENDENCY list is checked. Dir-name
        # agnostic, so the gfx/graphics dir-name mismatch needs no special case.
        deps="$(printf '%s' "$text" | sed -E 's/.*target_link_libraries[[:space:]]*\([[:space:]]*[A-Za-z0-9_:]+//')"
        for impl in "${SWAPPABLE[@]}"; do
            # Match the bare token only: nt_log, not nt_log_interface / nt_log_stub.
            if printf '%s' "$deps" | grep -Eq "(^|[^A-Za-z0-9_])${impl}([^A-Za-z0-9_]|\$)"; then
                echo "ERROR: $file:$lineno links real impl '$impl' (use ${impl}_interface):"
                echo "    $text"
                FAIL=1
            fi
        done
    done < <(awk -v fname="$cmake" '
        {
            line = $0
            sub(/#.*/, "", line)                 # strip trailing comment
        }
        # Not currently inside a call: look for a call start.
        !in_call {
            if (line ~ /target_link_libraries[[:space:]]*\(/) {
                in_call = 1
                start = NR
                acc = ""
                depth = 0
            } else {
                next
            }
        }
        in_call {
            # Append (single-spaced) and track paren depth to find the matching close.
            gsub(/^[[:space:]]+/, "", line)
            acc = (acc == "" ? line : acc " " line)
            n_open = gsub(/\(/, "(", line)
            n_close = gsub(/\)/, ")", line)
            depth += n_open - n_close
            if (depth <= 0) {
                gsub(/[[:space:]]+/, " ", acc)
                print fname ":" start ":" acc
                in_call = 0
            }
        }
    ' "$cmake")
done < <(find "$ROOT_DIR/engine" -name CMakeLists.txt)

if [ "$FAIL" -ne 0 ]; then
    echo "check_no_real_impl_links: FAILED"
    exit 1
fi
echo "check_no_real_impl_links: passed (no engine module links a real swappable impl)"
