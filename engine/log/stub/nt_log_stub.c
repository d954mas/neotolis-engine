#include "log/nt_log.h"

void nt_log_set_level(nt_log_level_t level) { (void)level; }

void nt_log_write(nt_log_level_t level, const char *domain, const char *fmt, ...) {
    (void)level;
    (void)domain;
    (void)fmt;
}
