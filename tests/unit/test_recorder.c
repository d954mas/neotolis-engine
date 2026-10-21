/* nt_recorder core: frame selection on the caller's clock and audio padding/trimming, against a
   test-defined platform backend. */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "recorder/nt_recorder.h"
#include "recorder/nt_recorder_internal.h"
#include "unity.h"
#include "window/nt_window.h"

/* ---- Test backend ---- */

static struct {
    uint32_t opens;
    uint32_t width, height;
    uint32_t videos;
    uint32_t next_index;
    bool index_gap;
    uint64_t audio_frames;
    uint64_t audio_nonzero;
    uint32_t closes;
    uint32_t closed_frames;
    bool fail_video_at; /* fail the video call with index == fail_index */
    uint32_t fail_index;
} s_be;

bool nt_recorder_platform_open(const char *name, uint32_t width, uint32_t height, uint32_t fps) {
    (void)name;
    (void)fps;
    s_be.opens++;
    s_be.width = width;
    s_be.height = height;
    return true;
}

bool nt_recorder_platform_video(uint32_t index) {
    if (s_be.fail_video_at && index == s_be.fail_index) {
        return false;
    }
    if (index != s_be.next_index) {
        s_be.index_gap = true;
    }
    s_be.next_index = index + 1;
    s_be.videos++;
    return true;
}

bool nt_recorder_platform_audio(const float *stereo, uint32_t frames) {
    for (uint32_t i = 0; i < frames * 2; i++) {
        if (stereo[i] != 0.0F) {
            s_be.audio_nonzero++;
        }
    }
    s_be.audio_frames += frames;
    return true;
}

void nt_recorder_platform_close(uint32_t frames) {
    s_be.closes++;
    s_be.closed_frames = frames;
}

void setUp(void) {
    memset(&s_be, 0, sizeof(s_be));
    g_nt_window.fb_width = 641;
    g_nt_window.fb_height = 360;
}

void tearDown(void) { (void)nt_recorder_stop(); }

/* Advances like nt_app: a float dt accumulated into a double time. */
static uint32_t run_lockstep(float step_dt, uint32_t fps, uint32_t steps) {
    double t = 10.0;
    TEST_ASSERT_TRUE(nt_recorder_start("clip.mp4", fps, t));
    for (uint32_t i = 0; i < steps; i++) {
        t += (double)step_dt;
        nt_recorder_frame(t, (double)step_dt);
    }
    return nt_recorder_stop();
}

/* ---- Frame selection ---- */

static void test_size_rounds_down_to_even(void) {
    TEST_ASSERT_TRUE(nt_recorder_start("clip.mp4", 30, 0.0));
    TEST_ASSERT_EQUAL_UINT32(640, s_be.width);
    TEST_ASSERT_EQUAL_UINT32(360, s_be.height);
}

static void test_one_frame_per_step(void) {
    TEST_ASSERT_EQUAL_UINT32(90, run_lockstep(1.0F / 30.0F, 30, 90));
    TEST_ASSERT_EQUAL_UINT32(90, s_be.videos);
    TEST_ASSERT_FALSE(s_be.index_gap);
    TEST_ASSERT_EQUAL_UINT32(90, s_be.closed_frames);
}

static void test_two_ticks_per_frame(void) { TEST_ASSERT_EQUAL_UINT32(500000, run_lockstep(1.0F / 60.0F, 30, 1000000)); }

/* 1/50 and 1/25 round down as floats; a fixed tolerance drifts one step late after ~4500 frames. */
static void test_float_step_below_period_does_not_drift(void) {
    TEST_ASSERT_EQUAL_UINT32(1000000, run_lockstep(1.0F / 50.0F, 50, 1000000));
    memset(&s_be, 0, sizeof(s_be));
    TEST_ASSERT_EQUAL_UINT32(1000000, run_lockstep(1.0F / 25.0F, 25, 1000000));
    TEST_ASSERT_FALSE(s_be.index_gap);
}

static void test_jittered_real_time_stays_cfr(void) {
    TEST_ASSERT_TRUE(nt_recorder_start("clip.mp4", 30, 0.0));
    double t = 0.0;
    for (uint32_t i = 0; i < 6000; i++) {
        double dt = (1.0 / 60.0) * ((i % 3 == 0) ? 0.7 : 1.15);
        t += dt;
        nt_recorder_frame(t, dt);
    }
    uint32_t frames = nt_recorder_stop();
    TEST_ASSERT_UINT32_WITHIN(1, (uint32_t)(t * 30.0), frames);
    TEST_ASSERT_FALSE(s_be.index_gap);
}

