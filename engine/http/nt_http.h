#ifndef NT_HTTP_H
#define NT_HTTP_H

/* Reference semantics = the web backend (fetch()); native approximates it with
 * libcurl. Canonical divergence table: docs/spec/assets/async-loading.md. */

#include "core/nt_types.h"

#ifndef NT_HTTP_MAX_REQUESTS
#define NT_HTTP_MAX_REQUESTS 8
#endif

typedef struct {
    uint32_t id;
} nt_http_request_t;

#define NT_HTTP_REQUEST_INVALID ((nt_http_request_t){0})

typedef enum {
    NT_HTTP_STATE_NONE = 0,
    NT_HTTP_STATE_PENDING,
    NT_HTTP_STATE_DOWNLOADING,
    NT_HTTP_STATE_DONE,   /* full response received, any HTTP status — check nt_http_status */
    NT_HTTP_STATE_FAILED, /* transport error, timeout or cancel — no usable body */
} nt_http_state_t;

typedef struct {
    const char *method; /* NULL -> "GET"; a body requires a non-GET/HEAD method;
                         * CONNECT/TRACE/TRACK are asserted out (fetch() forbids them) */
    const void *body;   /* copied at call time, caller keeps ownership; NULL -> no body */
    uint32_t body_size;
    const char *content_type;   /* NULL -> "application/octet-stream" when body != NULL */
    const char *const *headers; /* alternating name,value strings ({"Authorization","Bearer x",...});
                                 * sent as raw bytes — encode non-ASCII values yourself
                                 * (percent/base64) per usual HTTP practice. Validation differs
                                 * per backend (forbidden names, CORS, CR/LF) — see the spec's
                                 * divergence table */
    uint32_t header_count;      /* number of name/value PAIRS in headers */
    uint32_t timeout_ms;        /* 0 -> no timeout; clamped to INT_MAX ms (~24 days) */
} nt_http_options_t;

nt_result_t nt_http_init(void);
void nt_http_shutdown(void);
/* Pumps in-flight transfers on the native backend; no-op on web/stub. Call once per
 * frame while requests are in flight (nt_resource_step pumps for its own packs). */
void nt_http_update(void);
/* Returns NT_HTTP_REQUEST_INVALID when the module is uninitialized, url is NULL, or
 * all NT_HTTP_MAX_REQUESTS slots are busy (logged) — free finished requests to reuse slots. */
nt_http_request_t nt_http_request(const char *url);                                   /* GET shorthand */
nt_http_request_t nt_http_request_ex(const char *url, const nt_http_options_t *opts); /* opts NULL -> GET */
nt_http_state_t nt_http_state(nt_http_request_t req);
/* HTTP status code (200/404/...); 0 until the request completes (DONE or FAILED).
 * A FAILED request may still carry one (e.g. timeout mid-body). */
uint16_t nt_http_status(nt_http_request_t req);
/* Transport-level best effort while DOWNLOADING (compressed responses may make
 * received/total disagree with the decoded size, total may be 0 = unknown);
 * once DONE both equal the decoded body size. */
void nt_http_progress(nt_http_request_t req, uint32_t *received, uint32_t *total);
/* Response headers as "name: value\n" lines, names lowercased; NULL until the request
 * completes (and on a completion OOM). Pointer valid until nt_http_free or
 * nt_http_shutdown. Block shape differs per backend (duplicates, order, Set-Cookie)
 * — see the spec's divergence table. */
const char *nt_http_response_headers(nt_http_request_t req);
/* Transfers the completed response buffer to the caller; caller frees with free().
 * out_size may be NULL; when non-NULL it receives size or 0 on no transfer.
 * Returns NULL with size 0 when the request is not DONE, has no completed
 * buffer, was already taken, or the body is empty. */
uint8_t *nt_http_take_data(nt_http_request_t req, uint32_t *out_size);
/* Releases the request slot and frees only data that was not taken. */
void nt_http_free(nt_http_request_t req);

#endif /* NT_HTTP_H */
