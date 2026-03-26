#include "cmd_assert.hpp"
#include "cmd_capture.hpp"
#include "cmd_diagnose.hpp"
#include "cmd_validate.hpp"
#include "image_io.hpp"
#include "subcommand.hpp"
#include "vulkan_backend.hpp"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <goggles/filter_chain.hpp>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

namespace {

constexpr int EXIT_OK = 0;
constexpr int EXIT_PARTIAL = 1;
constexpr int EXIT_FATAL = 2;

struct CliArgs {
    std::string preset;
    std::string output_dir;
    std::vector<std::string> inputs;
    std::vector<std::pair<std::string, float>> params;
    uint32_t frames = 1;
    uint32_t scale = 0;
    uint32_t output_width = 0;
    uint32_t output_height = 0;
    bool verbose = false;
};

void print_usage() {
    std::fprintf(stderr,
                 "Usage: goggles-chain-cli --preset <path.slangp> --output <dir> [options] "
                 "<input...>\n"
                 "\n"
                 "Positional:\n"
                 "  <input...>              One or more image files, or a directory (PNG/JPG)\n"
                 "\n"
                 "Required:\n"
                 "  --preset <path>         RetroArch preset file (.slangp)\n"
                 "  --output <dir>          Output directory (created if missing)\n"
                 "\n"
                 "Optional:\n"
                 "  --frames <N>            Render N frames per image (default: 1)\n"
                 "  --scale <factor>        Output scale factor (e.g., 2 for 2x upscale)\n"
                 "  --output-size <WxH>     Explicit output dimensions (overrides --scale)\n"
                 "  --param <name=value>    Control override (repeatable)\n"
                 "  --verbose               Enable diagnostic logging\n"
                 "  --help                  Show usage\n"
                 "  --version               Show version\n");
}

void print_version() {
    std::fprintf(stderr, "goggles-chain-cli 0.1.0 (filter-chain API %u)\n",
                 goggles::filter_chain::get_api_version());
}

auto parse_uint32(const char* str) -> goggles::Result<uint32_t> {
    uint32_t value = 0;
    auto [ptr, ec] = std::from_chars(str, str + std::strlen(str), value);
    if (ec != std::errc{} || *ptr != '\0') {
        return goggles::make_error<uint32_t>(goggles::ErrorCode::invalid_data,
                                             std::string("Invalid integer: ") + str);
    }
    return value;
}

auto parse_float(const char* str) -> goggles::Result<float> {
    float value = 0.0F;
    auto [ptr, ec] = std::from_chars(str, str + std::strlen(str), value);
    if (ec != std::errc{} || *ptr != '\0') {
        return goggles::make_error<float>(goggles::ErrorCode::invalid_data,
                                          std::string("Invalid number: ") + str);
    }
    return value;
}

auto parse_args(int argc, char** argv) -> goggles::Result<CliArgs> {
    CliArgs args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_usage();
            std::exit(EXIT_OK);
        }
        if (arg == "--version") {
            print_version();
            std::exit(EXIT_OK);
        }
        if (arg == "--verbose") {
            args.verbose = true;
            continue;
        }
        if (arg == "--preset" && i + 1 < argc) {
            args.preset = argv[++i];
            continue;
        }
        if (arg == "--output" && i + 1 < argc) {
            args.output_dir = argv[++i];
            continue;
        }
        if (arg == "--frames" && i + 1 < argc) {
            auto frames_result = parse_uint32(argv[++i]);
            if (!frames_result) {
                return nonstd::make_unexpected(frames_result.error());
            }
            args.frames = *frames_result;
            if (args.frames == 0) {
                return goggles::make_error<CliArgs>(goggles::ErrorCode::invalid_data,
                                                    "--frames must be >= 1");
            }
            continue;
        }
        if (arg == "--scale" && i + 1 < argc) {
            auto scale_result = parse_uint32(argv[++i]);
            if (!scale_result) {
                return nonstd::make_unexpected(scale_result.error());
            }
            args.scale = *scale_result;
            if (args.scale == 0) {
                return goggles::make_error<CliArgs>(goggles::ErrorCode::invalid_data,
                                                    "--scale must be >= 1");
            }
            continue;
        }
        if (arg == "--output-size" && i + 1 < argc) {
            std::string size_str = argv[++i];
            auto x_pos = size_str.find('x');
            if (x_pos == std::string::npos) {
                x_pos = size_str.find('X');
            }
            if (x_pos == std::string::npos) {
                return goggles::make_error<CliArgs>(goggles::ErrorCode::invalid_data,
                                                    "--output-size must be WxH");
            }
            auto w_result = parse_uint32(size_str.substr(0, x_pos).c_str());
            if (!w_result) {
                return nonstd::make_unexpected(w_result.error());
            }
            auto h_result = parse_uint32(size_str.substr(x_pos + 1).c_str());
            if (!h_result) {
                return nonstd::make_unexpected(h_result.error());
            }
            args.output_width = *w_result;
            args.output_height = *h_result;
            if (args.output_width == 0 || args.output_height == 0) {
                return goggles::make_error<CliArgs>(goggles::ErrorCode::invalid_data,
                                                    "--output-size dimensions must be > 0");
            }
            continue;
        }
        if (arg == "--param" && i + 1 < argc) {
            std::string param_str = argv[++i];
            const auto eq_pos = param_str.find('=');
            if (eq_pos == std::string::npos) {
                return goggles::make_error<CliArgs>(goggles::ErrorCode::invalid_data,
                                                    "--param must be name=value");
            }
            auto name = param_str.substr(0, eq_pos);
            auto val_result = parse_float(param_str.substr(eq_pos + 1).c_str());
            if (!val_result) {
                return nonstd::make_unexpected(val_result.error());
            }
            args.params.emplace_back(std::move(name), *val_result);
            continue;
        }
        if (arg.starts_with("-")) {
            return goggles::make_error<CliArgs>(goggles::ErrorCode::invalid_data,
                                                "Unknown option: " + arg);
        }
        args.inputs.push_back(arg);
    }

    if (args.preset.empty()) {
        return goggles::make_error<CliArgs>(goggles::ErrorCode::invalid_data,
                                            "Missing required --preset");
    }
    if (args.output_dir.empty()) {
        return goggles::make_error<CliArgs>(goggles::ErrorCode::invalid_data,
                                            "Missing required --output");
    }
    if (args.inputs.empty()) {
        return goggles::make_error<CliArgs>(goggles::ErrorCode::invalid_data,
                                            "No input files specified");
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

auto compute_output_extent(const CliArgs& args, uint32_t input_width, uint32_t input_height)
    -> std::pair<uint32_t, uint32_t> {
    if (args.output_width != 0 && args.output_height != 0) {
        return {args.output_width, args.output_height};
    }
    if (args.scale > 0) {
        return {input_width * args.scale, input_height * args.scale};
    }
    return {input_width, input_height};
}

auto process_image(goggles::cli::VulkanBackend& vk,
                   goggles::filter_chain::Device& fc_device,
                   goggles::filter_chain::Program& fc_program, const CliArgs& args,
                   const std::filesystem::path& input_path, // NOLINT(bugprone-easily-swappable-parameters)
                   const std::filesystem::path& output_path) -> goggles::Result<void> {
    auto image_result = goggles::cli::load_image(input_path);
    if (!image_result) {
        return nonstd::make_unexpected(image_result.error());
    }
    auto image = std::move(*image_result);
    const auto [out_w, out_h] = compute_output_extent(args, image.width, image.height);

    auto source_result = vk.create_image(
        {.width = image.width, .height = image.height},
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    if (!source_result) {
        return nonstd::make_unexpected(source_result.error());
    }
    auto source = std::move(*source_result);

    auto target_result = vk.create_image(
        {.width = out_w, .height = out_h},
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    if (!target_result) {
        return nonstd::make_unexpected(target_result.error());
    }
    auto target = std::move(*target_result);

    auto upload_result = vk.upload_image(image.pixels.data(), image.width, image.height, source);
    if (!upload_result) {
        return nonstd::make_unexpected(upload_result.error());
    }

    // Create chain for this image
    auto chain_info = goggles_fc_chain_create_info_init();
    chain_info.target_format = VK_FORMAT_R8G8B8A8_UNORM;
    chain_info.frames_in_flight = 1;
    chain_info.initial_stage_mask = GOGGLES_FC_STAGE_MASK_ALL;
    auto chain_result =
        goggles::filter_chain::Chain::create(fc_device, fc_program, &chain_info);
    if (!chain_result) {
        return nonstd::make_unexpected(chain_result.error());
    }
    auto chain = std::move(*chain_result);

    // Apply parameter overrides
    for (const auto& [name, value] : args.params) {
        // Try effect stage first, then prechain
        auto result = chain.set_control_value_f32(goggles::filter_chain::Stage::effect, name,
                                                  value);
        if (!result) {
            result = chain.set_control_value_f32(goggles::filter_chain::Stage::prechain, name,
                                                 value);
        }
        if (!result) {
            return goggles::make_error<void>(goggles::ErrorCode::invalid_data,
                                             "Parameter not found: " + name);
        }
    }

    // Transition target to color attachment before first frame
    {
        auto cmd_result = vk.begin_commands();
        if (!cmd_result) {
            return nonstd::make_unexpected(cmd_result.error());
        }
        vk.transition_image(*cmd_result, target.image, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        auto submit_result = vk.submit_and_wait(*cmd_result);
        if (!submit_result) {
            return nonstd::make_unexpected(submit_result.error());
        }
    }

    // Render frames
    for (uint32_t frame = 0; frame < args.frames; ++frame) {
        auto cmd_result = vk.begin_commands();
        if (!cmd_result) {
            return nonstd::make_unexpected(cmd_result.error());
        }

        auto record_info = goggles_fc_record_info_vk_init();
        record_info.command_buffer = *cmd_result;
        record_info.source_image = source.image;
        record_info.source_view = source.view;
        record_info.source_extent = {.width = image.width, .height = image.height};
        record_info.target_view = target.view;
        record_info.target_extent = {.width = out_w, .height = out_h};
        record_info.frame_index = 0;

        auto record_result = chain.record_vk(&record_info);
        if (!record_result) {
            return nonstd::make_unexpected(record_result.error());
        }
        auto submit_result = vk.submit_and_wait(*cmd_result);
        if (!submit_result) {
            return nonstd::make_unexpected(submit_result.error());
        }
    }

    // Download result
    std::vector<uint8_t> result_pixels;
    auto download_result = vk.download_image(target, out_w, out_h,
                                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                             result_pixels);
    if (!download_result) {
        return nonstd::make_unexpected(download_result.error());
    }

    return goggles::cli::save_png(output_path, result_pixels.data(), out_w, out_h);
}

} // namespace

auto run_process(int argc, char** argv) -> int {
    auto args_result = parse_args(argc, argv);
    if (!args_result) {
        std::fprintf(stderr, "Error: %s\n", args_result.error().message.c_str());
        print_usage();
        return EXIT_FATAL;
    }
    auto args = std::move(*args_result);

    auto paths_result = goggles::cli::collect_input_images(args.inputs);
    if (!paths_result) {
        std::fprintf(stderr, "Error: %s\n", paths_result.error().message.c_str());
        return EXIT_FATAL;
    }
    auto input_paths = std::move(*paths_result);

    // Create output directory
    std::error_code ec;
    std::filesystem::create_directories(args.output_dir, ec);
    if (ec) {
        std::fprintf(stderr, "Error: Failed to create output directory '%s': %s\n",
                     args.output_dir.c_str(), ec.message().c_str());
        return EXIT_FATAL;
    }

    // Vulkan bootstrap
    auto vk_result = goggles::cli::VulkanBackend::create(args.verbose);
    if (!vk_result) {
        std::fprintf(stderr, "Error: %s\n", vk_result.error().message.c_str());
        return EXIT_FATAL;
    }
    auto vk = std::move(*vk_result);

    // FC instance
    auto instance_info = goggles_fc_instance_create_info_init();
    if (args.verbose) {
        instance_info.log_callback = log_callback;
    }

    auto fc_instance_result = goggles::filter_chain::Instance::create(&instance_info);
    if (!fc_instance_result) {
        std::fprintf(stderr, "Error: %s\n", fc_instance_result.error().message.c_str());
        return EXIT_FATAL;
    }
    auto fc_instance = std::move(*fc_instance_result);

    // FC device
    auto device_info = goggles_fc_vk_device_create_info_init();
    device_info.physical_device = vk.physical_device;
    device_info.device = vk.device;
    device_info.graphics_queue = vk.queue;
    device_info.graphics_queue_family_index = vk.queue_family_index;

    auto fc_device_result = goggles::filter_chain::Device::create(fc_instance, &device_info);
    if (!fc_device_result) {
        std::fprintf(stderr, "Error: %s\n", fc_device_result.error().message.c_str());
        return EXIT_FATAL;
    }
    auto fc_device = std::move(*fc_device_result);

    // FC program (shader compilation — once for all images)
    auto source = goggles_fc_preset_source_init();
    source.kind = GOGGLES_FC_PRESET_SOURCE_FILE;
    source.path = {.data = args.preset.c_str(), .size = args.preset.size()};

    auto fc_program_result = goggles::filter_chain::Program::create(fc_device, &source);
    if (!fc_program_result) {
        std::fprintf(stderr, "Error: Failed to load preset '%s': %s\n", args.preset.c_str(),
                     fc_program_result.error().message.c_str());
        return EXIT_FATAL;
    }
    auto fc_program = std::move(*fc_program_result);

    // Process each image
    uint32_t failures = 0;
    for (const auto& input_path : input_paths) {
        auto output_path =
            std::filesystem::path(args.output_dir) / input_path.filename().replace_extension(".png");

        auto result = process_image(vk, fc_device, fc_program, args, input_path, output_path);
        if (!result) {
            std::fprintf(stderr, "Error processing '%s': %s\n", input_path.string().c_str(),
                         result.error().message.c_str());
            ++failures;
            continue;
        }

        if (args.verbose) {
            std::fprintf(stderr, "Processed: %s -> %s\n", input_path.string().c_str(),
                         output_path.string().c_str());
        }
    }

    if (static_cast<size_t>(failures) == input_paths.size()) {
        return EXIT_FATAL;
    }
    if (failures > 0) {
        return EXIT_PARTIAL;
    }
    return EXIT_OK;
}

auto run_validate(int argc, char** argv) -> int {
    return goggles::cli::run_validate(argc, argv);
}

auto run_diagnose(int argc, char** argv) -> int {
    return goggles::cli::run_diagnose(argc, argv);
}

auto run_assert_image(int argc, char** argv) -> int {
    return goggles::cli::run_assert_image(argc, argv);
}

auto run_assert_clean(int argc, char** argv) -> int {
    return goggles::cli::run_assert_clean(argc, argv);
}

auto run_assert_no_degradation(int argc, char** argv) -> int {
    return goggles::cli::run_assert_no_degradation(argc, argv);
}

auto run_capture(int argc, char** argv) -> int {
    return goggles::cli::run_capture(argc, argv);
}

auto main(int argc, char** argv) -> int {
    auto [command, first_arg] = goggles::cli::parse_subcommand(argc, argv);

    switch (command) {
    case goggles::cli::Subcommand::process:
        return run_process(argc, argv);
    case goggles::cli::Subcommand::validate:
        return run_validate(argc - first_arg, argv + first_arg);
    case goggles::cli::Subcommand::diagnose:
        return run_diagnose(argc - first_arg, argv + first_arg);
    case goggles::cli::Subcommand::assert_image:
        return run_assert_image(argc - first_arg, argv + first_arg);
    case goggles::cli::Subcommand::assert_clean:
        return run_assert_clean(argc - first_arg, argv + first_arg);
    case goggles::cli::Subcommand::assert_no_degradation:
        return run_assert_no_degradation(argc - first_arg, argv + first_arg);
    case goggles::cli::Subcommand::capture:
        return run_capture(argc - first_arg, argv + first_arg);
    }
    return 2;
}
