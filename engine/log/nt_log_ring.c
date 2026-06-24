#include "log/nt_log_ring.h"

#if NT_LOG_RING_ENABLED

#include <stdio.h>

/* Fixed BSS ring, no heap, single-threaded (same assumption as nt_log); entries are owned copies. */
static struct {
    nt_log_ring_entry_t entries[NT_LOG_RING_DEPTH];
    uint16_t head;  /* next write slot */
    uint16_t count; /* live entries, saturates at NT_LOG_RING_DEPTH */
} s_ring;

void nt_log_ring_init(void) {
    s_ring.head = 0;
    s_ring.count = 0;
}

void nt_log_ring_clear(void) { nt_log_ring_init(); }

void nt_log_ring_sink(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)user;
    nt_log_ring_entry_t *e = &s_ring.entries[s_ring.head];
    e->level = level;

    /* Copy both — never store the caller's pointer (NULL-guard despite the "" sink contract). */
    (void)snprintf(e->domain, sizeof(e->domain), "%s", (domain != NULL) ? domain : "");
    (void)snprintf(e->msg, sizeof(e->msg), "%s", (msg != NULL) ? msg : "");

    s_ring.head = (uint16_t)((s_ring.head + 1) % NT_LOG_RING_DEPTH);
    if (s_ring.count < NT_LOG_RING_DEPTH) {
        s_ring.count++;
    }
}

uint16_t nt_log_ring_tail(uint16_t n, nt_log_level_t min_level, nt_log_ring_entry_t *out) {
    if (out == NULL || n == 0 || s_ring.count == 0) {
        return 0;
    }
    if (n > s_ring.count) {
        n = s_ring.count;
    }

    /* Walk backward from the newest, emitting newest-first. */
    uint16_t written = 0;
    uint16_t idx = s_ring.head; /* one past newest */
    for (uint16_t scanned = 0; scanned < s_ring.count && written < n; scanned++) {
        idx = (idx == 0) ? (uint16_t)(NT_LOG_RING_DEPTH - 1) : (uint16_t)(idx - 1);
        const nt_log_ring_entry_t *e = &s_ring.entries[idx];
        if (e->level >= min_level) {
            out[written++] = *e;
        }
    }
    return written;
}

#else /* NT_LOG_RING_ENABLED == 0 — zero-footprint no-op stubs for release/OFF mirror */

void nt_log_ring_init(void) {}
void nt_log_ring_clear(void) {}
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

#endif /* NT_LOG_RING_ENABLED */
