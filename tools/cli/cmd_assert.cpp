#include "cmd_assert.hpp"

#include "image_io.hpp"
#include "vulkan_backend.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <goggles/filter_chain.hpp>
#include <string>

namespace {

constexpr int EXIT_PASS = 0;
constexpr int EXIT_FAIL = 1;
constexpr int EXIT_ERROR = 2;

constexpr float DEFAULT_TOLERANCE = 0.02F;

// ── assert-image helpers ───────────────────────────────────────────────────

struct AssertImageArgs {
    std::string actual;
    std::string golden;
    float tolerance = DEFAULT_TOLERANCE;
};

void print_assert_image_usage() {
    std::fprintf(stderr,
                 "Usage: goggles-chain-cli assert-image <actual.png> <golden.png> "
                 "[--tolerance <float>]\n"
                 "\n"
                 "Compare two PNG images pixel-by-pixel.\n"
                 "\n"
                 "Options:\n"
                 "  --tolerance <float>     Per-channel tolerance 0..1 (default: 0.02)\n"
                 "  --help                  Show this usage message\n"
                 "\n"
                 "Exit codes:\n"
                 "  0  pass   Images match within tolerance\n"
                 "  1  fail   Images diverged\n"
                 "  2  error  Tool error (missing file, bad args, etc.)\n");
}

auto parse_assert_image_args(int argc, char** argv) -> goggles::Result<AssertImageArgs> {
    AssertImageArgs args;

    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_assert_image_usage();
            std::exit(EXIT_PASS);
        }
        if (arg == "--tolerance" && i + 1 < argc) {
            float value = 0.0F;
            const char* str = argv[++i];
            auto [ptr, ec] = std::from_chars(str, str + std::strlen(str), value);
            if (ec != std::errc{} || *ptr != '\0') {
                return goggles::make_error<AssertImageArgs>(goggles::ErrorCode::invalid_data,
                                                            std::string("Invalid tolerance: ") + str);
            }
            args.tolerance = value;
            continue;
        }
        if (arg.starts_with("-")) {
            return goggles::make_error<AssertImageArgs>(goggles::ErrorCode::invalid_data,
                                                        "Unknown option: " + arg);
        }
        if (args.actual.empty()) {
            args.actual = arg;
        } else if (args.golden.empty()) {
            args.golden = arg;
        } else {
            return goggles::make_error<AssertImageArgs>(goggles::ErrorCode::invalid_data,
                                                        "Unexpected argument: " + arg);
        }
    }

    if (args.actual.empty() || args.golden.empty()) {
        return goggles::make_error<AssertImageArgs>(goggles::ErrorCode::invalid_data,
                                                    "Expected: <actual.png> <golden.png>");
    }
    return args;
}

// ── assert-clean / assert-no-degradation helpers ───────────────────────────

struct AssertPresetArgs {
    std::string preset;
    bool verbose = false;
};

void print_assert_clean_usage() {
    std::fprintf(stderr,
                 "Usage: goggles-chain-cli assert-clean <preset.slangp> [--verbose]\n"
                 "\n"
                 "Assert that a preset compiles with no errors or warnings.\n"
                 "\n"
                 "Options:\n"
                 "  --verbose               Enable diagnostic logging\n"
                 "  --help                  Show this usage message\n"
                 "\n"
                 "Exit codes:\n"
                 "  0  pass   No authoring errors\n"
                 "  1  fail   Authoring issues found\n"
                 "  2  error  Tool error\n");
}

void print_assert_no_degradation_usage() {
    std::fprintf(stderr,
                 "Usage: goggles-chain-cli assert-no-degradation <preset.slangp> [--verbose]\n"
                 "\n"
                 "Assert that a preset has no degradation (zero warnings).\n"
                 "\n"
                 "Options:\n"
                 "  --verbose               Enable diagnostic logging\n"
                 "  --help                  Show this usage message\n"
                 "\n"
                 "Exit codes:\n"
                 "  0  pass   No degradation detected\n"
                 "  1  fail   Degradation detected\n"
                 "  2  error  Tool error\n");
}

