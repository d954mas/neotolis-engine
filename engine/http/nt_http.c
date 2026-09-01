#include "http/nt_http_internal.h"

#include "core/nt_assert.h"
#include "log/nt_log.h"

#include <stdlib.h>
#include <string.h>

/* ---- Module state ---- */

static struct {
    NtHttpSlot slots[NT_HTTP_MAX_REQUESTS + 1]; /* index 0 reserved */
    uint16_t free_queue[NT_HTTP_MAX_REQUESTS];
    uint16_t queue_top;
    bool initialized;
} s_http;

/* ---- Handle encoding: lower 16 bits = slot index, upper 16 bits = generation ---- */

static inline nt_http_request_t http_make(uint16_t index, uint16_t gen) { return (nt_http_request_t){.id = ((uint32_t)gen << 16) | index}; }

static inline uint16_t http_slot_index(nt_http_request_t r) { return (uint16_t)(r.id & 0xFFFF); }

static inline uint16_t http_generation(nt_http_request_t r) { return (uint16_t)(r.id >> 16); }

/* ---- Handle validation ---- */

static NtHttpSlot *http_validate(nt_http_request_t req) {
    uint16_t index = http_slot_index(req);
    uint16_t gen = http_generation(req);
    if (index == 0 || index > NT_HTTP_MAX_REQUESTS) {
        return NULL;
    }
    if (s_http.slots[index].generation != gen) {
        return NULL; /* stale handle */
    }
    return &s_http.slots[index];
}

/* ---- Request copy helpers ---- */

static char *http_strdup(const char *src) {
    size_t len = strlen(src) + 1;
    char *dst = malloc(len);
    NT_ASSERT(dst != NULL);
    memcpy(dst, src, len);
    return dst;
}

static void http_free_slot_buffers(NtHttpSlot *slot) {
    free(slot->url);
    free(slot->method);
    free(slot->body);
    free(slot->headers);
    free(slot->data);
    free(slot->resp_headers);
    slot->url = NULL;
    slot->method = NULL;
    slot->body = NULL;
    slot->headers = NULL;
    slot->data = NULL;
    slot->resp_headers = NULL;
}

static bool http_iequals(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca + ('a' - 'A'));
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb + ('a' - 'A'));
        }
        if (ca != cb) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/* Copies request headers into one blob of alternating NUL-terminated name,value
 * strings, appending a Content-Type pair when a body is present and the caller
 * did not already provide one. */
static void http_copy_headers(NtHttpSlot *slot, const nt_http_options_t *opts, const char *content_type) {
    size_t pairs = opts ? opts->header_count : 0;
    size_t total = 0;
    for (size_t i = 0; i < pairs * 2; i++) {
        total += strlen(opts->headers[i]) + 1;
    }
    if (content_type != NULL) {
        total += sizeof("Content-Type") + strlen(content_type) + 1;
    }
    if (total == 0) {
        return;
    }

    char *blob = malloc(total);
    NT_ASSERT(blob != NULL);
    char *p = blob;
    for (size_t i = 0; i < pairs * 2; i++) {
        size_t len = strlen(opts->headers[i]) + 1;
        memcpy(p, opts->headers[i], len);
        p += len;
    }
    if (content_type != NULL) {
        memcpy(p, "Content-Type", sizeof("Content-Type"));
        p += sizeof("Content-Type");
        memcpy(p, content_type, strlen(content_type) + 1);
    }

    slot->headers = blob;
    slot->headers_size = (uint32_t)total;
    slot->header_pairs = (uint32_t)pairs + (content_type != NULL ? 1U : 0U);
}

/* Content-Type to append for a body; NULL when there is no body or the caller
 * already passed one as an explicit header pair. Keyed off opts->body (caller
 * intent), not the copied bytes — a zero-size body keeps its Content-Type. */
static const char *http_default_content_type(const nt_http_options_t *opts) {
    if (opts == NULL || opts->body == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < opts->header_count; i++) {
        if (http_iequals(opts->headers[i * 2], "Content-Type")) {
            return NULL;
        }
    }
    return opts->content_type != NULL ? opts->content_type : "application/octet-stream";
}

