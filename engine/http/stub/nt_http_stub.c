#include "http/nt_http_internal.h"

/* Stub backend — immediately fail all requests */
bool nt_http_backend_init(void) { return true; }

void nt_http_backend_shutdown(void) {}

void nt_http_backend_update(void) {}

void nt_http_backend_request(uint16_t slot_index) {
    NtHttpSlot *slot = nt_http_get_slot(slot_index);
    if (slot) {
        slot->state = (uint8_t)NT_HTTP_STATE_FAILED;
    }
}

void nt_http_backend_cancel(uint16_t slot_index) { (void)slot_index; }
