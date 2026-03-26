#include "runtime_capture.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stb_image.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct Arguments {
    std::filesystem::path preset_path;
    std::string preset_name;
    std::filesystem::path output_dir;
    std::vector<uint32_t> frame_indices;
    std::vector<uint32_t> pass_ordinals;
    bool capture_all_passes = false;
    std::filesystem::path source_image;
    uint32_t source_width = 64U;
    uint32_t source_height = 64U;
    uint32_t target_width = 64U;
    uint32_t target_height = 64U;
};

auto split_csv(std::string_view csv_values) -> std::vector<std::string> {
    std::vector<std::string> parts;
    std::string current;
    std::stringstream stream{std::string(csv_values)};
    while (std::getline(stream, current, ',')) {
        if (!current.empty()) {
            parts.push_back(current);
        }
    }
    return parts;
}

auto parse_indices(std::string_view csv_values, const char* value_label)
    -> std::optional<std::vector<uint32_t>> {
    std::vector<uint32_t> indices;
    for (const auto& part : split_csv(csv_values)) {
        try {
            const auto parsed = static_cast<uint32_t>(std::stoul(part));
            indices.push_back(parsed);
        } catch (const std::exception&) {
            std::cerr << "Invalid " << value_label << " value: " << part << '\n';
            return std::nullopt;
        }
    }
    return indices;
}

auto parse_extent(std::string_view value) -> std::optional<std::pair<uint32_t, uint32_t>> {
    const auto x_pos = value.find('x');
    if (x_pos == std::string_view::npos) {
        return std::nullopt;
    }
    try {
        const auto width = static_cast<uint32_t>(std::stoul(std::string(value.substr(0, x_pos))));
        const auto height = static_cast<uint32_t>(std::stoul(std::string(value.substr(x_pos + 1))));
        return std::pair{width, height};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void print_usage() {
    std::cerr << "Usage: fc_visual_capture"
              << " --preset-path <path> --preset-name <name> --output-dir <dir>"
              << " --frames <csv>"
              << " [--passes <csv|all>]"
              << " [--source-image <path>]"
              << " [--source-extent <WxH>]"
              << " [--target-extent <WxH>]\n";
}

auto parse_args(int argc, char** argv) -> std::optional<Arguments> {
    Arguments args;

    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--preset-path" && index + 1 < argc) {
            args.preset_path = argv[++index];
        } else if (option == "--preset-name" && index + 1 < argc) {
            args.preset_name = argv[++index];
        } else if (option == "--output-dir" && index + 1 < argc) {
            args.output_dir = argv[++index];
        } else if (option == "--frames" && index + 1 < argc) {
            const auto parsed = parse_indices(argv[++index], "frame");
            if (!parsed.has_value()) {
                return std::nullopt;
            }
            args.frame_indices = *parsed;
        } else if (option == "--passes" && index + 1 < argc) {
            const std::string_view pass_value(argv[++index]);
            if (pass_value == "all") {
                args.capture_all_passes = true;
            } else {
                const auto parsed = parse_indices(pass_value, "pass");
                if (!parsed.has_value()) {
                    return std::nullopt;
                }
                args.pass_ordinals = *parsed;
            }
        } else if (option == "--source-image" && index + 1 < argc) {
            args.source_image = argv[++index];
        } else if (option == "--source-extent" && index + 1 < argc) {
            const auto extent = parse_extent(argv[++index]);
            if (!extent.has_value()) {
                std::cerr << "Invalid extent format, expected WxH\n";
                return std::nullopt;
            }
            args.source_width = extent->first;
            args.source_height = extent->second;
        } else if (option == "--target-extent" && index + 1 < argc) {
            const auto extent = parse_extent(argv[++index]);
            if (!extent.has_value()) {
                std::cerr << "Invalid extent format, expected WxH\n";
                return std::nullopt;
            }
            args.target_width = extent->first;
            args.target_height = extent->second;
        } else {
            std::cerr << "Unknown or incomplete option: " << option << '\n';
            return std::nullopt;
        }
    }

    if (args.preset_path.empty() || args.preset_name.empty() || args.output_dir.empty() ||
        args.frame_indices.empty()) {
        print_usage();
        return std::nullopt;
    }

    std::sort(args.frame_indices.begin(), args.frame_indices.end());
    args.frame_indices.erase(std::unique(args.frame_indices.begin(), args.frame_indices.end()),
                             args.frame_indices.end());
    return args;
}

