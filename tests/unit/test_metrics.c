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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ring_evicts_to_window);
    RUN_TEST(test_reset_clears_window);
    RUN_TEST(test_channels_independent);
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
