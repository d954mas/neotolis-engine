#include "log/nt_log_ring.h"

/* RED placeholder — bodies land in GREEN. */
void nt_log_ring_init(void) {}
void nt_log_ring_sink(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)level;
    (void)domain;
    (void)msg;
    (void)user;
}
uint16_t nt_log_ring_tail(uint16_t n, nt_log_level_t min_level, nt_log_ring_entry_t *out) {
    (void)n;
    (void)min_level;
    (void)out;
    return 0;
}
void nt_log_ring_clear(void) {}
