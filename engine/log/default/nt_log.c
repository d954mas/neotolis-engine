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
