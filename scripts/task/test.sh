#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/parse-preset.sh" "$@"

"$SCRIPT_DIR/build.sh" -p "$PRESET"
ctest --preset "$PRESET" --output-on-failure
