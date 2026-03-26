#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stb_image_write.h>
#include <vector>

namespace {

constexpr uint32_t WIDTH = 320;
constexpr uint32_t HEIGHT = 240;
constexpr uint32_t CHANNELS = 4;

struct Color {
    uint8_t r, g, b;
};

constexpr auto BARS = std::array{
    Color{.r = 235, .g = 235, .b = 235}, // white
    Color{.r = 235, .g = 235, .b = 16},  // yellow
    Color{.r = 16, .g = 235, .b = 235},  // cyan
    Color{.r = 16, .g = 235, .b = 16},   // green
    Color{.r = 235, .g = 16, .b = 235},  // magenta
    Color{.r = 235, .g = 16, .b = 16},   // red
    Color{.r = 16, .g = 16, .b = 235},   // blue
};
constexpr uint32_t BAR_COUNT = 7;

void fill_color_bars(std::vector<uint8_t>& pixels, uint32_t y_start, uint32_t y_end) {
    const auto bar_width = WIDTH / BAR_COUNT;
    for (uint32_t y = y_start; y < y_end; ++y) {
        for (uint32_t x = 0; x < WIDTH; ++x) {
            auto bar_index = x / bar_width;
            if (bar_index >= BAR_COUNT) {
                bar_index = BAR_COUNT - 1;
            }
            const auto offset = (y * WIDTH + x) * CHANNELS;
            pixels[offset + 0] = BARS[bar_index].r;
            pixels[offset + 1] = BARS[bar_index].g;
            pixels[offset + 2] = BARS[bar_index].b;
            pixels[offset + 3] = 255;
        }
    }
}

void fill_grayscale_ramp(std::vector<uint8_t>& pixels, uint32_t y_start, uint32_t y_end) {
    for (uint32_t y = y_start; y < y_end; ++y) {
        for (uint32_t x = 0; x < WIDTH; ++x) {
            const auto value = static_cast<uint8_t>(x * 255 / (WIDTH - 1));
            const auto offset = (y * WIDTH + x) * CHANNELS;
            pixels[offset + 0] = value;
            pixels[offset + 1] = value;
            pixels[offset + 2] = value;
            pixels[offset + 3] = 255;
        }
    }
}

} // namespace

auto main(int argc, char** argv) -> int {
    std::filesystem::path output_path = "assets/test_pattern_240p.png";
    if (argc > 1) {
        output_path = argv[1];
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(WIDTH) * HEIGHT * CHANNELS);

    const uint32_t bar_end = HEIGHT * 2 / 3;
    fill_color_bars(pixels, 0, bar_end);
    fill_grayscale_ramp(pixels, bar_end, HEIGHT);

    if (stbi_write_png(output_path.string().c_str(), static_cast<int>(WIDTH),
                       static_cast<int>(HEIGHT), static_cast<int>(CHANNELS), pixels.data(),
                       static_cast<int>(WIDTH * CHANNELS)) == 0) {
        std::cerr << "Failed to write " << output_path << '\n';
        return 1;
    }

    std::cout << "Generated " << output_path << " (" << WIDTH << "x" << HEIGHT << ")\n";
    return 0;
}
