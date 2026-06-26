#pragma once

/* Public host seam for the devapi capture group (NT_DEVAPI_GROUP_CAPTURE).
 *
 * A managed host that enables the capture group MUST call this once per RENDERED frame, after render
 * work and BEFORE the buffer swap: each in-use deferred slot whose target frame has arrived and which
 * carries a producer runs that producer once to fill its payload (D-05 — the GL readback is only valid
 * at the pre-swap seam; the back buffer is undefined post-swap). Slots without a producer
 * (frame.wait/time.step) are untouched; idempotent per slot. Without this call a deferred
 * capture.frame/region never resolves. The symbol is defined only when the capture group is compiled
 * into nt_devapi — a host that links it but omits the group gets a link-time signal.
 *
 * This is the ONLY capture symbol a host needs; it lives in a public header so a host never reaches
 * into nt_devapi_internal.h to drive captures (see examples/capture_host).
 */
void nt_devapi_capture_on_pre_swap(void);