auto parse_assert_preset_args(int argc, char** argv, bool is_clean) -> goggles::Result<AssertPresetArgs> {
    AssertPresetArgs args;

    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            if (is_clean) {
                print_assert_clean_usage();
            } else {
                print_assert_no_degradation_usage();
            }
            std::exit(EXIT_PASS);
        }
        if (arg == "--verbose") {
            args.verbose = true;
            continue;
        }
        if (arg.starts_with("-")) {
            return goggles::make_error<AssertPresetArgs>(goggles::ErrorCode::invalid_data,
                                                         "Unknown option: " + arg);
        }
        if (args.preset.empty()) {
            args.preset = arg;
        } else {
            return goggles::make_error<AssertPresetArgs>(goggles::ErrorCode::invalid_data,
                                                         "Unexpected argument: " + arg);
        }
    }

    if (args.preset.empty()) {
        return goggles::make_error<AssertPresetArgs>(goggles::ErrorCode::invalid_data,
                                                     "Missing required preset path");
    }
    return args;
}

void GOGGLES_FC_CALL log_callback(const goggles_fc_log_message_t* message, void* /*user_data*/) {
    const char* level_str = "???";
    switch (message->level) {
    case GOGGLES_FC_LOG_LEVEL_TRACE:
        level_str = "TRACE";
        break;
    case GOGGLES_FC_LOG_LEVEL_DEBUG:
        level_str = "DEBUG";
        break;
    case GOGGLES_FC_LOG_LEVEL_INFO:
        level_str = "INFO";
        break;
    case GOGGLES_FC_LOG_LEVEL_WARN:
        level_str = "WARN";
        break;
    case GOGGLES_FC_LOG_LEVEL_ERROR:
        level_str = "ERROR";
        break;
    case GOGGLES_FC_LOG_LEVEL_CRITICAL:
        level_str = "CRIT";
        break;
    default:
        break;
    }
    std::fprintf(stderr, "[%s] %.*s: %.*s\n", level_str, static_cast<int>(message->domain.size),
                 message->domain.data, static_cast<int>(message->message.size),
                 message->message.data);
}

auto create_fc_pipeline(goggles::cli::VulkanBackend& vk, const AssertPresetArgs& args)
    -> goggles::Result<goggles_fc_diagnostic_summary_t> {
    auto instance_info = goggles_fc_instance_create_info_init();
    if (args.verbose) {
        instance_info.log_callback = log_callback;
    }

    auto fc_instance = goggles::filter_chain::Instance::create(&instance_info);
    if (!fc_instance) {
        return goggles::make_error<goggles_fc_diagnostic_summary_t>(
            fc_instance.error().code, "FC instance: " + fc_instance.error().message);
    }

    auto device_info = goggles_fc_vk_device_create_info_init();
    device_info.physical_device = vk.physical_device;
    device_info.device = vk.device;
    device_info.graphics_queue = vk.queue;
    device_info.graphics_queue_family_index = vk.queue_family_index;

    auto fc_device = goggles::filter_chain::Device::create(*fc_instance, &device_info);
    if (!fc_device) {
        return goggles::make_error<goggles_fc_diagnostic_summary_t>(
            fc_device.error().code, "FC device: " + fc_device.error().message);
    }

    auto source = goggles_fc_preset_source_init();
    source.kind = GOGGLES_FC_PRESET_SOURCE_FILE;
    source.path = {.data = args.preset.c_str(), .size = args.preset.size()};

    auto fc_program = goggles::filter_chain::Program::create(*fc_device, &source);
    if (!fc_program) {
        return goggles::make_error<goggles_fc_diagnostic_summary_t>(
            fc_program.error().code, "Compilation: " + fc_program.error().message);
    }

    auto chain_info = goggles_fc_chain_create_info_init();
    chain_info.target_format = VK_FORMAT_R8G8B8A8_UNORM;
    chain_info.frames_in_flight = 1;
    chain_info.initial_stage_mask = GOGGLES_FC_STAGE_MASK_ALL;

    auto chain = goggles::filter_chain::Chain::create(*fc_device, *fc_program, &chain_info);
    if (!chain) {
        return goggles::make_error<goggles_fc_diagnostic_summary_t>(
            chain.error().code, "Chain: " + chain.error().message);
    }

    return chain->get_diagnostic_summary();
}

} // namespace

