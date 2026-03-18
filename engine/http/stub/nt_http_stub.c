#include "http/nt_http_internal.h"

/* Stub backend — immediately fail all requests */
void nt_http_backend_request(uint16_t slot_index, const char *url) {
    (void)url;
    NtHttpSlot *slot = nt_http_get_slot(slot_index);
    if (slot) {
        slot->state = (uint8_t)NT_HTTP_STATE_FAILED;
    }
}

void nt_http_backend_cancel(uint16_t slot_index) { (void)slot_index; }
