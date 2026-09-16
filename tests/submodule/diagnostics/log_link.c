#include <stdio.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "expected.h"
#include "log/nt_log.h"

_Static_assert(NT_LOG_MIN_LEVEL == NT426_EXPECT_LOG, "linked log target floor");
static unsigned s_events;
static bool s_invalid;
static bool s_ready;
static uint32_t s_region;
static unsigned s_atlas_warnings;

bool nt_resource_is_ready(nt_resource_t handle) {
    (void)handle;
    return s_ready;
}

uint32_t nt_atlas_find_region(nt_resource_t atlas, uint64_t name_hash) {
    (void)atlas;
    (void)name_hash;
    return s_region;
}

static void capture(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)user;
    if (level == NT_LOG_LEVEL_WARN && domain[0] == '\0' && strstr(msg, "nt_atlas_resolve_ref:") == msg) {
        ++s_atlas_warnings;
        return;
    }
    ++s_events;
    if (level != NT_LOG_LEVEL_ERROR || strcmp(domain, "consumer") != 0 || strcmp(msg, "probe") != 0) {
        s_invalid = true;
    }
}

int main(void) {
    nt_log_set_level(NT_LOG_LEVEL_INFO);
    nt_log_add_sink(capture, NULL);
    nt_log_write(NT_LOG_LEVEL_ERROR, "consumer", "probe");
    const bool wrote_unique = nt_log_write_unique(NT_LOG_LEVEL_ERROR, "consumer", "probe");
    const bool wrote_again = nt_log_write_unique(NT_LOG_LEVEL_ERROR, "consumer", "probe");
    const bool enabled = !NT426_LOG_STUB && NT_LOG_MIN_LEVEL < 3;
    if (s_invalid || s_events != (enabled ? 2U : 0U) || wrote_unique != enabled || wrote_again) {
        return 1;
    }
    nt_atlas_region_ref_t ref = {.atlas = {1}, .name_hash = 42, .region = NT_ATLAS_INVALID_REGION};
    nt_atlas_resolve_ref(&ref);
    if (ref.region != NT_ATLAS_INVALID_REGION || s_atlas_warnings != 0) {
        return 2;
    }
    s_ready = true;
    s_region = 7;
    nt_atlas_resolve_ref(&ref);
    if (ref.region != 7 || s_atlas_warnings != 0) {
        return 3;
    }
    ref.region = NT_ATLAS_INVALID_REGION;
    s_region = NT_ATLAS_INVALID_REGION;
    nt_atlas_resolve_ref(&ref);
    nt_atlas_resolve_ref(&ref);
    const unsigned warn_count = !NT426_LOG_STUB && NT_LOG_MIN_LEVEL <= 1 ? 1U : 0U;
    if (ref.region != NT_ATLAS_INVALID_REGION || s_atlas_warnings != warn_count) {
        return 4;
    }
    ref.name_hash = 43;
    nt_atlas_resolve_ref(&ref);
    nt_log_remove_sink(capture, NULL);
    if (s_invalid || s_atlas_warnings != 2U * warn_count) {
        return 5;
    }
    puts("NT426_CONFIG_PASS");
    return 0;
}
