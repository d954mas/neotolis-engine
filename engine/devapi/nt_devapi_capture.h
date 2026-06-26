#pragma once

#include "window/nt_window.h" /* nt_window_add_pre_swap_hook, for the inline install helper below. */

/* Public host API for the devapi capture group (NT_DEVAPI_GROUP_CAPTURE).
 *
 * A host that enables the capture group calls nt_devapi_capture_install_seam() ONCE at startup (after
 * nt_devapi_init + a window/GL context). It registers the pre-swap capture seam as a WINDOW pre-swap
 * hook and marks the host capture-capable. The host then just renders + calls nt_window_swap_buffers()
 * as usual — the seam runs automatically inside swap_buffers, while the freshly-rendered back buffer is
 * still valid (D-05; the back buffer is undefined post-swap). There is NO per-frame call to forget, and
 * a host that never installs the seam rejects captures with {error:capture_unavailable} (never hangs).
 *
 * nt_devapi_capture_on_pre_swap is the seam itself — the registered hook target. It is also exposed for
 * a host that drives presentation manually and prefers to call it directly between render and swap
 * instead of via the window hook (such a host should then NOT also install the hook). It walks the
 * devapi core's deferred queue and runs any ready capture producer at the GL-valid point; slots without
 * a producer (frame.wait/time.step) are untouched, and with no producer-bearing group compiled it safely
 * no-ops. These public symbols mean a host never reaches into nt_devapi_internal.h to drive captures.
 */
void nt_devapi_capture_on_pre_swap(void);
void nt_devapi_capture_arm(void); /* mark the host capture-capable (called by install_seam below). */

/* Install the capture seam: register it as a window pre-swap hook + mark the host capture-capable.
   static inline so the nt_window_add_pre_swap_hook reference lands in the HOST's translation unit (which
   links the real nt_window) — the devapi lib itself links only the header-only nt_window_interface, so a
   devapi unit test never needs nt_window. Idempotent; a host that never calls it rejects captures with
   capture_unavailable instead of hanging. */
static inline void nt_devapi_capture_install_seam(void) {
    nt_window_add_pre_swap_hook(nt_devapi_capture_on_pre_swap);
    nt_devapi_capture_arm();
}
