#include "log/nt_log.h"

void nt_log_init(void) {}
void nt_log_set_level(nt_log_level_t level) { (void)level; }

void nt_log_info(const char *fmt, ...) { (void)fmt; }
void nt_log_warn(const char *fmt, ...) { (void)fmt; }
void nt_log_error(const char *fmt, ...) { (void)fmt; }

void nt_log_info_impl(const char *domain, const char *fmt, ...) {
    (void)domain;
    (void)fmt;
}
void nt_log_warn_impl(const char *domain, const char *fmt, ...) {
    (void)domain;
    (void)fmt;
}
void nt_log_error_impl(const char *domain, const char *fmt, ...) {
    (void)domain;
    (void)fmt;
}
