#ifndef NT_LOG_H
#define NT_LOG_H

/* Log levels (ordered by severity) */
typedef enum {
    NT_LOG_LEVEL_INFO = 0,
    NT_LOG_LEVEL_WARN = 1,
    NT_LOG_LEVEL_ERROR = 2,
    NT_LOG_LEVEL_NONE = 3 /* suppress all logging */
} nt_log_level_t;

/* Format attribute for printf-style type checking (no GNU extensions) */
#if defined(__GNUC__) || defined(__clang__)
#define NT_PRINTF_ATTR(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define NT_PRINTF_ATTR(fmt_idx, arg_idx)
#endif

/* --- Lifecycle --- */
void nt_log_init(void);
void nt_log_set_level(nt_log_level_t level);

/* --- Plain functions (no domain, for game/example code) ---
   NOTE: NT_PRINTF_ATTR intentionally omitted during migration period.
   Existing call sites pass non-literal strings (nt_log_info(msg)), which
   triggers -Wformat-security with format attributes. Attributes will be
   added once all call sites are migrated to printf-style in Plan 02/03. */
void nt_log_info(const char *fmt, ...);
void nt_log_warn(const char *fmt, ...);
void nt_log_error(const char *fmt, ...);

/* --- Domain functions (called by macros, engine-internal) --- */
void nt_log_info_impl(const char *domain, const char *fmt, ...) NT_PRINTF_ATTR(2, 3);
void nt_log_warn_impl(const char *domain, const char *fmt, ...) NT_PRINTF_ATTR(2, 3);
void nt_log_error_impl(const char *domain, const char *fmt, ...) NT_PRINTF_ATTR(2, 3);

/* --- Domain resolution --- */
#ifndef NT_LOG_DOMAIN
#ifdef NT_LOG_DOMAIN_DEFAULT
#define NT_LOG_DOMAIN NT_LOG_DOMAIN_DEFAULT
#endif
#endif

/* --- Domain macros --- */
#ifdef NT_LOG_DOMAIN
#define NT_LOG_INFO(...) nt_log_info_impl(NT_LOG_DOMAIN, __VA_ARGS__)
#define NT_LOG_WARN(...) nt_log_warn_impl(NT_LOG_DOMAIN, __VA_ARGS__)
#define NT_LOG_ERROR(...) nt_log_error_impl(NT_LOG_DOMAIN, __VA_ARGS__)
#else
/* Compile error when domain macros used without domain defined */
#define NT_LOG_INFO(...)                                                                                                                                                                               \
    do {                                                                                                                                                                                               \
        _Static_assert(0, "NT_LOG_DOMAIN not defined. Either #define "                                                                                                                                 \
                          "NT_LOG_DOMAIN \"name\" before "                                                                                                                                             \
                          "including nt_log.h, or add LOG_DOMAIN to "                                                                                                                                  \
                          "nt_add_module()");                                                                                                                                                          \
    } while (0)
#define NT_LOG_WARN(...)                                                                                                                                                                               \
    do {                                                                                                                                                                                               \
        _Static_assert(0, "NT_LOG_DOMAIN not defined. Either #define "                                                                                                                                 \
                          "NT_LOG_DOMAIN \"name\" before "                                                                                                                                             \
                          "including nt_log.h, or add LOG_DOMAIN to "                                                                                                                                  \
                          "nt_add_module()");                                                                                                                                                          \
    } while (0)
#define NT_LOG_ERROR(...)                                                                                                                                                                              \
    do {                                                                                                                                                                                               \
        _Static_assert(0, "NT_LOG_DOMAIN not defined. Either #define "                                                                                                                                 \
                          "NT_LOG_DOMAIN \"name\" before "                                                                                                                                             \
                          "including nt_log.h, or add LOG_DOMAIN to "                                                                                                                                  \
                          "nt_add_module()");                                                                                                                                                          \
    } while (0)
#endif

#endif /* NT_LOG_H */
