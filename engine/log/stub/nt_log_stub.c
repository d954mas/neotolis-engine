#include "log/nt_log.h"

void nt_log_set_level(nt_log_level_t level) { (void)level; }

void nt_log_add_sink(nt_log_sink_fn fn, void *user) {
    (void)fn;
    (void)user;
}

void nt_log_remove_sink(nt_log_sink_fn fn, void *user) {
    (void)fn;
    (void)user;
}

void nt_log_write(nt_log_level_t level, const char *domain, const char *fmt, ...) {
    (void)level;
    (void)domain;
    (void)fmt;
}

bool nt_log_write_unique(nt_log_level_t level, const char *domain, const char *fmt, ...) {
    (void)level;
    (void)domain;
    (void)fmt;
    return false;
}
