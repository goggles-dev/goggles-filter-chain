#include "cmd_capture.hpp"

#include "image_io.hpp"
#include "vulkan_backend.hpp"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <goggles/filter_chain.hpp>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "capture/renderdoc_bridge.hpp"
#include "capture/runtime_capture.hpp"

namespace {

constexpr int EXIT_OK = 0;
constexpr int EXIT_FAIL = 2;

struct CaptureArgs {
    std::string preset;
    std::string output_dir;
    std::vector<std::string> inputs;
    uint32_t frames = 1;
    bool verbose = false;
};

void print_capture_usage() {
    std::fprintf(
        stderr,
        "Usage: goggles-chain-cli capture <preset.slangp> <input-images...> --output <dir>\n"
        "                                 [--frames <N>] [--verbose]\n"
        "\n"
        "Run the filter chain with RenderDoc capture support.\n"
        "\n"
        "When RenderDoc is injected (via renderdoccmd or LD_PRELOAD), this command\n"
        "sets a capture file path template and brackets the render with start/end\n"
        "frame capture calls. The resulting .rdc file is placed alongside the\n"
        "diagnostic bundle in the output directory.\n"
        "\n"
        "Without RenderDoc, behaves identically to 'diagnose' (produces JSON + PNGs).\n"
        "\n"
        "Positional:\n"
        "  <preset.slangp>         RetroArch preset file\n"
        "  <input-images...>       One or more input images (PNG/JPG)\n"
        "\n"
        "Required:\n"
        "  --output <dir>          Output directory for capture bundle\n"
        "\n"
        "Optional:\n"
        "  --frames <N>            Number of frames to render (default: 1)\n"
        "  --verbose               Enable diagnostic logging to stderr\n"
        "  --help                  Show this usage message\n"
        "\n"
        "Output bundle:\n"
        "  output/report.json              Forensic DiagnosticReport\n"
        "  output/pass_0_frame0.png        Intermediate output per pass\n"
        "  output/final_frame0.png         Final chain output\n"
        "  output/capture.rdc              RenderDoc capture (when RenderDoc is active)\n");
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

auto parse_capture_args(int argc, char** argv) -> goggles::Result<CaptureArgs> {
    CaptureArgs args;

    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_capture_usage();
            std::exit(EXIT_OK);
        }
        if (arg == "--verbose") {
            args.verbose = true;
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
                return goggles::make_error<CaptureArgs>(goggles::ErrorCode::invalid_data,
                                                        "--frames must be >= 1");
            }
            continue;
        }
        if (arg.starts_with("-")) {
            return goggles::make_error<CaptureArgs>(goggles::ErrorCode::invalid_data,
                                                    "Unknown option: " + arg);
        }
        // First positional is the preset, rest are input images
        if (args.preset.empty()) {
            args.preset = arg;
        } else {
            args.inputs.push_back(arg);
        }
    }

    if (args.preset.empty()) {
        return goggles::make_error<CaptureArgs>(goggles::ErrorCode::invalid_data,
                                                "Missing required preset path");
    }
    if (args.inputs.empty()) {
        return goggles::make_error<CaptureArgs>(goggles::ErrorCode::invalid_data,
                                                "No input images specified");
    }
    if (args.output_dir.empty()) {
        return goggles::make_error<CaptureArgs>(goggles::ErrorCode::invalid_data,
                                                "Missing required --output directory");
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

/// Query the pass count by creating a temporary FC pipeline through the public API.
auto discover_pass_count(goggles::cli::VulkanBackend& vk, const CaptureArgs& args)
    -> goggles::Result<uint32_t> {
    auto instance_info = goggles_fc_instance_create_info_init();
    if (args.verbose) {
        instance_info.log_callback = log_callback;
    }

    auto fc_instance = goggles::filter_chain::Instance::create(&instance_info);
    if (!fc_instance) {
        return goggles::make_error<uint32_t>(fc_instance.error().code,
                                             "FC instance: " + fc_instance.error().message);
    }

    auto device_info = goggles_fc_vk_device_create_info_init();
    device_info.physical_device = vk.physical_device;
    device_info.device = vk.device;
    device_info.graphics_queue = vk.queue;
    device_info.graphics_queue_family_index = vk.queue_family_index;

    auto fc_device = goggles::filter_chain::Device::create(*fc_instance, &device_info);
    if (!fc_device) {
        return goggles::make_error<uint32_t>(fc_device.error().code,
                                             "FC device: " + fc_device.error().message);
    }

    auto source = goggles_fc_preset_source_init();
    source.kind = GOGGLES_FC_PRESET_SOURCE_FILE;
    source.path = {.data = args.preset.c_str(), .size = args.preset.size()};

    auto fc_program = goggles::filter_chain::Program::create(*fc_device, &source);
    if (!fc_program) {
        return goggles::make_error<uint32_t>(fc_program.error().code,
                                             "Compilation: " + fc_program.error().message);
    }

    auto report = fc_program->get_report();
    if (!report) {
        return goggles::make_error<uint32_t>(report.error().code,
                                             "Report: " + report.error().message);
    }

    return report->pass_count;
}

/// Build a RuntimeCapturePlan from parsed arguments and the loaded source image.
auto build_capture_plan(const CaptureArgs& args, goggles::cli::ImageData& image,
                        uint32_t pass_count) -> goggles::fc::RuntimeCapturePlan {
    std::vector<uint32_t> frame_indices;
    frame_indices.reserve(args.frames);
    for (uint32_t i = 0; i < args.frames; ++i) {
        frame_indices.push_back(i);
    }

    std::vector<uint32_t> pass_ordinals;
    pass_ordinals.reserve(pass_count);
    for (uint32_t i = 0; i < pass_count; ++i) {
        pass_ordinals.push_back(i);
    }

    goggles::fc::RuntimeCapturePlan plan;
    plan.preset_path = args.preset;
    plan.preset_name = std::filesystem::path(args.preset).stem().string();
    plan.frame_indices = std::move(frame_indices);
    plan.pass_ordinals = std::move(pass_ordinals);
    plan.source_extent = {.width = image.width, .height = image.height};
    plan.target_extent = {.width = image.width, .height = image.height};
    plan.source_pixels = std::move(image.pixels);
    plan.forensic_diagnostics = true;
    return plan;
}

/// Write the capture result bundle (report.json + images) to the output directory.
auto write_capture_bundle(const goggles::fc::RuntimeCaptureResult& result,
                          const std::filesystem::path& output_dir, bool verbose) -> int {
    if (!result.diagnostic_report_json.empty()) {
        const auto report_path = output_dir / "report.json";
        auto* file = std::fopen(report_path.string().c_str(), "w");
        if (file == nullptr) {
            std::fprintf(stderr, "Error: Failed to write report.json\n");
            return EXIT_FAIL;
        }
        std::fprintf(file, "%s\n", result.diagnostic_report_json.c_str());
        std::fclose(file);
        if (verbose) {
            std::fprintf(stderr, "Wrote: %s\n", report_path.string().c_str());
        }
    }

    std::error_code ec;
    for (const auto& [frame, src_path] : result.final_frames) {
        const auto dst_name = "final_frame" + std::to_string(frame) + ".png";
        const auto dst_path = output_dir / dst_name;
        std::filesystem::copy_file(src_path, dst_path,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::fprintf(stderr, "Error: Failed to copy %s: %s\n", dst_name.c_str(),
                         ec.message().c_str());
            return EXIT_FAIL;
        }
        if (verbose) {
            std::fprintf(stderr, "Wrote: %s\n", dst_path.string().c_str());
        }
    }

    for (const auto& [frame, pass_map] : result.pass_frames) {
        for (const auto& [ordinal, src_path] : pass_map) {
            const auto dst_name =
                "pass_" + std::to_string(ordinal) + "_frame" + std::to_string(frame) + ".png";
            const auto dst_path = output_dir / dst_name;
            std::filesystem::copy_file(src_path, dst_path,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                std::fprintf(stderr, "Error: Failed to copy %s: %s\n", dst_name.c_str(),
                             ec.message().c_str());
                return EXIT_FAIL;
            }
            if (verbose) {
                std::fprintf(stderr, "Wrote: %s\n", dst_path.string().c_str());
            }
        }
    }

    return EXIT_OK;
}

} // namespace

