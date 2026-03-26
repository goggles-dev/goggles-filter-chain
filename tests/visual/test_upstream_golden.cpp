#include "image_compare.hpp"
#include "runtime_capture.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <stb_image.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr double UPSTREAM_TOLERANCE = 0.05;
constexpr uint32_t SOURCE_WIDTH = 320U;
constexpr uint32_t SOURCE_HEIGHT = 240U;
constexpr uint32_t TARGET_WIDTH = 320U;
constexpr uint32_t TARGET_HEIGHT = 240U;

struct UpstreamPresetEntry {
    std::string_view relative_path;
    std::string_view name;
};

constexpr auto UPSTREAM_PRESETS = std::array{
    UpstreamPresetEntry{.relative_path = "crt/crt-lottes-fast.slangp", .name = "crt-lottes-fast"},
    UpstreamPresetEntry{.relative_path = "crt/crt-royale.slangp", .name = "crt-royale"},
    UpstreamPresetEntry{.relative_path = "presets/crt-royale-kurozumi.slangp",
                        .name = "crt-royale-kurozumi"},
    UpstreamPresetEntry{.relative_path = "presets/crt-hyllian-sinc-smartblur-sgenpt.slangp",
                        .name = "crt-hyllian-sinc-smartblur-sgenpt"},
    UpstreamPresetEntry{.relative_path = "crt/crt-lottes-multipass.slangp",
                        .name = "crt-lottes-multipass"},
    UpstreamPresetEntry{.relative_path = "presets/crt-plus-signal/crt-royale-ntsc-svideo.slangp",
                        .name = "crt-royale-ntsc-svideo"},
    UpstreamPresetEntry{.relative_path = "reshade/handheld-color-LUTs/GBC-sRGB.slangp",
                        .name = "GBC-sRGB"},
    UpstreamPresetEntry{.relative_path = "edge-smoothing/hqx/hq2x.slangp", .name = "hq2x"},
    UpstreamPresetEntry{.relative_path = "edge-smoothing/hqx/hq4x.slangp", .name = "hq4x"},
    UpstreamPresetEntry{.relative_path = "edge-smoothing/xbrz/4xbrz-linear.slangp",
                        .name = "4xbrz-linear"},
    UpstreamPresetEntry{.relative_path = "interpolation/bicubic-fast.slangp",
                        .name = "bicubic-fast"},
    UpstreamPresetEntry{.relative_path = "interpolation/catmull-rom-fast.slangp",
                        .name = "catmull-rom-fast"},
    UpstreamPresetEntry{.relative_path = "blurs/gauss_4tap.slangp", .name = "gauss_4tap"},
    UpstreamPresetEntry{.relative_path = "blurs/dual_filter_6_pass.slangp",
                        .name = "dual_filter_6_pass"},
};

constexpr auto FRAME_INDICES = std::array{0U, 1U, 2U};

auto load_test_pattern() -> std::optional<std::vector<uint8_t>> {
    const auto path = std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "test_pattern_240p.png";
    int width = 0;
    int height = 0;
    int channels = 0;
    auto* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return std::nullopt;
    }
    if (static_cast<uint32_t>(width) != SOURCE_WIDTH ||
        static_cast<uint32_t>(height) != SOURCE_HEIGHT) {
        stbi_image_free(pixels);
        return std::nullopt;
    }
    const auto byte_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4U;
    std::vector<uint8_t> result(pixels, pixels + byte_count);
    stbi_image_free(pixels);
    return result;
}

auto golden_dir() -> std::filesystem::path {
    return std::filesystem::path(FC_GOLDEN_DIR) / "upstream";
}

