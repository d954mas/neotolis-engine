#include "log/nt_log.h"
#include "hash/nt_hash.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/console.h>
#endif

/* Suppress -Wformat-nonliteral: vsnprintf/fprintf here forward caller-supplied
   format strings. Type-safety is enforced at call sites via NT_PRINTF_ATTR. */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif

#ifndef NT_LOG_BUF_SIZE
#define NT_LOG_BUF_SIZE 512
#endif

static nt_log_level_t s_log_level = NT_LOG_LEVEL_INFO;

void nt_log_set_level(nt_log_level_t level) { s_log_level = level; }

void nt_log_write(nt_log_level_t level, const char *domain, const char *fmt, ...) {
    static const char *const level_names[] = {"INFO", "WARN", "ERROR"};
    if (s_log_level > level || level >= NT_LOG_LEVEL_NONE) {
        return;
    }
    char msg[NT_LOG_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    if (written >= (int)sizeof(msg)) {
        msg[sizeof(msg) - 4] = '.';
        msg[sizeof(msg) - 3] = '.';
        msg[sizeof(msg) - 2] = '.';
    }

#ifdef __EMSCRIPTEN__
    char out[NT_LOG_BUF_SIZE + 64];
    if (domain) {
        (void)snprintf(out, sizeof(out), "%s [%s] %s", level_names[level], domain, msg);
    } else {
        (void)snprintf(out, sizeof(out), "%s %s", level_names[level], msg);
    }
    switch (level) {
    case NT_LOG_LEVEL_INFO:
        emscripten_console_log(out);
        break;
    case NT_LOG_LEVEL_WARN:
        emscripten_console_warn(out);
        break;
    case NT_LOG_LEVEL_ERROR:
        emscripten_console_error(out);
        break;
    default:
        break;
    }
#else
    /* INFO -> stdout, WARN + ERROR -> stderr */
    FILE *stream = (level >= NT_LOG_LEVEL_WARN) ? stderr : stdout;
    if (domain) {
        (void)fprintf(stream, "%s [%s] %s\n", level_names[level], domain, msg);
    } else {
        (void)fprintf(stream, "%s %s\n", level_names[level], msg);
    }
#endif
}

/* Content-dedup for nt_log_write_unique: remembers message HASHES (not full text), so each
 * distinct message logs once. Saturates without eviction — N distinct messages already means
 * "go look", further ones add no signal. Single-threaded (same assumption as NT_LOG_ONCE_). */
#ifndef NT_LOG_UNIQUE_MAX
#define NT_LOG_UNIQUE_MAX 1024
#endif
static uint64_t s_unique_hashes[NT_LOG_UNIQUE_MAX];
static uint32_t s_unique_count;
static bool s_unique_saturated;

bool nt_log_write_unique(nt_log_level_t level, const char *domain, const char *fmt, ...) {
    if (s_log_level > level || level >= NT_LOG_LEVEL_NONE) {
        return false; /* filtered: don't record, so it can still log if the level is lowered later */
    }
    char msg[NT_LOG_BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    if (written < 0) {
        return false;
    }
    const size_t len = (written < (int)sizeof(msg)) ? (size_t)written : sizeof(msg) - 1;
    const uint64_t h = nt_hash64(msg, (uint32_t)len).value;
    for (uint32_t i = 0; i < s_unique_count; i++) {
        if (s_unique_hashes[i] == h) {
            return false; /* this exact message already logged */
        }
    }
    if (s_unique_count == NT_LOG_UNIQUE_MAX) {
        if (!s_unique_saturated) {
            s_unique_saturated = true;
            nt_log_write(NT_LOG_LEVEL_WARN, NULL, "nt_log: unique-dedup saturated at %d messages, suppressing further", NT_LOG_UNIQUE_MAX);
        }
        return false;
    }
    s_unique_hashes[s_unique_count++] = h;
    nt_log_write(level, domain, "%s", msg);
    return true;
}
