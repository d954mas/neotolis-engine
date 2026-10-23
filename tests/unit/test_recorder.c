/* nt_recorder core: frame selection on the caller's clock and the audio stream, against a test-defined
   platform backend and log. */

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "log/nt_log.h"
#include "recorder/nt_recorder.h"
#include "recorder/nt_recorder_internal.h"
#include "unity.h"
#include "window/nt_window.h"

/* ---- Test backend ---- */

static struct {
    uint32_t opens;
    uint32_t width, height, fps;
    uint32_t videos;
    uint32_t next_index;
    bool index_gap;
    double now;               /* caller time of the current frame call */
    double worst_video_error; /* largest |now - deadline| of an encoded frame */
    double t0, period;        /* t0: the origin the test expects */
    uint64_t audio_frames;
    uint64_t at;         /* samples written in this recording */
    bool position_wrong; /* an audio call whose position is not at */
    uint64_t audio_nonzero;
    float last_sample;
    bool stream_broken; /* a non-zero sample that is not the previous one + 1 */
    uint32_t closes;
    uint32_t closed_frames;
    bool fail_video;
    uint32_t fail_index;
    bool fail_audio;
    bool fail_open;
    uint32_t warns;
    char warn[160];    /* the last WARN line */
    char info[160];    /* the last INFO line */
    float next_sample; /* push_stream's counter */
} s_be;

bool nt_recorder_platform_open(const char *name, uint32_t width, uint32_t height, uint32_t fps) {
    (void)name;
    s_be.opens++;
    if (s_be.fail_open) {
        return false;
    }
    s_be.width = width;
    s_be.height = height;
    s_be.fps = fps;
    s_be.period = 1.0 / (double)fps;
    s_be.at = 0;
    return true;
}

bool nt_recorder_platform_video(uint32_t first, uint32_t count) {
    TEST_ASSERT_TRUE(count > 0);
    if (s_be.fail_video && s_be.fail_index - first < count) {
        return false;
    }
    for (uint32_t index = first; index < first + count; index++) {
        if (index != s_be.next_index) {
            s_be.index_gap = true;
        }
        double err = fabs(s_be.now - (s_be.t0 + ((double)(index + 1U) * s_be.period)));
        if (err > s_be.worst_video_error) {
            s_be.worst_video_error = err;
        }
        s_be.next_index = index + 1;
        s_be.videos++;
    }
    return true;
}

bool nt_recorder_platform_audio(uint64_t at, const float *stereo, uint32_t frames) {
    if (s_be.fail_audio) {
        return false;
    }
    s_be.position_wrong |= at != s_be.at;
    s_be.at += frames;
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
    s_be.warns += level == NT_LOG_LEVEL_WARN ? 1U : 0U;
    if (level == NT_LOG_LEVEL_INFO || level == NT_LOG_LEVEL_WARN) {
        va_list args;
        va_start(args, fmt);
        (void)vsnprintf(level == NT_LOG_LEVEL_INFO ? s_be.info : s_be.warn, sizeof(s_be.info), fmt, args);
        va_end(args);
    }
}

void setUp(void) {
    memset(&s_be, 0, sizeof(s_be));
    g_nt_window.fb_width = 641;
    g_nt_window.fb_height = 361;
}

void tearDown(void) {
    (void)nt_recorder_stop();
    TEST_ASSERT_FALSE(s_be.position_wrong);
}

/* t: the origin the first frame call should set, for the video deadline check. */
static void start_at(uint32_t fps, double t) {
    s_be.t0 = t;
    TEST_ASSERT_TRUE(nt_recorder_start("clip.mp4", fps));
    TEST_ASSERT_EQUAL_UINT32(fps, s_be.fps);
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

/* Every second step lands on a deadline (within float step error); the others lie half a period away. */
static void test_two_ticks_per_frame(void) {
    TEST_ASSERT_EQUAL_UINT32(10000, run_lockstep(1.0F / 60.0F, 30, 20000));
    TEST_ASSERT_FALSE(s_be.index_gap);
    TEST_ASSERT_TRUE(s_be.worst_video_error < 1e-4);
}

/* 1/50 and 1/25 round down as floats, so time falls ever further behind the deadlines; the dt margin
   absorbs it over 20000 frames. */
static void test_float_step_below_period_does_not_drift(void) {
    TEST_ASSERT_EQUAL_UINT32(20000, run_lockstep(1.0F / 50.0F, 50, 20000));
    TEST_ASSERT_FALSE(s_be.index_gap);
    memset(&s_be, 0, sizeof(s_be));
    TEST_ASSERT_EQUAL_UINT32(20000, run_lockstep(1.0F / 25.0F, 25, 20000));
    TEST_ASSERT_FALSE(s_be.index_gap);
}

/* The margin comes from the frame's own dt, so with uneven dt a deadline can land up to
   dt - previous_dt / 2 away (0.8 / 60 here). */
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
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
}

