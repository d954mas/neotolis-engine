#include "http/nt_http.h"

/* ---- Slot data (defined in nt_http.c) ---- */

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t received;
    uint32_t total;
    uint16_t generation;
    uint8_t state; /* nt_http_state_t */
    uint8_t _pad;
} NtHttpSlot;

extern NtHttpSlot *nt_http_get_slot(uint16_t slot_index);

/* Stub backend — immediately fail all requests */
void nt_http_backend_request(uint16_t slot_index, const char *url) {
    (void)url;
    NtHttpSlot *slot = nt_http_get_slot(slot_index);
    if (slot) {
        slot->state = (uint8_t)NT_HTTP_STATE_FAILED;
    }
}
