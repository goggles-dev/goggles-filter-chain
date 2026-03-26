#pragma once

#include "diagnostic_report.hpp"

#include <string>

namespace goggles::diagnostics {

[[nodiscard]] auto serialize_report_json(const DiagnosticReport& report) -> std::string;

} // namespace goggles::diagnostics
