#include "runtime_capture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

TEST_CASE("capture_all_passes populates pass_frames for all passes", "[capture][visual]") {
    const auto preset_path =
        std::filesystem::path(FILTER_CHAIN_ASSET_DIR) / "shaders/test/format.slangp";

    goggles::test::RuntimeCapturePlan plan{};
    plan.preset_path = preset_path;
    plan.preset_name = "capture_all_test";
    plan.frame_indices = {0U};
    plan.capture_all_passes = true;

    auto capture = goggles::test::capture_runtime_outputs(plan);
    if (!capture) {
        SKIP(capture.error().message);
    }

    // format.slangp has 2 passes (pass 0 and pass 1)
    REQUIRE(capture->pass_frames.count(0U) == 1);
    const auto& frame0_passes = capture->pass_frames.at(0U);
    CHECK(frame0_passes.size() == 2);
    CHECK(frame0_passes.count(0U) == 1);
    CHECK(frame0_passes.count(1U) == 1);
    CHECK(std::filesystem::exists(frame0_passes.at(0U)));
    CHECK(std::filesystem::exists(frame0_passes.at(1U)));
}
