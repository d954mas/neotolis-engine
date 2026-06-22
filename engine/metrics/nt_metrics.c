#include "metrics/nt_metrics.h"

#if NT_METRICS_ENABLED

#include "core/nt_assert.h"
#include "hash/nt_hash.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One ring per channel: head/count/wrap idiom. All values stored as double so
   aggregate math is uniform regardless of source. */
typedef struct {
    double ring[NT_METRICS_WINDOW];
    uint16_t head;  /* next write slot */
    uint16_t count; /* live samples, saturates at NT_METRICS_WINDOW */
} nt_metrics_ring_t;

/* head/count are uint16_t; count saturates AT the window size, so an override past UINT16_MAX would
   wrap silently. */
_Static_assert(NT_METRICS_WINDOW <= UINT16_MAX, "NT_METRICS_WINDOW must fit the uint16_t ring head/count");

static struct {
    nt_metrics_ring_t fixed[NT_METRICS_CHANNEL_COUNT];

    /* User counters, insertion-ordered + capped. Keyed by the FULL 64-bit name hash so two
       counters sharing the first 31 chars do not alias into one ring. The exact last value is
       kept in a tagged union so int counts survive past 2^53 and floats keep their decimals; the
       windowed ring (as double) backs perf.stats. The truncated name is for display/serialization. */
    nt_metrics_ring_t user_ring[NT_METRICS_MAX_USER_CHANNELS];
    uint64_t user_hashes[NT_METRICS_MAX_USER_CHANNELS];
    uint64_t user_u[NT_METRICS_MAX_USER_CHANNELS];
    double user_f[NT_METRICS_MAX_USER_CHANNELS];
    bool user_is_float[NT_METRICS_MAX_USER_CHANNELS];
    char user_names[NT_METRICS_MAX_USER_CHANNELS][NT_METRICS_USER_NAME_MAX];
    uint16_t user_count;

