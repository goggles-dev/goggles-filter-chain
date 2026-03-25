#include "image_io.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

namespace goggles::cli {

auto load_image(const std::filesystem::path& path) -> goggles::Result<ImageData> {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (data == nullptr) {
        return goggles::make_error<ImageData>(
            goggles::ErrorCode::file_read_failed,
            "Failed to load image: " + path.string() + " (" + stbi_failure_reason() + ")");
    }

    const auto byte_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    ImageData result;
    result.width = static_cast<uint32_t>(width);
    result.height = static_cast<uint32_t>(height);
    result.pixels.assign(data, data + byte_count);
    stbi_image_free(data);

    return result;
}

auto save_png(const std::filesystem::path& path, const uint8_t* pixels, uint32_t width,
              uint32_t height) -> goggles::Result<void> {
    const int stride = static_cast<int>(width * 4u);
    if (stbi_write_png(path.string().c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                       pixels, stride) == 0) {
        return goggles::make_error<void>(goggles::ErrorCode::file_write_failed,
                                        "Failed to write PNG: " + path.string());
    }
    return {};
}

auto collect_input_images(const std::vector<std::string>& inputs)
    -> goggles::Result<std::vector<std::filesystem::path>> {
    std::vector<std::filesystem::path> paths;

    for (const auto& input : inputs) {
        std::filesystem::path p(input);
        if (std::filesystem::is_directory(p)) {
            for (const auto& entry : std::filesystem::directory_iterator(p)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                auto ext = entry.path().extension().string();
                for (auto& c : ext) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                    paths.push_back(entry.path());
                }
            }
        } else if (std::filesystem::is_regular_file(p)) {
            paths.push_back(p);
        } else {
            return goggles::make_error<std::vector<std::filesystem::path>>(
                goggles::ErrorCode::file_not_found, "Input not found: " + input);
        }
    }

    if (paths.empty()) {
        return goggles::make_error<std::vector<std::filesystem::path>>(
            goggles::ErrorCode::invalid_data, "No input images found");
    }

    return paths;
}

} // namespace goggles::cli
