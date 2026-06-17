/* L1 tests for the managed loop time-control API (TIME-01/02/03/06 + D-11 sim-advance).
   Drives nt_app_run_managed via the window-free stub backend: records g_nt_app.dt per frame
   and asserts the single-scalar chokepoint behaves per mode (RUN/PAUSE/MANUAL/scale) plus the
   loop-agnostic render flag. Bit-exact 1/60 lockstep is the determinism contract (D-10/D-12). */

#include "app/nt_app.h"
#include "unity.h"

#include <math.h>
#include <string.h>

/* ---- Recorder ---- */

#define DT_LOG_CAP 64

static float s_dt_log[DT_LOG_CAP];
static int s_frame_log_count; /* number of frame-fn invocations recorded */
static int s_advance_target;  /* quit once g_nt_app.frame reaches this (sim-advance count) */
static int s_iter_target;     /* quit once this many frame-fn invocations happen (idle loops) */

/* Records dt; quits after the configured sim-advance OR iteration target. Using two targets
   lets PAUSE/MANUAL-idle tests terminate (frame never advances there). */
static void record_frame_fn(void) {
    if (s_frame_log_count < DT_LOG_CAP) {
        s_dt_log[s_frame_log_count] = g_nt_app.dt;
    }
    s_frame_log_count++;

    bool advance_hit = (s_advance_target > 0) && ((int)g_nt_app.frame >= s_advance_target);
    bool iter_hit = (s_iter_target > 0) && (s_frame_log_count >= s_iter_target);
    if (advance_hit || iter_hit) {
        nt_app_quit();
    }
}

void setUp(void) {
    memset(s_dt_log, 0, sizeof(s_dt_log));
    s_frame_log_count = 0;
    s_advance_target = 0;
    s_iter_target = 0;

    /* Reset engine state to construction defaults (matches g_nt_app initializer in nt_app.c). */
    memset(&g_nt_app, 0, sizeof(g_nt_app));
    g_nt_app.max_dt = 0.1F;
    g_nt_app.scale = 1.0F;
    g_nt_app.step_dt = 1.0F / 60.0F;
    g_nt_app.render_enabled = true;
}

void tearDown(void) { /* per-test cleanup not required */ }

/* TIME-06: RUN mode advances exactly like the raw loop — frame++ each iteration, dt = clamped
   wall dt (here bounded by a tiny max_dt so every recorded dt is the clamp value, deterministic). */
void test_managed_run_matches_raw(void) {
    g_nt_app.max_dt = 0.001F;
    g_nt_app.mode = NT_APP_MODE_RUN;
    s_advance_target = 5;

    nt_app_run_managed(record_frame_fn);

    /* Every iteration advances the sim (frame++ each loop, like the raw loop). dt is the clamped
       wall dt; on a fast machine the first iteration's wall dt can be ~0, so assert only the
       upper bound (clamp) here — the > 0 path is covered by MANUAL's exact-dt tests. */
    TEST_ASSERT_EQUAL_UINT32(5, g_nt_app.frame);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE_MESSAGE(s_dt_log[i] >= 0.0F, "RUN dt must be non-negative");
        TEST_ASSERT_TRUE_MESSAGE(s_dt_log[i] <= 0.001F + 1e-6F, "RUN dt must be clamped to max_dt");
    }
}

/* TIME-01: PAUSE zeroes dt and freezes frame while the frame fn keeps being called. */
void test_pause_zeroes_dt_and_freezes_frame(void) {
    g_nt_app.mode = NT_APP_MODE_RUN;
    nt_app_pause();
    s_iter_target = 5; /* frame never advances under pause -> terminate by iteration count */

    nt_app_run_managed(record_frame_fn);

    TEST_ASSERT_EQUAL_UINT32(0, g_nt_app.frame); /* frozen */
    TEST_ASSERT_TRUE_MESSAGE(s_frame_log_count >= 5, "frame fn must keep running under pause");
    const float zero = 0.0F;
    for (int i = 0; i < 5; i++) {
        /* bit-exact zero (UNITY_EXCLUDE_FLOAT is defined for these targets). */
        TEST_ASSERT_EQUAL_MEMORY(&zero, &s_dt_log[i], sizeof(float));
    }
}

/* TIME-02: MANUAL mode feeds a bit-exact 1/60 dt for each pending step; frame == N. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_manual_step_bit_exact_1_60(void) {
    const float expected = 1.0F / 60.0F;
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    g_nt_app.step_dt = expected;
    nt_app_step(5);
    s_advance_target = 5;

    nt_app_run_managed(record_frame_fn);

    TEST_ASSERT_EQUAL_UINT32(5, g_nt_app.frame);
    for (int i = 0; i < 5; i++) {
        /* bit-exact: the engine feeds step_dt with no wall clock and no max_dt clamp. */
        TEST_ASSERT_EQUAL_MEMORY(&expected, &s_dt_log[i], sizeof(float));
    }
}

