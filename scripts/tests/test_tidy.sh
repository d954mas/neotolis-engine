#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$TMP_DIR/bin" "$TMP_DIR/build"
ROOT_WIN="$ROOT_DIR"
command -v cygpath > /dev/null 2>&1 && ROOT_WIN="$(cygpath -m "$ROOT_DIR")"
printf '[{"file":"%s/engine/core/nt_core.c","command":"clang"},{"file":"%s/engine/hash/nt_hash.c","command":"clang"}]\n' "$ROOT_WIN" "$ROOT_WIN" \
    > "$TMP_DIR/build/compile_commands.json"

write_fake() {
    cat > "$TMP_DIR/bin/clang-tidy" <<'EOF'
#!/usr/bin/env bash
case "${FAKE_TIDY_MODE:?}" in
    success)
        count=0
        for arg in "$@"; do
            case "$arg" in *.c) count=$((count + 1)) ;; esac
        done
        [ "$count" -eq 1 ] || exit 99
        exit 0
        ;;
    diagnostic)
        echo "$ROOT_FOR_FAKE/engine/core/nt_core.c:1:1: error: project diagnostic"
        exit 1
        ;;
    deps_error)
        echo "$ROOT_FOR_FAKE/deps/fake.h:1:1: error: vendored diagnostic"
        exit 1
        ;;
    silent)
        exit 1
        ;;
esac
EOF
    chmod +x "$TMP_DIR/bin/clang-tidy"
}

write_fake
export PATH="$TMP_DIR/bin:$PATH"
export ROOT_FOR_FAKE="$ROOT_DIR"

export FAKE_TIDY_MODE=success
bash "$ROOT_DIR/scripts/tidy.sh" "$TMP_DIR/build" \
    engine/core/nt_core.c engine/hash/nt_hash.c > "$TMP_DIR/success.log"

for mode in diagnostic deps_error silent; do
    export FAKE_TIDY_MODE="$mode"
    if bash "$ROOT_DIR/scripts/tidy.sh" "$TMP_DIR/build" engine/core/nt_core.c > "$TMP_DIR/$mode.log" 2>&1; then
        echo "$mode clang-tidy failure was accepted"
        exit 1
    fi
done

echo "test_tidy: passed"
