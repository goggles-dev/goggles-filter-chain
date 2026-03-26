#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

source "${SCRIPT_DIR}/parse-preset.sh" "$@"
BUILD_DIR="${PROJECT_ROOT}/build/${PRESET}"
GOLDEN_DIR="${PROJECT_ROOT}/tests/golden"
CAPTURE_BIN="${BUILD_DIR}/tests/visual/fc_visual_capture"
SHADERS_DIR="${PROJECT_ROOT}/shaders/retroarch"
TEST_PATTERN="${PROJECT_ROOT}/assets/test_pattern_240p.png"

echo "==> Updating golden reference images (preset: ${PRESET})"
echo "    Build dir:    ${BUILD_DIR}"
echo "    Golden dir:   ${GOLDEN_DIR}"

if [[ ! -x "${CAPTURE_BIN}" ]]; then
    echo "ERROR: fc_visual_capture not found at ${CAPTURE_BIN}"
    echo "       Run 'pixi run build -p ${PRESET}' first."
    exit 1
fi

# Warn if not running on lavapipe
if [[ -z "${VK_ICD_FILENAMES:-}" ]]; then
    echo "WARNING: VK_ICD_FILENAMES not set."
    echo "         Golden images should be generated on lavapipe for CI reproducibility."
    echo "         Set: export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
    echo ""
fi

export ASAN_OPTIONS="detect_leaks=0"

mkdir -p "${GOLDEN_DIR}"

# ── Internal goldens ─────────────────────────────────────────────────────

echo ""
echo "--> Capturing runtime_format intermediate goldens ..."
"${CAPTURE_BIN}" \
    --preset-path "${PROJECT_ROOT}/assets/shaders/test/format.slangp" \
    --preset-name runtime_format \
    --output-dir "${GOLDEN_DIR}" \
    --frames 0 \
    --passes 0,1
# The tool produces runtime_format_frame0.png which we don't need as a golden
rm -f "${GOLDEN_DIR}/runtime_format_frame0.png"
# Rename pass files to drop _frame0 suffix (matches existing convention)
for f in "${GOLDEN_DIR}/runtime_format_pass"*"_frame0.png"; do
    if [[ -f "$f" ]]; then
        newname="${f%_frame0.png}.png"
        mv "$f" "$newname"
    fi
done
echo "    Done: ${GOLDEN_DIR}/runtime_format_pass0.png"
echo "    Done: ${GOLDEN_DIR}/runtime_format_pass1.png"

echo ""
echo "--> Capturing runtime_history temporal goldens ..."
"${CAPTURE_BIN}" \
    --preset-path "${PROJECT_ROOT}/assets/shaders/test/history.slangp" \
    --preset-name runtime_history \
    --output-dir "${GOLDEN_DIR}" \
    --frames 1,3 \
    --passes 0
echo "    Done: ${GOLDEN_DIR}/runtime_history_frame1.png"
echo "    Done: ${GOLDEN_DIR}/runtime_history_frame3.png"
echo "    Done: ${GOLDEN_DIR}/runtime_history_pass0_frame1.png"
echo "    Done: ${GOLDEN_DIR}/runtime_history_pass0_frame3.png"

# ── Upstream goldens ─────────────────────────────────────────────────────

if [[ ! -f "${TEST_PATTERN}" ]]; then
    echo ""
    echo "WARNING: Test pattern not found at ${TEST_PATTERN}"
    echo "         Skipping upstream golden generation."
    echo "         Build and run fc_generate_test_pattern first."
    exit 0
fi

if [[ ! -d "${SHADERS_DIR}" ]]; then
    echo ""
    echo "WARNING: Upstream shaders not found at ${SHADERS_DIR}"
    echo "         Skipping upstream golden generation."
    echo "         Run 'pixi run shader-fetch' first."
    exit 0
fi

# Preset table: relative_path|name
PRESETS=(
    "crt/crt-lottes-fast.slangp|crt-lottes-fast"
    "crt/crt-royale.slangp|crt-royale"
    "presets/crt-royale-kurozumi.slangp|crt-royale-kurozumi"
    "presets/crt-hyllian-sinc-smartblur-sgenpt.slangp|crt-hyllian-sinc-smartblur-sgenpt"
    "crt/crt-lottes-multipass.slangp|crt-lottes-multipass"
    "presets/crt-plus-signal/crt-royale-ntsc-svideo.slangp|crt-royale-ntsc-svideo"
    "reshade/handheld-color-LUTs/GBC-sRGB.slangp|GBC-sRGB"
    "edge-smoothing/hqx/hq2x.slangp|hq2x"
    "edge-smoothing/hqx/hq4x.slangp|hq4x"
    "edge-smoothing/xbrz/4xbrz-linear.slangp|4xbrz-linear"
    "interpolation/bicubic-fast.slangp|bicubic-fast"
    "interpolation/catmull-rom-fast.slangp|catmull-rom-fast"
    "blurs/gauss_4tap.slangp|gauss_4tap"
    "blurs/dual_filter_6_pass.slangp|dual_filter_6_pass"
)

echo ""
echo "==> Generating upstream golden images"

UPSTREAM_GOLDEN_DIR="${GOLDEN_DIR}/upstream"
GENERATED=0
SKIPPED=0

for entry in "${PRESETS[@]}"; do
    IFS='|' read -r rel_path name <<< "$entry"
    preset_file="${SHADERS_DIR}/${rel_path}"

    if [[ ! -f "${preset_file}" ]]; then
        echo "    SKIP: ${name} (preset not found: ${preset_file})"
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    output_dir="${UPSTREAM_GOLDEN_DIR}/${name}"
    mkdir -p "${output_dir}"

    echo ""
    echo "--> Capturing ${name} ..."

    # Capture to a temp directory first, then rename to match spec convention
    TMPDIR=$(mktemp -d)
    if ! "${CAPTURE_BIN}" \
        --preset-path "${preset_file}" \
        --preset-name "${name}" \
        --output-dir "${TMPDIR}" \
        --frames 0,1,2 \
        --passes all \
        --source-image "${TEST_PATTERN}" \
        --source-extent 320x240 \
        --target-extent 320x240; then
        echo "    ERROR: Capture failed for ${name}"
        rm -rf "${TMPDIR}"
        continue
    fi

    # Rename final frames: {name}_frame{N}.png -> final_frame{N}.png
    for f in "${TMPDIR}/${name}_frame"*.png; do
        if [[ -f "$f" ]]; then
            base=$(basename "$f")
            new_name="final_${base#"${name}_"}"
            mv "$f" "${output_dir}/${new_name}"
        fi
    done

    # Rename pass frames: {name}_pass{N}_frame{M}.png -> pass_{N}_frame{M}.png
    for f in "${TMPDIR}/${name}_pass"*.png; do
        if [[ -f "$f" ]]; then
            base=$(basename "$f")
            new_name="${base#"${name}_"}"
            mv "$f" "${output_dir}/${new_name}"
        fi
    done

    rm -rf "${TMPDIR}"
    file_count=$(find "${output_dir}" -name "*.png" | wc -l)
    echo "    Done: ${output_dir}/ (${file_count} files)"
    GENERATED=$((GENERATED + 1))
done

echo ""
echo "==> Golden image generation complete"
echo "    Generated: ${GENERATED} presets"
echo "    Skipped:   ${SKIPPED} presets"
echo ""
echo "    Review with: feh ${UPSTREAM_GOLDEN_DIR}/*/*.png"
echo "    Commit when satisfied:"
echo "      git add tests/golden/"
echo "      git commit -m 'chore(test): update golden reference images'"
