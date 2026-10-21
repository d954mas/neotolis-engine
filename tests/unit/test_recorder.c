/* nt_recorder core: frame selection on the caller's clock and audio sync, against a test-defined
   platform backend. */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "recorder/nt_recorder.h"
#include "recorder/nt_recorder_internal.h"
#include "unity.h"
#include "window/nt_window.h"

/* ---- Test backend ---- */

#define PRE_START_SAMPLE 9.0F

static struct {
    uint32_t opens;
    uint32_t width, height;
    uint32_t videos;
    uint32_t next_index;
    bool index_gap;
    double now;               /* caller time of the current frame call */
    double worst_video_error; /* largest |now - deadline| of an encoded frame */
    double t0, period;
    uint64_t audio_frames;
    uint64_t audio_nonzero;
    bool saw_pre_start;
    uint32_t closes;
    uint32_t closed_frames;
    bool fail_video;
    uint32_t fail_index;
    bool fail_audio;
} s_be;

bool nt_recorder_platform_open(const char *name, uint32_t width, uint32_t height, uint32_t fps) {
    (void)name;
    s_be.opens++;
    s_be.width = width;
    s_be.height = height;
    s_be.period = 1.0 / (double)fps;
    return true;
}

bool nt_recorder_platform_video(uint32_t index) {
    if (s_be.fail_video && index == s_be.fail_index) {
        return false;
    }
    if (index != s_be.next_index) {
        s_be.index_gap = true;
    }
    double err = fabs(s_be.now - (s_be.t0 + ((double)(index + 1U) * s_be.period)));
    if (err > s_be.worst_video_error) {
        s_be.worst_video_error = err;
    }
    s_be.next_index = index + 1;
    s_be.videos++;
    return true;
}

