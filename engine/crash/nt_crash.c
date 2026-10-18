#include "crash/nt_crash.h"

#include "core/nt_assert.h"
#include "crash/nt_crash_internal.h"
#include "log/nt_log.h"

#include <string.h>

nt_crash_state_t g_nt_crash;

static void ring_put(const char *s, uint32_t n) {
    while (n > 0) {
        uint32_t chunk = NT_CRASH_LOG_BYTES - g_nt_crash.pos;
        if (chunk > n) {
            chunk = n;
        }
        memcpy(g_nt_crash.ring + g_nt_crash.pos, s, chunk);
        g_nt_crash.pos += chunk;
        if (g_nt_crash.pos == NT_CRASH_LOG_BYTES) {
            g_nt_crash.pos = 0;
            g_nt_crash.full = 1;
        }
        s += chunk;
        n -= chunk;
    }
}

/* No busy flag: nothing in the engine faults inside this memcpy; only an external kill can
   interrupt it, which may garble one line. */
static void crash_sink(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)user;
    if (level < NT_LOG_LEVEL_WARN) {
        return;
    }
    const char *name = level == NT_LOG_LEVEL_WARN ? "WARN" : "ERROR";
    ring_put(name, (uint32_t)strlen(name));
    if (domain[0] != '\0') {
        ring_put(" [", 2);
        ring_put(domain, (uint32_t)strlen(domain));
        ring_put("]", 1);
    }
    ring_put(" ", 1);
    ring_put(msg, (uint32_t)strlen(msg));
    ring_put("\n", 1);
}

static void copy_text(char *dst, const char *src) {
    size_t n = strlen(src);
    NT_ASSERT(n > 0 && n < NT_CRASH_TEXT_MAX);
    memcpy(dst, src, n + 1);
}

void nt_crash_init(const char *name, const char *game_version) {
    NT_ASSERT(!g_nt_crash.initialized);
    NT_ASSERT(name && game_version);
    copy_text(g_nt_crash.name, name);
    copy_text(g_nt_crash.game_version, game_version);
    g_nt_crash.initialized = true;
    nt_log_add_sink(crash_sink, NULL);
    nt_crash_platform_init();
}

uint32_t nt_crash_load_previous(char *out, uint32_t cap) {
    NT_ASSERT(g_nt_crash.initialized);
    NT_ASSERT(out && cap >= 1);
    return nt_crash_platform_load(out, cap);
}

void nt_crash_clear_previous(void) {
    NT_ASSERT(g_nt_crash.initialized);
    nt_crash_platform_clear();
}