static void test_resize_ends_with_audio_padded_to_the_video(void) {
    start_at(30, 0.0);
    push_stream(800);
    frame_at(1.0 / 60.0, 1.0 / 60.0);
    frame_at(1.0 / 30.0, 1.0 / 60.0);
    g_nt_window.fb_width = 800;
    frame_at(2.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closed_frames);
    TEST_ASSERT_EQUAL_UINT64(1600, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());

    start_at(30, 0.0);
    g_nt_window.fb_height = 500;
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
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
    TEST_ASSERT_EQUAL_UINT64(2ULL * 1600ULL, s_be.audio_frames); /* silence beside the frames, no end padding */
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
}

static void test_open_failure_refuses_to_start(void) {
    s_be.fail_open = true;
    TEST_ASSERT_FALSE(nt_recorder_start("clip.mp4", 30));
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
}

static void test_tiny_framebuffer_refuses_to_start(void) {
    g_nt_window.fb_width = 1;
    TEST_ASSERT_FALSE(nt_recorder_start("clip.mp4", 30));
    g_nt_window.fb_width = 641;
    g_nt_window.fb_height = 1;
    TEST_ASSERT_FALSE(nt_recorder_start("clip.mp4", 30));
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(0, s_be.opens);
    TEST_ASSERT_EQUAL_UINT32(2, s_be.warns);
}

/* After a recording, so the core holds a stale clock and size that a missing idle check would use. */
static void test_idle_calls_are_inert(void) {
    start_at(30, 0.0);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    memset(&s_be, 0, sizeof(s_be));
    float samples[4] = {1, 1, 1, 1};
    nt_recorder_frame(1.0, 1.0);
    nt_recorder_push_audio(samples, 2);
    nt_recorder_push_audio(NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(0, s_be.closes);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.videos);
    TEST_ASSERT_EQUAL_UINT64(0, s_be.audio_frames);
}

