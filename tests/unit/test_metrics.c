/* L1 CTest for the nt_metrics perf collector (now the perf source of truth), no devapi.
 * Drives the REAL public paths: nt_metrics_count/_f for user counters and nt_metrics_sample(frame)
 * for the fixed channels, so percentiles/skip-guards/fps are asserted as the host would see them. */

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

/* clang-format off */
#include "core/nt_assert.h"
#include "metrics/nt_metrics.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"
/* clang-format on */

#if NT_METRICS_ENABLED

/* Unity's double/float asserts are compiled out in this config; compare with a tolerance. */
static bool near(double a, double b) { return fabs(a - b) <= 1e-9; }

void setUp(void) {
    nt_test_assert_install(); /* the collision death-test arms this; a stray assert elsewhere still traps loudly */
    nt_metrics_init();
}
void tearDown(void) {}

/* Push one frame carrying a given frame_ms; the other scalars are inert defaults (gpu -1 sentinel so
   it never enters the gpu window). Lets a test feed a KNOWN frame_ms set through the real sample path. */
static void push_frame_ms(double frame_ms) {
    nt_metrics_frame_t f = {.frame_ms = (float)frame_ms, .cpu_ms = 0.0F, .gpu_ms = -1.0F};
    nt_metrics_sample(&f);
}

/* ---- ring + sample + reset ---- */

/* After WINDOW+5 frames a channel holds exactly WINDOW samples (oldest evicted). */
static void test_ring_evicts_to_window(void) {
    for (int i = 0; i < NT_METRICS_WINDOW + 5; i++) {
        push_frame_ms((double)(i + 1)); /* 1..WINDOW+5 (skip 0: <= 0 is gated) */
    }
    nt_metrics_stats_t s;
    nt_metrics_channel_stats(NT_METRICS_FRAME_MS, &s);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)NT_METRICS_WINDOW, s.samples);
    /* Pushed 1..WINDOW+5; oldest 5 evicted => window holds 6..WINDOW+5. */
    TEST_ASSERT_TRUE(near(s.max, (double)(NT_METRICS_WINDOW + 5)));
    TEST_ASSERT_TRUE(near(s.min, 6.0));
}

/* nt_metrics_reset() zeroes counts: the next stats read sees samples==0. */
static void test_reset_clears_window(void) {
    for (int i = 0; i < 10; i++) {
        push_frame_ms(1.0);
    }
    nt_metrics_reset();
    nt_metrics_stats_t s;
    nt_metrics_channel_stats(NT_METRICS_FRAME_MS, &s);
    TEST_ASSERT_EQUAL_UINT32(0U, s.samples);
}

/* Channels are independent: a frame with gpu sentinel leaves gpu empty while cpu fills. */
static void test_channels_independent(void) {
    nt_metrics_frame_t f = {.frame_ms = 16.0F, .cpu_ms = 8.0F, .gpu_ms = -1.0F};
    nt_metrics_sample(&f);
    nt_metrics_stats_t gpu;
    nt_metrics_channel_stats(NT_METRICS_GPU_MS, &gpu);
    TEST_ASSERT_EQUAL_UINT32(0U, gpu.samples);
    nt_metrics_stats_t cpu;
    nt_metrics_channel_stats(NT_METRICS_CPU_MS, &cpu);
    TEST_ASSERT_EQUAL_UINT32(1U, cpu.samples);
}

/* ---- on-query aggregates (nearest-rank percentiles + lows + budget) ---- */

