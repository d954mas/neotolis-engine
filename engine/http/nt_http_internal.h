#ifndef NT_HTTP_INTERNAL_H
#define NT_HTTP_INTERNAL_H

#include "http/nt_http.h"

/* ---- Slot data (single definition, shared by core + backends) ---- */

typedef struct {
    /* Request copies (owned by the module, freed on nt_http_free/shutdown) */
    char *url;
    char *method;
    uint8_t *body;
    uint32_t body_size;
    char *headers; /* alternating NUL-terminated name,value pairs */
    uint32_t headers_size;
    uint32_t header_pairs;
    uint32_t timeout_ms;
    /* Response */
    uint8_t *data;
    uint32_t size;
    uint32_t received;
    uint32_t total;
    char *resp_headers; /* "name: value\n" lines, lowercase names, NUL-terminated */
    uint16_t status;
    uint16_t generation;
    uint8_t state; /* nt_http_state_t */
} NtHttpSlot;

/* ---- Backend access ---- */

NtHttpSlot *nt_http_get_slot(uint16_t slot_index);

/* ---- Backend functions (implemented per platform) ----
 * Request parameters are read from the slot's request fields, which stay valid
 * until nt_http_free / nt_http_shutdown. */

void nt_http_backend_init(void);
void nt_http_backend_shutdown(void);
void nt_http_backend_request(uint16_t slot_index);
void nt_http_backend_cancel(uint16_t slot_index);
void nt_http_backend_update(void);

#endif /* NT_HTTP_INTERNAL_H */
