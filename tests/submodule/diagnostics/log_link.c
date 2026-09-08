#include <stdio.h>
#include <string.h>

#include "expected.h"
#include "log/nt_log.h"

_Static_assert(NT_LOG_MIN_LEVEL == NT426_EXPECT_LOG, "linked log target floor");
static unsigned s_events;
static bool s_invalid;

static void capture(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)user;
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
    nt_log_remove_sink(capture, NULL);
    const bool enabled = !NT426_LOG_STUB && NT_LOG_MIN_LEVEL < 3;
    if (s_invalid || s_events != (enabled ? 2U : 0U) || wrote_unique != enabled || wrote_again) {
        return 1;
    }
    puts("NT426_CONFIG_PASS");
    return 0;
}
