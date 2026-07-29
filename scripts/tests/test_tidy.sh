#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$TMP_DIR/bin" "$TMP_DIR/build"
ROOT_WIN="$ROOT_DIR"
command -v cygpath > /dev/null 2>&1 && ROOT_WIN="$(cygpath -m "$ROOT_DIR")"
printf '[{"file":"%s/engine/core/nt_core.c","command":"clang"}]\n' "$ROOT_WIN" \
    > "$TMP_DIR/build/compile_commands.json"

write_fake() {
    cat > "$TMP_DIR/bin/clang-tidy" <<'EOF'
#!/usr/bin/env bash
case "${FAKE_TIDY_MODE:?}" in
    deps_retry)
        if [ ! -f "$FAKE_TIDY_STATE" ]; then
            : > "$FAKE_TIDY_STATE"
            echo "$ROOT_FOR_FAKE/engine/core/nt_core.c:1:1: error: no such file or directory: '$ROOT_FOR_FAKE/engine/core/nt_core.c'"
            echo "$ROOT_FOR_FAKE/deps/fake.h:1:1: error: vendored warning"
        else
            echo "$ROOT_FOR_FAKE/deps/fake.h:1:1: error: vendored warning"
        fi
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
export FAKE_TIDY_STATE="$TMP_DIR/state"

export FAKE_TIDY_MODE=deps_retry
bash "$ROOT_DIR/scripts/tidy.sh" "$TMP_DIR/build" engine/core/nt_core.c > "$TMP_DIR/deps.log"

rm -f "$FAKE_TIDY_STATE"
export FAKE_TIDY_MODE=silent
if bash "$ROOT_DIR/scripts/tidy.sh" "$TMP_DIR/build" engine/core/nt_core.c > "$TMP_DIR/silent.log" 2>&1; then
    echo "silent clang-tidy failure was accepted"
    exit 1
fi

echo "test_tidy: passed"
