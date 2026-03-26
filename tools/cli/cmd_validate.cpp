#include "cmd_validate.hpp"

#include "vulkan_backend.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <goggles/filter_chain.hpp>
#include <string>
#include <vulkan/vulkan.h>

#include "diagnostics/diagnostic_report.hpp"
#include "diagnostics/diagnostic_report_json.hpp"

namespace {

constexpr int EXIT_PASS = 0;
constexpr int EXIT_DEGRADED = 1;
constexpr int EXIT_FAIL = 2;

struct ValidateArgs {
    std::string preset;
    bool json = false;
    bool verbose = false;
};

void print_validate_usage() {
    std::fprintf(stderr,
                 "Usage: goggles-chain-cli validate <preset.slangp> [--json] [--verbose]\n"
                 "\n"
                 "Compile a preset through shader compilation and reflection without rendering.\n"
                 "Reports authoring findings and exits with a meaningful code.\n"
                 "\n"
                 "Positional:\n"
                 "  <preset.slangp>         RetroArch preset file\n"
                 "\n"
                 "Options:\n"
                 "  --json                  Emit full DiagnosticReport JSON to stdout\n"
                 "  --verbose               Enable diagnostic logging to stderr\n"
                 "  --help                  Show this usage message\n"
                 "\n"
                 "Exit codes:\n"
                 "  0  pass      No errors or warnings\n"
                 "  1  degraded  Warnings or fallbacks present\n"
                 "  2  fail      Errors that prevent correct rendering, or tool errors\n");
}

auto parse_validate_args(int argc, char** argv) -> goggles::Result<ValidateArgs> {
    ValidateArgs args;

    for (int i = 0; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help") {
            print_validate_usage();
            std::exit(EXIT_PASS);
        }
        if (arg == "--json") {
            args.json = true;
            continue;
        }
        if (arg == "--verbose") {
            args.verbose = true;
            continue;
        }
        if (arg.starts_with("-")) {
            return goggles::make_error<ValidateArgs>(goggles::ErrorCode::invalid_data,
                                                     "Unknown option: " + arg);
        }
        if (args.preset.empty()) {
            args.preset = arg;
        } else {
            return goggles::make_error<ValidateArgs>(goggles::ErrorCode::invalid_data,
                                                     "Unexpected argument: " + arg);
        }
    }

    if (args.preset.empty()) {
        return goggles::make_error<ValidateArgs>(goggles::ErrorCode::invalid_data,
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

auto determine_verdict(const goggles_fc_diagnostic_summary_t& summary)
    -> const char* {
    if (summary.error_count > 0) {
        return "fail";
    }
    if (summary.warning_count > 0) {
        return "degraded";
    }
    return "pass";
}

auto determine_exit_code(const goggles_fc_diagnostic_summary_t& summary) -> int {
    if (summary.error_count > 0) {
        return EXIT_FAIL;
    }
    if (summary.warning_count > 0) {
        return EXIT_DEGRADED;
    }
    return EXIT_PASS;
}

auto build_minimal_report(const goggles_fc_diagnostic_summary_t& summary,
                          const goggles_fc_program_report_t& program_report,
                          const goggles_fc_program_source_info_t& source_info)
    -> goggles::diagnostics::DiagnosticReport {
    goggles::diagnostics::DiagnosticReport report{};
    report.reporting_mode = goggles::diagnostics::CaptureMode::standard;

    report.session_identity.preset_hash = "";
    report.session_identity.expanded_source_hash = "";
    report.session_identity.compiled_contract_hash = "";
    report.session_identity.generation_id = 0;
    report.session_identity.frame_start = 0;
    report.session_identity.frame_end = 0;
    report.session_identity.capture_mode = "validate";
    report.session_identity.environment_fingerprint = "";

    if (summary.error_count > 0) {
        goggles::diagnostics::AuthoringVerdict verdict{};
        verdict.result = goggles::diagnostics::VerdictResult::fail;
        report.authoring_verdict = verdict;
        report.degraded = false;
    } else if (summary.warning_count > 0) {
        goggles::diagnostics::AuthoringVerdict verdict{};
        verdict.result = goggles::diagnostics::VerdictResult::degraded;
        report.authoring_verdict = verdict;
        report.degraded = true;
    } else {
        goggles::diagnostics::AuthoringVerdict verdict{};
        verdict.result = goggles::diagnostics::VerdictResult::pass;
        report.authoring_verdict = verdict;
        report.degraded = false;
    }

    report.error_counts_by_category.authoring = summary.error_count;

    // Store pass count info from program report — no manifest available from public API
    (void)program_report;
    (void)source_info;

    return report;
}

auto build_failure_report(const std::string& error_message) -> goggles::diagnostics::DiagnosticReport {
    goggles::diagnostics::DiagnosticReport report{};
    report.reporting_mode = goggles::diagnostics::CaptureMode::standard;

    report.session_identity.capture_mode = "validate";

    goggles::diagnostics::AuthoringVerdict verdict{};
    verdict.result = goggles::diagnostics::VerdictResult::fail;
    goggles::diagnostics::AuthoringFinding finding{};
    finding.severity = goggles::diagnostics::Severity::error;
    finding.localization.pass_ordinal = 0;
    finding.localization.stage = "compilation";
    finding.message = error_message;
    verdict.findings.push_back(finding);
    report.authoring_verdict = verdict;
    report.degraded = false;

    report.error_counts_by_category.authoring = 1;

    return report;
}

auto emit_failure(const std::string& msg, bool json) -> int {
    std::fprintf(stderr, "Error: %s\n", msg.c_str());
    if (json) {
        auto report = build_failure_report(msg);
        std::printf("%s\n", goggles::diagnostics::serialize_report_json(report).c_str());
    }
    return EXIT_FAIL;
}

auto create_fc_pipeline(goggles::cli::VulkanBackend& vk, const ValidateArgs& args)
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

auto run_validate(int argc, char** argv) -> int {
    auto args_result = parse_validate_args(argc, argv);
    if (!args_result) {
        std::fprintf(stderr, "Error: %s\n", args_result.error().message.c_str());
        print_validate_usage();
        return EXIT_FAIL;
    }
    auto args = std::move(*args_result);

    if (!std::filesystem::exists(args.preset)) {
        return emit_failure("Preset file not found: " + args.preset, args.json);
    }

    auto vk_result = VulkanBackend::create(args.verbose);
    if (!vk_result) {
        return emit_failure("Vulkan init failed: " + vk_result.error().message, args.json);
    }

    auto summary_result = create_fc_pipeline(*vk_result, args);
    if (!summary_result) {
        if (!args.json) {
            std::fprintf(stderr, "Error: %s\n", summary_result.error().message.c_str());
            std::fprintf(stderr, "---\nverdict: fail\n");
        } else {
            auto report = build_failure_report(summary_result.error().message);
            std::printf("%s\n", goggles::diagnostics::serialize_report_json(report).c_str());
        }
        return EXIT_FAIL;
    }
    auto summary = *summary_result;

    if (args.json) {
        goggles_fc_program_report_t program_report{};
        goggles_fc_program_source_info_t source_info{};
        auto report = build_minimal_report(summary, program_report, source_info);
        std::printf("%s\n", goggles::diagnostics::serialize_report_json(report).c_str());
    } else {
        std::fprintf(stderr, "---\nverdict: %s (%u error, %u warning, %u info)\n",
                     determine_verdict(summary), summary.error_count, summary.warning_count,
                     summary.info_count);
    }

    return determine_exit_code(summary);
}

} // namespace goggles::cli