bool nt_recorder_platform_audio(const float *stereo, uint32_t frames) {
    if (s_be.fail_audio) {
        return false;
    }
    for (uint32_t i = 0; i < frames * 2; i++) {
        if (stereo[i] != 0.0F) {
            s_be.audio_nonzero++;
        }
        if (stereo[i] == PRE_START_SAMPLE) {
            s_be.saw_pre_start = true;
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
    g_nt_window.fb_height = 361;
}

void tearDown(void) { (void)nt_recorder_stop(); }

static void start_at(uint32_t fps, double t) {
    s_be.t0 = t;
    TEST_ASSERT_TRUE(nt_recorder_start("clip.mp4", fps, t));
}

static void frame_at(double t, double dt) {
    s_be.now = t;
    nt_recorder_frame(t, dt);
}

/* Advances like nt_app: a float dt accumulated into a double time. */
static uint32_t run_lockstep(float step_dt, uint32_t fps, uint32_t steps) {
    double t = 10.0;
    start_at(fps, t);
    for (uint32_t i = 0; i < steps; i++) {
        t += (double)step_dt;
        frame_at(t, (double)step_dt);
    }
    return nt_recorder_stop();
}

static void fill(float *buf, uint32_t frames, float v) {
    for (uint32_t i = 0; i < frames * 2; i++) {
        buf[i] = v;
    }
}

/* ---- Frame selection ---- */

static void test_size_rounds_down_to_even(void) {
    start_at(30, 0.0);
    TEST_ASSERT_EQUAL_UINT32(640, s_be.width);
    TEST_ASSERT_EQUAL_UINT32(360, s_be.height);
}

static void test_one_frame_per_step(void) {
    TEST_ASSERT_EQUAL_UINT32(90, run_lockstep(1.0F / 30.0F, 30, 90));
    TEST_ASSERT_EQUAL_UINT32(90, s_be.videos);
    TEST_ASSERT_FALSE(s_be.index_gap);
    TEST_ASSERT_EQUAL_UINT32(90, s_be.closed_frames);
}

static void test_two_ticks_per_frame(void) { TEST_ASSERT_EQUAL_UINT32(10000, run_lockstep(1.0F / 60.0F, 30, 20000)); }

/* 1/50 and 1/25 round down as floats; a fixed tolerance drifts one step late after ~4500 frames. */
static void test_float_step_below_period_does_not_drift(void) {
    TEST_ASSERT_EQUAL_UINT32(20000, run_lockstep(1.0F / 50.0F, 50, 20000));
    memset(&s_be, 0, sizeof(s_be));
    TEST_ASSERT_EQUAL_UINT32(20000, run_lockstep(1.0F / 25.0F, 25, 20000));
    TEST_ASSERT_FALSE(s_be.index_gap);
}

/* The margin comes from the frame's own dt, so with uneven dt a deadline can land up to
   dt - previous_dt / 2 away (0.8 / 60 here); a take-once-reached rule would reach a full dt (1.15 / 60). */
static void test_jittered_real_time_picks_nearest_frames(void) {
    start_at(30, 0.0);
    double t = 0.0;
    for (uint32_t i = 0; i < 6000; i++) {
        double dt = (1.0 / 60.0) * ((i % 3 == 0) ? 0.7 : 1.15);
        t += dt;
        frame_at(t, dt);
    }
    uint32_t frames = nt_recorder_stop();
    TEST_ASSERT_UINT32_WITHIN(1, (uint32_t)(t * 30.0), frames);
    TEST_ASSERT_FALSE(s_be.index_gap);
    TEST_ASSERT_TRUE(s_be.worst_video_error <= (0.8 / 60.0) + 1e-9);
}

static void test_hitch_resubmits_the_frame(void) {
    start_at(30, 0.0);
    frame_at(0.1, 0.1);
    TEST_ASSERT_EQUAL_UINT32(3, s_be.videos);
}

static void test_frozen_time_writes_nothing(void) {
    start_at(30, 5.0);
    for (int i = 0; i < 100; i++) {
        frame_at(5.0, 0.0);
    }
    TEST_ASSERT_EQUAL_UINT32(0, s_be.videos);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.closed_frames);
}

static void test_resize_ends_the_recording(void) {
    start_at(30, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    g_nt_window.fb_width = 800;
    frame_at(2.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closed_frames);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
}

static void test_video_failure_ends_the_recording(void) {
    s_be.fail_video = true;
    s_be.fail_index = 2;
    start_at(30, 0.0);
    for (int i = 1; i <= 5; i++) {
        frame_at(i / 30.0, 1.0 / 30.0);
    }
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(2, s_be.closed_frames);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
}

static void test_audio_failure_at_stop_returns_zero(void) {
    start_at(30, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    s_be.fail_audio = true;
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
    TEST_ASSERT_FALSE(nt_recorder_active());
}

static void test_tiny_framebuffer_refuses_to_start(void) {
    g_nt_window.fb_width = 1;
    TEST_ASSERT_FALSE(nt_recorder_start("clip.mp4", 30, 0.0));
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(0, s_be.opens);
}

static void test_idle_calls_are_inert(void) {
    float samples[4] = {1, 1, 1, 1};
    nt_recorder_frame(1.0, 1.0);
    nt_recorder_push_audio(samples, 2);
    nt_recorder_push_audio(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(0, s_be.closes);
}

/* ---- Audio ---- */

static void test_silent_track_matches_video_length(void) {
    TEST_ASSERT_EQUAL_UINT32(45, run_lockstep(1.0F / 30.0F, 30, 45));
    TEST_ASSERT_EQUAL_UINT64(45ULL * 1600ULL, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(0, s_be.audio_nonzero);
}

/* A lockstep mixer pushes each step's block before the frame call: nothing is padded or dropped, and
   the block pushed in the start frame (time before t0) is not in the file. */
static void test_lockstep_blocks_are_kept_whole(void) {
    static float s_step[1600 * 2];
    double t = 0.0;
    start_at(30, t);
    fill(s_step, 1600, PRE_START_SAMPLE);
    nt_recorder_push_audio(s_step, 1600);
    frame_at(t, 0.0);
    fill(s_step, 1600, 0.25F);
    for (int i = 0; i < 30; i++) {
        t += (double)(1.0F / 30.0F);
        nt_recorder_push_audio(s_step, 1600);
        frame_at(t, (double)(1.0F / 30.0F));
    }
    TEST_ASSERT_EQUAL_UINT32(30, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(30ULL * 1600ULL, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(30ULL * 1600ULL * 2ULL, s_be.audio_nonzero);
    TEST_ASSERT_FALSE(s_be.saw_pre_start);
}

/* A real-time mixer delivers 128-sample blocks that never match a frame: the lag carries over instead
   of becoming silence mid-track. */
static void test_block_mixer_is_not_padded_mid_track(void) {
    static float s_block[128 * 2];
    fill(s_block, 128, 0.5F);
    start_at(30, 0.0);
    double t = 0.0;
    uint64_t pushed = 0;
    for (int i = 0; i < 600; i++) {
        t += 1.0 / 60.0;
        while ((pushed + 128U) <= (uint64_t)(t * 48000.0)) {
            nt_recorder_push_audio(s_block, 128);
            pushed += 128;
        }
        frame_at(t, 1.0 / 60.0);
    }
    (void)nt_recorder_stop();
    /* Only the final pad at stop (< one block) may be silence. */
    TEST_ASSERT_TRUE(s_be.audio_nonzero >= (s_be.audio_frames - 128U) * 2U);
}

static void test_surplus_beyond_a_frame_is_dropped(void) {
    static float s_big[4800 * 2];
    fill(s_big, 4800, 0.5F);
    double t = 0.0;
    start_at(30, t);
    for (int i = 0; i < 10; i++) {
        t += 1.0 / 30.0;
        nt_recorder_push_audio(s_big, 4800);
        frame_at(t, 1.0 / 30.0);
    }
    TEST_ASSERT_EQUAL_UINT32(10, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(10ULL * 1600ULL, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(10ULL * 1600ULL * 2ULL, s_be.audio_nonzero);
}

static void test_audio_failure_ends_the_recording(void) {
    static float s_step[1600 * 2];
    start_at(30, 0.0);
    s_be.fail_audio = true;
    nt_recorder_push_audio(s_step, 1600);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_size_rounds_down_to_even);
    RUN_TEST(test_one_frame_per_step);
    RUN_TEST(test_two_ticks_per_frame);
    RUN_TEST(test_float_step_below_period_does_not_drift);
    RUN_TEST(test_jittered_real_time_picks_nearest_frames);
    RUN_TEST(test_hitch_resubmits_the_frame);
    RUN_TEST(test_frozen_time_writes_nothing);
    RUN_TEST(test_resize_ends_the_recording);
    RUN_TEST(test_video_failure_ends_the_recording);
    RUN_TEST(test_audio_failure_at_stop_returns_zero);
    RUN_TEST(test_tiny_framebuffer_refuses_to_start);
    RUN_TEST(test_idle_calls_are_inert);
    RUN_TEST(test_silent_track_matches_video_length);
    RUN_TEST(test_lockstep_blocks_are_kept_whole);
    RUN_TEST(test_block_mixer_is_not_padded_mid_track);
    RUN_TEST(test_surplus_beyond_a_frame_is_dropped);
    RUN_TEST(test_audio_failure_ends_the_recording);
    return UNITY_END();
}
