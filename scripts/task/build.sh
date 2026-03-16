#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/parse-preset.sh" "$@"

mkdir -p "${CCACHE_TEMPDIR:-$REPO_ROOT/.cache/ccache-tmp}"
cmake --fresh -S "$REPO_ROOT" --preset "$PRESET"
cmake --build "$REPO_ROOT/build/$PRESET"