static void test_hitch_resubmits_the_frame(void) {
    TEST_ASSERT_TRUE(nt_recorder_start("clip.mp4", 30, 0.0));
    nt_recorder_frame(0.1, 0.1);
    TEST_ASSERT_EQUAL_UINT32(3, s_be.videos);
}

static void test_frozen_time_writes_nothing(void) {
    TEST_ASSERT_TRUE(nt_recorder_start("clip.mp4", 30, 5.0));
    for (int i = 0; i < 100; i++) {
        nt_recorder_frame(5.0, 0.0);
    }
    TEST_ASSERT_EQUAL_UINT32(0, s_be.videos);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.closed_frames);
}

static void test_resize_ends_the_recording(void) {
    TEST_ASSERT_TRUE(nt_recorder_start("clip.mp4", 30, 0.0));
    nt_recorder_frame(1.0 / 30.0, 1.0 / 30.0);
    g_nt_window.fb_width = 800;
    nt_recorder_frame(2.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closed_frames);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
}

static void test_encoder_failure_ends_the_recording(void) {
    s_be.fail_video_at = true;
    s_be.fail_index = 2;
    TEST_ASSERT_TRUE(nt_recorder_start("clip.mp4", 30, 0.0));
    for (int i = 1; i <= 5; i++) {
        nt_recorder_frame(i / 30.0, 1.0 / 30.0);
    }
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(2, s_be.closed_frames);
}

static void test_zero_framebuffer_refuses_to_start(void) {
    g_nt_window.fb_width = 1;
    TEST_ASSERT_FALSE(nt_recorder_start("clip.mp4", 30, 0.0));
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(0, s_be.opens);
}

static void test_idle_calls_are_inert(void) {
    float samples[4] = {1, 1, 1, 1};
    nt_recorder_frame(1.0, 1.0);
    nt_recorder_push_audio(samples, 2);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(0, s_be.closes);
}

/* ---- Audio ---- */

static void test_silent_track_matches_video_length(void) {
    TEST_ASSERT_EQUAL_UINT32(45, run_lockstep(1.0F / 30.0F, 30, 45));
    TEST_ASSERT_EQUAL_UINT64(45ULL * 1600ULL, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(0, s_be.audio_nonzero);
}

/* A mixer that pushes each step's samples: the first step's audio precedes file time 0 and is
   trimmed; the last frame's interval is padded. */
static void test_exact_lockstep_audio_is_kept(void) {
    static float s_step[1600 * 2];
    for (int i = 0; i < 1600 * 2; i++) {
        s_step[i] = 0.25F;
    }
    double t = 0.0;
    TEST_ASSERT_TRUE(nt_recorder_start("clip.mp4", 30, t));
    for (int i = 0; i < 30; i++) {
        t += (double)(1.0F / 30.0F);
        nt_recorder_push_audio(s_step, 1600);
        nt_recorder_frame(t, (double)(1.0F / 30.0F));
    }
    TEST_ASSERT_EQUAL_UINT32(30, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(30ULL * 1600ULL, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(29ULL * 1600ULL * 2ULL, s_be.audio_nonzero);
}

static void test_excess_audio_is_trimmed(void) {
    static float s_big[4800 * 2];
    double t = 0.0;
    TEST_ASSERT_TRUE(nt_recorder_start("clip.mp4", 30, t));
    for (int i = 0; i < 10; i++) {
        t += 1.0 / 30.0;
        nt_recorder_push_audio(s_big, 4800);
        nt_recorder_frame(t, 1.0 / 30.0);
    }
    TEST_ASSERT_EQUAL_UINT32(10, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(10ULL * 1600ULL, s_be.audio_frames);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_size_rounds_down_to_even);
    RUN_TEST(test_one_frame_per_step);
    RUN_TEST(test_two_ticks_per_frame);
    RUN_TEST(test_float_step_below_period_does_not_drift);
    RUN_TEST(test_jittered_real_time_stays_cfr);
    RUN_TEST(test_hitch_resubmits_the_frame);
    RUN_TEST(test_frozen_time_writes_nothing);
    RUN_TEST(test_resize_ends_the_recording);
    RUN_TEST(test_encoder_failure_ends_the_recording);
    RUN_TEST(test_zero_framebuffer_refuses_to_start);
    RUN_TEST(test_idle_calls_are_inert);
    RUN_TEST(test_silent_track_matches_video_length);
    RUN_TEST(test_exact_lockstep_audio_is_kept);
    RUN_TEST(test_excess_audio_is_trimmed);
    return UNITY_END();
}
