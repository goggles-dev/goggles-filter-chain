#include "image_compare.hpp"
#include "runtime_capture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr double TEMPORAL_TOLERANCE = 0.05;

} // namespace

TEST_CASE("temporal golden infrastructure captures requested frames", "[visual][diagnostics]") {
    const std::string preset_name = "runtime_history";
    const auto preset_path =
        std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders/test/history.slangp";
    const std::vector<uint32_t> frame_indices = {1U, 3U};

    bool any_golden = false;
    for (const auto frame_index : frame_indices) {
        const auto final_golden = std::filesystem::path(FC_GOLDEN_DIR) /
                                  (preset_name + "_frame" + std::to_string(frame_index) + ".png");
        any_golden = any_golden || std::filesystem::exists(final_golden);
    }
    if (!any_golden) {
        SKIP("Temporal goldens not found - generate files like filter-chain/tests/golden/" +
             preset_name + "_frame1.png");
    }

    goggles::test::RuntimeCapturePlan plan{};
    plan.preset_path = preset_path;
    plan.preset_name = preset_name;
    plan.frame_indices = frame_indices;

    auto capture = goggles::test::capture_runtime_outputs(plan);
    if (!capture) {
        SKIP(capture.error().message);
    }

    CHECK(capture->diagnostic_summary.current_frame == 4u);
    CHECK(capture->diagnostic_summary.total_events == 0u);

    for (const auto frame_index : frame_indices) {
        const auto final_golden = std::filesystem::path(FC_GOLDEN_DIR) /
                                  (preset_name + "_frame" + std::to_string(frame_index) + ".png");
        if (!std::filesystem::exists(final_golden)) {
            SKIP("Missing temporal final golden for frame " + std::to_string(frame_index));
        }

        const auto actual_final = goggles::test::load_png(capture->final_frames.at(frame_index));
        REQUIRE(actual_final);
        const auto golden_final = goggles::test::load_png(final_golden);
        REQUIRE(golden_final);
        const auto final_comparison =
            goggles::test::compare_images(*actual_final, *golden_final, TEMPORAL_TOLERANCE);
        CHECK(final_comparison.passed);
    }
}