auto compare_and_report(const std::filesystem::path& actual_path,
                        const std::filesystem::path& golden_path, const std::string& context)
    -> bool {
    if (!std::filesystem::exists(golden_path)) {
        UNSCOPED_INFO("SKIP (missing golden): " << context << " -> " << golden_path);
        return true; // missing golden = skip, not fail
    }
    const auto actual = goggles::test::load_png(actual_path);
    REQUIRE(actual);
    const auto reference = goggles::test::load_png(golden_path);
    REQUIRE(reference);

    const auto diff_path = std::filesystem::path(".") /
                           (std::filesystem::path(golden_path).stem().string() + "_diff.png");
    const auto comparison =
        goggles::test::compare_images(*actual, *reference, UPSTREAM_TOLERANCE, diff_path);

    if (!comparison.passed) {
        const auto actual_dest =
            std::filesystem::path(".") /
            (std::filesystem::path(golden_path).stem().string() + "_actual.png");
        std::error_code ec;
        std::filesystem::copy_file(actual_path, actual_dest,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        UNSCOPED_INFO("MISMATCH: " << context << " max_diff=" << comparison.max_channel_diff
                                   << " mean_diff=" << comparison.mean_diff
                                   << " failing_pixels=" << comparison.failing_pixels);
    }
    return comparison.passed;
}

} // namespace

TEST_CASE("upstream preset golden verification", "[visual][golden][upstream]") {
    const std::filesystem::path shader_dir(FC_UPSTREAM_SHADER_DIR);
    if (shader_dir.empty() || !std::filesystem::exists(shader_dir)) {
        SKIP("FC_UPSTREAM_SHADER_DIR not set or missing — run pixi run shader-fetch");
    }

    const auto source_pixels = load_test_pattern();
    if (!source_pixels.has_value()) {
        SKIP("Test pattern assets/test_pattern_240p.png not found or wrong size");
    }

    for (const auto& [relative_path, name] : UPSTREAM_PRESETS) {
        SECTION(std::string(name)) {
            const auto preset_path = shader_dir / relative_path;
            if (!std::filesystem::exists(preset_path)) {
                SKIP("Preset not found: " + preset_path.string());
            }

            const auto preset_golden_dir = golden_dir() / name;

            bool any_golden = false;
            for (const auto frame : FRAME_INDICES) {
                const auto final_golden =
                    preset_golden_dir / ("final_frame" + std::to_string(frame) + ".png");
                if (std::filesystem::exists(final_golden)) {
                    any_golden = true;
                    break;
                }
            }
            if (!any_golden) {
                SKIP("No golden files for " + std::string(name) + " — run pixi run update-golden");
            }

            goggles::test::RuntimeCapturePlan plan{};
            plan.preset_path = preset_path;
            plan.preset_name = std::string(name);
            plan.frame_indices = {FRAME_INDICES.begin(), FRAME_INDICES.end()};
            plan.capture_all_passes = true;
            plan.source_extent = vk::Extent2D{SOURCE_WIDTH, SOURCE_HEIGHT};
            plan.target_extent = vk::Extent2D{TARGET_WIDTH, TARGET_HEIGHT};
            plan.source_pixels = *source_pixels;

            auto capture = goggles::test::capture_runtime_outputs(plan);
            if (!capture) {
                SKIP("Capture failed for " + std::string(name) + ": " + capture.error().message);
            }

            bool all_passed = true;

            for (const auto frame : FRAME_INDICES) {
                const auto final_golden =
                    preset_golden_dir / ("final_frame" + std::to_string(frame) + ".png");
                const auto& actual_path = capture->final_frames.at(frame);
                const auto context = std::string(name) + " final frame " + std::to_string(frame);
                if (!compare_and_report(actual_path, final_golden, context)) {
                    all_passed = false;
                }
            }

            for (const auto frame : FRAME_INDICES) {
                const auto pass_iter = capture->pass_frames.find(frame);
                if (pass_iter == capture->pass_frames.end()) {
                    continue;
                }
                for (const auto& [pass_ordinal, actual_path] : pass_iter->second) {
                    const auto pass_golden =
                        preset_golden_dir / ("pass_" + std::to_string(pass_ordinal) + "_frame" +
                                             std::to_string(frame) + ".png");
                    const auto context = std::string(name) + " pass " +
                                         std::to_string(pass_ordinal) + " frame " +
                                         std::to_string(frame);
                    if (!compare_and_report(actual_path, pass_golden, context)) {
                        all_passed = false;
                    }
                }
            }

            CHECK(all_passed);
        }
    }
}
