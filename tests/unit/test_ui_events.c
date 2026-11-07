/* nt_ui_events — the single canonical mutating interaction step.
 *
 * Mirrors test_nt_ui_interaction's dt-driven frame idiom: drive ctx->frame_dt via the
 * nt_ui_begin dt arg, step across frames, assert latched edges/timers.
 *
 * Coverage:
 *   capture edges, gestures without a state cell, hold_progress ramp 0..1, one-shot long_pressed,
 *   leave-hitbox cancel, double-click window/radius/pairing. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_state.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Known bbox (non-square, non-origin so a stray axis swap is visible). */
#define BTN_X 100.0F
#define BTN_Y 200.0F
#define BTN_W 160.0F
#define BTN_H 48.0F
#define BTN_CX (BTN_X + (BTN_W * 0.5F))
#define BTN_CY (BTN_Y + (BTN_H * 0.5F))

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static nt_pointer_t make_pointer(float x, float y, bool is_down, bool is_pressed, bool is_released) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = is_down;
    p.buttons[NT_BUTTON_LEFT].is_pressed = is_pressed;
    p.buttons[NT_BUTTON_LEFT].is_released = is_released;
    return p;
}

static void declare_btn_element(void) {
    CLAY({.id = CLAY_ID("btn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
}

/* A frame that declares the bbox and steps via nt_ui_events. dt drives the gesture timers. */
static nt_ui_events_t events_btn_frame(const nt_pointer_t *p, float dt, float long_press_secs) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, dt, p, 1, 0);
    declare_btn_element();
    nt_ui_events_t e = nt_ui_events(s_fx.ctx, nt_ui_id("btn"), long_press_secs);
    nt_ui_end(s_fx.ctx);
    return e;
}

/* Warm-up: register btn so next-frame arbitration sees it (front-most resolve reads prev frame). */
static void warm_btn_frame(const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1, 0);
    declare_btn_element();
    (void)nt_ui_events(s_fx.ctx, nt_ui_id("btn"), 0.0F);
    nt_ui_end(s_fx.ctx);
}

