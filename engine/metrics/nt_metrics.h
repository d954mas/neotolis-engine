#ifndef NT_METRICS_H
#define NT_METRICS_H

#include "core/nt_types.h"

/* Dev-only perf collector: the host pushes per-frame scalars; reads no clock itself. */

/* NT_METRICS_ENABLED=0 (release/OFF mirror) compiles no-op bodies for zero footprint. */
#ifndef NT_METRICS_ENABLED
#define NT_METRICS_ENABLED 1
#endif

/* Window depth per channel ring. 256 keeps the BSS footprint small; note p99.9
 * collapses to max below ~1000 samples (nearest-rank math) — raise to 1024
 * via -DNT_METRICS_WINDOW=1024 if a distinct 0.1%-low is needed. */
#ifndef NT_METRICS_WINDOW
#define NT_METRICS_WINDOW 256
#endif
/* head/count are uint16_t and count saturates AT the window, so WINDOW must fit in uint16_t (asserted). */
_Static_assert(NT_METRICS_WINDOW > 0 && NT_METRICS_WINDOW <= UINT16_MAX, "NT_METRICS_WINDOW must be in (0, UINT16_MAX]");

/* Max distinct user counters. Each carries one windowed ring + the last exact value. */
#ifndef NT_METRICS_MAX_USER_CHANNELS
#define NT_METRICS_MAX_USER_CHANNELS 16
#endif
/* nt_metrics_user_count() returns uint16_t, so MAX_USER_CHANNELS must fit in uint16_t (asserted). */
_Static_assert(NT_METRICS_MAX_USER_CHANNELS > 0 && NT_METRICS_MAX_USER_CHANNELS <= UINT16_MAX, "NT_METRICS_MAX_USER_CHANNELS must be in (0, UINT16_MAX]");

#ifndef NT_METRICS_USER_NAME_MAX
#define NT_METRICS_USER_NAME_MAX 32
#endif

/* Fixed channels. Pushed every frame from the host's nt_metrics_frame_t.
 * NT_METRICS_CHANNEL_COUNT bounds the fixed-channel arrays; user channels are
 * keyed separately by name hash. */
typedef enum {
    NT_METRICS_FRAME_MS = 0,
    NT_METRICS_CPU_MS,
    NT_METRICS_GPU_MS, /* pushed only when the host's gpu_ms >= 0; any negative value reads as absent (timer unsupported) and never enters the window (empty => samples:0) */
    NT_METRICS_DRAW_CALLS,
    NT_METRICS_MEM_USED, /* carries nt_metrics_frame_t.mem_used = platform resident/in-use bytes (not reserved) */
    NT_METRICS_SCRATCH_HWM,
    NT_METRICS_SCRATCH_USED,
    NT_METRICS_POOL_OCCUPANCY,
    NT_METRICS_CHANNEL_COUNT,
} nt_metrics_channel_t;

/* On-query windowed aggregates for one channel. When `samples == 0` the
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

/* Raw per-frame data the host pushes once per frame. The host owns all measurement
 * (its own clock for frame_ms/cpu_ms, gfx for gpu_ms/draw_calls, platform/scratch
 * for memory) — nt_metrics only stores + aggregates. */
typedef struct {
    float frame_ms;        /* host loop dt in ms; derives fps + the frame_ms channel; skipped if non-finite or <= 0 */
    float cpu_ms;          /* host-measured CPU frame time */
    float gpu_ms;          /* -1.0F sentinel when the GPU timer is unsupported; any negative value reads as absent (gpu channel skipped) */
    uint32_t draw_calls;   /* last frame's draw-call count */
    uint64_t mem_used;     /* platform memory in use */
    uint32_t scratch_hwm;  /* scratch high-water mark */
    uint32_t scratch_used; /* scratch in use this frame */
} nt_metrics_frame_t;

/* ---- Lifecycle ---- */

/* Zero all channel rings + user counters + the last-frame snapshot. */
void nt_metrics_init(void);

/* Clear the window (counts -> 0) without tearing down state (backs perf.reset). */
void nt_metrics_reset(void);

/* ---- User counters (this module owns them; the host sets them) ----
 * Keyed by full 64-bit name hash so prefix-31 collisions don't alias. */
void nt_metrics_count(const char *name, uint64_t value);
void nt_metrics_count_f(const char *name, double value);

/* ---- Per-frame sample (hot path: heap-free) ---- */
void nt_metrics_sample(const nt_metrics_frame_t *f);

/* ---- Read side for consumers (overlay + perf.snapshot) ---- */

/* Rolling-average fps derived from the frame_ms window; 0 on an empty window. */
float nt_metrics_fps(void);

/* The last frame pushed via nt_metrics_sample(), for the immediate perf.snapshot view.
 * Zero-filled (and gpu_ms = -1 sentinel) before the first sample. */
void nt_metrics_last(nt_metrics_frame_t *out);

/* ---- On-query aggregates (off hot path) ---- */

/* Compute windowed aggregates for a fixed channel into *out. Uses a fixed stack
 * scratch copy + qsort (no heap); the live ring is untouched. n==0 => samples:0 +
 * zeroed aggregates; n==1 => every percentile == that value. */
void nt_metrics_channel_stats(nt_metrics_channel_t channel, nt_metrics_stats_t *out);

/* fps-low helpers derived from the frame_ms channel.
 * 1%-low fps  = 1000.0 / p99(frame_ms)   (guarded: 0 when p99 <= 0)
 * 0.1%-low    = 1000.0 / p99.9(frame_ms) (guarded: 0 when p99.9 <= 0) */
double nt_metrics_fps_low_1pct(void);
double nt_metrics_fps_low_01pct(void);

/* Fraction (0..100) of frame_ms samples strictly greater than budget_ms.
 * Returns 0 on an empty window. budget_ms validation belongs to the L2 handler;
 * this computes for any finite budget. */
double nt_metrics_over_budget_pct(double budget_ms);

/* ---- User-counter enumeration ---- */

/* Count of distinct user counters currently stored (<= NT_METRICS_MAX_USER_CHANNELS). */
uint16_t nt_metrics_user_count(void);

/* Name of user counter `i`, or NULL if `i` is out of range. Stable within a frame. */
const char *nt_metrics_user_name(uint16_t i);

/* Read counter `i`'s EXACT last value. Out-params may be NULL to skip. For an int
 * counter `*u` is the exact uint64 and `*is_float` false; for a float counter `*f`
 * is the exact double and `*is_float` true. `i` out of range writes nothing. */
void nt_metrics_user_get(uint16_t i, const char **name, uint64_t *u, double *f, bool *is_float);

/* Windowed aggregates for user counter `i` into *out. Out-of-range `i` => samples:0. */
void nt_metrics_user_stats(uint16_t i, nt_metrics_stats_t *out);

#endif /* NT_METRICS_H */
