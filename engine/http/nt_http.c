#include "http/nt_http.h"

#include <stdlib.h>
#include <string.h>

/* ---- Backend function (implemented in web/native/stub .c) ---- */

extern void nt_http_backend_request(uint16_t slot_index, const char *url);

/* ---- Slot data ---- */

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t received;
    uint32_t total;
    uint16_t generation;
    uint8_t state; /* nt_http_state_t */
    uint8_t _pad;
} NtHttpSlot;

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

    s_http.initialized = true;
    return NT_OK;
}

void nt_http_shutdown(void) {
    /* Free any remaining slot data */
    for (uint16_t i = 1; i <= NT_HTTP_MAX_REQUESTS; i++) {
        if (s_http.slots[i].data != NULL) {
            free(s_http.slots[i].data);
        }
    }
    memset(&s_http, 0, sizeof(s_http));
}

/* ---- Request management ---- */

nt_http_request_t nt_http_request(const char *url) {
    if (!s_http.initialized || url == NULL || s_http.queue_top == 0) {
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

    slot->data = NULL;
    slot->size = 0;
    slot->received = 0;
    slot->total = 0;
    slot->state = (uint8_t)NT_HTTP_STATE_PENDING;
    slot->_pad = 0;

    nt_http_backend_request(index, url);

    return http_make(index, slot->generation);
}

nt_http_state_t nt_http_state(nt_http_request_t req) {
    NtHttpSlot *slot = http_validate(req);
    if (!slot) {
        return NT_HTTP_STATE_NONE;
    }
    return (nt_http_state_t)slot->state;
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

    /* Free any remaining data */
    if (slot->data != NULL) {
        free(slot->data);
        slot->data = NULL;
    }

    /* Clear slot state */
    slot->size = 0;
    slot->received = 0;
    slot->total = 0;
    slot->state = (uint8_t)NT_HTTP_STATE_NONE;
    slot->_pad = 0;

    /* Increment generation so stale handles are rejected */
    slot->generation++;
    if (slot->generation == 0) {
        slot->generation = 1;
    }

    /* Return slot to free queue */
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
