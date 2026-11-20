#!/usr/bin/env bash
# Installs the pinned toolchain into ~/.neotolis/toolchain, shared by every checkout and
# worktree: LLVM, CMake and Ninja (.toolchain-versions) and emsdk (.emsdk-version). Python
# and Node are installed by the developer (docs/build.md#pinned-toolchain).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

python "$SCRIPT_DIR/setup_llvm.py"
python "$SCRIPT_DIR/setup_buildtools.py"
bash "$SCRIPT_DIR/setup_emsdk.sh"