/* ---- capture parity — nt_ui_events matches step_interaction edges ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_capture_parity_with_step_interaction(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Press inside. */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_events_t e2 = events_btn_frame(&f2, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(e2.hovered);
    TEST_ASSERT_TRUE(e2.pressed);
    TEST_ASSERT_TRUE(e2.held); /* pressed && hovered */
    TEST_ASSERT_FALSE(e2.clicked);
    TEST_ASSERT_FALSE(e2.released);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Hold. */
    nt_pointer_t f3 = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_events_t e3 = events_btn_frame(&f3, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(e3.pressed);
    TEST_ASSERT_TRUE(e3.held);
    TEST_ASSERT_FALSE(e3.clicked);

    /* Release inside -> clicked one-shot, capture cleared. */
    nt_pointer_t f4 = make_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_events_t e4 = events_btn_frame(&f4, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(e4.released);
    TEST_ASSERT_TRUE(e4.clicked);
    TEST_ASSERT_FALSE(e4.pressed);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
}

/* held is pressed && hovered: dragging off the widget while down keeps pressed but drops held. */
static void test_events_held_is_pressed_and_hovered(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&f2, 0.0F, 0.0F);

    /* Drag off-widget, still down: capture holds (pressed) but not over widget (not held). */
    const float far_x = BTN_X + BTN_W + 200.0F;
    const float far_y = BTN_Y + BTN_H + 200.0F;
    nt_pointer_t f3 = make_pointer(far_x, far_y, true, false, false);
    nt_ui_events_t e3 = events_btn_frame(&f3, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(e3.pressed);
    TEST_ASSERT_FALSE(e3.hovered);
    TEST_ASSERT_FALSE(e3.held);
    TEST_ASSERT_TRUE(float_near(e3.drag_dx, far_x - BTN_CX, 0.01F));
}

/* ---- gestures live in captures[] and the ctx, never in the state pool ---- */
static void test_events_gestures_use_no_state_cell(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    const uint32_t before = nt_ui_state_used_slots(s_fx.ctx);

    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&f2, 0.0F, 0.5F);
    nt_pointer_t f3 = make_pointer(BTN_CX, BTN_CY, true, false, false);
    (void)events_btn_frame(&f3, 0.25F, 0.5F);

    TEST_ASSERT_EQUAL_UINT32(before, nt_ui_state_used_slots(s_fx.ctx));
}

/* ---- hold_progress rises linearly to 1.0, long_pressed fires once at the top ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_hold_progress_ramp_and_one_shot_long_press(void) {
    const float lp = 1.0F; /* long-press at 1.0s */

    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Press inside; the hold starts at 0 -> progress 0. */
    nt_pointer_t fp = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_events_t e0 = events_btn_frame(&fp, 0.0F, lp);
    TEST_ASSERT_TRUE(e0.held);
    TEST_ASSERT_TRUE(float_near(e0.hold_progress, 0.0F, 0.001F));
    TEST_ASSERT_FALSE(e0.long_pressed);

    /* Hold for 0.25s -> ~0.25 progress, no long-press yet. */
    nt_pointer_t fh = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_events_t e1 = events_btn_frame(&fh, 0.25F, lp);
    TEST_ASSERT_TRUE(float_near(e1.hold_progress, 0.25F, 0.01F));
    TEST_ASSERT_FALSE(e1.long_pressed);

    /* Hold to 0.50s -> ~0.50, monotone rising. */
    nt_ui_events_t e2 = events_btn_frame(&fh, 0.25F, lp);
    TEST_ASSERT_TRUE(e2.hold_progress > e1.hold_progress);
    TEST_ASSERT_TRUE(float_near(e2.hold_progress, 0.50F, 0.01F));
    TEST_ASSERT_FALSE(e2.long_pressed);

    /* dt==0 pauses the hold without cancelling it. */
    nt_ui_events_t paused = events_btn_frame(&fh, 0.0F, lp);
    TEST_ASSERT_TRUE(float_near(paused.hold_progress, e2.hold_progress, 0.001F));

    /* Cross 1.0s -> progress clamps to 1.0 AND long_pressed fires this frame. */
    nt_ui_events_t e3 = events_btn_frame(&fh, 0.60F, lp);
    TEST_ASSERT_TRUE(float_near(e3.hold_progress, 1.0F, 0.001F));
    TEST_ASSERT_TRUE(e3.long_pressed);

    /* Keep holding -> long_pressed does NOT re-fire (one-shot per hold); progress stays 1.0. */
    nt_ui_events_t e4 = events_btn_frame(&fh, 0.20F, lp);
    TEST_ASSERT_FALSE(e4.long_pressed);
    TEST_ASSERT_TRUE(float_near(e4.hold_progress, 1.0F, 0.001F));
}

/* ---- an in-box jitter past the old move_radius does NOT cancel the hold (it tracks the hitbox) ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_inbox_jitter_keeps_hold(void) {
    const float lp = 1.0F;

    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Press inside, hold to 0.5s. */
    nt_pointer_t fp = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&fp, 0.0F, lp);
    nt_pointer_t fh = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_events_t e_mid = events_btn_frame(&fh, 0.5F, lp);
    TEST_ASSERT_TRUE(e_mid.hold_progress > 0.0F);

    /* Jitter +40px (well past the old 16px radius) but STILL inside the bbox -> hold KEEPS rising. */
    nt_pointer_t fj = make_pointer(BTN_CX + 40.0F, BTN_CY, true, false, false);
    nt_ui_events_t e_jit = events_btn_frame(&fj, 0.2F, lp);
    TEST_ASSERT_TRUE(e_jit.held);
    TEST_ASSERT_TRUE(e_jit.hold_progress > e_mid.hold_progress);

    /* Cross long_press_secs while jittering in-box -> long_pressed DOES fire. */
    nt_ui_events_t e_lp = events_btn_frame(&fj, 0.5F, lp);
    TEST_ASSERT_TRUE(e_lp.long_pressed);
}

