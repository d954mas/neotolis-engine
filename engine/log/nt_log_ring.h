#ifndef NT_LOG_RING_H
#define NT_LOG_RING_H

#include "log/nt_log.h"
#include <stdint.h>

/* Reusable dev-only log-tail ring (D-03/D-04). A host attaches nt_log_ring_sink via
 * nt_log_add_sink; every nt_log_write then COPIES {level, domain, msg} into a bounded
 * ring. The devapi log.tail command (Plan 06) reads it. L1-testable without devapi.
 *
 * Dev-only: NT_LOG_RING_ENABLED=0 (release/OFF mirror) compiles real no-op bodies, so
 * the ring ships zero footprint in release. Default ON in debug. */
#ifndef NT_LOG_RING_ENABLED
#define NT_LOG_RING_ENABLED 1
#endif

#ifndef NT_LOG_RING_DEPTH
#define NT_LOG_RING_DEPTH 1024
#endif

/* Reuse nt_log's 512-byte truncation semantics for the message buffer. */
#ifndef NT_LOG_RING_MSG_SIZE
#define NT_LOG_RING_MSG_SIZE 512
#endif

#ifndef NT_LOG_RING_DOMAIN_SIZE
#define NT_LOG_RING_DOMAIN_SIZE 64
#endif

/* POD entry the L2 reader serializes without touching ring internals.
 * domain/msg are owned copies (never borrowed caller pointers — T-68-01-CPY). */
typedef struct {
    nt_log_level_t level;
    char domain[NT_LOG_RING_DOMAIN_SIZE];
    char msg[NT_LOG_RING_MSG_SIZE];
} nt_log_ring_entry_t;

/* Reset head/count to empty. */
void nt_log_ring_init(void);

/* Same shape as nt_log_sink_fn — register via nt_log_add_sink(nt_log_ring_sink, NULL). */
void nt_log_ring_sink(nt_log_level_t level, const char *domain, const char *msg, void *user);

/* Copy the newest min(n, stored) entries into out[], NEWEST FIRST. n is capped at the
 * stored count and at NT_LOG_RING_DEPTH. When min_level != NT_LOG_LEVEL_INFO, only
 * entries with level >= min_level are returned (still newest-first, up to n).
 * Returns the number written. */
uint16_t nt_log_ring_tail(uint16_t n, nt_log_level_t min_level, nt_log_ring_entry_t *out);

/* Drop all stored entries. */
void nt_log_ring_clear(void);

#endif /* NT_LOG_RING_H */