/* fetch() normalizes these six methods to uppercase (and only these — PATCH is
 * deliberately case-preserved by the spec); mirror it so both backends send the
 * same bytes for method = "put" etc. */
static const char *http_normalize_method(const char *method) {
    static const char *const known[] = {"DELETE", "GET", "HEAD", "OPTIONS", "POST", "PUT"};
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (http_iequals(method, known[i])) {
            return known[i];
        }
    }
    return method;
}

/* Copies method/body/headers/timeout out of opts so the caller keeps ownership. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — NT_ASSERT expansions
static void http_copy_request(NtHttpSlot *slot, const char *url, const nt_http_options_t *opts) {
    NT_ASSERT(opts == NULL || opts->header_count == 0 || opts->headers != NULL);
    slot->url = http_strdup(url);
    slot->method = http_strdup(http_normalize_method(opts != NULL && opts->method != NULL ? opts->method : "GET"));
    slot->body = NULL;
    slot->body_size = 0;
    if (opts != NULL && opts->body != NULL && opts->body_size > 0) {
        /* A body on GET/HEAD is a caller bug: fetch rejects it, curl would silently
         * reinterpret it as POST — crash early instead of diverging per backend. */
        NT_ASSERT(!http_iequals(slot->method, "GET") && !http_iequals(slot->method, "HEAD"));
        slot->body = malloc(opts->body_size);
        NT_ASSERT(slot->body != NULL);
        memcpy(slot->body, opts->body, opts->body_size);
        slot->body_size = opts->body_size;
    }

    slot->headers = NULL;
    slot->headers_size = 0;
    slot->header_pairs = 0;
    http_copy_headers(slot, opts, http_default_content_type(opts));

    slot->timeout_ms = opts != NULL ? opts->timeout_ms : 0;
}

/* ---- Lifecycle ---- */

nt_result_t nt_http_init(void) {
    if (s_http.initialized) {
        return NT_ERR_INIT_FAILED;
    }

    memset(&s_http, 0, sizeof(s_http));

    /* Fill free queue: stack with lowest index on top (first alloc gets 1) */
    s_http.queue_top = NT_HTTP_MAX_REQUESTS;
    for (uint16_t i = 0; i < NT_HTTP_MAX_REQUESTS; i++) {
        s_http.free_queue[i] = (uint16_t)(NT_HTTP_MAX_REQUESTS - i);
    }

    if (!nt_http_backend_init()) {
        memset(&s_http, 0, sizeof(s_http));
        return NT_ERR_INIT_FAILED;
    }
    s_http.initialized = true;
    return NT_OK;
}

void nt_http_shutdown(void) {
    /* Guard double-shutdown: backend teardown (curl_global_cleanup) must pair 1:1 with init */
    if (!s_http.initialized) {
        return;
    }
    nt_http_backend_shutdown();
    for (uint16_t i = 1; i <= NT_HTTP_MAX_REQUESTS; i++) {
        http_free_slot_buffers(&s_http.slots[i]);
    }
    memset(&s_http, 0, sizeof(s_http));
}

void nt_http_update(void) {
    if (!s_http.initialized) {
        return;
    }
    nt_http_backend_update();
}

/* ---- Request management ---- */

nt_http_request_t nt_http_request(const char *url) { return nt_http_request_ex(url, NULL); }

nt_http_request_t nt_http_request_ex(const char *url, const nt_http_options_t *opts) {
    if (!s_http.initialized || url == NULL || s_http.queue_top == 0) {
        /* Pool exhaustion is a silent INVALID otherwise — indistinguishable from bad args */
        if (s_http.initialized && url != NULL) {
            NT_LOG_WARN("all %d request slots busy, dropping %s", NT_HTTP_MAX_REQUESTS, url);
        }
        return NT_HTTP_REQUEST_INVALID;
    }

    /* Allocate slot from free queue */
    s_http.queue_top--;
    uint16_t index = s_http.free_queue[s_http.queue_top];

    NtHttpSlot *slot = &s_http.slots[index];

    /* Increment generation (skip 0: reserved for invalid handles) */
    slot->generation++;
    if (slot->generation == 0) {
        slot->generation = 1;
    }

    http_copy_request(slot, url, opts);

    slot->data = NULL;
    slot->size = 0;
    slot->received = 0;
    slot->total = 0;
    slot->resp_headers = NULL;
    slot->status = 0;
    slot->state = (uint8_t)NT_HTTP_STATE_PENDING;

    nt_http_backend_request(index);

    return http_make(index, slot->generation);
}

