#include "metrics/nt_metrics.h"

#if NT_METRICS_ENABLED

#include "core/nt_assert.h"
#include "core/nt_platform.h"
#include "debug_overlay/nt_debug_overlay.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "memory/nt_mem_scratch.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One ring per channel: head/count/wrap idiom (mirrors nt_debug_overlay's fps_ring).
   All values stored as double so aggregate math is uniform regardless of source. */
typedef struct {
    double ring[NT_METRICS_WINDOW];
    uint16_t head;  /* next write slot */
    uint16_t count; /* live samples, saturates at NT_METRICS_WINDOW */
} nt_metrics_ring_t;

static struct {
    nt_metrics_ring_t fixed[NT_METRICS_CHANNEL_COUNT];

    /* User channels keyed by the FULL 64-bit name hash (matches the overlay's nt_hash64_str keying,
       nt_debug_overlay.c) so two counters sharing the first 31 chars do not alias into one ring. The
       truncated name copy is kept only for serialization. Insertion-ordered, capped. */
    nt_metrics_ring_t user[NT_METRICS_MAX_USER_CHANNELS];
    uint64_t user_hashes[NT_METRICS_MAX_USER_CHANNELS];
    char user_names[NT_METRICS_MAX_USER_CHANNELS][NT_METRICS_USER_NAME_MAX];
    uint16_t user_count;
} s_metrics;

// #region ring helpers
static void ring_push(nt_metrics_ring_t *r, double v) {
    r->ring[r->head] = v;
    r->head = (uint16_t)((r->head + 1) % NT_METRICS_WINDOW);
    if (r->count < NT_METRICS_WINDOW) {
        r->count++;
    }
}

/* Copy the live samples (oldest..newest) into out[], returns n. No heap. */
static uint16_t ring_snapshot(const nt_metrics_ring_t *r, double *out) {
    uint16_t n = r->count;
    /* Oldest sample sits at (head - count) mod WINDOW. */
    uint16_t idx = (uint16_t)((r->head + NT_METRICS_WINDOW - n) % NT_METRICS_WINDOW);
    for (uint16_t i = 0; i < n; i++) {
        out[i] = r->ring[idx];
        idx = (uint16_t)((idx + 1) % NT_METRICS_WINDOW);
    }
    return n;
}
// #endregion

// #region aggregates
static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }
    return 0;
}

/* Nearest-rank index into a sorted [0, n-1] array, clamped. */
static double percentile(const double *sorted, uint16_t n, double p) {
    int idx = (int)ceil(p / 100.0 * (double)n) - 1;
    if (idx < 0) {
        idx = 0;
    }
    if (idx > (int)n - 1) {
        idx = (int)n - 1;
    }
    return sorted[idx];
}

static void ring_stats(const nt_metrics_ring_t *r, nt_metrics_stats_t *out) {
    double scratch[NT_METRICS_WINDOW];
    NT_ASSERT(sizeof(scratch) / sizeof(scratch[0]) >= NT_METRICS_WINDOW);

    uint16_t n = ring_snapshot(r, scratch);
    if (n == 0) {
        *out = (nt_metrics_stats_t){0};
        return;
    }

    double sum = 0.0;
    for (uint16_t i = 0; i < n; i++) {
        sum += scratch[i];
    }
    qsort(scratch, n, sizeof(scratch[0]), cmp_double);

    out->samples = n;
    out->avg = sum / (double)n;
    out->min = scratch[0];
    out->max = scratch[n - 1];
    out->median = percentile(scratch, n, 50.0);
    out->p95 = percentile(scratch, n, 95.0);
    out->p99 = percentile(scratch, n, 99.0);
    out->p99_9 = percentile(scratch, n, 99.9);
}
// #endregion

// #region lifecycle
void nt_metrics_init(void) { memset(&s_metrics, 0, sizeof(s_metrics)); }

void nt_metrics_reset(void) {
    for (int c = 0; c < NT_METRICS_CHANNEL_COUNT; c++) {
        s_metrics.fixed[c].head = 0;
        s_metrics.fixed[c].count = 0;
    }
    for (uint16_t u = 0; u < s_metrics.user_count; u++) {
        s_metrics.user[u].head = 0;
        s_metrics.user[u].count = 0;
    }
}
// #endregion

// #region per-frame sample (heap-free)
/* Find or append a user channel ring by name. Returns NULL when the table is full
   (drop the counter — a dev-only collector overflow is non-fatal here, the overlay
   itself caps counters via NT_ASSERT at write time). */
static nt_metrics_ring_t *user_ring_for(const char *name) {
    uint64_t h = nt_hash64_str(name).value;
    for (uint16_t i = 0; i < s_metrics.user_count; i++) {
        if (s_metrics.user_hashes[i] == h) {
            return &s_metrics.user[i];
        }
    }
    if (s_metrics.user_count >= NT_METRICS_MAX_USER_CHANNELS) {
        return NULL;
    }
    uint16_t i = s_metrics.user_count++;
    s_metrics.user_hashes[i] = h;
    (void)snprintf(s_metrics.user_names[i], NT_METRICS_USER_NAME_MAX, "%s", name);
    return &s_metrics.user[i];
}

