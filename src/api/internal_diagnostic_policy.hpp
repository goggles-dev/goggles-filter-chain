#pragma once

#include <goggles/filter_chain.h>

struct GogglesFcDiagnosticPolicy {
    uint32_t struct_size;
    bool enabled;
    goggles_fc_log_level_t log_level;
    uint32_t max_events;
};

#ifdef __cplusplus
using goggles_fc_diagnostic_policy_t = GogglesFcDiagnosticPolicy;
#else
typedef struct GogglesFcDiagnosticPolicy goggles_fc_diagnostic_policy_t;
#endif

static inline goggles_fc_diagnostic_policy_t goggles_fc_diagnostic_policy_init(GOGGLES_FC_NOARGS) {
    goggles_fc_diagnostic_policy_t value;
    value.struct_size = GOGGLES_FC_STRUCT_SIZE(goggles_fc_diagnostic_policy_t);
    value.enabled = false;
    value.log_level = GOGGLES_FC_LOG_LEVEL_INFO;
    value.max_events = 0u;
    return value;
}
