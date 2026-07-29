#!/usr/bin/env bash
# The agent inner-loop entry: auto-format changed files, then run the read-only
# check. All check.sh modes pass through: --fast | (default) | --push | --full.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bash "$SCRIPT_DIR/fmt.sh"
exec bash "$SCRIPT_DIR/check.sh" "$@"
