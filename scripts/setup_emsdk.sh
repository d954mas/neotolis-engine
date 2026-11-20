#!/usr/bin/env bash
# Installs and activates the emsdk pinned by .emsdk-version into
# ~/.neotolis/toolchain/emsdk-<version>/, shared by every checkout and worktree; the
# versioned directory lets branches with different pins coexist. Local setup and CI run it.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT_DIR/.emsdk-version")"
EMSDK_DIR="${USERPROFILE:-$HOME}/.neotolis/toolchain/emsdk-$VERSION"
if command -v cygpath > /dev/null 2>&1; then
    EMSDK_DIR="$(cygpath -u "$EMSDK_DIR")"
fi

if [ ! -f "$EMSDK_DIR/.activated" ]; then
    # The emsdk tag of the same version pins the installer itself, not only the SDK.
    [ -d "$EMSDK_DIR/.git" ] || git clone --depth 1 --branch "$VERSION" https://github.com/emscripten-core/emsdk.git "$EMSDK_DIR"
    "$EMSDK_DIR/emsdk" install "$VERSION"
    "$EMSDK_DIR/emsdk" activate "$VERSION"
    touch "$EMSDK_DIR/.activated"
fi
echo "emsdk $VERSION installed in $EMSDK_DIR"
echo "Activate it in each shell: source $EMSDK_DIR/emsdk_env.sh"
