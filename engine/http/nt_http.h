#ifndef NT_HTTP_H
#define NT_HTTP_H

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
    NT_HTTP_STATE_DONE,
    NT_HTTP_STATE_FAILED,
} nt_http_state_t;

nt_result_t nt_http_init(void);
void nt_http_shutdown(void);
nt_http_request_t nt_http_request(const char *url);
nt_http_state_t nt_http_state(nt_http_request_t req);
void nt_http_progress(nt_http_request_t req, uint32_t *received, uint32_t *total);
uint8_t *nt_http_take_data(nt_http_request_t req, uint32_t *out_size);
void nt_http_free(nt_http_request_t req);

#endif /* NT_HTTP_H */
