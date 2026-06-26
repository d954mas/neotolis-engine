#pragma once

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
void nt_devapi_capture_install_seam(void);
void nt_devapi_capture_on_pre_swap(void);