nt_http_state_t nt_http_state(nt_http_request_t req) {
    NtHttpSlot *slot = http_validate(req);
    if (!slot) {
        return NT_HTTP_STATE_NONE;
    }
    return (nt_http_state_t)slot->state;
}

uint16_t nt_http_status(nt_http_request_t req) {
    NtHttpSlot *slot = http_validate(req);
    if (!slot) {
        return 0;
    }
    return slot->status;
}

void nt_http_progress(nt_http_request_t req, uint32_t *received, uint32_t *total) {
    NtHttpSlot *slot = http_validate(req);
    if (!slot) {
        if (received) {
            *received = 0;
        }
        if (total) {
            *total = 0;
        }
        return;
    }
    if (received) {
        *received = slot->received;
    }
    if (total) {
        *total = slot->total;
    }
}

const char *nt_http_response_headers(nt_http_request_t req) {
    NtHttpSlot *slot = http_validate(req);
    if (!slot) {
        return NULL;
    }
    return slot->resp_headers;
}

uint8_t *nt_http_take_data(nt_http_request_t req, uint32_t *out_size) {
    NtHttpSlot *slot = http_validate(req);
    if (!slot || slot->state != (uint8_t)NT_HTTP_STATE_DONE) {
        if (out_size) {
            *out_size = 0;
        }
        return NULL;
    }

    uint8_t *ptr = slot->data;
    uint32_t sz = slot->size;
    slot->data = NULL;
    slot->size = 0;
    /* Canonical empty contract: web hands over malloc(0) (non-NULL under emmalloc),
     * native hands over NULL — normalize so both backends return NULL/0 */
    if (sz == 0) {
        free(ptr);
        ptr = NULL;
    }

    if (out_size) {
        *out_size = sz;
    }
    return ptr;
}

void nt_http_free(nt_http_request_t req) {
    uint16_t index = http_slot_index(req);
    NtHttpSlot *slot = http_validate(req);
    if (!slot) {
        return;
    }

    /* Cancel in-flight backend request (aborts fetch on web, curl transfer on native) */
    if (slot->state == (uint8_t)NT_HTTP_STATE_PENDING || slot->state == (uint8_t)NT_HTTP_STATE_DOWNLOADING) {
        nt_http_backend_cancel(index);
    }

    http_free_slot_buffers(slot);

    /* Clear slot state */
    slot->body_size = 0;
    slot->headers_size = 0;
    slot->header_pairs = 0;
    slot->timeout_ms = 0;
    slot->size = 0;
    slot->received = 0;
    slot->total = 0;
    slot->status = 0;
    slot->state = (uint8_t)NT_HTTP_STATE_NONE;

    /* Increment generation so stale handles are rejected */
    slot->generation++;
    if (slot->generation == 0) {
        slot->generation = 1;
    }

    /* Return slot to free queue. A full queue means a double-free through a
     * generation-wrapped stale handle — crash early instead of writing OOB. */
    NT_ASSERT(s_http.queue_top < NT_HTTP_MAX_REQUESTS);
    s_http.free_queue[s_http.queue_top] = index;
    s_http.queue_top++;
}

/* ---- Backend access (for web callbacks) ---- */

NtHttpSlot *nt_http_get_slot(uint16_t slot_index) {
    if (slot_index == 0 || slot_index > NT_HTTP_MAX_REQUESTS) {
        return NULL;
    }
    return &s_http.slots[slot_index];
}
