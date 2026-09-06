#!/usr/bin/env bash
# Assert effective cache values of a configured build dir. Presets state a policy
# (release = no dev tooling); this reads what CMake actually cached, so a preset or
# option-default edit that drops the policy fails CI instead of shipping tooling.
# Values compare as literal text (ON/OFF as CMake's option() stores them).
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
    line=$(grep -E "^${name}:[A-Z]*=" "$cache" | head -1 || true)
    if [ -z "$line" ]; then
        echo "  $name=<missing>  expected $want" >&2
        fail=1
        continue
    fi
    got="${line#*=}"
    if [ "$got" = "$want" ]; then
        echo "  $name=$got"
    else
        echo "  $name=$got  expected $want" >&2
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "ERROR: $dir does not match its preset policy — delete the dir and reconfigure the preset" >&2
    exit 1
fi
echo "OK: $dir"