namespace goggles::cli {

auto run_capture(int argc, char** argv) -> int {
    auto args_result = parse_capture_args(argc, argv);
    if (!args_result) {
        std::fprintf(stderr, "Error: %s\n", args_result.error().message.c_str());
        print_capture_usage();
        return EXIT_FAIL;
    }
    auto args = std::move(*args_result);

    if (!std::filesystem::exists(args.preset)) {
        std::fprintf(stderr, "Error: Preset file not found: %s\n", args.preset.c_str());
        return EXIT_FAIL;
    }

    auto paths_result = collect_input_images(args.inputs);
    if (!paths_result) {
        std::fprintf(stderr, "Error: %s\n", paths_result.error().message.c_str());
        return EXIT_FAIL;
    }

    auto image_result = load_image((*paths_result)[0]);
    if (!image_result) {
        std::fprintf(stderr, "Error: %s\n", image_result.error().message.c_str());
        return EXIT_FAIL;
    }
    auto image = std::move(*image_result);

    std::error_code ec;
    std::filesystem::create_directories(args.output_dir, ec);
    if (ec) {
        std::fprintf(stderr, "Error: Failed to create output directory '%s': %s\n",
                     args.output_dir.c_str(), ec.message().c_str());
        return EXIT_FAIL;
    }

    // Initialize the RenderDoc bridge (graceful no-op if unavailable)
    auto renderdoc = goggles::fc::RenderDocBridge::create();
    if (renderdoc.available()) {
        std::fprintf(stderr, "RenderDoc detected; capture will be saved to output directory.\n");
        auto capture_template = (std::filesystem::path(args.output_dir) / "capture").string();
        renderdoc.set_capture_path(capture_template.c_str());
    } else {
        std::fprintf(stderr, "RenderDoc not detected; proceeding without GPU capture.\n");
    }

    auto vk_result = VulkanBackend::create(args.verbose);
    if (!vk_result) {
        std::fprintf(stderr, "Error: Vulkan init failed: %s\n",
                     vk_result.error().message.c_str());
        return EXIT_FAIL;
    }

    auto pass_count_result = discover_pass_count(*vk_result, args);
    if (!pass_count_result) {
        std::fprintf(stderr, "Error: %s\n", pass_count_result.error().message.c_str());
        return EXIT_FAIL;
    }

    auto plan = build_capture_plan(args, image, *pass_count_result);

    if (args.verbose) {
        std::fprintf(stderr, "Capturing %u frame(s) with %u pass(es) for preset '%s'\n",
                     args.frames, *pass_count_result, plan.preset_name.c_str());
    }

    // Bracket the runtime capture with RenderDoc start/end if available.
    // The capture module creates its own VkDevice internally, which RenderDoc
    // hooks via LD_PRELOAD. We pass VK_NULL_HANDLE to capture all devices.
    if (renderdoc.available()) {
        renderdoc.start_capture(VK_NULL_HANDLE);
    }

    auto capture_result = goggles::fc::capture_runtime_outputs(plan);

    if (renderdoc.available()) {
        renderdoc.end_capture(VK_NULL_HANDLE);
    }

    if (!capture_result) {
        std::fprintf(stderr, "Error: Capture failed: %s\n",
                     capture_result.error().message.c_str());
        return EXIT_FAIL;
    }

    const auto output_dir = std::filesystem::path(args.output_dir);
    int write_status = write_capture_bundle(*capture_result, output_dir, args.verbose);
    if (write_status != EXIT_OK) {
        return write_status;
    }

    std::fprintf(stderr, "Capture bundle written to: %s\n", args.output_dir.c_str());
    if (renderdoc.available()) {
        std::fprintf(stderr, "RenderDoc .rdc file saved with template: %s\n",
                     (output_dir / "capture").string().c_str());
    }
    return EXIT_OK;
}

} // namespace goggles::cli
