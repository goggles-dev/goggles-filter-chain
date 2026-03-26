#include "diagnostics/diagnostic_report.hpp"
#include "diagnostics/diagnostic_report_json.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace goggles::diagnostics;

TEST_CASE("serialize_report_json includes version", "[diagnostic_report_json]") {
    DiagnosticReport report{};
    auto json = serialize_report_json(report);
    REQUIRE(json.find("\"version\":1") != std::string::npos);
}

TEST_CASE("serialize_report_json includes session identity", "[diagnostic_report_json]") {
    DiagnosticReport report{};
    report.session_identity.preset_hash = "abc123";
    report.session_identity.generation_id = 7;
    auto json = serialize_report_json(report);
    REQUIRE(json.find("\"preset_hash\":\"abc123\"") != std::string::npos);
    REQUIRE(json.find("\"generation_id\":7") != std::string::npos);
}

TEST_CASE("serialize_report_json includes verdict", "[diagnostic_report_json]") {
    DiagnosticReport report{};
    AuthoringVerdict verdict{};
    verdict.result = VerdictResult::degraded;
    AuthoringFinding finding{};
    finding.severity = Severity::warning;
    finding.localization.pass_ordinal = 2;
    finding.localization.stage = "fragment";
    finding.message = "semantic 'MVP' unresolved, using identity matrix";
    verdict.findings.push_back(finding);
    report.authoring_verdict = verdict;
    auto json = serialize_report_json(report);
    REQUIRE(json.find("\"result\":\"degraded\"") != std::string::npos);
    REQUIRE(json.find("\"pass\":2") != std::string::npos);
    REQUIRE(json.find("semantic 'MVP' unresolved") != std::string::npos);
}

TEST_CASE("serialize_report_json includes error counts", "[diagnostic_report_json]") {
    DiagnosticReport report{};
    report.error_counts_by_category.authoring = 0;
    report.error_counts_by_category.runtime = 1;
    auto json = serialize_report_json(report);
    REQUIRE(json.find("\"error_counts\"") != std::string::npos);
    REQUIRE(json.find("\"authoring\":0") != std::string::npos);
    REQUIRE(json.find("\"runtime\":1") != std::string::npos);
}

TEST_CASE("serialize_report_json includes binding coverage", "[diagnostic_report_json]") {
    DiagnosticReport report{};
    report.binding_coverage.push_back({.pass_ordinal = 0, .resolved = 4});
    auto json = serialize_report_json(report);
    REQUIRE(json.find("\"binding_coverage\"") != std::string::npos);
    REQUIRE(json.find("\"resolved\":4") != std::string::npos);
}

TEST_CASE("serialize_report_json includes degradation entries", "[diagnostic_report_json]") {
    DiagnosticReport report{};
    report.degradation_entries.push_back({
        .pass_ordinal = 3,
        .expected_resource = "color_grade.png",
        .substituted_resource = "fallback_1x1_white",
        .frame_index = 1,
        .type = DegradationType::texture_fallback,
    });
    auto json = serialize_report_json(report);
    REQUIRE(json.find("\"degradation\"") != std::string::npos);
    REQUIRE(json.find("\"color_grade.png\"") != std::string::npos);
    REQUIRE(json.find("\"texture_fallback\"") != std::string::npos);
}

TEST_CASE("serialize_report_json includes compile summaries", "[diagnostic_report_json]") {
    DiagnosticReport report{};
    report.compile_summaries.push_back({
        .pass_ordinal = 0,
        .evidence =
            {
                .stage = "fragment",
                .success = true,
                .messages = {},
                .timing_us = 142.3,
                .cache_hit = false,
            },
    });
    auto json = serialize_report_json(report);
    REQUIRE(json.find("\"compile_summaries\"") != std::string::npos);
    REQUIRE(json.find("\"stage\":\"fragment\"") != std::string::npos);
    REQUIRE(json.find("\"success\":true") != std::string::npos);
}

TEST_CASE("serialize_report_json includes manifest when present", "[diagnostic_report_json]") {
    DiagnosticReport report{};
    ChainManifest manifest;
    manifest.add_pass({.ordinal = 0, .shader_path = "stock.slang"});
    manifest.add_texture({.name = "lut1", .path = "lut.png"});
    report.manifest = manifest;
    auto json = serialize_report_json(report);
    REQUIRE(json.find("\"manifest\"") != std::string::npos);
    REQUIRE(json.find("\"stock.slang\"") != std::string::npos);
    REQUIRE(json.find("\"lut1\"") != std::string::npos);
}
