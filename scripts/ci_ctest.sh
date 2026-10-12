#!/usr/bin/env bash
# CI ctest: every test except the `display` label in parallel, then the display
# tests alone. glfwInit on the shared xvfb display fails sporadically while
# sanitizer tests load every core, so display tests never share the runner.
# Arguments pass through to both ctest runs (--preset/--test-dir, -R, -E ...).
set -euo pipefail
ctest "$@" --output-on-failure -j "$(nproc)" -LE display
ctest "$@" --output-on-failure -L display
