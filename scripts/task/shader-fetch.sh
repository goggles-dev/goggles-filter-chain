#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SHADERS_DIR="${PROJECT_ROOT}/shaders/retroarch"
REPO_URL="https://github.com/libretro/slang-shaders.git"

echo "==> Fetching upstream shaders (sparse checkout)"
echo "    Destination: ${SHADERS_DIR}"

if [[ -d "${SHADERS_DIR}/.git" ]]; then
    echo "    Updating existing sparse checkout..."
    git -C "${SHADERS_DIR}" fetch --depth 1 origin
    git -C "${SHADERS_DIR}" reset --hard origin/HEAD
else
    if [[ -d "${SHADERS_DIR}" ]]; then
        rm -rf "${SHADERS_DIR}"
    fi

    git clone --depth 1 --filter=blob:none --sparse \
        "${REPO_URL}" "${SHADERS_DIR}"

    git -C "${SHADERS_DIR}" sparse-checkout set --no-cone \
        '/stock.slang' \
        '/include/' \
        '/crt/' \
        '/blurs/' \
        '/presets/' \
        '/misc/shaders/' \
        '/dithering/shaders/' \
        '/reshade/handheld-color-LUTs/' \
        '/reshade/shaders/LUT/' \
        '/edge-smoothing/hqx/' \
        '/edge-smoothing/xbrz/' \
        '/interpolation/'
fi

echo ""
echo "==> Verifying sparse checkout"

REQUIRED_DIRS=("crt" "blurs" "presets" "include" "interpolation")
for dir in "${REQUIRED_DIRS[@]}"; do
    if [[ ! -d "${SHADERS_DIR}/${dir}" ]]; then
        echo "ERROR: Expected directory '${dir}' not found in ${SHADERS_DIR}" >&2
        exit 1
    fi
done

REQUIRED_FILES=("stock.slang" "crt/crt-lottes-fast.slangp" "blurs/gauss_4tap.slangp")
for file in "${REQUIRED_FILES[@]}"; do
    if [[ ! -f "${SHADERS_DIR}/${file}" ]]; then
        echo "ERROR: Expected file '${file}' not found in ${SHADERS_DIR}" >&2
        exit 1
    fi
done

COMMIT=$(git -C "${SHADERS_DIR}" rev-parse --short HEAD)
SHADER_COUNT=$(find "${SHADERS_DIR}" \( -name "*.slang" -o -name "*.slangp" \) | wc -l)
echo "    Installed ${SHADER_COUNT} shader files (commit: ${COMMIT})"
echo ""
echo "==> Shader fetch complete"