void nt_metrics_sample(void) {
    /* fps==0 means no frame recorded yet (overlay fps_count==0); pushing 1000/0 would poison the
       window with a ~1e9 ms outlier for the whole window. Skip like the gpu -1.0F sentinel below. */
    float fps = nt_debug_overlay_get_fps();
    if (fps > 0.0F) {
        ring_push(&s_metrics.fixed[NT_METRICS_FRAME_MS], (double)(1000.0F / fps));
    }
    ring_push(&s_metrics.fixed[NT_METRICS_CPU_MS], (double)nt_debug_overlay_get_cpu_ms());
    /* gpu_ms: only sample when a real timer is present. The -1.0F sentinel (timer unsupported) must
       NOT enter the window, else perf.stats would report avg/min/max:-1 — contradicting
       perf.snapshot's gpu_ms:null contract. An all-sentinel host leaves the window empty (samples:0). */
    float gpu = nt_debug_overlay_get_gpu_ms();
    if (gpu >= 0.0F) {
        ring_push(&s_metrics.fixed[NT_METRICS_GPU_MS], (double)gpu);
    }
    ring_push(&s_metrics.fixed[NT_METRICS_DRAW_CALLS], (double)nt_gfx_get_frame_draw_calls());
    ring_push(&s_metrics.fixed[NT_METRICS_MEM_TOTAL], (double)nt_platform_memory_usage().used);
    ring_push(&s_metrics.fixed[NT_METRICS_SCRATCH_HWM], (double)nt_mem_scratch_high_water_mark());
    ring_push(&s_metrics.fixed[NT_METRICS_SCRATCH_USED], (double)nt_mem_scratch_used());
    ring_push(&s_metrics.fixed[NT_METRICS_POOL_OCCUPANCY], 0.0); /* no portable pool occupancy source yet */

    uint16_t uc = nt_debug_overlay_user_count();
    for (uint16_t i = 0; i < uc; i++) {
        const char *name = NULL;
        double value = 0.0;
        bool is_float = false;
        nt_debug_overlay_user_get(i, &name, &value, &is_float);
        if (name != NULL) {
            nt_metrics_ring_t *r = user_ring_for(name);
            if (r != NULL) {
                ring_push(r, value);
            }
        }
    }
}
// #endregion

// #region on-query readers
void nt_metrics_channel_stats(nt_metrics_channel_t channel, nt_metrics_stats_t *out) {
    NT_ASSERT(out != NULL);
    NT_ASSERT(channel >= 0 && channel < NT_METRICS_CHANNEL_COUNT);
    ring_stats(&s_metrics.fixed[channel], out);
}

double nt_metrics_fps_low_1pct(void) {
    nt_metrics_stats_t s;
    ring_stats(&s_metrics.fixed[NT_METRICS_FRAME_MS], &s);
    return (s.samples > 0 && s.p99 > 0.0) ? (1000.0 / s.p99) : 0.0;
}

double nt_metrics_fps_low_01pct(void) {
    nt_metrics_stats_t s;
    ring_stats(&s_metrics.fixed[NT_METRICS_FRAME_MS], &s);
    return (s.samples > 0 && s.p99_9 > 0.0) ? (1000.0 / s.p99_9) : 0.0;
}

double nt_metrics_over_budget_pct(double budget_ms) {
    double scratch[NT_METRICS_WINDOW];
    uint16_t n = ring_snapshot(&s_metrics.fixed[NT_METRICS_FRAME_MS], scratch);
    if (n == 0) {
        return 0.0;
    }
    uint16_t over = 0;
    for (uint16_t i = 0; i < n; i++) {
        if (scratch[i] > budget_ms) {
            over++;
        }
    }
    return (double)over / (double)n * 100.0;
}

uint16_t nt_metrics_user_count(void) { return s_metrics.user_count; }

const char *nt_metrics_user_name(uint16_t i) {
    if (i >= s_metrics.user_count) {
        return NULL;
    }
    return s_metrics.user_names[i];
}

void nt_metrics_user_stats(uint16_t i, nt_metrics_stats_t *out) {
    NT_ASSERT(out != NULL);
    if (i >= s_metrics.user_count) {
        *out = (nt_metrics_stats_t){0};
        return;
    }
    ring_stats(&s_metrics.user[i], out);
}
// #endregion

#ifdef NT_TEST_ACCESS
void nt_metrics_test_push(nt_metrics_channel_t channel, double value) {
    NT_ASSERT(channel >= 0 && channel < NT_METRICS_CHANNEL_COUNT);
    ring_push(&s_metrics.fixed[channel], value);
}

/* Push into a user channel by FULL name (find-or-append by hash), bypassing the overlay's 32-char
   name truncation so a test can prove the metrics ring keys by full hash, not a truncated strcmp. */
void nt_metrics_test_push_user(const char *name, double value) {
    nt_metrics_ring_t *r = user_ring_for(name);
    if (r != NULL) {
        ring_push(r, value);
    }
}
#endif

#else /* NT_METRICS_ENABLED == 0 — zero-footprint no-op stubs for release/OFF mirror */

void nt_metrics_init(void) {}
void nt_metrics_reset(void) {}
void nt_metrics_sample(void) {}
void nt_metrics_channel_stats(nt_metrics_channel_t channel, nt_metrics_stats_t *out) {
    (void)channel;
    if (out != NULL) {
        *out = (nt_metrics_stats_t){0};
    }
}
double nt_metrics_fps_low_1pct(void) { return 0.0; }
double nt_metrics_fps_low_01pct(void) { return 0.0; }
double nt_metrics_over_budget_pct(double budget_ms) {
    (void)budget_ms;
    return 0.0;
}
uint16_t nt_metrics_user_count(void) { return 0; }
const char *nt_metrics_user_name(uint16_t i) {
    (void)i;
    return NULL;
}
void nt_metrics_user_stats(uint16_t i, nt_metrics_stats_t *out) {
    (void)i;
    if (out != NULL) {
        *out = (nt_metrics_stats_t){0};
    }
}

#endif /* NT_METRICS_ENABLED */
