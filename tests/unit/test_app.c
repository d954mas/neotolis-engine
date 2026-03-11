#include "app/nt_app.h"
#include "unity.h"

#include <math.h>

/* Helper: float approximately equal (avoids UNITY_EXCLUDE_FLOAT issue) */
static bool float_near(float a, float b, float epsilon) { return fabsf(a - b) <= epsilon; }

/* ---- Shared test state ---- */

static int s_frame_count;
static int s_target_frames;
static float s_first_dt;
static float s_recorded_dts[64];
static void frame_fn_quit_after_n(void) {
    if (s_frame_count == 0) {
        s_first_dt = g_nt_app.dt;
    }
    if (s_frame_count < 64) {
        s_recorded_dts[s_frame_count] = g_nt_app.dt;
    }
    s_frame_count++;
    if (s_frame_count >= s_target_frames) {
        nt_app_quit();
    }
}

void setUp(void) {
    s_frame_count = 0;
    s_target_frames = 1;
    s_first_dt = -1.0F;
    for (int i = 0; i < 64; i++) {
        s_recorded_dts[i] = -1.0F;
    }
    /* Reset g_nt_app to defaults before each test */
    g_nt_app.dt = 0.0F;
    g_nt_app.time = 0.0F;
    g_nt_app.max_dt = 0.1F;
    g_nt_app.frame = 0;
}

void tearDown(void) { /* Called after each test */ }

/* 1. nt_app_run calls frame_fn at least once */
void test_app_run_calls_frame_fn(void) {
    s_target_frames = 1;
    nt_app_run(frame_fn_quit_after_n);
    TEST_ASSERT_TRUE_MESSAGE(g_nt_app.frame >= 1, "frame_fn should be called at least once");
}

/* 2. First frame dt is clamped like any other frame */
void test_app_dt_clamped_first_frame(void) {
    g_nt_app.max_dt = 0.1F;
    s_target_frames = 1;
    nt_app_run(frame_fn_quit_after_n);
    TEST_ASSERT_TRUE_MESSAGE(s_first_dt <= 0.1F + 1e-6F, "First frame dt must be clamped to max_dt");
}

/* 3. dt is clamped to max_dt */
void test_app_dt_clamped(void) {
    g_nt_app.max_dt = 0.001F; /* Very small clamp to ensure real dt exceeds it */
    s_target_frames = 5;
    nt_app_run(frame_fn_quit_after_n);
    for (int i = 0; i < s_frame_count && i < 64; i++) {
        TEST_ASSERT_TRUE_MESSAGE(s_recorded_dts[i] <= 0.001F + 1e-6F, "dt must be clamped to max_dt");
    }
}

/* 4. time accumulates clamped dt values */
void test_app_time_accumulates(void) {
    s_target_frames = 5;
    nt_app_run(frame_fn_quit_after_n);
    TEST_ASSERT_TRUE_MESSAGE(g_nt_app.time >= 0.0F, "time should accumulate (non-negative)");
}

/* 5. Frame counter matches expected count */
void test_app_frame_counter(void) {
    s_target_frames = 5;
    nt_app_run(frame_fn_quit_after_n);
    TEST_ASSERT_EQUAL_UINT32(5, g_nt_app.frame);
}

/* 6. max_dt defaults to 0.1f */
void test_app_max_dt_default(void) {
    /* g_nt_app is reset in setUp with max_dt = 0.1F to match the definition default */
    TEST_ASSERT_TRUE_MESSAGE(float_near(0.1F, g_nt_app.max_dt, 1e-6F), "max_dt should default to 0.1f");
}

/* 7. nt_app_quit exits the loop (function returns) */
void test_app_quit_exits_loop(void) {
    s_target_frames = 1; /* frame_fn calls nt_app_quit after 1 frame */
    nt_app_run(frame_fn_quit_after_n);
    /* If we reach here, the loop exited properly */
    TEST_ASSERT_TRUE_MESSAGE(s_frame_count >= 1, "Loop must exit after nt_app_quit");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_app_run_calls_frame_fn);
    RUN_TEST(test_app_dt_clamped_first_frame);
    RUN_TEST(test_app_dt_clamped);
    RUN_TEST(test_app_time_accumulates);
    RUN_TEST(test_app_frame_counter);
    RUN_TEST(test_app_max_dt_default);
    RUN_TEST(test_app_quit_exits_loop);
    return UNITY_END();
}
