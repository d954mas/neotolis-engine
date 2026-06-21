#ifndef NT_METRICS_H
#define NT_METRICS_H

#include "core/nt_types.h"

/* Layer-1 perf collector (D-06/07, OBS-07/08). A bounded, no-heap, dev-only
 * per-frame sampler: nt_metrics_sample() reads existing sources once and stores
 * one double per channel into fixed BSS rings; windowed aggregates compute on
 * query off the hot path via a sorted scratch copy. The L2 perf.* devapi group
 * (Plan 06) wraps this; the channel set + stats field names/widths below are the
 * stable serialization contract.
 *
 * Dev-only: NT_METRICS_ENABLED=0 (release/OFF mirror) compiles real no-op bodies,
 * so the collector ships zero footprint in release. Default ON in debug. The gate
 * is independent of NT_DEVAPI_ENABLED (D-06). */
#ifndef NT_METRICS_ENABLED
#define NT_METRICS_ENABLED 1
#endif

/* Window depth per channel ring. 256 keeps the BSS footprint small; note p99.9
 * collapses to max below ~1000 samples (nearest-rank math, A4) — raise to 1024
 * via -DNT_METRICS_WINDOW=1024 if a distinct 0.1%-low is needed. */
#ifndef NT_METRICS_WINDOW
#define NT_METRICS_WINDOW 256
#endif

/* Max user channels mirrored from overlay counters. Matches the overlay's
 * NT_DEBUG_OVERLAY_MAX_USER_COUNTERS so every counter has a ring. */
#ifndef NT_METRICS_MAX_USER_CHANNELS
#define NT_METRICS_MAX_USER_CHANNELS 16
#endif

#ifndef NT_METRICS_USER_NAME_MAX
#define NT_METRICS_USER_NAME_MAX 32
#endif

/* Fixed channels (D-07). Sampled every frame from existing engine sources.
 * NT_METRICS_CHANNEL_COUNT bounds the fixed-channel arrays; user channels are
 * keyed separately by name. */
typedef enum {
    NT_METRICS_FRAME_MS = 0,
    NT_METRICS_CPU_MS,
    NT_METRICS_GPU_MS, /* sampled only when a real timer is present; -1.0F sentinel never enters the window (empty => samples:0) */
    NT_METRICS_DRAW_CALLS,
    NT_METRICS_MEM_TOTAL,
    NT_METRICS_SCRATCH_HWM,
    NT_METRICS_SCRATCH_USED,
    NT_METRICS_POOL_OCCUPANCY,
    NT_METRICS_CHANNEL_COUNT,
} nt_metrics_channel_t;

/* On-query windowed aggregates for one channel (OBS-08). When `samples == 0` the
 * caller treats the numeric fields as null (the L2 handler emits JSON null) — the
 * struct still zero-fills them. percentiles are nearest-rank over the live window. */
typedef struct {
    uint32_t samples; /* n in [0, NT_METRICS_WINDOW]; 0 => empty window */
    double avg;
    double min;
    double max;
    double median; /* p50 */
    double p95;
    double p99;
    double p99_9;
} nt_metrics_stats_t;

/* ---- Lifecycle ---- */

/* Zero all channel rings + user channels. */
void nt_metrics_init(void);

/* Clear the window (counts -> 0) without tearing down state (D-11 perf.reset). */
void nt_metrics_reset(void);

/* ---- Per-frame sample (hot path: heap-free) ----
 * Reads the fixed sources once (overlay getters, gfx draw_calls, scratch hwm/used,
 * nt_platform memory) and every overlay user counter, writing one double per channel
 * into its ring. Call EXPLICITLY right after nt_debug_overlay_frame_end() (D-07). */
void nt_metrics_sample(void);

/* ---- On-query aggregates (off hot path) ---- */

/* Compute windowed aggregates for a fixed channel into *out. Uses a fixed stack
 * scratch copy + qsort (no heap); the live ring is untouched. n==0 => samples:0 +
 * zeroed aggregates; n==1 => every percentile == that value. */
void nt_metrics_channel_stats(nt_metrics_channel_t channel, nt_metrics_stats_t *out);

/* fps-low helpers derived from the frame_ms channel (OBS-08).
 * 1%-low fps  = 1000.0 / p99(frame_ms)   (guarded: 0 when p99 <= 0)
 * 0.1%-low    = 1000.0 / p99.9(frame_ms) (guarded: 0 when p99.9 <= 0) */
double nt_metrics_fps_low_1pct(void);
double nt_metrics_fps_low_01pct(void);

/* Fraction (0..100) of frame_ms samples strictly greater than budget_ms.
 * Returns 0 on an empty window. budget_ms validation belongs to the L2 handler;
 * this computes for any finite budget. */
double nt_metrics_over_budget_pct(double budget_ms);

/* ---- User-channel enumeration (mirrors overlay counters into rings) ---- */

/* Count of distinct user channels currently sampled (<= NT_METRICS_MAX_USER_CHANNELS). */
uint16_t nt_metrics_user_count(void);

/* Name of user channel `i`, or NULL if `i` is out of range. Stable within a frame. */
const char *nt_metrics_user_name(uint16_t i);

/* Windowed aggregates for user channel `i` into *out. Out-of-range `i` => samples:0. */
void nt_metrics_user_stats(uint16_t i, nt_metrics_stats_t *out);

// #region test_access
/* Push a raw value into a fixed channel's ring without a real frame loop, so unit
 * tests feed KNOWN sample sets and assert percentiles deterministically. */
#ifdef NT_TEST_ACCESS
void nt_metrics_test_push(nt_metrics_channel_t channel, double value);
#endif
// #endregion

#endif /* NT_METRICS_H */
