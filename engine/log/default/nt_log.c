#include "log/nt_log.h"
#include "core/nt_assert.h"
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

/* Fixed BSS sink registry (D-01 "~4 slots", overridable -D). Tiny + always-compiled;
   the dev-only gating lives in the attached sink (nt_log_ring), not here. */
#ifndef NT_LOG_MAX_SINKS
#define NT_LOG_MAX_SINKS 4
#endif
static nt_log_sink_fn s_sinks[NT_LOG_MAX_SINKS];
static void *s_sink_user[NT_LOG_MAX_SINKS];
static uint8_t s_sink_count;

void nt_log_add_sink(nt_log_sink_fn fn, void *user) {
    NT_ASSERT(fn != NULL); /* host-call invariant, not bot input */
    /* Idempotent: re-registering the exact (fn,user) pair is a no-op, so callers can attach a sink
       across re-inits without leaking duplicate slots (each would fan out the same line twice). */
    for (uint8_t i = 0; i < s_sink_count; i++) {
        if (s_sinks[i] == fn && s_sink_user[i] == user) {
            return;
        }
    }
    NT_ASSERT(s_sink_count < NT_LOG_MAX_SINKS); /* registry full is a host-call invariant */
    s_sinks[s_sink_count] = fn;
    s_sink_user[s_sink_count] = user;
    s_sink_count++;
}

void nt_log_remove_sink(nt_log_sink_fn fn, void *user) {
    NT_ASSERT(fn != NULL); /* host-call invariant, not bot input */
    for (uint8_t i = 0; i < s_sink_count; i++) {
        if (s_sinks[i] == fn && s_sink_user[i] == user) {
            /* Compact the tail down so the array stays dense (order is irrelevant to fan-out). */
            for (uint8_t j = i + 1U; j < s_sink_count; j++) {
                s_sinks[j - 1U] = s_sinks[j];
                s_sink_user[j - 1U] = s_sink_user[j];
            }
            s_sink_count--;
            s_sinks[s_sink_count] = NULL;
            s_sink_user[s_sink_count] = NULL;
            return;
        }
    }
}

/* Append a "..." marker to a truncated msg WITHOUT leaving a split multibyte sequence — the line is
   later stored verbatim in the log ring and serialized as a JSON string, where cJSON rejects invalid
   UTF-8. Walk back over any trailing UTF-8 continuation bytes (0b10xxxxxx), then drop a now-orphaned
   lead byte (>= 0xC0) whose continuation bytes were cut — both leave the marker on a codepoint
   boundary. buf must hold at least 4 bytes (NT_LOG_BUF_SIZE is 512). */
static void append_truncation_marker(char *buf, size_t cap) {
    size_t end = cap - 4; /* room for "..." + NUL */
    while (end > 0 && ((unsigned char)buf[end - 1] & 0xC0U) == 0x80U) {
        end--;
    }
    /* A trailing lead byte here lost its continuation bytes to truncation -> orphan; drop it too. */
    if (end > 0 && (unsigned char)buf[end - 1] >= 0xC0U) {
        end--;
    }
    buf[end] = '.';
    buf[end + 1] = '.';
    buf[end + 2] = '.';
    buf[end + 3] = '\0';
}

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
    if (written < 0) {
        msg[0] = '\0'; /* encoding error: forward an empty (NUL-terminated) line, never garbage */
    } else if (written >= (int)sizeof(msg)) {
        append_truncation_marker(msg, sizeof(msg));
    }

    /* Fan out the final formatted line to registered sinks (single-threaded). */
    for (uint8_t i = 0; i < s_sink_count; i++) {
        s_sinks[i](level, domain ? domain : "", msg, s_sink_user[i]);
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
