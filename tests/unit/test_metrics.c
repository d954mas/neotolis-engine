/* L1 CTest for the nt_metrics perf collector (OBS-07/08), no devapi.
 * Feeds KNOWN sample sets via the NT_TEST_ACCESS push hook so percentiles are
 * asserted deterministically without a real frame loop. */

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "metrics/nt_metrics.h"
#include "unity.h"
/* clang-format on */

#if NT_METRICS_ENABLED

/* Unity's double/float asserts are compiled out in this config; compare with a
 * tolerance via TEST_ASSERT_TRUE (matches test_nt_ui_probe's near_eq pattern). */
static bool near(double a, double b) { return fabs(a - b) <= 1e-9; }

void setUp(void) { nt_metrics_init(); }
void tearDown(void) {}

/* ---- Task 1: ring + sample + reset ---- */

/* After WINDOW+5 pushes a channel holds exactly WINDOW samples (oldest evicted). */
static void test_ring_evicts_to_window(void) {
    for (int i = 0; i < NT_METRICS_WINDOW + 5; i++) {
        nt_metrics_test_push(NT_METRICS_FRAME_MS, (double)i);
    }
    nt_metrics_stats_t s;
    nt_metrics_channel_stats(NT_METRICS_FRAME_MS, &s);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)NT_METRICS_WINDOW, s.samples);
    /* Oldest (0..4) evicted; window now holds 5..WINDOW+4 => max == WINDOW+4. */
    TEST_ASSERT_TRUE(near(s.max, (double)(NT_METRICS_WINDOW + 4)));
    TEST_ASSERT_TRUE(near(s.min, 5.0));
}

/* nt_metrics_reset() zeroes counts: the next stats read sees samples==0. */
static void test_reset_clears_window(void) {
    for (int i = 0; i < 10; i++) {
        nt_metrics_test_push(NT_METRICS_CPU_MS, 1.0);
    }
    nt_metrics_reset();
    nt_metrics_stats_t s;
    nt_metrics_channel_stats(NT_METRICS_CPU_MS, &s);
    TEST_ASSERT_EQUAL_UINT32(0U, s.samples);
}

/* Channels are independent: pushing one does not bleed into another. */
static void test_channels_independent(void) {
    nt_metrics_test_push(NT_METRICS_FRAME_MS, 16.0);
    nt_metrics_stats_t s;
    nt_metrics_channel_stats(NT_METRICS_DRAW_CALLS, &s);
    TEST_ASSERT_EQUAL_UINT32(0U, s.samples);
}

/* ---- Task 2: on-query aggregates (nearest-rank percentiles + lows + budget) ---- */

/* KNOWN set 1..100 => avg 50.5, min 1, max 100, p50=50, p95=95, p99=99 (nearest-rank). */
static void test_percentiles_known_set(void) {
    for (int v = 1; v <= 100; v++) {
        nt_metrics_test_push(NT_METRICS_FRAME_MS, (double)v);
    }
    nt_metrics_stats_t s;
    nt_metrics_channel_stats(NT_METRICS_FRAME_MS, &s);
    TEST_ASSERT_EQUAL_UINT32(100U, s.samples);
    TEST_ASSERT_TRUE(near(s.avg, 50.5));
    TEST_ASSERT_TRUE(near(s.min, 1.0));
    TEST_ASSERT_TRUE(near(s.max, 100.0));
    TEST_ASSERT_TRUE(near(s.median, 50.0)); /* idx ceil(0.50*100)-1 = 49 -> value 50 */
    TEST_ASSERT_TRUE(near(s.p95, 95.0));    /* idx 94 -> value 95 */
    TEST_ASSERT_TRUE(near(s.p99, 99.0));    /* idx 98 -> value 99 */
}

/* Empty window: samples==0, aggregates zeroed (L2 emits null). */
static void test_empty_window(void) {
    nt_metrics_stats_t s;
    nt_metrics_channel_stats(NT_METRICS_GPU_MS, &s);
    TEST_ASSERT_EQUAL_UINT32(0U, s.samples);
    TEST_ASSERT_TRUE(near(s.avg, 0.0));
    TEST_ASSERT_TRUE(near(s.max, 0.0));
}

/* Single sample: every percentile == that value; avg==min==max==value. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_single_sample(void) {
    nt_metrics_test_push(NT_METRICS_CPU_MS, 42.0);
    nt_metrics_stats_t s;
    nt_metrics_channel_stats(NT_METRICS_CPU_MS, &s);
    TEST_ASSERT_EQUAL_UINT32(1U, s.samples);
    TEST_ASSERT_TRUE(near(s.avg, 42.0));
    TEST_ASSERT_TRUE(near(s.min, 42.0));
    TEST_ASSERT_TRUE(near(s.max, 42.0));
    TEST_ASSERT_TRUE(near(s.median, 42.0));
    TEST_ASSERT_TRUE(near(s.p95, 42.0));
    TEST_ASSERT_TRUE(near(s.p99, 42.0));
    TEST_ASSERT_TRUE(near(s.p99_9, 42.0));
}

/* fps-lows derived from frame_ms: feed 1..100ms => p99=99 => 1%-low = 1000/99. */
static void test_fps_lows(void) {
    for (int v = 1; v <= 100; v++) {
        nt_metrics_test_push(NT_METRICS_FRAME_MS, (double)v);
    }
    TEST_ASSERT_TRUE(near(nt_metrics_fps_low_1pct(), 1000.0 / 99.0));
    /* p99.9 collapses to max (100) at n=100 (A4): 0.1%-low = 1000/100 = 10. */
    TEST_ASSERT_TRUE(near(nt_metrics_fps_low_01pct(), 10.0));
}

/* fps-lows guard divide-by-zero on an empty window. */
static void test_fps_lows_empty(void) {
    TEST_ASSERT_TRUE(near(nt_metrics_fps_low_1pct(), 0.0));
    TEST_ASSERT_TRUE(near(nt_metrics_fps_low_01pct(), 0.0));
}

/* over_budget_pct: 1..100ms, budget 90 => 10 samples (91..100) over => 10%. */
static void test_over_budget_pct(void) {
    for (int v = 1; v <= 100; v++) {
        nt_metrics_test_push(NT_METRICS_FRAME_MS, (double)v);
    }
    TEST_ASSERT_TRUE(near(nt_metrics_over_budget_pct(90.0), 10.0));
    TEST_ASSERT_TRUE(near(nt_metrics_over_budget_pct(100.0), 0.0)); /* strictly greater */
    TEST_ASSERT_TRUE(near(nt_metrics_over_budget_pct(0.0), 100.0));
}

/* over_budget_pct on an empty window returns 0 (no divide-by-zero). */
static void test_over_budget_empty(void) { TEST_ASSERT_TRUE(near(nt_metrics_over_budget_pct(16.0), 0.0)); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ring_evicts_to_window);
    RUN_TEST(test_reset_clears_window);
    RUN_TEST(test_channels_independent);
    RUN_TEST(test_percentiles_known_set);
    RUN_TEST(test_empty_window);
    RUN_TEST(test_single_sample);
    RUN_TEST(test_fps_lows);
    RUN_TEST(test_fps_lows_empty);
    RUN_TEST(test_over_budget_pct);
    RUN_TEST(test_over_budget_empty);
    return UNITY_END();
}

#else /* NT_METRICS_ENABLED == 0 — stub-links main so the OFF mirror stays green */

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    return UNITY_END();
}

#endif /* NT_METRICS_ENABLED */
