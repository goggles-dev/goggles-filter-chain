#pragma once

#include <cstdint>
#include <filesystem>
#include <goggles/error.hpp>
#include <string>
#include <vector>

namespace goggles::cli {

struct ImageData {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
};

[[nodiscard]] auto load_image(const std::filesystem::path& path) -> goggles::Result<ImageData>;

[[nodiscard]] auto save_png(const std::filesystem::path& path, const uint8_t* pixels,
                            uint32_t width, uint32_t height) -> goggles::Result<void>;

[[nodiscard]] auto collect_input_images(const std::vector<std::string>& inputs)
    -> goggles::Result<std::vector<std::filesystem::path>>;

} // namespace goggles::cli
