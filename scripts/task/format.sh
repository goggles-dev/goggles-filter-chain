#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

mapfile -d '' -t cpp_files < <(find "$REPO_ROOT/include" "$REPO_ROOT/src" "$REPO_ROOT/tests" -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) -print0)

if (( ${#cpp_files[@]} > 0 )); then
  clang-format -i "${cpp_files[@]}"
fi

taplo fmt "$REPO_ROOT/pixi.toml"
