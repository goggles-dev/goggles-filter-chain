#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "$0")/parse-preset.sh" "$@"

mkdir -p "${CCACHE_TEMPDIR:-.cache/ccache-tmp}"
cmake --preset "$PRESET"
cmake --build --preset "$PRESET"
