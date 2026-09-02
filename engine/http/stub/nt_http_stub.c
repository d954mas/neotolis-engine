#include "http/nt_http_internal.h"

#ifdef NT_HTTP_STUB_IS_FALLBACK
#include "log/nt_log.h"
#endif

/* Stub backend — immediately fail all requests */
bool nt_http_backend_init(void) {
#ifdef NT_HTTP_STUB_IS_FALLBACK
    /* Compiled INTO nt_http under NT_HTTP_CURL=OFF (deliberate nt_http_stub
     * linkage stays silent — failing is its contract) */
    NT_LOG_WARN("native HTTP backend disabled (NT_HTTP_CURL=OFF) — all requests will FAIL");
#endif
    return true;
}

void nt_http_backend_shutdown(void) {}

void nt_http_backend_update(void) {}

void nt_http_backend_request(uint16_t slot_index) {
    NtHttpSlot *slot = nt_http_get_slot(slot_index);
    if (slot) {
        slot->state = (uint8_t)NT_HTTP_STATE_FAILED;
    }
}

void nt_http_backend_cancel(uint16_t slot_index) { (void)slot_index; }
