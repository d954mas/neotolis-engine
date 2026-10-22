/* nt_recorder core: frame selection on the caller's clock and the audio stream, against a test-defined
   platform backend and log. */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "log/nt_log.h"
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
    double now;               /* caller time of the current frame call */
    double worst_video_error; /* largest |now - deadline| of an encoded frame */
    double t0, period;
    uint64_t audio_frames;
    uint64_t audio_nonzero;
    float last_sample;
    bool stream_broken; /* a non-zero sample that is not the previous one + 1 */
    uint32_t closes;
    uint32_t closed_frames;
    bool fail_video;
    uint32_t fail_index;
    bool fail_audio;
    uint32_t warns;
    float next_sample; /* push_stream's counter */
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
    for (uint32_t i = 0; i < frames; i++) {
        float v = stereo[(size_t)i * 2U];
        if (v != 0.0F) {
            s_be.stream_broken |= v != s_be.last_sample + 1.0F;
            s_be.last_sample = v;
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

void nt_log_write(nt_log_level_t level, const char *domain, const char *fmt, ...) {
    (void)domain;
    (void)fmt;
    s_be.warns += level == NT_LOG_LEVEL_WARN ? 1U : 0U;
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

/* Pushes the next samples of a counting stream (1, 2, 3, ...), so the backend sees any loss, duplicate
   or reorder. */
static void push_stream(uint32_t frames) {
    static float s_buf[4096 * 2];
    TEST_ASSERT_TRUE(frames <= 4096);
    for (uint32_t i = 0; i < frames; i++) {
        s_be.next_sample += 1.0F;
        s_buf[(size_t)i * 2U] = s_be.next_sample;
        s_buf[((size_t)i * 2U) + 1U] = s_be.next_sample;
    }
    nt_recorder_push_audio(s_buf, frames);
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

/* Every second step lands exactly on a deadline; the odd steps lie half a period away. */
static void test_two_ticks_per_frame(void) {
    TEST_ASSERT_EQUAL_UINT32(10000, run_lockstep(1.0F / 60.0F, 30, 20000));
    TEST_ASSERT_FALSE(s_be.index_gap);
    TEST_ASSERT_TRUE(s_be.worst_video_error < 1e-4);
}

/* 1/50 and 1/25 round down as floats; a fixed tolerance drifts one step late after ~4500 frames. */
static void test_float_step_below_period_does_not_drift(void) {
    TEST_ASSERT_EQUAL_UINT32(20000, run_lockstep(1.0F / 50.0F, 50, 20000));
    TEST_ASSERT_FALSE(s_be.index_gap);
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
    TEST_ASSERT_EQUAL_UINT32(3, s_be.next_index);
    TEST_ASSERT_FALSE(s_be.index_gap);
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

static void test_time_jump_over_two_seconds_ends_the_recording(void) {
    start_at(30, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    frame_at(3.0, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closed_frames);
    TEST_ASSERT_EQUAL_UINT64(1600, s_be.audio_frames);
}

static void test_resize_ends_with_audio_padded_to_the_video(void) {
    start_at(30, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    g_nt_window.fb_width = 800;
    frame_at(2.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closed_frames);
    TEST_ASSERT_EQUAL_UINT64(1600, s_be.audio_frames);
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

/* A clock jump of years ends the recording after a bounded count instead of counting every deadline. */
static void test_huge_time_jump_ends_quickly(void) {
    start_at(30, 0.0);
    frame_at(1.5e8, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(0, s_be.closed_frames);
}

/* Exactly two seconds of due frames are caught up; one more ends the recording. */
static void test_catch_up_bound_is_two_seconds_of_frames(void) {
    start_at(30, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    frame_at(61.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_TRUE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(61, nt_recorder_stop());

    memset(&s_be, 0, sizeof(s_be));
    start_at(30, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    frame_at(62.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closed_frames);
}

/* ---- Audio ---- */

static void test_silent_track_matches_video_length(void) {
    TEST_ASSERT_EQUAL_UINT32(45, run_lockstep(1.0F / 30.0F, 30, 45));
    TEST_ASSERT_EQUAL_UINT64(45ULL * 1600ULL, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(0, s_be.audio_nonzero);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.warns);
}

/* A mixer like devapi_host's (samples per step from absolute times) at a 144 Hz step with t0 off the
   sample grid: the stream reaches the encoder whole and in order, and never drifts from the clock. */
static void test_lockstep_mixer_stream_is_written_whole(void) {
    double t = 10.0 + (0.4 / 48000.0);
    start_at(60, t);
    const float step = 1.0F / 144.0F;
    uint64_t pushed = 0;
    for (int i = 0; i < 14400; i++) {
        double start = t;
        t += (double)step;
        uint32_t n = (uint32_t)(llround(t * 48000.0) - llround(start * 48000.0));
        push_stream(n);
        pushed += n;
        frame_at(t, (double)step);
    }
    (void)nt_recorder_stop();
    TEST_ASSERT_EQUAL_UINT64(pushed, s_be.audio_nonzero);
    TEST_ASSERT_FALSE(s_be.stream_broken);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.warns);
}

/* A mixer that renders a 1024-sample block whenever the clock has passed one stays within the bound. */
static void test_block_mixer_does_not_warn(void) {
    start_at(30, 0.0);
    uint64_t pushed = 0;
    double t = 0.0;
    for (int i = 0; i < 3600; i++) {
        t += 1.0 / 60.0;
        while (pushed + 1024U <= (uint64_t)(t * 48000.0)) {
            push_stream(1024);
            pushed += 1024;
        }
        frame_at(t, 1.0 / 60.0);
    }
    TEST_ASSERT_EQUAL_UINT32(0, s_be.warns);
}

/* Silence keeps pace with the video until the mixer starts half a second in; its stream then follows
   whole, with no WARN. */
static void test_silence_until_the_first_push(void) {
    start_at(30, 0.0);
    uint64_t pushed = 0;
    for (int i = 1; i <= 600; i++) {
        if (i > 30) {
            push_stream(800);
            pushed += 800;
        }
        frame_at(i / 60.0, 1.0 / 60.0);
        if (i == 30) {
            TEST_ASSERT_EQUAL_UINT64(15ULL * 1600ULL, s_be.audio_frames);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(300, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(pushed, s_be.audio_nonzero);
    TEST_ASSERT_FALSE(s_be.stream_broken);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.warns);
}

/* A mixer that stops for half a second shifts the rest of the track: one WARN, however long it runs. */
static void test_mixer_gap_warns_once(void) {
    start_at(30, 0.0);
    for (int i = 1; i <= 600; i++) {
        if (i <= 60 || i > 90) {
            push_stream(800);
        }
        frame_at(i / 60.0, 1.0 / 60.0);
    }
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
    TEST_ASSERT_TRUE(nt_recorder_active());
}

/* Short audio is padded with silence to the video at stop; audio pushed past the video stays. */
static void test_stop_pads_short_audio_and_keeps_long(void) {
    start_at(30, 0.0);
    push_stream(800);
    frame_at(3.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_EQUAL_UINT32(3, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(4800, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(800, s_be.audio_nonzero);

    memset(&s_be, 0, sizeof(s_be));
    start_at(30, 0.0);
    push_stream(4000);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_EQUAL_UINT32(1, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(4000, s_be.audio_frames);
}

static void test_final_audio_failure_returns_zero(void) {
    start_at(30, 0.0);
    push_stream(800);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    s_be.fail_audio = true;
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
}

static void test_audio_failure_ends_the_recording(void) {
    start_at(30, 0.0);
    s_be.fail_audio = true;
    push_stream(1600);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
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
    RUN_TEST(test_time_jump_over_two_seconds_ends_the_recording);
    RUN_TEST(test_huge_time_jump_ends_quickly);
    RUN_TEST(test_catch_up_bound_is_two_seconds_of_frames);
    RUN_TEST(test_resize_ends_with_audio_padded_to_the_video);
    RUN_TEST(test_video_failure_ends_the_recording);
    RUN_TEST(test_tiny_framebuffer_refuses_to_start);
    RUN_TEST(test_idle_calls_are_inert);
    RUN_TEST(test_silent_track_matches_video_length);
    RUN_TEST(test_lockstep_mixer_stream_is_written_whole);
    RUN_TEST(test_block_mixer_does_not_warn);
    RUN_TEST(test_silence_until_the_first_push);
    RUN_TEST(test_mixer_gap_warns_once);
    RUN_TEST(test_stop_pads_short_audio_and_keeps_long);
    RUN_TEST(test_final_audio_failure_returns_zero);
    RUN_TEST(test_audio_failure_ends_the_recording);
    return UNITY_END();
}