/* KNOWN set 1..100 => avg 50.5, min 1, max 100, p50=50, p95=95, p99=99 (nearest-rank). */
static void test_percentiles_known_set(void) {
    for (int v = 1; v <= 100; v++) {
        push_frame_ms((double)v);
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

/* fps-lows derived from frame_ms: feed 1..100ms => p99=99 => 1%-low = 1000/99. */
static void test_fps_lows(void) {
    for (int v = 1; v <= 100; v++) {
        push_frame_ms((double)v);
    }
    TEST_ASSERT_TRUE(near(nt_metrics_fps_low_1pct(), 1000.0 / 99.0));
    /* p99.9 collapses to max (100) at n=100: 0.1%-low = 1000/100 = 10. */
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
        push_frame_ms((double)v);
    }
    TEST_ASSERT_TRUE(near(nt_metrics_over_budget_pct(90.0), 10.0));
    TEST_ASSERT_TRUE(near(nt_metrics_over_budget_pct(100.0), 0.0)); /* strictly greater */
    TEST_ASSERT_TRUE(near(nt_metrics_over_budget_pct(0.0), 100.0));
}

/* over_budget_pct on an empty window returns 0 (no divide-by-zero). */
static void test_over_budget_empty(void) { TEST_ASSERT_TRUE(near(nt_metrics_over_budget_pct(16.0), 0.0)); }

/* ---- skip guards in nt_metrics_sample ---- */

/* gpu_ms < 0 sentinel must never enter the window (matches perf.snapshot's gpu_ms:null), while
   sibling channels still sample — proving the loop ran. */
static void test_gpu_sentinel_not_sampled(void) {
    for (int i = 0; i < 5; i++) {
        nt_metrics_frame_t f = {.frame_ms = 16.0F, .cpu_ms = 8.0F, .gpu_ms = -1.0F};
        nt_metrics_sample(&f);
    }
    nt_metrics_stats_t gpu;
    nt_metrics_channel_stats(NT_METRICS_GPU_MS, &gpu);
    TEST_ASSERT_EQUAL_UINT32(0U, gpu.samples);
    nt_metrics_stats_t cpu;
    nt_metrics_channel_stats(NT_METRICS_CPU_MS, &cpu);
    TEST_ASSERT_EQUAL_UINT32(5U, cpu.samples);
}

/* A real gpu sample (>= 0) DOES enter the window. */
static void test_gpu_real_sampled(void) {
    nt_metrics_frame_t f = {.frame_ms = 16.0F, .cpu_ms = 8.0F, .gpu_ms = 4.0F};
    nt_metrics_sample(&f);
    nt_metrics_stats_t gpu;
    nt_metrics_channel_stats(NT_METRICS_GPU_MS, &gpu);
    TEST_ASSERT_EQUAL_UINT32(1U, gpu.samples);
    TEST_ASSERT_TRUE(near(gpu.avg, 4.0));
}

/* frame_ms <= 0 / non-finite is the first-frame/garbage dt: it must NOT enter FRAME_MS (a 0 dt would
   poison fps), while cpu still samples. */
static void test_frame_ms_skips_nonpositive_and_nonfinite(void) {
    nt_metrics_frame_t z = {.frame_ms = 0.0F, .cpu_ms = 8.0F, .gpu_ms = -1.0F};
    nt_metrics_sample(&z); /* 0 -> skipped */
    nt_metrics_frame_t neg = {.frame_ms = -1.0F, .cpu_ms = 8.0F, .gpu_ms = -1.0F};
    nt_metrics_sample(&neg); /* < 0 -> skipped */
    nt_metrics_frame_t inf = {.frame_ms = INFINITY, .cpu_ms = 8.0F, .gpu_ms = -1.0F};
    nt_metrics_sample(&inf); /* non-finite -> skipped */
    nt_metrics_frame_t nan_f = {.frame_ms = NAN, .cpu_ms = 8.0F, .gpu_ms = -1.0F};
    nt_metrics_sample(&nan_f); /* non-finite -> skipped */

    nt_metrics_stats_t frame;
    nt_metrics_channel_stats(NT_METRICS_FRAME_MS, &frame);
    TEST_ASSERT_EQUAL_UINT32(0U, frame.samples); /* all four frame_ms skipped */
    nt_metrics_stats_t cpu;
    nt_metrics_channel_stats(NT_METRICS_CPU_MS, &cpu);
    TEST_ASSERT_EQUAL_UINT32(4U, cpu.samples); /* cpu sampled every frame */
}

/* ---- rolling fps ---- */

/* fps is the rolling avg over the frame_ms window: 60 frames at 16.667ms => ~60 fps; an empty
   window => 0. */
static void test_fps_rolling_avg(void) {
    TEST_ASSERT_TRUE(near((double)nt_metrics_fps(), 0.0)); /* empty window */
    for (int i = 0; i < 60; i++) {
        push_frame_ms(1000.0 / 60.0);
    }
    TEST_ASSERT_TRUE(fabs((double)nt_metrics_fps() - 60.0) < 0.5);
    /* Replace the window with 30 fps frames -> avg moves to ~30. */
    for (int i = 0; i < NT_METRICS_WINDOW; i++) {
        push_frame_ms(1000.0 / 30.0);
    }
    TEST_ASSERT_TRUE(fabs((double)nt_metrics_fps() - 30.0) < 0.5);
}

/* ---- last-frame snapshot ---- */

/* nt_metrics_last returns the last pushed frame verbatim (the perf.snapshot view). */
static void test_last_frame_snapshot(void) {
    nt_metrics_frame_t f = {.frame_ms = 12.5F, .cpu_ms = 7.25F, .gpu_ms = 3.5F, .draw_calls = 42U, .mem_used = 1234U, .scratch_hwm = 99U, .scratch_used = 11U};
    nt_metrics_sample(&f);
    nt_metrics_frame_t last;
    nt_metrics_last(&last);
    TEST_ASSERT_TRUE(near((double)last.frame_ms, 12.5));
    TEST_ASSERT_TRUE(near((double)last.cpu_ms, 7.25));
    TEST_ASSERT_TRUE(near((double)last.gpu_ms, 3.5));
    TEST_ASSERT_EQUAL_UINT32(42U, last.draw_calls);
}

/* ---- user counters: full-name hash keying via the REAL public path ---- */

/* NT_METRICS_USER_NAME_MAX is 32, so the display name truncates to the first 31 chars. Two names
   LONGER than 31 chars that differ WITHIN the first 31 must (a) truncate to DISTINCT displays and
   (b) carry distinct full 64-bit hashes — so they land in two counters, each holding its own value.
   This exercises the >31-char truncation path under hash keying; it would FAIL a buggy
   strcmp-on-truncated-name impl only if the names collided after truncation (see the next test),
   so the partner is the genuine truncation+display coverage. Driven through nt_metrics_count. */
static void test_user_counters_long_truncated_distinct(void) {
    /* 40 chars each, differ at index 4 (well inside the first 31) -> distinct 31-char displays. */
    const char *a = "physA_substep_accumulator_milliseconds_x";
    const char *b = "physB_substep_accumulator_milliseconds_x";
    TEST_ASSERT_TRUE(strlen(a) > (size_t)(NT_METRICS_USER_NAME_MAX - 1));
    TEST_ASSERT_TRUE(strlen(b) > (size_t)(NT_METRICS_USER_NAME_MAX - 1));
    nt_metrics_count(a, 10U);
    nt_metrics_count(b, 20U);
    TEST_ASSERT_EQUAL_UINT16(2U, nt_metrics_user_count());

    /* insertion-ordered: slot 0 is `a`, slot 1 is `b`. Each display is the truncated name (first 31
       chars) and each channel holds ONLY its own value. */
    const size_t disp = (size_t)(NT_METRICS_USER_NAME_MAX - 1);
    const char *n0 = NULL;
    const char *n1 = NULL;
    uint64_t u0 = 0;
    uint64_t u1 = 0;
    bool float0 = true;
    bool float1 = true;
    nt_metrics_user_get(0, &n0, &u0, NULL, &float0);
    nt_metrics_user_get(1, &n1, &u1, NULL, &float1);
    TEST_ASSERT_FALSE(float0);
    TEST_ASSERT_FALSE(float1);
    TEST_ASSERT_EQUAL_INT(0, strncmp(n0, a, disp));
    TEST_ASSERT_EQUAL_UINT64(10U, u0);
    TEST_ASSERT_EQUAL_INT(0, strncmp(n1, b, disp));
    TEST_ASSERT_EQUAL_UINT64(20U, u1);
    /* distinct truncated displays (differ within the first 31 chars). */
    TEST_ASSERT_TRUE(strncmp(n0, n1, disp) != 0);
}

/* Two names IDENTICAL in their first 31 chars but differing only at index >= 31 truncate to the SAME
   display while keeping DISTINCT full hashes — an ambiguous exposed name. The second nt_metrics_count
   (the new slot) must trip the display-collision NT_ASSERT. Uses the setjmp assert-trap harness. */
static void test_user_counter_truncated_collision_asserts(void) {
    /* Identical first 31 chars (the truncation window), differ only at index >= 31. */
    const char *a = "abcdefghijklmnopqrstuvwxyz01234_suffixA";
    const char *b = "abcdefghijklmnopqrstuvwxyz01234_suffixB";
    TEST_ASSERT_TRUE(strncmp(a, b, (size_t)(NT_METRICS_USER_NAME_MAX - 1)) == 0); /* same truncated display */
    TEST_ASSERT_TRUE(strcmp(a, b) != 0);                                          /* distinct full names -> distinct hashes */
    nt_metrics_count(a, 1U);
    TEST_ASSERT_EQUAL_UINT16(1U, nt_metrics_user_count());
    NT_TEST_EXPECT_ASSERT(nt_metrics_count(b, 2U)); /* new slot's display collides -> assert */
}

/* ---- exact uint64 round-trip past 2^53 (P2-A) ---- */

/* An int counter above 2^53 must survive nt_metrics_user_get EXACTLY (a double would lose the low
   bits). Prove the union keeps full uint64 precision. */
static void test_user_counter_uint64_exact(void) {
    const uint64_t big = (1ULL << 53) + 1ULL; /* not representable as a double */
    nt_metrics_count("huge", big);
    const char *name = NULL;
    uint64_t u = 0;
    double f = 0.0;
    bool is_float = true;
    nt_metrics_user_get(0, &name, &u, &f, &is_float);
    TEST_ASSERT_EQUAL_STRING("huge", name);
    TEST_ASSERT_FALSE(is_float);
    TEST_ASSERT_EQUAL_UINT64(big, u); /* exact: no double squash */
}

/* count_f keeps the double; re-writing a name flips its tag (last write wins). */
static void test_user_counter_float_and_tag_flip(void) {
    nt_metrics_count("v", 5U); /* int */
    const char *name = NULL;
    uint64_t u = 0;
    double f = 0.0;
    bool is_float = true;
    nt_metrics_user_get(0, &name, &u, &f, &is_float);
    TEST_ASSERT_FALSE(is_float);
    TEST_ASSERT_EQUAL_UINT64(5U, u);

    nt_metrics_count_f("v", 2.5); /* same name -> flips to float, single counter */
    nt_metrics_user_get(0, &name, &u, &f, &is_float);
    TEST_ASSERT_TRUE(is_float);
    TEST_ASSERT_TRUE(near(f, 2.5));
    TEST_ASSERT_EQUAL_UINT16(1U, nt_metrics_user_count());
}

/* user counter values flow into a windowed ring on each sample (backs perf.stats user_channels). */
static void test_user_counter_windowed(void) {
    nt_metrics_count_f("frame_ms", 10.0);
    nt_metrics_frame_t fr = {.frame_ms = 16.0F, .gpu_ms = -1.0F};
    nt_metrics_sample(&fr);
    nt_metrics_count_f("frame_ms", 20.0);
    nt_metrics_sample(&fr);
    TEST_ASSERT_EQUAL_UINT16(1U, nt_metrics_user_count());
    nt_metrics_stats_t s;
    nt_metrics_user_stats(0, &s);
    TEST_ASSERT_EQUAL_UINT32(2U, s.samples);
    TEST_ASSERT_TRUE(near(s.avg, 15.0)); /* (10 + 20) / 2 */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ring_evicts_to_window);
    RUN_TEST(test_reset_clears_window);
    RUN_TEST(test_channels_independent);
    RUN_TEST(test_percentiles_known_set);
    RUN_TEST(test_empty_window);
    RUN_TEST(test_fps_lows);
    RUN_TEST(test_fps_lows_empty);
    RUN_TEST(test_over_budget_pct);
    RUN_TEST(test_over_budget_empty);
    RUN_TEST(test_gpu_sentinel_not_sampled);
    RUN_TEST(test_gpu_real_sampled);
    RUN_TEST(test_frame_ms_skips_nonpositive_and_nonfinite);
    RUN_TEST(test_fps_rolling_avg);
    RUN_TEST(test_last_frame_snapshot);
    RUN_TEST(test_user_counters_long_truncated_distinct);
    RUN_TEST(test_user_counter_truncated_collision_asserts);
    RUN_TEST(test_user_counter_uint64_exact);
    RUN_TEST(test_user_counter_float_and_tag_flip);
    RUN_TEST(test_user_counter_windowed);
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