/* TIME-02 reproducibility: the same step sequence yields a byte-identical dt array twice. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_manual_reproducible_across_two_runs(void) {
    const float expected = 1.0F / 60.0F;
    float run_a[8];
    float run_b[8];

    g_nt_app.mode = NT_APP_MODE_MANUAL;
    g_nt_app.step_dt = expected;
    nt_app_step(8);
    s_advance_target = 8;
    nt_app_run_managed(record_frame_fn);
    memcpy(run_a, s_dt_log, sizeof(run_a));

    /* Second run from the same defaults. */
    setUp();
    g_nt_app.mode = NT_APP_MODE_MANUAL;
    g_nt_app.step_dt = expected;
    nt_app_step(8);
    s_advance_target = 8;
    nt_app_run_managed(record_frame_fn);
    memcpy(run_b, s_dt_log, sizeof(run_b));

    TEST_ASSERT_EQUAL_MEMORY(run_a, run_b, sizeof(run_a));
}

/* TIME-03: dt-scale multiplies the wall dt in RUN mode (dt = clamp(wall, max_dt) * scale).
   Pace the loop via target_dt so each frame's wall dt reliably reaches the clamp, making the
   scaled result deterministic: max_dt = 1ms, target_dt = 5ms -> wall dt clamps to 1ms ->
   scale=2 -> dt == 2ms. Skip the first iteration (its wall dt is ~0, pre-pacing). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_scale_multiplies_wall_dt(void) {
    g_nt_app.max_dt = 0.001F;    /* clamp at 1ms */
    g_nt_app.target_dt = 0.005F; /* pace each frame to ~5ms wall so wall dt always hits the clamp */
    g_nt_app.mode = NT_APP_MODE_RUN;
    nt_app_set_scale(2.0F);
    s_advance_target = 6;

    nt_app_run_managed(record_frame_fn);

    /* Frames 2..N (index >= 1) are paced: wall dt >= target_dt > max_dt, so dt == max_dt*scale. */
    const float scaled_clamp = 0.001F * 2.0F;
    for (int i = 1; i < 6; i++) {
        TEST_ASSERT_TRUE_MESSAGE(s_dt_log[i] <= scaled_clamp + 1e-6F, "scaled dt must respect scale*clamp ceiling");
        TEST_ASSERT_TRUE_MESSAGE(s_dt_log[i] >= scaled_clamp - 1e-6F, "paced wall dt * scale must equal scale*clamp");
    }
}

/* D-11 sim-advance proxy: MANUAL with no pending steps never advances the frame (so the deferred
   tick — driven by sim-advance in managed mode — would never fire during manual-idle). The full
   L1+L2 "yield after N sim-advances, never during pause/idle" test lands in Plan 02's
   test_devapi_time.c, where the deferred queue + submit() are linked. */
void test_manual_idle_freezes_frame(void) {
    g_nt_app.mode = NT_APP_MODE_MANUAL; /* no nt_app_step() -> pending_steps == 0 */
    s_iter_target = 5;

    nt_app_run_managed(record_frame_fn);

    TEST_ASSERT_EQUAL_UINT32(0, g_nt_app.frame); /* frozen: no sim-advance, no tick */
    TEST_ASSERT_TRUE_MESSAGE(s_frame_log_count >= 5, "frame fn must keep running in manual-idle");
}

/* TIME-04 (L1 half): render flag defaults true and toggles; loop-agnostic engine state. */
void test_render_flag_default_true_and_toggles(void) {
    TEST_ASSERT_TRUE(nt_app_render_enabled());
    nt_app_set_render_enabled(false);
    TEST_ASSERT_FALSE(nt_app_render_enabled());
    nt_app_set_render_enabled(true);
    TEST_ASSERT_TRUE(nt_app_render_enabled());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_managed_run_matches_raw);
    RUN_TEST(test_pause_zeroes_dt_and_freezes_frame);
    RUN_TEST(test_manual_step_bit_exact_1_60);
    RUN_TEST(test_manual_reproducible_across_two_runs);
    RUN_TEST(test_scale_multiplies_wall_dt);
    RUN_TEST(test_manual_idle_freezes_frame);
    RUN_TEST(test_render_flag_default_true_and_toggles);
    return UNITY_END();
}
