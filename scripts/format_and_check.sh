#!/usr/bin/env bash
# The agent entry point: auto-format changed files, then run the read-only
# check. check.sh modes pass through: (default) | --push | --full.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bash "$SCRIPT_DIR/fmt.sh"
exec bash "$SCRIPT_DIR/check.sh" "$@"
