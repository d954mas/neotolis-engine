#ifndef NT_DEVAPI_STUB_H
#define NT_DEVAPI_STUB_H

/* No-op stubs for NT_DEVAPI_ENABLED=0 builds. Engine call-sites (Phase 64
   poll/drain) include this instead of nt_devapi.h when devapi is compiled out,
   so the calls vanish to nothing with zero release cost. */

#include "core/nt_types.h"

static inline nt_result_t nt_devapi_init(void) { return NT_OK; }
static inline void nt_devapi_shutdown(void) {}
static inline const char *nt_devapi_submit(const char *line) {
    (void)line;
    return NULL;
}

#endif /* NT_DEVAPI_STUB_H */
