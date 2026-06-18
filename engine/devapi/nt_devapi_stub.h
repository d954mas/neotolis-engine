#ifndef NT_DEVAPI_STUB_H
#define NT_DEVAPI_STUB_H

/* No-op stubs for NT_DEVAPI_ENABLED=0 builds: call-sites include this instead of
   nt_devapi.h, so devapi calls vanish at zero release cost. Signatures mirror the real API. */

#include <stdbool.h>
#include <stdint.h>

#include "core/nt_types.h"
#include "devapi/nt_devapi_types.h"

static inline nt_result_t nt_devapi_init(void) { return NT_OK; }
static inline void nt_devapi_shutdown(void) {}

static inline nt_result_t nt_devapi_register(const nt_devapi_command_desc *desc, nt_devapi_handler_fn handler, void *user_data) {
    (void)desc;
    (void)handler;
    (void)user_data;
    return NT_OK;
}

static inline const char *nt_devapi_submit(const char *line) {
    (void)line;
    return NULL;
}

static inline const char *nt_devapi_poll_response(void) { return NULL; }

static inline bool nt_devapi_net_start(uint16_t port) {
    (void)port;
    return false;
}
static inline void nt_devapi_net_poll(void) {}
static inline void nt_devapi_update(void) {}
static inline void nt_devapi_net_stop(void) {}
static inline bool nt_devapi_net_wait_for_client(uint32_t timeout_ms) {
    (void)timeout_ms;
    return false;
}

#endif /* NT_DEVAPI_STUB_H */