/* ---- leaving the hitbox (still pressed) cancels the hold: progress resets, long_pressed never fires ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_leave_hitbox_cancels_hold(void) {
    const float lp = 1.0F;

    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Press inside, hold to 0.5s. */
    nt_pointer_t fp = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&fp, 0.0F, lp);
    nt_pointer_t fh = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_events_t e_mid = events_btn_frame(&fh, 0.5F, lp);
    TEST_ASSERT_TRUE(e_mid.hold_progress > 0.0F);

    /* Drag OUTSIDE the bbox, still pressed -> held drops -> hold cancels, progress resets. */
    nt_pointer_t fo = make_pointer(BTN_X + BTN_W + 50.0F, BTN_CY, true, false, false);
    nt_ui_events_t e_out = events_btn_frame(&fo, 0.2F, lp);
    TEST_ASSERT_FALSE(e_out.held);
    TEST_ASSERT_TRUE(float_near(e_out.hold_progress, 0.0F, 0.001F));

    /* Keep holding past long_press_secs -> long_pressed must NEVER fire (candidate cancelled). */
    nt_ui_events_t e_after = events_btn_frame(&fo, 1.5F, lp);
    TEST_ASSERT_FALSE(e_after.long_pressed);
    TEST_ASSERT_TRUE(float_near(e_after.hold_progress, 0.0F, 0.001F));

    /* Back inside, still down: the hold stays cancelled until a new press. */
    nt_ui_events_t e_back = events_btn_frame(&fh, 1.0F, lp);
    TEST_ASSERT_TRUE(e_back.held);
    TEST_ASSERT_FALSE(e_back.long_pressed);
    TEST_ASSERT_TRUE(float_near(e_back.hold_progress, 0.0F, 0.001F));
}

/* ---- a repeat press re-arms the one-shot on the same widget ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_new_press_rearms_long_press(void) {
    nt_pointer_t idle = make_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_pointer_t press = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_pointer_t hold = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_pointer_t release = make_pointer(BTN_CX, BTN_CY, false, false, true);
    warm_btn_frame(&idle);

    (void)events_btn_frame(&press, 0.0F, 1.0F);
    TEST_ASSERT_TRUE(events_btn_frame(&hold, 1.0F, 1.0F).long_pressed);
    (void)events_btn_frame(&release, 0.0F, 1.0F);

    nt_ui_events_t fresh = events_btn_frame(&press, 0.0F, 1.0F);
    TEST_ASSERT_TRUE(fresh.pressed_now);
    TEST_ASSERT_FALSE(fresh.long_pressed);
    TEST_ASSERT_TRUE(float_near(fresh.hold_progress, 0.0F, 0.001F));
    TEST_ASSERT_TRUE(events_btn_frame(&hold, 1.0F, 1.0F).long_pressed);
}

/* ---- a frame without a step drops the capture, so a hold never resumes without a new press ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_unstepped_frame_ends_hold(void) {
    nt_pointer_t idle = make_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_pointer_t press = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_pointer_t hold = make_pointer(BTN_CX, BTN_CY, true, false, false);
    warm_btn_frame(&idle);
    (void)events_btn_frame(&press, 0.0F, 1.0F);
    TEST_ASSERT_TRUE(float_near(events_btn_frame(&hold, 0.5F, 1.0F).hold_progress, 0.5F, 0.001F));

    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &hold, 1, 0);
    declare_btn_element();
    nt_ui_end(s_fx.ctx);

    nt_ui_events_t resumed = events_btn_frame(&hold, 1.0F, 1.0F);
    TEST_ASSERT_FALSE(resumed.pressed);
    TEST_ASSERT_FALSE(resumed.long_pressed);
    TEST_ASSERT_TRUE(float_near(resumed.hold_progress, 0.0F, 0.001F));
}

/* ---- the window, the radius and the third press ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_double_click_window_radius_and_third_press(void) {
    nt_pointer_t idle = make_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_pointer_t press = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_pointer_t release = make_pointer(BTN_CX, BTN_CY, false, false, true);
    warm_btn_frame(&idle);
    nt_ui_set_gesture_constants(s_fx.ctx, 0.25F, 8.0F);

    (void)events_btn_frame(&press, 0.0F, 0.0F);
    (void)events_btn_frame(&release, 0.125F, 0.0F);
    nt_ui_events_t late = events_btn_frame(&press, 0.25F, 0.0F);
    TEST_ASSERT_TRUE(late.pressed_now);
    TEST_ASSERT_FALSE(late.double_clicked);

    (void)events_btn_frame(&release, 0.0F, 0.0F);
    press.x += 9.0F;
    nt_ui_events_t distant = events_btn_frame(&press, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(distant.pressed_now);
    TEST_ASSERT_FALSE(distant.double_clicked);

    release.x = press.x;
    (void)events_btn_frame(&release, 0.125F, 0.0F);
    press.x += 8.0F;
    TEST_ASSERT_TRUE(events_btn_frame(&press, 0.125F, 0.0F).double_clicked);

    release.x = press.x;
    (void)events_btn_frame(&release, 0.0F, 0.0F);
    nt_ui_events_t third = events_btn_frame(&press, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(third.pressed_now);
    TEST_ASSERT_FALSE(third.double_clicked);
}

/* ---- frames where the widget is not stepped still age the previous press ---- */
static void unstepped_btn_frame(const nt_pointer_t *p, float dt) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, dt, p, 1, 0);
    declare_btn_element();
    nt_ui_end(s_fx.ctx);
}

