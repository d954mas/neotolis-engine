#pragma once

#include "window/nt_window.h" /* nt_window_add_pre_swap_hook, for the inline install helper below. */

/* Cap on concurrent in-flight captures, independent of the shared deferred queue. Bounds the per-seam
   encode burst + held base64 payloads a flooding client can trigger (a full 4096x4096 capture is tens of
   MiB each); raise with -D for a host that needs more pipelining. */
#ifndef NT_DEVAPI_CAPTURE_MAX_INFLIGHT
#define NT_DEVAPI_CAPTURE_MAX_INFLIGHT 4
#endif

/* Public host API for the devapi capture group (NT_DEVAPI_GROUP_CAPTURE), so a host never reaches into
   nt_devapi_internal.h. A host installs the seam once at startup; an enabled-but-unarmed host rejects
   captures with capture_unavailable. Full seam/timing contract: spec section 24.10. */

/* The seam: runs any ready capture producer at the GL-valid pre-swap point. The window-hook target; a
   host driving presentation manually may call it directly instead (then it must not also install the hook). */
void nt_devapi_capture_on_pre_swap(void);
void nt_devapi_capture_arm(void); /* mark the host capture-capable (called by install_seam). */

/* Install the seam (register the window pre-swap hook + mark capture-capable). static inline so the
   nt_window_add_pre_swap_hook reference lands in the host's TU (which links the real nt_window); the
   devapi lib links only the header-only nt_window_interface. Idempotent. */
static inline void nt_devapi_capture_install_seam(void) {
    /* Arm only if the hook actually registered: a full hook table (assert compiled out) would otherwise
       leave the host capture-capable with no seam, so captures would defer forever, not capture_unavailable. */
    if (nt_window_add_pre_swap_hook(nt_devapi_capture_on_pre_swap)) {
        nt_devapi_capture_arm();
    }
}
