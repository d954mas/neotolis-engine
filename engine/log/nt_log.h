#ifndef NT_LOG_H
#define NT_LOG_H

#include <stdbool.h>
#include <stddef.h>

#ifndef NT_LOG_MIN_LEVEL
#error "NT_LOG_MIN_LEVEL must be defined by the nt_log_interface target (0..3)"
#elif NT_LOG_MIN_LEVEL < 0 || NT_LOG_MIN_LEVEL > 3
#error "NT_LOG_MIN_LEVEL must be in 0..3"
#endif

/* Log levels (ordered by severity) */
typedef enum {
    NT_LOG_LEVEL_INFO = 0,
    NT_LOG_LEVEL_WARN = 1,
    NT_LOG_LEVEL_ERROR = 2,
    NT_LOG_LEVEL_NONE = 3 /* suppress all logging */
} nt_log_level_t;

_Static_assert(NT_LOG_LEVEL_INFO == 0 && NT_LOG_LEVEL_WARN == 1 && NT_LOG_LEVEL_ERROR == 2 && NT_LOG_LEVEL_NONE == 3, "log floor values must match levels");

/* Format attribute for printf-style type checking */
#if defined(__GNUC__) || defined(__clang__)
#define NT_PRINTF_ATTR(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define NT_PRINTF_ATTR(fmt_idx, arg_idx)
#endif

/* --- Lifecycle --- */
void nt_log_set_level(nt_log_level_t level);

/* --- Sink hook ---
 * Receives the already-FORMATTED line (no fmt/va_list re-entry), so a sink never
 * re-parses caller input. domain is "" (never NULL) when the call site had none.
 * Bounded fixed registry, single-threaded (same assumption as NT_LOG_ONCE_). */
typedef void (*nt_log_sink_fn)(nt_log_level_t level, const char *domain, const char *msg, void *user);
/* Idempotent: re-adding the exact (fn,user) pair is a no-op (never a duplicate slot). */
void nt_log_add_sink(nt_log_sink_fn fn, void *user);
/* Remove the slot matching (fn,user); a no-op if no such slot is registered. */
void nt_log_remove_sink(nt_log_sink_fn fn, void *user);

/* Max length (incl. NUL) of a formatted line handed to a sink. nt_log_write truncates to this on a
 * UTF-8-safe boundary, so a sink's own copy buffer must be >= this to never re-truncate mid-codepoint. */
#ifndef NT_LOG_BUF_SIZE
#define NT_LOG_BUF_SIZE 512
#endif
/* append_truncation_marker writes "..."+NUL at cap-4; a smaller buffer underflows the size_t index. */
_Static_assert(NT_LOG_BUF_SIZE >= 4, "NT_LOG_BUF_SIZE must be >= 4 (room for the \"...\" truncation marker)");

/* --- Single log function --- */
void nt_log_write(nt_log_level_t level, const char *domain, const char *fmt, ...) NT_PRINTF_ATTR(3, 4);

/* Write the formatted message at most once per distinct content, program-wide (dedups by the
 * produced string, unlike the per-call-site *_once macros). Returns true if it actually wrote.
 * Bounded + saturating; single-threaded (same assumption as NT_LOG_ONCE_). */
bool nt_log_write_unique(nt_log_level_t level, const char *domain, const char *fmt, ...) NT_PRINTF_ATTR(3, 4);

#define NT_LOG_ONCE_(write_call)                                                                                                                                                                       \
    do {                                                                                                                                                                                               \
        static bool nt_log_once_done_ = false;                                                                                                                                                         \
        if (!nt_log_once_done_) {                                                                                                                                                                      \
            nt_log_once_done_ = true;                                                                                                                                                                  \
            write_call;                                                                                                                                                                                \
        }                                                                                                                                                                                              \
    } while (0)

#if NT_LOG_MIN_LEVEL <= 0
#define NT_LOG_INFO_(domain, ...) nt_log_write(NT_LOG_LEVEL_INFO, domain, __VA_ARGS__)
#define NT_LOG_INFO_ONCE_(domain, ...) NT_LOG_ONCE_(NT_LOG_INFO_(domain, __VA_ARGS__))
#define NT_LOG_INFO_UNIQUE_(domain, ...) nt_log_write_unique(NT_LOG_LEVEL_INFO, domain, __VA_ARGS__)
#else
#define NT_LOG_INFO_(...) ((void)0)
#define NT_LOG_INFO_ONCE_(...) ((void)0)
#define NT_LOG_INFO_UNIQUE_(...) false
#endif

#if NT_LOG_MIN_LEVEL <= 1
#define NT_LOG_WARN_(domain, ...) nt_log_write(NT_LOG_LEVEL_WARN, domain, __VA_ARGS__)
#define NT_LOG_WARN_ONCE_(domain, ...) NT_LOG_ONCE_(NT_LOG_WARN_(domain, __VA_ARGS__))
#define NT_LOG_WARN_UNIQUE_(domain, ...) nt_log_write_unique(NT_LOG_LEVEL_WARN, domain, __VA_ARGS__)
#else
#define NT_LOG_WARN_(...) ((void)0)
#define NT_LOG_WARN_ONCE_(...) ((void)0)
#define NT_LOG_WARN_UNIQUE_(...) false
#endif