    nt_metrics_frame_t last; /* last frame pushed via nt_metrics_sample (snapshot view) */
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
void nt_metrics_init(void) {
    memset(&s_metrics, 0, sizeof(s_metrics));
    s_metrics.last.gpu_ms = -1.0F; /* sentinel until the first real gpu sample */
}

void nt_metrics_reset(void) {
    for (int c = 0; c < NT_METRICS_CHANNEL_COUNT; c++) {
        s_metrics.fixed[c].head = 0;
        s_metrics.fixed[c].count = 0;
    }
    for (uint16_t u = 0; u < s_metrics.user_count; u++) {
        s_metrics.user_ring[u].head = 0;
        s_metrics.user_ring[u].count = 0;
    }
}
// #endregion

// #region user counters (owned here)
/* Find a user counter slot by full name hash, or append a new one. Returns the slot index, or
   UINT16_MAX when the table is full (drop the counter — a dev-only collector overflow is non-fatal).
   A new slot asserts its truncated display name does not collide with an existing distinct counter's,
   so the exposed name stays unambiguous end to end. */
static uint16_t user_slot_for(const char *name) {
    uint64_t h = nt_hash64_str(name).value;
    for (uint16_t i = 0; i < s_metrics.user_count; i++) {
        if (s_metrics.user_hashes[i] == h) {
            return i;
        }
    }
    if (s_metrics.user_count >= NT_METRICS_MAX_USER_CHANNELS) {
        return UINT16_MAX;
    }
    uint16_t i = s_metrics.user_count;
    char display[NT_METRICS_USER_NAME_MAX];
    (void)snprintf(display, sizeof(display), "%s", name);
    for (uint16_t j = 0; j < i; j++) {
        NT_ASSERT(strcmp(s_metrics.user_names[j], display) != 0 && "nt_metrics: two distinct user counters share a truncated display name; shorten one or raise NT_METRICS_USER_NAME_MAX");
    }
    s_metrics.user_hashes[i] = h;
    memcpy(s_metrics.user_names[i], display, sizeof(display));
    s_metrics.user_count++;
    return i;
}

void nt_metrics_count(const char *name, uint64_t value) {
    NT_ASSERT(name != NULL);
    uint16_t i = user_slot_for(name);
    if (i == UINT16_MAX) {
        return;
    }
    s_metrics.user_u[i] = value;
    s_metrics.user_is_float[i] = false;
}

void nt_metrics_count_f(const char *name, double value) {
    NT_ASSERT(name != NULL);
    /* A non-finite counter is a caller bug: cJSON would serialize NaN/Inf as `null` and poison the
       aggregates. Reject at the boundary (dev-only) so the wire stays `number`. */
    NT_ASSERT(isfinite(value));
    uint16_t i = user_slot_for(name);
    if (i == UINT16_MAX) {
        return;
    }
    s_metrics.user_f[i] = value;
    s_metrics.user_is_float[i] = true;
}
// #endregion

// #region per-frame sample (hot path: heap-free)
void nt_metrics_sample(const nt_metrics_frame_t *f) {
    NT_ASSERT(f != NULL);
    /* cpu_ms/gpu_ms are real measurements: a non-finite value is a host bug, not a sentinel — assert
       it never reaches the wire as a cJSON `null` (frame_ms keeps its <=0 first-frame sentinel). */
    NT_ASSERT(isfinite(f->cpu_ms) && isfinite(f->gpu_ms));

    /* frame_ms <= 0 or non-finite is the first-frame/garbage dt: pushing it would poison the window
       (a 0 dt yields infinite fps, a huge dt a fps spike). Skip the frame_ms + derived-fps channel,
       still sample the rest so a sibling read proves the loop ran. */
    if (isfinite(f->frame_ms) && f->frame_ms > 0.0F) {
        ring_push(&s_metrics.fixed[NT_METRICS_FRAME_MS], (double)f->frame_ms);
    }
    ring_push(&s_metrics.fixed[NT_METRICS_CPU_MS], (double)f->cpu_ms);
    /* gpu_ms: the < 0 sentinel (timer unsupported) must NOT enter the window, else perf.stats would
       report a confident avg/min/max:-1 — contradicting perf.snapshot's gpu_ms:null contract. */
    if (f->gpu_ms >= 0.0F) {
        ring_push(&s_metrics.fixed[NT_METRICS_GPU_MS], (double)f->gpu_ms);
    }
    ring_push(&s_metrics.fixed[NT_METRICS_DRAW_CALLS], (double)f->draw_calls);
    ring_push(&s_metrics.fixed[NT_METRICS_MEM_TOTAL], (double)f->mem_used);
    ring_push(&s_metrics.fixed[NT_METRICS_SCRATCH_HWM], (double)f->scratch_hwm);
    ring_push(&s_metrics.fixed[NT_METRICS_SCRATCH_USED], (double)f->scratch_used);
    /* pool_occupancy: no portable provider yet — leave it unsampled so perf.stats reports samples:0 +
       null aggregates (an honest "unavailable", not a measured 0), mirroring the gpu_ms-absent path. */

    for (uint16_t i = 0; i < s_metrics.user_count; i++) {
        double v = s_metrics.user_is_float[i] ? s_metrics.user_f[i] : (double)s_metrics.user_u[i];
        ring_push(&s_metrics.user_ring[i], v);
    }

    s_metrics.last = *f;
}
// #endregion

// #region read side for consumers
float nt_metrics_fps(void) {
    double scratch[NT_METRICS_WINDOW];
    uint16_t n = ring_snapshot(&s_metrics.fixed[NT_METRICS_FRAME_MS], scratch);
    if (n == 0) {
        return 0.0F;
    }
    double sum_ms = 0.0;
    for (uint16_t i = 0; i < n; i++) {
        sum_ms += scratch[i];
    }
    /* rolling avg fps = frames / total seconds = 1000*n / sum(frame_ms). */
    return (sum_ms > 0.0) ? (float)(1000.0 * (double)n / sum_ms) : 0.0F;
}

void nt_metrics_last(nt_metrics_frame_t *out) {
    NT_ASSERT(out != NULL);
    *out = s_metrics.last;
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

void nt_metrics_user_get(uint16_t i, const char **name, uint64_t *u, double *f, bool *is_float) {
    if (i >= s_metrics.user_count) {
        return;
    }
    if (name != NULL) {
        *name = s_metrics.user_names[i];
    }
    if (u != NULL) {
        *u = s_metrics.user_u[i];
    }
    if (f != NULL) {
        *f = s_metrics.user_f[i];
    }
    if (is_float != NULL) {
        *is_float = s_metrics.user_is_float[i];
    }
}

void nt_metrics_user_stats(uint16_t i, nt_metrics_stats_t *out) {
    NT_ASSERT(out != NULL);
    if (i >= s_metrics.user_count) {
        *out = (nt_metrics_stats_t){0};
        return;
    }
    ring_stats(&s_metrics.user_ring[i], out);
}
// #endregion

#else /* NT_METRICS_ENABLED == 0 — zero-footprint no-op stubs for release/OFF mirror */

void nt_metrics_init(void) {}
void nt_metrics_reset(void) {}
void nt_metrics_count(const char *name, uint64_t value) {
    (void)name;
    (void)value;
}
void nt_metrics_count_f(const char *name, double value) {
    (void)name;
    (void)value;
}
void nt_metrics_sample(const nt_metrics_frame_t *f) { (void)f; }
float nt_metrics_fps(void) { return 0.0F; }
void nt_metrics_last(nt_metrics_frame_t *out) {
    if (out != NULL) {
        *out = (nt_metrics_frame_t){0};
        out->gpu_ms = -1.0F;
    }
}
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
void nt_metrics_user_get(uint16_t i, const char **name, uint64_t *u, double *f, bool *is_float) {
    (void)i;
    (void)name;
    (void)u;
    (void)f;
    (void)is_float;
}
void nt_metrics_user_stats(uint16_t i, nt_metrics_stats_t *out) {
    (void)i;
    if (out != NULL) {
        *out = (nt_metrics_stats_t){0};
    }
}

#endif /* NT_METRICS_ENABLED */
