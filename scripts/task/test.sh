#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/parse-preset.sh" "$@"

bash "$SCRIPT_DIR/build.sh" -p "$PRESET"
ctest --test-dir "$REPO_ROOT/build/$PRESET" --output-on-failure