#if NT_LOG_MIN_LEVEL <= 2
#define NT_LOG_ERROR_(domain, ...) nt_log_write(NT_LOG_LEVEL_ERROR, domain, __VA_ARGS__)
#define NT_LOG_ERROR_ONCE_(domain, ...) NT_LOG_ONCE_(NT_LOG_ERROR_(domain, __VA_ARGS__))
#define NT_LOG_ERROR_UNIQUE_(domain, ...) nt_log_write_unique(NT_LOG_LEVEL_ERROR, domain, __VA_ARGS__)
#else
#define NT_LOG_ERROR_(...) ((void)0)
#define NT_LOG_ERROR_ONCE_(...) ((void)0)
#define NT_LOG_ERROR_UNIQUE_(...) false
#endif

#define nt_log_info(...) NT_LOG_INFO_(NULL, __VA_ARGS__)
#define nt_log_info_once(...) NT_LOG_INFO_ONCE_(NULL, __VA_ARGS__)
#define nt_log_info_unique(...) NT_LOG_INFO_UNIQUE_(NULL, __VA_ARGS__)
#define nt_log_warn(...) NT_LOG_WARN_(NULL, __VA_ARGS__)
#define nt_log_warn_once(...) NT_LOG_WARN_ONCE_(NULL, __VA_ARGS__)
#define nt_log_warn_unique(...) NT_LOG_WARN_UNIQUE_(NULL, __VA_ARGS__)
#define nt_log_error(...) NT_LOG_ERROR_(NULL, __VA_ARGS__)
#define nt_log_error_once(...) NT_LOG_ERROR_ONCE_(NULL, __VA_ARGS__)
#define nt_log_error_unique(...) NT_LOG_ERROR_UNIQUE_(NULL, __VA_ARGS__)

/* --- Domain resolution --- */
#ifndef NT_LOG_DOMAIN
#ifdef NT_LOG_DOMAIN_DEFAULT
#define NT_LOG_DOMAIN NT_LOG_DOMAIN_DEFAULT
#endif
#endif

/* Domain configuration remains required even when a level is compiled out. */
#ifdef NT_LOG_DOMAIN
#define NT_LOG_INFO(...) NT_LOG_INFO_(NT_LOG_DOMAIN, __VA_ARGS__)
#define NT_LOG_INFO_ONCE(...) NT_LOG_INFO_ONCE_(NT_LOG_DOMAIN, __VA_ARGS__)
#define NT_LOG_INFO_UNIQUE(...) NT_LOG_INFO_UNIQUE_(NT_LOG_DOMAIN, __VA_ARGS__)
#define NT_LOG_WARN(...) NT_LOG_WARN_(NT_LOG_DOMAIN, __VA_ARGS__)
#define NT_LOG_WARN_ONCE(...) NT_LOG_WARN_ONCE_(NT_LOG_DOMAIN, __VA_ARGS__)
#define NT_LOG_WARN_UNIQUE(...) NT_LOG_WARN_UNIQUE_(NT_LOG_DOMAIN, __VA_ARGS__)
#define NT_LOG_ERROR(...) NT_LOG_ERROR_(NT_LOG_DOMAIN, __VA_ARGS__)
#define NT_LOG_ERROR_ONCE(...) NT_LOG_ERROR_ONCE_(NT_LOG_DOMAIN, __VA_ARGS__)
#define NT_LOG_ERROR_UNIQUE(...) NT_LOG_ERROR_UNIQUE_(NT_LOG_DOMAIN, __VA_ARGS__)
#else
#define NT_LOG_MISSING_DOMAIN_                                                                                                                                                                         \
    ((bool)sizeof(struct {                                                                                                                                                                             \
        _Static_assert(0, "NT_LOG_DOMAIN not defined: define NT_LOG_DOMAIN or use nt_add_module LOG_DOMAIN");                                                                                          \
        char unused;                                                                                                                                                                                   \
    }))
#define NT_LOG_INFO(...) NT_LOG_MISSING_DOMAIN_
#define NT_LOG_INFO_ONCE(...) NT_LOG_MISSING_DOMAIN_
#define NT_LOG_INFO_UNIQUE(...) NT_LOG_MISSING_DOMAIN_
#define NT_LOG_WARN(...) NT_LOG_MISSING_DOMAIN_
#define NT_LOG_WARN_ONCE(...) NT_LOG_MISSING_DOMAIN_
#define NT_LOG_WARN_UNIQUE(...) NT_LOG_MISSING_DOMAIN_
#define NT_LOG_ERROR(...) NT_LOG_MISSING_DOMAIN_
#define NT_LOG_ERROR_ONCE(...) NT_LOG_MISSING_DOMAIN_
#define NT_LOG_ERROR_UNIQUE(...) NT_LOG_MISSING_DOMAIN_

#endif

#endif /* NT_LOG_H */
