#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "usage: cmake_linked_libraries.sh <CMakeLists.txt> <target|--first-executable> <EMSCRIPTEN|NATIVE>" >&2
    exit 2
fi

case "$3" in
    EMSCRIPTEN|NATIVE) ;;
    *)
        echo "ERROR: mode must be EMSCRIPTEN or NATIVE" >&2
        exit 2
        ;;
esac

wanted_target="$2"
if [ "$wanted_target" = "--first-executable" ]; then
    wanted_target=$(sed -nE 's/^[[:space:]]*add_executable[[:space:]]*\([[:space:]]*([A-Za-z0-9_.:+-]+).*/\1/p' "$1" | head -1)
    if [ -z "$wanted_target" ]; then
        echo "ERROR: no add_executable target found in $1" >&2
        exit 1
    fi
fi

awk -v wanted_target="$wanted_target" -v mode="$3" '
    function trim(value) {
        sub(/^[[:space:]]+/, "", value)
        sub(/[[:space:]]+$/, "", value)
        return value
    }
    function condition_value(expression) {
        expression = trim(expression)
        if (expression == "EMSCRIPTEN") {
            condition_known[level] = 1
            return mode == "EMSCRIPTEN"
        }
        if (expression == "NOT EMSCRIPTEN") {
            condition_known[level] = 1
            return mode != "EMSCRIPTEN"
        }
        condition_known[level] = 0
        return 1
    }
    function emit_call(call, token_count, tokens, i) {
        sub(/^.*target_link_libraries[[:space:]]*\(/, "", call)
        sub(/\)[[:space:]]*$/, "", call)
        call = trim(call)
        token_count = split(call, tokens, /[[:space:]]+/)
        if (tokens[1] != wanted_target) {
            return
        }
        for (i = 2; i <= token_count; i++) {
            if (tokens[i] != "PRIVATE" && tokens[i] != "PUBLIC" && tokens[i] != "INTERFACE" && tokens[i] != "") {
                print tokens[i]
            }
        }
    }
    BEGIN {
        active = 1
        level = 0
    }
    {
        line = $0
        sub(/\r$/, "", line)
        sub(/#.*/, "", line)
    }
    !in_call && line ~ /^[[:space:]]*if[[:space:]]*\(/ {
        expression = line
        sub(/^[[:space:]]*if[[:space:]]*\(/, "", expression)
        sub(/\)[[:space:]]*$/, "", expression)
        level++
        parent_active[level] = active
        branch_value[level] = condition_value(expression)
        active = parent_active[level] && branch_value[level]
        next
    }
    !in_call && line ~ /^[[:space:]]*else[[:space:]]*\(/ {
        if (condition_known[level]) {
            active = parent_active[level] && !branch_value[level]
        } else {
            active = parent_active[level]
        }
        next
    }
    !in_call && line ~ /^[[:space:]]*endif[[:space:]]*\(/ {
        active = parent_active[level]
        delete parent_active[level]
        delete branch_value[level]
        delete condition_known[level]
        level--
        next
    }
    !in_call && active && line ~ /target_link_libraries[[:space:]]*\(/ {
        in_call = 1
        acc = ""
        depth = 0
    }
    in_call {
        acc = acc " " line
        opens = line
        closes = line
        depth += gsub(/\(/, "", opens) - gsub(/\)/, "", closes)
        if (depth == 0) {
            emit_call(acc)
            in_call = 0
        }
    }
' "$1"
