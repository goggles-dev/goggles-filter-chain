#pragma once

#include <filesystem>
#include <goggles/filter_chain.h>
#include <goggles/error.hpp>
#include <goggles/filter_chain/scale_mode.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace goggles::test {

struct TempDir {
    std::filesystem::path path;

    TempDir();
    ~TempDir();

    TempDir(const TempDir&) = delete;
    auto operator=(const TempDir&) -> TempDir& = delete;
};

struct RuntimeCapturePlan {
    std::filesystem::path preset_path;
    std::string preset_name;
    std::vector<uint32_t> frame_indices;
    std::vector<uint32_t> pass_ordinals;
    std::vector<std::pair<std::string, float>> control_overrides;
    vk::Extent2D source_extent{64U, 64U};
    vk::Extent2D target_extent{64U, 64U};
    goggles::ScaleMode scale_mode = goggles::ScaleMode::stretch;
    uint32_t integer_scale = 0U;
};

struct RuntimeCaptureResult {
    std::unique_ptr<TempDir> temp_dir;
    std::unordered_map<uint32_t, std::filesystem::path> final_frames;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::filesystem::path>> pass_frames;
    goggles_fc_diagnostic_summary_t diagnostic_summary = goggles_fc_diagnostic_summary_init();
};

[[nodiscard]] auto capture_runtime_outputs(const RuntimeCapturePlan& plan)
    -> Result<RuntimeCaptureResult>;

} // namespace goggles::test