namespace goggles::cli {

auto run_assert_image(int argc, char** argv) -> int {
    auto args_result = parse_assert_image_args(argc, argv);
    if (!args_result) {
        std::fprintf(stderr, "Error: %s\n", args_result.error().message.c_str());
        print_assert_image_usage();
        return EXIT_ERROR;
    }
    auto args = std::move(*args_result);

    auto actual_result = load_image(args.actual);
    if (!actual_result) {
        std::fprintf(stderr, "Error: failed to load actual image: %s\n",
                     actual_result.error().message.c_str());
        return EXIT_ERROR;
    }
    auto actual = std::move(*actual_result);

    auto golden_result = load_image(args.golden);
    if (!golden_result) {
        std::fprintf(stderr, "Error: failed to load golden image: %s\n",
                     golden_result.error().message.c_str());
        return EXIT_ERROR;
    }
    auto golden = std::move(*golden_result);

    if (actual.width != golden.width || actual.height != golden.height) {
        std::fprintf(stderr, "FAIL: dimension mismatch (actual %ux%u, golden %ux%u)\n",
                     actual.width, actual.height, golden.width, golden.height);
        return EXIT_FAIL;
    }

    const uint32_t total_pixels = actual.width * actual.height;
    const float threshold = args.tolerance * 255.0F;
    uint32_t failing_pixels = 0;

    for (uint32_t i = 0; i < total_pixels; ++i) {
        const size_t offset = static_cast<size_t>(i) * 4;
        bool pixel_ok = true;
        for (int c = 0; c < 4; ++c) {
            float diff = std::fabs(static_cast<float>(actual.pixels[offset + static_cast<size_t>(c)]) -
                                   static_cast<float>(golden.pixels[offset + static_cast<size_t>(c)]));
            if (diff > threshold) {
                pixel_ok = false;
                break;
            }
        }
        if (!pixel_ok) {
            ++failing_pixels;
        }
    }

    if (failing_pixels == 0) {
        std::fprintf(stderr, "PASS: images match (tolerance=%.2f)\n",
                     static_cast<double>(args.tolerance));
        return EXIT_PASS;
    }

    std::fprintf(stderr, "FAIL: images diverged (%u failing pixels out of %u)\n", failing_pixels,
                 total_pixels);
    return EXIT_FAIL;
}

auto run_assert_clean(int argc, char** argv) -> int {
    auto args_result = parse_assert_preset_args(argc, argv, /*is_clean=*/true);
    if (!args_result) {
        std::fprintf(stderr, "Error: %s\n", args_result.error().message.c_str());
        print_assert_clean_usage();
        return EXIT_ERROR;
    }
    auto args = std::move(*args_result);

    if (!std::filesystem::exists(args.preset)) {
        std::fprintf(stderr, "Error: preset file not found: %s\n", args.preset.c_str());
        return EXIT_ERROR;
    }

    auto vk_result = VulkanBackend::create(args.verbose);
    if (!vk_result) {
        std::fprintf(stderr, "Error: Vulkan init failed: %s\n",
                     vk_result.error().message.c_str());
        return EXIT_ERROR;
    }

    auto summary_result = create_fc_pipeline(*vk_result, args);
    if (!summary_result) {
        std::fprintf(stderr, "Error: %s\n", summary_result.error().message.c_str());
        return EXIT_ERROR;
    }
    auto summary = *summary_result;

    if (summary.error_count == 0 && summary.warning_count == 0) {
        std::fprintf(stderr, "PASS: preset has no authoring errors\n");
        return EXIT_PASS;
    }

    std::fprintf(stderr, "FAIL: preset has authoring issues (%u errors, %u warnings)\n",
                 summary.error_count, summary.warning_count);
    return EXIT_FAIL;
}

auto run_assert_no_degradation(int argc, char** argv) -> int {
    auto args_result = parse_assert_preset_args(argc, argv, /*is_clean=*/false);
    if (!args_result) {
        std::fprintf(stderr, "Error: %s\n", args_result.error().message.c_str());
        print_assert_no_degradation_usage();
        return EXIT_ERROR;
    }
    auto args = std::move(*args_result);

    if (!std::filesystem::exists(args.preset)) {
        std::fprintf(stderr, "Error: preset file not found: %s\n", args.preset.c_str());
        return EXIT_ERROR;
    }

    auto vk_result = VulkanBackend::create(args.verbose);
    if (!vk_result) {
        std::fprintf(stderr, "Error: Vulkan init failed: %s\n",
                     vk_result.error().message.c_str());
        return EXIT_ERROR;
    }

    auto summary_result = create_fc_pipeline(*vk_result, args);
    if (!summary_result) {
        std::fprintf(stderr, "Error: %s\n", summary_result.error().message.c_str());
        return EXIT_ERROR;
    }
    auto summary = *summary_result;

    if (summary.warning_count == 0) {
        std::fprintf(stderr, "PASS: no degradation detected\n");
        return EXIT_PASS;
    }

    std::fprintf(stderr, "FAIL: degradation detected (%u warnings)\n", summary.warning_count);
    return EXIT_FAIL;
}

} // namespace goggles::cli
