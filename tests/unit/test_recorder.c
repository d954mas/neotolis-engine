/* nt_recorder core: frame selection on the caller's clock and audio placement on the track, against
   a test-defined platform backend. */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "recorder/nt_recorder.h"
#include "recorder/nt_recorder_internal.h"
#include "unity.h"
#include "window/nt_window.h"

/* ---- Test backend ---- */

#define PRE_START_SAMPLE 9.0F
#define MAX_RUNS 16

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
    uint64_t run_starts[MAX_RUNS]; /* track positions where non-zero audio starts after silence */
    uint32_t runs;
    bool last_nonzero;
    bool saw_pre_start;
    uint64_t audio_ahead; /* most audio past the written video after any frame call */
    uint32_t closes;
    uint32_t closed_frames;
    bool fail_video;
    uint32_t fail_index;
    bool fail_audio;
    /* Tagged mode: a pushed sample holds 1 + its true track position. */
    bool tagged;
    float last_tag;
    bool tag_order_broken;
    uint64_t tags;
    double worst_tag_error;
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
        bool nonzero = v != 0.0F;
        if (nonzero) {
            s_be.audio_nonzero++;
            if (!s_be.last_nonzero && s_be.runs < MAX_RUNS) {
                s_be.run_starts[s_be.runs++] = s_be.audio_frames + i;
            }
        }
        if (v == PRE_START_SAMPLE && !s_be.tagged) {
            s_be.saw_pre_start = true;
        }
        if (s_be.tagged && nonzero) {
            s_be.tag_order_broken |= v <= s_be.last_tag;
            s_be.last_tag = v;
            s_be.tags++;
            double err = fabs((double)(s_be.audio_frames + i) - (double)(v - 1.0F));
            s_be.worst_tag_error = err > s_be.worst_tag_error ? err : s_be.worst_tag_error;
        }
        s_be.last_nonzero = nonzero;
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
    double ahead = (double)s_be.audio_frames - ((double)s_be.videos * s_be.period * 48000.0);
    if (ahead > (double)s_be.audio_ahead) {
        s_be.audio_ahead = (uint64_t)ahead;
    }
}

static void push_block(float v, uint32_t frames, double t_start) {
    static float s_buf[96000 * 2];
    TEST_ASSERT_TRUE(frames <= 96000);
    for (uint32_t i = 0; i < frames * 2; i++) {
        s_buf[i] = v;
    }
    nt_recorder_push_audio(s_buf, frames, t_start);
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
    nt_recorder_push_audio(samples, 2, 0.0);
    nt_recorder_push_audio(NULL, 0, 0.0);
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(0, s_be.closes);
}

/* ---- Audio ---- */