static void test_events_unstepped_frames_count_toward_window(void) {
    nt_pointer_t idle = make_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_pointer_t press = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_pointer_t release = make_pointer(BTN_CX, BTN_CY, false, false, true);
    warm_btn_frame(&idle);
    (void)events_btn_frame(&press, 0.0F, 0.0F);
    (void)events_btn_frame(&release, 0.05F, 0.0F);

    for (int i = 0; i < 600; ++i) {
        unstepped_btn_frame(&idle, 1.0F / 60.0F);
    }
    warm_btn_frame(&idle); /* Re-register before the next press. */

    nt_ui_events_t resumed = events_btn_frame(&press, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(resumed.pressed_now);
    TEST_ASSERT_FALSE(resumed.double_clicked);
}

/* ---- only UI time separates presses: a pause at dt==0 keeps the pair ---- */
static void test_events_zero_dt_gap_keeps_double_click(void) {
    nt_pointer_t idle = make_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_pointer_t press = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_pointer_t release = make_pointer(BTN_CX, BTN_CY, false, false, true);
    warm_btn_frame(&idle);
    (void)events_btn_frame(&press, 0.0F, 0.0F);
    (void)events_btn_frame(&release, 0.0F, 0.0F);

    unstepped_btn_frame(&idle, 0.0F);
    warm_btn_frame(&idle);

    TEST_ASSERT_TRUE(events_btn_frame(&press, 0.0F, 0.0F).double_clicked);
}

/* ---- a press on another widget replaces the candidate: A, B, A is not a double-click ---- */
static nt_ui_events_t two_btn_frame(const nt_pointer_t *p, bool want_other) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1, 0);
    declare_btn_element();
    CLAY({.id = CLAY_ID("other"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y + BTN_H}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_events_t other = nt_ui_events(s_fx.ctx, nt_ui_id("other"), 0.0F);
    nt_ui_events_t btn = nt_ui_events(s_fx.ctx, nt_ui_id("btn"), 0.0F);
    nt_ui_end(s_fx.ctx);
    return want_other ? other : btn;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_press_on_other_widget_breaks_pair(void) {
    const float other_cy = BTN_Y + BTN_H + (BTN_H * 0.5F);
    nt_pointer_t a_press = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_pointer_t a_release = make_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_pointer_t b_idle = make_pointer(BTN_CX, other_cy, false, false, false);
    nt_pointer_t b_press = make_pointer(BTN_CX, other_cy, true, true, false);
    nt_pointer_t b_release = make_pointer(BTN_CX, other_cy, false, false, true);
    nt_ui_set_gesture_constants(s_fx.ctx, 10.0F, 1000.0F);

    (void)two_btn_frame(&b_idle, true);
    TEST_ASSERT_TRUE(two_btn_frame(&a_press, false).pressed_now);
    (void)two_btn_frame(&a_release, false);
    TEST_ASSERT_TRUE(two_btn_frame(&b_press, true).pressed_now);
    (void)two_btn_frame(&b_release, true);
    nt_ui_events_t again = two_btn_frame(&a_press, false);
    TEST_ASSERT_TRUE(again.pressed_now);
    TEST_ASSERT_FALSE(again.double_clicked);
}

/* ---- query is pure: before the step it sees the same press, pair and hold the step returns ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_query_matches_step_on_gesture_frames(void) {
    nt_pointer_t idle = make_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_pointer_t press = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_pointer_t release = make_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_pointer_t hold = make_pointer(BTN_CX, BTN_CY, true, false, false);
    warm_btn_frame(&idle);
    (void)events_btn_frame(&press, 0.0F, 0.0F);
    (void)events_btn_frame(&release, 0.0F, 0.0F);

    const nt_pointer_t *frames[2] = {&press, &hold};
    for (int i = 0; i < 2; ++i) {
        nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.25F, frames[i], 1, 0);
        declare_btn_element();
        const nt_ui_interaction_t q = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
        const nt_ui_interaction_t q2 = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
        const nt_ui_interaction_t st = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn"));
        nt_ui_end(s_fx.ctx);
        TEST_ASSERT_EQUAL_MEMORY(&q, &q2, sizeof q);
        TEST_ASSERT_EQUAL_MEMORY(&q, &st, sizeof q);
        TEST_ASSERT_EQUAL(i == 0, st.double_clicked);
        TEST_ASSERT_TRUE(float_near(st.hold_secs, i == 0 ? 0.0F : 0.25F, 0.001F));
        TEST_ASSERT_TRUE(float_near(st.hold_prev_secs, 0.0F, 0.001F));
    }
}

/* ---- 2^19 s: a float clock this old would swallow a 1/60 step; timers restart at each press ---- */
#define LONG_SESSION_SECS 524288.0F

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_long_session_keeps_timers_precise(void) {
    nt_pointer_t idle = make_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_pointer_t press = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_pointer_t release = make_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_pointer_t hold = make_pointer(BTN_CX, BTN_CY, true, false, false);
    warm_btn_frame(&idle);
    (void)events_btn_frame(&idle, LONG_SESSION_SECS, 1.0F);
    (void)events_btn_frame(&press, 0.0F, 1.0F);
    (void)events_btn_frame(&release, 0.125F, 1.0F);
    TEST_ASSERT_TRUE(events_btn_frame(&press, 0.125F, 1.0F).double_clicked);

    nt_ui_events_t held = {0};
    for (int i = 0; i < 15; ++i) {
        held = events_btn_frame(&hold, 1.0F / 60.0F, 1.0F);
    }
    TEST_ASSERT_TRUE(float_near(held.hold_progress, 0.25F, 0.001F));
}

/* nt_ui_set_gesture_constants asserts finite & >= 0 (fail-early, no silent clamp). */
static void test_set_gesture_constants_rejects_bad_values(void) {
    NT_TEST_EXPECT_ASSERT(nt_ui_set_gesture_constants(s_fx.ctx, -1.0F, 6.0F));
    NT_TEST_EXPECT_ASSERT(nt_ui_set_gesture_constants(s_fx.ctx, 0.30F, -2.0F));
    NT_TEST_EXPECT_ASSERT(nt_ui_set_gesture_constants(s_fx.ctx, NAN, 6.0F));
    NT_TEST_EXPECT_ASSERT(nt_ui_set_gesture_constants(s_fx.ctx, 0.30F, INFINITY));
    /* A valid pair is accepted. */
    nt_ui_set_gesture_constants(s_fx.ctx, 0.25F, 8.0F);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_events_capture_parity_with_step_interaction);
    RUN_TEST(test_events_held_is_pressed_and_hovered);
    RUN_TEST(test_events_gestures_use_no_state_cell);
    RUN_TEST(test_events_hold_progress_ramp_and_one_shot_long_press);
    RUN_TEST(test_events_inbox_jitter_keeps_hold);
    RUN_TEST(test_events_leave_hitbox_cancels_hold);
    RUN_TEST(test_events_new_press_rearms_long_press);
    RUN_TEST(test_events_unstepped_frame_ends_hold);
    RUN_TEST(test_events_double_click_window_radius_and_third_press);
    RUN_TEST(test_events_unstepped_frames_count_toward_window);
    RUN_TEST(test_events_zero_dt_gap_keeps_double_click);
    RUN_TEST(test_events_press_on_other_widget_breaks_pair);
    RUN_TEST(test_query_matches_step_on_gesture_frames);
    RUN_TEST(test_events_long_session_keeps_timers_precise);
    RUN_TEST(test_set_gesture_constants_rejects_bad_values);
    return UNITY_END();
}
