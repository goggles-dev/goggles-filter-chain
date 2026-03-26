#include "diagnostics/diagnostic_report_json.hpp"

#include "diagnostics/json_writer.hpp"

#include <string>

namespace goggles::diagnostics {
namespace {

auto verdict_result_string(VerdictResult r) -> const char* {
    switch (r) {
    case VerdictResult::pass:
        return "pass";
    case VerdictResult::degraded:
        return "degraded";
    case VerdictResult::fail:
        return "fail";
    }
    return "unknown";
}

auto severity_string(Severity s) -> const char* {
    switch (s) {
    case Severity::debug:
        return "debug";
    case Severity::info:
        return "info";
    case Severity::warning:
        return "warning";
    case Severity::error:
        return "error";
    }
    return "unknown";
}

auto degradation_type_string(DegradationType t) -> const char* {
    switch (t) {
    case DegradationType::texture_fallback:
        return "texture_fallback";
    case DegradationType::semantic_unresolved:
        return "semantic_unresolved";
    case DegradationType::reflection_loss:
        return "reflection_loss";
    }
    return "unknown";
}

auto timeline_event_type_string(TimelineEventType t) -> const char* {
    switch (t) {
    case TimelineEventType::prechain_start:
        return "prechain_start";
    case TimelineEventType::prechain_end:
        return "prechain_end";
    case TimelineEventType::pass_start:
        return "pass_start";
    case TimelineEventType::pass_end:
        return "pass_end";
    case TimelineEventType::final_composition_start:
        return "final_composition_start";
    case TimelineEventType::final_composition_end:
        return "final_composition_end";
    case TimelineEventType::history_push:
        return "history_push";
    case TimelineEventType::feedback_copy:
        return "feedback_copy";
    case TimelineEventType::allocation:
        return "allocation";
    }
    return "unknown";
}

auto category_string(Category c) -> const char* {
    switch (c) {
    case Category::authoring:
        return "authoring";
    case Category::runtime:
        return "runtime";
    case Category::quality:
        return "quality";
    case Category::capture:
        return "capture";
    }
    return "unknown";
}

void write_session(JsonWriter& w, const SessionIdentity& id) {
    w.key("session");
    w.begin_object();
    w.key("preset_hash");
    w.value_string(id.preset_hash);
    w.key("expanded_source_hash");
    w.value_string(id.expanded_source_hash);
    w.key("compiled_contract_hash");
    w.value_string(id.compiled_contract_hash);
    w.key("generation_id");
    w.value(id.generation_id);
    w.key("frame_range");
    w.begin_array();
    w.value(id.frame_start);
    w.value(id.frame_end);
    w.end_array();
    w.key("capture_mode");
    w.value_string(id.capture_mode);
    w.key("environment_fingerprint");
    w.value_string(id.environment_fingerprint);
    w.end_object();
}

void write_verdict(JsonWriter& w, const AuthoringVerdict& verdict) {
    w.key("verdict");
    w.begin_object();
    w.key("result");
    w.value_string(verdict_result_string(verdict.result));
    w.key("findings");
    w.begin_array();
    for (const auto& f : verdict.findings) {
        w.begin_object();
        w.key("severity");
        w.value_string(severity_string(f.severity));
        w.key("pass");
        w.value(f.localization.pass_ordinal);
        w.key("stage");
        w.value_string(f.localization.stage);
        w.key("message");
        w.value_string(f.message);
        w.end_object();
    }
    w.end_array();
    w.end_object();
}

void write_error_counts(JsonWriter& w, const ErrorCountsByCategory& counts) {
    w.key("error_counts");
    w.begin_object();
    w.key("authoring");
    w.value(counts.authoring);
    w.key("runtime");
    w.value(counts.runtime);
    w.key("quality");
    w.value(counts.quality);
    w.key("capture");
    w.value(counts.capture);
    w.end_object();
}

void write_manifest(JsonWriter& w, const ChainManifest& manifest) {
    w.key("manifest");
    w.begin_object();
    w.key("passes");
    w.begin_array();
    for (const auto& p : manifest.passes()) {
        w.begin_object();
        w.key("ordinal");
        w.value(p.ordinal);
        w.key("shader_path");
        w.value_string(p.shader_path);
        w.key("scale_type_x");
        w.value_string(p.scale_type_x);
        w.key("scale_type_y");
        w.value_string(p.scale_type_y);
        w.key("scale_x");
        w.value(static_cast<double>(p.scale_x));
        w.key("scale_y");
        w.value(static_cast<double>(p.scale_y));
        w.key("format_override");
        w.value_string(p.format_override);
        w.key("wrap_mode");
        w.value_string(p.wrap_mode);
        w.key("alias");
        w.value_string(p.alias);
        w.end_object();
    }
    w.end_array();
    w.key("textures");
    w.begin_array();
    for (const auto& t : manifest.textures()) {
        w.begin_object();
        w.key("name");
        w.value_string(t.name);
        w.key("path");
        w.value_string(t.path);
        w.key("filter_mode");
        w.value_string(t.filter_mode);
        w.key("mipmap");
        w.value(t.mipmap);
        w.key("wrap_mode");
        w.value_string(t.wrap_mode);
        w.end_object();
    }
    w.end_array();
    w.end_object();
}

void write_binding_coverage(JsonWriter& w, const std::vector<BindingCoverageRow>& rows) {
    w.key("binding_coverage");
    w.begin_array();
    for (const auto& row : rows) {
        w.begin_object();
        w.key("pass");
        w.value(row.pass_ordinal);
        w.key("resolved");
        w.value(row.resolved);
        w.key("substituted");
        w.value(row.substituted);
        w.key("unresolved");
        w.value(row.unresolved);
        w.end_object();
    }
    w.end_array();
}

void write_semantic_coverage(JsonWriter& w, const std::vector<SemanticCoverageRow>& rows) {
    w.key("semantic_coverage");
    w.begin_array();
    for (const auto& row : rows) {
        w.begin_object();
        w.key("pass");
        w.value(row.pass_ordinal);
        w.key("parameter");
        w.value(row.parameter);
        w.key("semantic");
        w.value(row.semantic);
        w.key("static");
        w.value(row.statik);
        w.key("unresolved");
        w.value(row.unresolved);
        w.end_object();
    }
    w.end_array();
}

void write_degradation(JsonWriter& w, const std::vector<DegradationEntry>& entries) {
    w.key("degradation");
    w.begin_array();
    for (const auto& e : entries) {
        w.begin_object();
        w.key("pass");
        w.value(e.pass_ordinal);
        w.key("expected");
        w.value_string(e.expected_resource);
        w.key("substituted");
        w.value_string(e.substituted_resource);
        w.key("frame");
        w.value(e.frame_index);
        w.key("type");
        w.value_string(degradation_type_string(e.type));
        w.end_object();
    }
    w.end_array();
}

void write_compile_summaries(JsonWriter& w, const std::vector<CompileSummaryEntry>& summaries) {
    w.key("compile_summaries");
    w.begin_array();
    for (const auto& entry : summaries) {
        w.begin_object();
        w.key("pass");
        w.value(entry.pass_ordinal);
        w.key("stage");
        w.value_string(entry.evidence.stage);
        w.key("success");
        w.value(entry.evidence.success);
        w.key("cache_hit");
        w.value(entry.evidence.cache_hit);
        w.key("timing_us");
        w.value(entry.evidence.timing_us);
        w.key("messages");
        w.begin_array();
        for (const auto& msg : entry.evidence.messages) {
            w.value_string(msg);
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();
}

void write_execution_trace(JsonWriter& w, const std::vector<TimelineEvent>& events) {
    w.key("execution_trace");
    w.begin_array();
    for (const auto& e : events) {
        w.begin_object();
        w.key("type");
        w.value_string(timeline_event_type_string(e.type));
        w.key("pass");
        w.value(e.pass_ordinal);
        w.key("cpu_timestamp_ns");
        w.value(e.cpu_timestamp_ns);
        if (e.gpu_duration_us.has_value()) {
            w.key("gpu_duration_us");
            w.value(*e.gpu_duration_us);
        }
        w.end_object();
    }
    w.end_array();
}

void write_event_timeline(JsonWriter& w, const std::vector<DiagnosticEvent>& events) {
    w.key("event_timeline");
    w.begin_array();
    for (const auto& e : events) {
        w.begin_object();
        w.key("severity");
        w.value_string(severity_string(e.severity));
        w.key("category");
        w.value_string(category_string(e.category));
        w.key("pass");
        w.value(e.localization.pass_ordinal);
        w.key("stage");
        w.value_string(e.localization.stage);
        w.key("message");
        w.value_string(e.message);
        w.end_object();
    }
    w.end_array();
}

} // namespace

auto serialize_report_json(const DiagnosticReport& report) -> std::string {
    JsonWriter w;
    w.begin_object();

    w.key("version");
    w.value(static_cast<int32_t>(1));

    write_session(w, report.session_identity);

    if (report.authoring_verdict.has_value()) {
        write_verdict(w, *report.authoring_verdict);
    }

    write_error_counts(w, report.error_counts_by_category);

    if (report.manifest.has_value()) {
        write_manifest(w, *report.manifest);
    }

    write_binding_coverage(w, report.binding_coverage);
    write_semantic_coverage(w, report.semantic_coverage);
    write_degradation(w, report.degradation_entries);
    write_compile_summaries(w, report.compile_summaries);
    write_execution_trace(w, report.execution_trace);
    write_event_timeline(w, report.event_timeline);

    w.end_object();
    return w.str();
}

} // namespace goggles::diagnostics
