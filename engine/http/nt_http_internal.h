#ifndef NT_HTTP_INTERNAL_H
#define NT_HTTP_INTERNAL_H

#include "http/nt_http.h"

/* ---- Slot data (single definition, shared by core + backends) ---- */

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t received;
    uint32_t total;
    uint16_t generation;
    uint8_t state; /* nt_http_state_t */
    uint8_t _pad;
} NtHttpSlot;

/* ---- Backend access ---- */

NtHttpSlot *nt_http_get_slot(uint16_t slot_index);

/* ---- Backend functions (implemented per platform) ---- */

void nt_http_backend_request(uint16_t slot_index, const char *url);
void nt_http_backend_cancel(uint16_t slot_index);

#endif /* NT_HTTP_INTERNAL_H */