auto copy_capture(const std::filesystem::path& source, const std::filesystem::path& destination)
    -> bool {
    std::error_code ec;
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "Failed to copy " << source << " to " << destination << ": " << ec.message()
                  << '\n';
        return false;
    }
    return true;
}

auto load_source_image(const std::filesystem::path& path, uint32_t expected_width,
                       uint32_t expected_height) -> std::optional<std::vector<uint8_t>> {
    int width = 0;
    int height = 0;
    int channels = 0;
    const auto* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        std::cerr << "Failed to load source image: " << path << '\n';
        return std::nullopt;
    }
    if (static_cast<uint32_t>(width) != expected_width ||
        static_cast<uint32_t>(height) != expected_height) {
        std::cerr << "Source image size " << width << "x" << height << " does not match expected "
                  << expected_width << "x" << expected_height << '\n';
        stbi_image_free(const_cast<unsigned char*>(pixels));
        return std::nullopt;
    }
    const auto byte_count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4U;
    std::vector<uint8_t> result(pixels, pixels + byte_count);
    stbi_image_free(const_cast<unsigned char*>(pixels));
    return result;
}

} // namespace

auto main(int argc, char** argv) -> int {
    const auto parsed = parse_args(argc, argv);
    if (!parsed.has_value()) {
        return 2;
    }

    std::error_code ec;
    std::filesystem::create_directories(parsed->output_dir, ec);
    if (ec) {
        std::cerr << "Failed to create output directory " << parsed->output_dir << ": "
                  << ec.message() << '\n';
        return 1;
    }

    goggles::test::RuntimeCapturePlan plan{};
    plan.preset_path = parsed->preset_path;
    plan.preset_name = parsed->preset_name;
    plan.frame_indices = parsed->frame_indices;
    plan.pass_ordinals = parsed->pass_ordinals;
    plan.capture_all_passes = parsed->capture_all_passes;
    plan.source_extent = vk::Extent2D{parsed->source_width, parsed->source_height};
    plan.target_extent = vk::Extent2D{parsed->target_width, parsed->target_height};

    if (!parsed->source_image.empty()) {
        auto pixels =
            load_source_image(parsed->source_image, parsed->source_width, parsed->source_height);
        if (!pixels.has_value()) {
            return 1;
        }
        plan.source_pixels = std::move(*pixels);
    }

    auto capture = goggles::test::capture_runtime_outputs(plan);
    if (!capture) {
        std::cerr << capture.error().message << '\n';
        return 1;
    }

    for (const auto frame_index : parsed->frame_indices) {
        const auto final_iter = capture->final_frames.find(frame_index);
        if (final_iter == capture->final_frames.end()) {
            std::cerr << "Missing captured final frame for index " << frame_index << '\n';
            return 1;
        }

        const auto final_destination = parsed->output_dir / (parsed->preset_name + "_frame" +
                                                             std::to_string(frame_index) + ".png");
        if (!copy_capture(final_iter->second, final_destination)) {
            return 1;
        }

        const auto pass_iter = capture->pass_frames.find(frame_index);
        if (pass_iter != capture->pass_frames.end()) {
            for (const auto& [pass_ordinal, pass_path] : pass_iter->second) {
                const auto pass_destination =
                    parsed->output_dir /
                    (parsed->preset_name + "_pass" + std::to_string(pass_ordinal) + "_frame" +
                     std::to_string(frame_index) + ".png");
                if (!copy_capture(pass_path, pass_destination)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}
