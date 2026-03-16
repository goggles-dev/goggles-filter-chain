#!/usr/bin/env bash
set -euo pipefail

pixi run semgrep
cmake --preset quality
cmake --build --preset quality