/* A clock jump of years ends the recording after a bounded count instead of counting every deadline. */
static void test_huge_time_jump_ends_quickly(void) {
    start_at(30, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    frame_at(1.5e8, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closed_frames);
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

/* ---- Origin ---- */

/* Native devapi starts inside a step after nt_app advanced time; that step's samples and frame follow,
   so the origin is the step start: every frame lands on its deadline and nothing drifts. */
static void test_origin_is_the_step_start(void) {
    start_at(30, 5.0);
    for (int i = 1; i <= 90; i++) {
        push_stream(1600);
        frame_at(5.0 + (i / 30.0), 1.0 / 30.0);
    }
    TEST_ASSERT_EQUAL_UINT32(90, nt_recorder_stop());
    TEST_ASSERT_TRUE(s_be.worst_video_error < 1e-9);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.warns);
}

/* Frame calls at frozen time (MANUAL idle) set the origin there; the steps then record. */
static void test_frozen_start_then_steps(void) {
    start_at(30, 5.0);
    frame_at(5.0, 0.0);
    frame_at(5.0, 0.0);
    for (int i = 1; i <= 30; i++) {
        frame_at(5.0 + (i / 30.0), 1.0 / 30.0);
    }
    TEST_ASSERT_EQUAL_UINT32(30, nt_recorder_stop());
    TEST_ASSERT_TRUE(s_be.worst_video_error < 1e-9);
}

/* Render off after start: the samples pushed before the first frame call set the origin, and that call
   takes the frames of the whole span. */
static void test_pushes_before_the_first_frame_set_the_origin(void) {
    start_at(30, 0.0);
    for (int i = 0; i < 3; i++) {
        push_stream(1600);
    }
    frame_at(0.1, 1.0 / 30.0);
    TEST_ASSERT_EQUAL_UINT32(3, s_be.videos);
    for (int i = 4; i <= 30; i++) {
        push_stream(1600);
        frame_at(i / 30.0, 1.0 / 30.0);
    }
    TEST_ASSERT_EQUAL_UINT32(30, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(30ULL * 1600ULL, s_be.audio_frames);
    TEST_ASSERT_FALSE(s_be.stream_broken);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.warns);
}

/* Over two seconds pushed before the first frame call end the recording, as any two-second gap does. */
static void test_long_render_off_after_start_ends(void) {
    start_at(30, 0.0);
    for (int i = 0; i < 64; i++) {
        push_stream(1600);
    }
    frame_at(64.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
}

static void test_stop_without_frames_closes_empty(void) {
    start_at(30, 0.0);
    push_stream(800);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.closed_frames);
}

/* A recording started right after another takes its own origin from its first frame call. */
static void test_restart_takes_a_new_origin(void) {
    start_at(30, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_EQUAL_UINT32(1, nt_recorder_stop());
    start_at(30, 10.0);
    for (int i = 1; i <= 30; i++) {
        frame_at(10.0 + (i / 30.0), 1.0 / 30.0);
    }
    TEST_ASSERT_EQUAL_UINT32(30, nt_recorder_stop());
    TEST_ASSERT_TRUE(s_be.worst_video_error < 1e-9);
}

/* ---- Audio ---- */

static void test_silent_track_matches_video_length(void) {
    TEST_ASSERT_EQUAL_UINT32(45, run_lockstep(1.0F / 30.0F, 30, 45));
    TEST_ASSERT_EQUAL_UINT64(45ULL * 1600ULL, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(0, s_be.audio_nonzero);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.warns);
}

/* A mixer like devapi_host's (samples per step from absolute times) at a 144 Hz step with t0 off the
   sample grid: the stream reaches the encoder whole and in order, and stays within the drift bound. */
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

/* Silence keeps pace with the clock until the mixer starts 0.2 s in, between the 2 fps frames; its
   stream then follows on time, with no WARN. An empty push is not a first push, and a new recording
   starts silent again. */
static void test_silence_until_the_first_push(void) {
    start_at(2, 0.0);
    uint64_t pushed = 0;
    for (int i = 1; i <= 600; i++) {
        if (i > 12) {
            push_stream(800);
            pushed += 800;
        }
        frame_at(i / 60.0, 1.0 / 60.0);
        if (i == 12) {
            TEST_ASSERT_EQUAL_UINT64(12ULL * 800ULL, s_be.audio_frames);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(20, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(pushed, s_be.audio_nonzero);
    TEST_ASSERT_EQUAL_UINT64(20ULL * 24000ULL, s_be.audio_frames);
    TEST_ASSERT_FALSE(s_be.stream_broken);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.warns);

    start_at(2, 0.0);
    uint64_t before = s_be.audio_frames;
    push_stream(0);
    frame_at(0.5, 0.5);
    TEST_ASSERT_EQUAL_UINT64(before + 24000U, s_be.audio_frames);
}

/* Silence rounds to the nearest sample (1.3 -> 1, 2.7 -> 3). */
static void test_silence_rounds_to_the_nearest_sample(void) {
    start_at(30, 0.0);
    frame_at(1.3 / 48000.0, 1.3 / 48000.0);
    TEST_ASSERT_EQUAL_UINT64(1, s_be.audio_frames);
    frame_at(2.7 / 48000.0, 1.4 / 48000.0);
    TEST_ASSERT_EQUAL_UINT64(3, s_be.audio_frames);
}

/* A mixer that starts while render is off continues the track from the last frame call, so it runs
   behind the clock by the gap; the WARN says so. */
static void test_mixer_starting_during_render_off_warns(void) {
    start_at(30, 0.0);
    frame_at(1.0 / 60.0, 1.0 / 60.0);
    for (int i = 21; i <= 31; i++) { /* steps 2..20 had no frame call and no mixer */
        push_stream(800);
    }
    frame_at(31.0 / 60.0, 1.0 / 60.0);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
    TEST_ASSERT_NOT_NULL(strstr(s_be.warn, "behind"));
}

static void test_silence_failure_ends_the_recording(void) {
    start_at(30, 0.0);
    s_be.fail_audio = true;
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
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

/* Pushes `per_step` samples for each of `steps` 1/60 steps at fps 30, from `first` on. */
static void run_mixer(int first, int steps, uint32_t per_step) {
    for (int i = first; i < first + steps; i++) {
        push_stream(per_step);
        frame_at(i / 60.0, 1.0 / 60.0);
    }
}

/* The bound is 100 ms either way: a gap of 83 ms passes, 133 ms warns, and a stream running ahead
   warns from just over 100 ms. A new recording warns again. */
static void test_drift_bound_both_ways_and_per_recording(void) {
    start_at(30, 0.0);
    run_mixer(1, 60, 800);
    run_mixer(66, 60, 800); /* 5 steps (83 ms) missing: within the bound */
    TEST_ASSERT_EQUAL_UINT32(0, s_be.warns);
    run_mixer(129, 60, 800); /* 3 more (133 ms total) */
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
    TEST_ASSERT_NOT_NULL(strstr(s_be.warn, "behind"));
    (void)nt_recorder_stop();

    s_be.warns = 0;
    start_at(30, 0.0);
    run_mixer(1, 60, 800);
    push_stream(4096);
    push_stream(656); /* 99 ms ahead */
    run_mixer(61, 1, 800);
    TEST_ASSERT_EQUAL_UINT32(0, s_be.warns);
    push_stream(96); /* 101 ms ahead */
    run_mixer(62, 1, 800);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
    TEST_ASSERT_NOT_NULL(strstr(s_be.warn, "ahead of"));
}

/* End padding rounds to the nearest sample at a rate that does not divide 48000. */
static void test_padding_rounds_at_fps_7(void) {
    start_at(7, 0.0);
    frame_at(1.0 / 7.0, 1.0 / 7.0);
    push_stream(1);
    frame_at(2.0 / 7.0, 1.0 / 7.0);
    TEST_ASSERT_EQUAL_UINT32(2, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(13714, s_be.audio_frames); /* 13714.29 */

    memset(&s_be, 0, sizeof(s_be));
    start_at(7, 0.0);
    frame_at(1.0 / 7.0, 1.0 / 7.0);
    push_stream(1);
    frame_at(4.0 / 7.0, 1.0 / 7.0);
    TEST_ASSERT_EQUAL_UINT32(4, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(27429, s_be.audio_frames); /* 27428.57 */
}

/* The end line reports the audio pushed against the video, before padding hides the difference. */
static void test_end_line_reports_audio_against_video(void) {
    start_at(30, 0.0);
    push_stream(800);
    frame_at(1.0 / 60.0, 1.0 / 60.0);
    frame_at(3.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_EQUAL_UINT32(3, nt_recorder_stop());
    TEST_ASSERT_EQUAL_STRING("3 frames, 0.10 s; audio 0.02 s before padding (-83 ms against the video)", s_be.info);
}

/* Short audio is padded with silence to the video at stop; audio pushed past the video stays. */
static void test_stop_pads_short_audio_and_keeps_long(void) {
    start_at(30, 0.0);
    push_stream(800);
    frame_at(1.0 / 60.0, 1.0 / 60.0);
    frame_at(3.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_EQUAL_UINT32(3, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(4800, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(800, s_be.audio_nonzero);

    memset(&s_be, 0, sizeof(s_be));
    start_at(30, 0.0);
    push_stream(800);
    frame_at(1.0 / 60.0, 1.0 / 60.0);
    push_stream(3200);
    frame_at(1.0 / 30.0, 1.0 / 60.0);
    TEST_ASSERT_EQUAL_UINT32(1, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(4000, s_be.audio_frames);
}

static void test_final_audio_failure_returns_zero(void) {
    start_at(30, 0.0);
    push_stream(800);
    frame_at(1.0 / 60.0, 1.0 / 60.0);
    frame_at(1.0 / 30.0, 1.0 / 60.0);
    s_be.fail_audio = true;
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
}

static void test_audio_failure_ends_the_recording(void) {
    start_at(30, 0.0);
    s_be.fail_audio = true;
    push_stream(1600);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.warns);
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
    RUN_TEST(test_open_failure_refuses_to_start);
    RUN_TEST(test_tiny_framebuffer_refuses_to_start);
    RUN_TEST(test_idle_calls_are_inert);
    RUN_TEST(test_origin_is_the_step_start);
    RUN_TEST(test_frozen_start_then_steps);
    RUN_TEST(test_pushes_before_the_first_frame_set_the_origin);
    RUN_TEST(test_long_render_off_after_start_ends);
    RUN_TEST(test_stop_without_frames_closes_empty);
    RUN_TEST(test_restart_takes_a_new_origin);
    RUN_TEST(test_silent_track_matches_video_length);
    RUN_TEST(test_lockstep_mixer_stream_is_written_whole);
    RUN_TEST(test_block_mixer_does_not_warn);
    RUN_TEST(test_silence_until_the_first_push);
    RUN_TEST(test_silence_rounds_to_the_nearest_sample);
    RUN_TEST(test_mixer_starting_during_render_off_warns);
    RUN_TEST(test_silence_failure_ends_the_recording);
    RUN_TEST(test_mixer_gap_warns_once);
    RUN_TEST(test_drift_bound_both_ways_and_per_recording);
    RUN_TEST(test_padding_rounds_at_fps_7);
    RUN_TEST(test_end_line_reports_audio_against_video);
    RUN_TEST(test_stop_pads_short_audio_and_keeps_long);
    RUN_TEST(test_final_audio_failure_returns_zero);
    RUN_TEST(test_audio_failure_ends_the_recording);
    return UNITY_END();
}
