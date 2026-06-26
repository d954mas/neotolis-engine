#pragma once

/* Public host seam for the devapi capture group (NT_DEVAPI_GROUP_CAPTURE).
 *
 * A managed host that enables the capture group MUST call this once per RENDERED frame, after render
 * work and BEFORE the buffer swap: each in-use deferred slot whose target frame has arrived and which
 * carries a producer runs that producer once to fill its payload (D-05 — the GL readback is only valid
 * at the pre-swap seam; the back buffer is undefined post-swap). Slots without a producer
 * (frame.wait/time.step) are untouched; idempotent per slot. Without this call a deferred
 * capture.frame/region never resolves.
 *
 * The symbol is generic producer-drain infrastructure that is ALWAYS present in the devapi core
 * (NT_DEVAPI_ENABLED), independent of NT_DEVAPI_GROUP_CAPTURE — it walks the core's private deferred
 * queue, so it cannot live behind the group's compile gate. With no producer-bearing group compiled
 * it finds no producer slots and safely no-ops; there is therefore NO link-time gate, so a host that
 * enables the capture group MUST wire this call by contract (see examples/capture_host).
 *
 * This is the ONLY capture symbol a host needs; it lives in a public header so a host never reaches
 * into nt_devapi_internal.h to drive captures.
 */
void nt_devapi_capture_on_pre_swap(void);
