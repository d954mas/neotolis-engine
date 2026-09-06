#!/usr/bin/env bash
# Assert effective cache values of a configured build dir. Presets state a policy
# (release = no dev tooling); this reads what CMake actually cached, which is what a
# stale or hand-edited dir will silently diverge from. option() defaults apply only to a
# fresh cache, so dependents (log ring / metrics / introspect) must be asserted too.
#
# Usage: check_preset_cache.sh <build-dir> VAR=VALUE [VAR=VALUE...]
set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <build-dir> VAR=VALUE [VAR=VALUE...]" >&2
    exit 2
fi

dir="$1"
shift
cache="$dir/CMakeCache.txt"
if [ ! -f "$cache" ]; then
    echo "ERROR: $cache not found — configure the preset first" >&2
    exit 1
fi

fail=0
for expect in "$@"; do
    name="${expect%%=*}"
    want="${expect#*=}"
    # Cache entries are typed (NAME:BOOL=OFF); UNINITIALIZED comes from a bare -D.
    got=$(grep -E "^${name}:[A-Z]*=" "$cache" | head -1 | cut -d= -f2- || true)
    if [ "$got" = "$want" ]; then
        echo "  $name=$got"
    else
        echo "  $name=${got:-<missing>}  expected $want" >&2
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "ERROR: $dir does not match its preset policy — delete the dir and reconfigure the preset" >&2
    exit 1
fi
echo "OK: $dir"
