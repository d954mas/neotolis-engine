#include "log/nt_log.h"
#include <stdarg.h>
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
#define NT_LOG_BUF_SIZE 256
#endif

static nt_log_level_t s_log_level = NT_LOG_LEVEL_INFO;

void nt_log_init(void) { /* no-op, reserved for future use */ }
void nt_log_set_level(nt_log_level_t level) { s_log_level = level; }

/* Shared formatting + output helper */
static void log_write(nt_log_level_t level, const char *domain, const char *fmt, va_list args) {
    static const char *const level_names[] = {"INFO", "WARN", "ERROR"};
    if (level >= NT_LOG_LEVEL_NONE) {
        return;
    }
    char msg[NT_LOG_BUF_SIZE];
    (void)vsnprintf(msg, sizeof(msg), fmt, args);

#ifdef __EMSCRIPTEN__
    char out[NT_LOG_BUF_SIZE + 64];
    if (domain) {
        (void)snprintf(out, sizeof(out), "%s:%s: %s", level_names[level], domain, msg);
    } else {
        (void)snprintf(out, sizeof(out), "%s: %s", level_names[level], msg);
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
        (void)fprintf(stream, "%s:%s: %s\n", level_names[level], domain, msg);
    } else {
        (void)fprintf(stream, "%s: %s\n", level_names[level], msg);
    }
#endif
}

/* Plain functions (no domain) */
void nt_log_info(const char *fmt, ...) {
    if (s_log_level > NT_LOG_LEVEL_INFO) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    log_write(NT_LOG_LEVEL_INFO, NULL, fmt, args);
    va_end(args);
}

void nt_log_warn(const char *fmt, ...) {
    if (s_log_level > NT_LOG_LEVEL_WARN) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    log_write(NT_LOG_LEVEL_WARN, NULL, fmt, args);
    va_end(args);
}

void nt_log_error(const char *fmt, ...) {
    if (s_log_level > NT_LOG_LEVEL_ERROR) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    log_write(NT_LOG_LEVEL_ERROR, NULL, fmt, args);
    va_end(args);
}

/* Domain functions (called by macros) */
void nt_log_info_impl(const char *domain, const char *fmt, ...) {
    if (s_log_level > NT_LOG_LEVEL_INFO) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    log_write(NT_LOG_LEVEL_INFO, domain, fmt, args);
    va_end(args);
}

void nt_log_warn_impl(const char *domain, const char *fmt, ...) {
    if (s_log_level > NT_LOG_LEVEL_WARN) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    log_write(NT_LOG_LEVEL_WARN, domain, fmt, args);
    va_end(args);
}

void nt_log_error_impl(const char *domain, const char *fmt, ...) {
    if (s_log_level > NT_LOG_LEVEL_ERROR) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    log_write(NT_LOG_LEVEL_ERROR, domain, fmt, args);
    va_end(args);
}
