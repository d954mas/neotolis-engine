/* Web frame-cap pacing replayed on synthetic RAF clocks: a jittered tick stream per display rate
   is fed to nt_app_pace_tick and the frames it lets through are counted. */

#include "app/nt_app_pace_internal.h"
#include "unity.h"

#include <stdint.h>

#define TARGET_60_MS (1000.0 / 60.0)
#define JITTER_MS 0.5

typedef struct pace_run_t {
    double next_ms;
    uint32_t rng;
    int ticks;
    int frames;
    double last_frame_ms;
    double min_interval_ms;
} pace_run_t;

void setUp(void) {}

void tearDown(void) {}

static pace_run_t pace_run_new(void) { return (pace_run_t){.rng = 0x2545F491U, .last_frame_ms = -1.0, .min_interval_ms = 1e9}; }

/* Deterministic uniform jitter in [-JITTER_MS, JITTER_MS]. */
static double pace_jitter(pace_run_t *run) {
    run->rng = run->rng * 1664525U + 1013904223U;
    return ((double)(run->rng >> 8) / (double)(1U << 24) * 2.0 - 1.0) * JITTER_MS;
}

/* Feeds ticks of a `hz` display from start_ms for `seconds`; returns frames run in that span. */
static int pace_feed(pace_run_t *run, double hz, double start_ms, double seconds, double target_ms) {
    int frames = 0;
    int count = (int)(seconds * hz);
    for (int i = 0; i < count; i++) {
        double now = start_ms + ((double)i * 1000.0 / hz) + pace_jitter(run);
        run->ticks++;
        if (!nt_app_pace_tick(&run->next_ms, now, target_ms)) {
            continue;
        }
        if (run->last_frame_ms >= 0.0 && now - run->last_frame_ms < run->min_interval_ms) {
            run->min_interval_ms = now - run->last_frame_ms;
        }
        run->last_frame_ms = now;
        run->frames++;
        frames++;
    }
    return frames;
}

void test_faster_displays_run_at_target_rate(void) {
    static const double rates[] = {60.0, 75.0, 90.0, 120.0, 144.0, 165.0, 240.0};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        pace_run_t run = pace_run_new();
        double seconds = 10.0;
        double fps = (double)pace_feed(&run, rates[i], 1000.0, seconds, TARGET_60_MS) / seconds;
        TEST_ASSERT_TRUE_MESSAGE(fps >= 58.0 && fps <= 61.0, "capped rate is not ~60");
        /* No catch-up pairs: frames stay at least half a target period apart. */
        TEST_ASSERT_TRUE(run.min_interval_ms >= TARGET_60_MS * 0.5);
    }
}

void test_slower_displays_run_every_tick(void) {
    static const double rates[] = {30.0, 50.0, 59.94};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        pace_run_t run = pace_run_new();
        pace_feed(&run, rates[i], 1000.0, 20.0, TARGET_60_MS);
        TEST_ASSERT_EQUAL_INT(run.ticks, run.frames);
    }
}

void test_lower_cap_divides_display_rate(void) {
    pace_run_t run = pace_run_new();
    double fps = (double)pace_feed(&run, 144.0, 1000.0, 10.0, 1000.0 / 30.0) / 10.0;
    TEST_ASSERT_TRUE(fps >= 29.0 && fps <= 31.0);
}

void test_gap_restarts_schedule_without_catch_up(void) {
    pace_run_t run = pace_run_new();
    pace_feed(&run, 144.0, 1000.0, 1.0, TARGET_60_MS);
    /* Hidden tab: no RAF for 2 s. The first tick back runs, then pacing resumes at 60. */
    int first_100ms = pace_feed(&run, 144.0, 4000.0, 0.1, TARGET_60_MS);
    TEST_ASSERT_TRUE(first_100ms >= 5 && first_100ms <= 7);
    int half_second = pace_feed(&run, 144.0, 4000.0 + (14.0 * 1000.0 / 144.0), 0.5, TARGET_60_MS);
    TEST_ASSERT_TRUE(half_second >= 29 && half_second <= 31);
    TEST_ASSERT_TRUE(run.min_interval_ms >= TARGET_60_MS * 0.5);
}

void test_zero_target_is_uncapped(void) {
    pace_run_t run = pace_run_new();
    pace_feed(&run, 144.0, 1000.0, 2.0, 0.0);
    TEST_ASSERT_EQUAL_INT(run.ticks, run.frames);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_faster_displays_run_at_target_rate);
    RUN_TEST(test_slower_displays_run_every_tick);
    RUN_TEST(test_lower_cap_divides_display_rate);
    RUN_TEST(test_gap_restarts_schedule_without_catch_up);
    RUN_TEST(test_zero_target_is_uncapped);
    return UNITY_END();
}