static void test_silent_track_matches_video_length(void) {
    TEST_ASSERT_EQUAL_UINT32(45, run_lockstep(1.0F / 30.0F, 30, 45));
    TEST_ASSERT_EQUAL_UINT64(45ULL * 1600ULL, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(0, s_be.audio_nonzero);
}

/* A lockstep mixer pushes each step's block at the step's start: all of it is kept, and a block from
   before t0 is not in the file. */
static void test_lockstep_blocks_are_kept_whole(void) {
    start_at(30, 0.0);
    push_block(PRE_START_SAMPLE, 1600, -1.0 / 30.0);
    double t = 0.0;
    for (int i = 0; i < 30; i++) {
        push_block(0.25F, 1600, t);
        t += 1.0 / 30.0;
        frame_at(t, 1.0 / 30.0);
    }
    TEST_ASSERT_EQUAL_UINT32(30, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(30ULL * 1600ULL, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(30ULL * 1600ULL, s_be.audio_nonzero);
    TEST_ASSERT_FALSE(s_be.saw_pre_start);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.runs);
}

/* Audio never runs past the video: three 1/60 steps at fps 30 are one frame (1600 samples); 1.9 s at
   fps 1 is one second. */
static void test_audio_stops_at_the_video_end(void) {
    start_at(30, 0.0);
    for (int i = 0; i < 3; i++) {
        push_block(0.5F, 800, i / 60.0);
        frame_at((i + 1) / 60.0, 1.0 / 60.0);
    }
    TEST_ASSERT_EQUAL_UINT64(1600, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT32(1, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(1600, s_be.audio_frames);

    memset(&s_be, 0, sizeof(s_be));
    start_at(1, 0.0);
    for (int i = 0; i < 114; i++) {
        push_block(0.5F, 800, i / 60.0);
        frame_at((i + 1) / 60.0, 1.0 / 60.0);
    }
    TEST_ASSERT_EQUAL_UINT32(1, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(48000, s_be.audio_frames);
    TEST_ASSERT_EQUAL_UINT64(0, s_be.audio_ahead);
}

/* A block held for the next frame keeps its place when frame calls and pushes then pause: the next
   block lands after a gap of silence at its own time. */
static void test_held_audio_and_a_later_block_keep_their_places(void) {
    start_at(30, 0.0);
    push_block(0.5F, 800, 0.0);
    frame_at(1.0 / 60.0, 1.0 / 60.0);
    push_block(0.5F, 800, 11.0 / 60.0);
    frame_at(12.0 / 60.0, 1.0 / 60.0);
    TEST_ASSERT_EQUAL_UINT32(2, s_be.runs);
    TEST_ASSERT_EQUAL_UINT64(0, s_be.run_starts[0]);
    TEST_ASSERT_EQUAL_UINT64(8800, s_be.run_starts[1]);
    TEST_ASSERT_EQUAL_UINT64(9600, s_be.audio_frames);
}

/* A real-time mixer delivers 128-sample blocks that never match a frame: they join into one run. */
static void test_block_mixer_is_one_run(void) {
    start_at(30, 0.0);
    double t = 0.0;
    uint64_t pushed = 0;
    for (int i = 0; i < 600; i++) {
        t += 1.0 / 60.0;
        while ((pushed + 128U) <= (uint64_t)(t * 48000.0)) {
            push_block(0.5F, 128, (double)pushed / 48000.0);
            pushed += 128;
        }
        frame_at(t, 1.0 / 60.0);
    }
    (void)nt_recorder_stop();
    TEST_ASSERT_EQUAL_UINT32(1, s_be.runs);
    TEST_ASSERT_TRUE(s_be.audio_nonzero >= s_be.audio_frames - 128U);
}

static void test_overlapping_push_is_dropped(void) {
    start_at(30, 0.0);
    push_block(0.5F, 1600, 0.0);
    push_block(0.5F, 1600, 0.0);
    frame_at(2.0 / 30.0, 2.0 / 30.0);
    TEST_ASSERT_EQUAL_UINT32(2, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64(1600, s_be.audio_nonzero);
    TEST_ASSERT_EQUAL_UINT64(3200, s_be.audio_frames);
}

static void test_drift_within_a_block_joins(void) {
    start_at(30, 0.0);
    push_block(0.5F, 1600, 0.0);
    push_block(0.5F, 1600, (1600.0 + 300.0) / 48000.0);
    frame_at(2.0 / 30.0, 2.0 / 30.0);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.runs);
    TEST_ASSERT_EQUAL_UINT64(3200, s_be.audio_nonzero);
}

/* With nothing queued, silence stays one block behind the video, so a mixer that delivers late is still
   placed at its own time. */
static void test_late_block_within_a_block_is_kept(void) {
    start_at(30, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    push_block(0.5F, 1600, 800.0 / 48000.0);
    frame_at(2.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.runs);
    TEST_ASSERT_EQUAL_UINT64(800, s_be.run_starts[0]);
}

/* At 60 fps a frame is 800 samples, under one block: the first frame writes no silence, so a block
   that arrives late at t0 is kept whole. */
static void test_first_late_block_at_high_fps_is_kept(void) {
    start_at(60, 0.0);
    frame_at(1.0 / 60.0, 1.0 / 60.0);
    push_block(0.5F, 800, 0.0);
    frame_at(2.0 / 60.0, 1.0 / 60.0);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.runs);
    TEST_ASSERT_EQUAL_UINT64(0, s_be.run_starts[0]);
}

/* A clock jump of years ends the recording after a bounded count instead of counting every deadline. */
static void test_huge_time_jump_ends_quickly(void) {
    start_at(30, 0.0);
    frame_at(1.5e8, 1.0 / 30.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(0, s_be.closed_frames);
}

/* Rounding puts a block one sample off the end of the previous one; with the queue already written it
   still continues the run instead of losing or inserting a sample. */
static void test_block_after_written_audio_joins(void) {
    start_at(30, 0.0);
    push_block(0.5F, 1600, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    push_block(0.5F, 1600, 1599.0 / 48000.0);
    frame_at(2.0 / 30.0, 1.0 / 30.0);
    push_block(0.5F, 1600, 3201.0 / 48000.0);
    frame_at(3.0 / 30.0, 1.0 / 30.0);
    TEST_ASSERT_EQUAL_UINT32(1, s_be.runs);
    TEST_ASSERT_EQUAL_UINT64(4800, s_be.audio_nonzero);
}

/* A mixer like devapi_host's at a 144 Hz step with t0 off the sample grid: one run, no lost samples. */
static void test_lockstep_mixer_off_the_sample_grid_is_one_run(void) {
    double t = 10.0 + (0.4 / 48000.0);
    start_at(60, t);
    const float step = 1.0F / 144.0F;
    uint64_t pushed = 0;
    for (int i = 0; i < 1440; i++) {
        double start = t;
        t += (double)step;
        uint32_t n = (uint32_t)(llround(t * 48000.0) - llround(start * 48000.0));
        push_block(0.5F, n, start);
        pushed += n;
        frame_at(t, (double)step);
    }
    (void)nt_recorder_stop();
    TEST_ASSERT_EQUAL_UINT32(1, s_be.runs);
    TEST_ASSERT_UINT64_WITHIN(800, pushed, s_be.audio_nonzero);
}

/* A real-time mixer delivers each block two frames late (~33 ms, audio thread to the main thread):
   every sample is kept in one run. */
static void test_late_real_time_mixer_keeps_every_sample(void) {
    start_at(30, 0.0);
    uint64_t pushed = 0;
    for (int i = 1; i <= 600; i++) {
        if (i > 2) {
            push_block(0.5F, 800, (double)(i - 3) / 60.0);
            pushed += 800;
        }
        frame_at(i / 60.0, 1.0 / 60.0);
    }
    (void)nt_recorder_stop();
    TEST_ASSERT_EQUAL_UINT32(1, s_be.runs);
    TEST_ASSERT_EQUAL_UINT64(pushed, s_be.audio_nonzero);
}

/* Render off for 2.4 s at fps 1 is within the catch-up bound, so the mixer's audio meanwhile is kept. */
static void test_render_off_gap_keeps_its_audio(void) {
    start_at(1, 0.0);
    uint64_t pushed = 0;
    for (int i = 1; i <= 300; i++) {
        push_block(0.5F, 800, (double)(i - 1) / 60.0);
        pushed += 800;
        if (i <= 60 || i > 204) {
            frame_at(i / 60.0, 1.0 / 60.0);
        }
    }
    TEST_ASSERT_TRUE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(5, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.runs);
    TEST_ASSERT_EQUAL_UINT64(pushed, s_be.audio_nonzero);
}

/* Render off past the catch-up bound fills the queue: the recording ends there, not with lost audio. */
static void test_full_audio_queue_ends_the_recording(void) {
    start_at(30, 0.0);
    push_block(0.5F, 1600, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    for (int i = 2; i <= 150 && nt_recorder_active(); i++) {
        push_block(0.5F, 1600, (double)(i - 1) / 30.0);
    }
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closed_frames);
    TEST_ASSERT_EQUAL_UINT64(1600, s_be.audio_frames);
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

/* One block far ahead of the clock cannot wait in the queue: the recording ends instead of every later
   block being dropped as an overlap. */
static void test_block_far_ahead_ends_the_recording(void) {
    start_at(30, 0.0);
    push_block(0.5F, 800, 5.0);
    TEST_ASSERT_FALSE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
}

/* fps 1: silence written 250 ms behind the first frame, a real-time mixer resuming 200 ms behind it,
   then render off for 2.9 s (within the catch-up bound): the queue holds all of it. */
static void test_queue_holds_catch_up_after_late_silence(void) {
    start_at(1, 0.0);
    for (int i = 1; i <= 60; i++) {
        frame_at(i / 60.0, 1.0 / 60.0);
    }
    for (int k = 48; k < 234; k++) {
        push_block(0.5F, 800, k / 60.0);
    }
    frame_at(234.0 / 60.0, 1.0 / 60.0);
    TEST_ASSERT_TRUE(nt_recorder_active());
    TEST_ASSERT_EQUAL_UINT32(3, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT64((3ULL * 48000ULL) - (48ULL * 800ULL), s_be.audio_nonzero);
}

static uint32_t next_rand(uint32_t *state) {
    *state = (*state * 1664525U) + 1013904223U;
    return *state >> 8;
}

/* A real-time mixer with 128-sample blocks: start times off by up to 300 samples, delivery up to three
   frames late, pauses of half a second. Each sample holds 1 + its true track position. */
#define MIX_BLOCK 128U
#define MIX_OFF 300
#define MIX_MAX_BLOCKS 40000U

static struct {
    uint32_t rng;
    uint64_t made;      /* true position after the last block rendered */
    uint64_t delivered; /* true position of the next block to reach the main thread */
    uint32_t blocks;
    uint64_t starts[MIX_MAX_BLOCKS];
} s_mix;

static void mixer_advance(double t) {
    static float s_tag[MIX_BLOCK * 2U];
    if (next_rand(&s_mix.rng) % 400U == 0) {
        s_mix.made += 24000; /* the mixer pauses for half a second */
        s_mix.delivered = s_mix.made;
    }
    while (s_mix.made + MIX_BLOCK <= (uint64_t)(t * 48000.0)) {
        s_mix.made += MIX_BLOCK;
    }
    double reach = (t - ((double)(next_rand(&s_mix.rng) % 4U) / 60.0)) * 48000.0;
    for (; s_mix.delivered + MIX_BLOCK <= s_mix.made && (double)(s_mix.delivered + MIX_BLOCK) <= reach; s_mix.delivered += MIX_BLOCK) {
        for (uint32_t k = 0; k < MIX_BLOCK; k++) {
            s_tag[(size_t)k * 2U] = (float)(s_mix.delivered + k + 1U);
            s_tag[((size_t)k * 2U) + 1U] = s_tag[(size_t)k * 2U];
        }
        double off = (double)((int32_t)(next_rand(&s_mix.rng) % ((2U * MIX_OFF) + 1U)) - MIX_OFF);
        nt_recorder_push_audio(s_tag, MIX_BLOCK, ((double)s_mix.delivered + off) / 48000.0);
        TEST_ASSERT_TRUE(s_mix.blocks < MIX_MAX_BLOCKS);
        s_mix.starts[s_mix.blocks++] = s_mix.delivered;
    }
}

/* Under jittered frame times and render-off stretches of up to a second, no sample of that mixer is
   lost, reordered or placed more than a join away, and the track never leads the video. */
static void test_real_time_mixer_invariants(void) {
    memset(&s_mix, 0, sizeof(s_mix));
    s_mix.rng = 12345U;
    s_be.tagged = true;
    start_at(30, 0.0);
    double t = 0.0;
    uint32_t render_off = 0;
    for (int i = 0; i < 6000; i++) {
        double dt = (1.0 / 60.0) * (0.7 + ((double)(next_rand(&s_mix.rng) % 600U) / 1000.0));
        t += dt;
        mixer_advance(t);
        if (render_off == 0 && next_rand(&s_mix.rng) % 300U == 0) {
            render_off = 1 + (next_rand(&s_mix.rng) % 60U);
        }
        if (render_off > 0) {
            render_off--;
        } else {
            frame_at(t, dt);
        }
    }
    TEST_ASSERT_TRUE(nt_recorder_active());
    uint64_t video_end = (uint64_t)nt_recorder_stop() * 1600U;
    /* Blocks clear of the video end by more than the placement bound are written whole; none past it. */
    const uint64_t bound = 1024U + MIX_OFF;
    uint64_t must = 0;
    uint64_t may = 0;
    for (uint32_t b = 0; b < s_mix.blocks; b++) {
        must += s_mix.starts[b] + MIX_BLOCK + bound <= video_end ? MIX_BLOCK : 0U;
        may += s_mix.starts[b] < video_end + bound ? MIX_BLOCK : 0U;
    }
    TEST_ASSERT_FALSE(s_be.tag_order_broken);
    TEST_ASSERT_TRUE(s_be.worst_tag_error <= (double)bound);
    TEST_ASSERT_EQUAL_UINT64(0, s_be.audio_ahead);
    TEST_ASSERT_TRUE(s_be.tags >= must && s_be.tags <= may);
    TEST_ASSERT_TRUE(must > 30ULL * 48000ULL);
}

static void test_final_audio_failure_returns_zero(void) {
    start_at(30, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
    s_be.fail_audio = true;
    TEST_ASSERT_EQUAL_UINT32(0, nt_recorder_stop());
    TEST_ASSERT_EQUAL_UINT32(1, s_be.closes);
}

static void test_audio_failure_ends_the_recording(void) {
    start_at(30, 0.0);
    s_be.fail_audio = true;
    push_block(0.5F, 1600, 0.0);
    frame_at(1.0 / 30.0, 1.0 / 30.0);
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
    RUN_TEST(test_resize_ends_with_audio_padded_to_the_video);
    RUN_TEST(test_video_failure_ends_the_recording);
    RUN_TEST(test_tiny_framebuffer_refuses_to_start);
    RUN_TEST(test_idle_calls_are_inert);
    RUN_TEST(test_silent_track_matches_video_length);
    RUN_TEST(test_lockstep_blocks_are_kept_whole);
    RUN_TEST(test_audio_stops_at_the_video_end);
    RUN_TEST(test_held_audio_and_a_later_block_keep_their_places);
    RUN_TEST(test_block_mixer_is_one_run);
    RUN_TEST(test_overlapping_push_is_dropped);
    RUN_TEST(test_drift_within_a_block_joins);
    RUN_TEST(test_late_block_within_a_block_is_kept);
    RUN_TEST(test_first_late_block_at_high_fps_is_kept);
    RUN_TEST(test_huge_time_jump_ends_quickly);
    RUN_TEST(test_block_after_written_audio_joins);
    RUN_TEST(test_lockstep_mixer_off_the_sample_grid_is_one_run);
    RUN_TEST(test_late_real_time_mixer_keeps_every_sample);
    RUN_TEST(test_render_off_gap_keeps_its_audio);
    RUN_TEST(test_full_audio_queue_ends_the_recording);
    RUN_TEST(test_catch_up_bound_is_two_seconds_of_frames);
    RUN_TEST(test_block_far_ahead_ends_the_recording);
    RUN_TEST(test_queue_holds_catch_up_after_late_silence);
    RUN_TEST(test_real_time_mixer_invariants);
    RUN_TEST(test_final_audio_failure_returns_zero);
    RUN_TEST(test_audio_failure_ends_the_recording);
    return UNITY_END();
}
