/* Gestures of nt_ui_step_interaction: hold, long press and double-click.
 *
 * Frames drive UI time through the nt_ui_begin dt arg. Exact binary fractions keep the inclusive
 * window, radius and threshold checks deterministic. */

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
#define OTHER_CY (BTN_CY + BTN_H)

#define LONG_PRESS_SECS 1.0F

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool float_near(float a, float b) { return fabsf(a - b) <= 0.001F; }

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

static nt_pointer_t idle_at(float y) { return make_pointer(BTN_CX, y, false, false, false); }
static nt_pointer_t press_at(float y) { return make_pointer(BTN_CX, y, true, true, false); }
static nt_pointer_t hold_at(float y) { return make_pointer(BTN_CX, y, true, false, false); }
static nt_pointer_t release_at(float y) { return make_pointer(BTN_CX, y, false, false, true); }

static void declare_btn_element(void) {
    CLAY({.id = CLAY_ID("btn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
}

static nt_ui_interaction_t pointers_frame(const nt_pointer_t *pointers, uint32_t count, float dt) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, dt, pointers, count, 0);
    declare_btn_element();
    const nt_ui_interaction_t e = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    return e;
}

static nt_ui_interaction_t btn_frame(nt_pointer_t p, float dt) { return pointers_frame(&p, 1, dt); }

/* The widget stays declared but skips its step: no capture commit, no registry entry. */
static void unstepped_frame(nt_pointer_t p, float dt) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, dt, &p, 1, 0);
    declare_btn_element();
    nt_ui_end(s_fx.ctx);
}

/* ---- the hold ramps by dt, pauses at dt==0, crosses the threshold once ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_hold_ramp_and_one_shot_crossing(void) {
    (void)btn_frame(idle_at(BTN_CY), 0.0F);

    nt_ui_interaction_t e = btn_frame(press_at(BTN_CY), 0.25F);
    TEST_ASSERT_TRUE(float_near(nt_ui_hold_progress(&e, LONG_PRESS_SECS), 0.0F));
    TEST_ASSERT_FALSE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));

    e = btn_frame(hold_at(BTN_CY), 0.5F);
    TEST_ASSERT_TRUE(float_near(nt_ui_hold_progress(&e, LONG_PRESS_SECS), 0.5F));
    TEST_ASSERT_FALSE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));

    e = btn_frame(hold_at(BTN_CY), 0.0F);
    TEST_ASSERT_TRUE(float_near(nt_ui_hold_progress(&e, LONG_PRESS_SECS), 0.5F));

    /* Landing exactly on the threshold fires; the following frames, paused or not, do not. */
    e = btn_frame(hold_at(BTN_CY), 0.5F);
    TEST_ASSERT_TRUE(float_near(nt_ui_hold_progress(&e, LONG_PRESS_SECS), 1.0F));
    TEST_ASSERT_TRUE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));

    e = btn_frame(hold_at(BTN_CY), 0.0F);
    TEST_ASSERT_FALSE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));
    e = btn_frame(hold_at(BTN_CY), 0.5F);
    TEST_ASSERT_FALSE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));
    TEST_ASSERT_TRUE(float_near(nt_ui_hold_progress(&e, LONG_PRESS_SECS), 1.0F));
}

/* A widget with no hold reads an empty ramp and no crossing; a threshold must be positive. */
static void test_hold_progress_is_zero_without_a_hold(void) {
    const nt_ui_interaction_t none = {0};
    TEST_ASSERT_TRUE(float_near(nt_ui_hold_progress(&none, LONG_PRESS_SECS), 0.0F));
    TEST_ASSERT_FALSE(nt_ui_hold_crossed(&none, LONG_PRESS_SECS));
    NT_TEST_EXPECT_ASSERT((void)nt_ui_hold_crossed(&none, 0.0F));
    NT_TEST_EXPECT_ASSERT((void)nt_ui_hold_progress(&none, 0.0F));
}

/* ---- motion inside the hitbox, past the move radius, keeps the hold ---- */
static void test_motion_inside_hitbox_keeps_hold(void) {
    (void)btn_frame(idle_at(BTN_CY), 0.0F);
    (void)btn_frame(press_at(BTN_CY), 0.0F);
    nt_pointer_t moved = hold_at(BTN_CY);
    moved.x += 40.0F;
    const nt_ui_interaction_t e = btn_frame(moved, LONG_PRESS_SECS);
    TEST_ASSERT_TRUE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));
}

/* ---- leaving the hitbox cancels the hold until a new press, not until the pointer returns ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_leaving_hitbox_cancels_hold_until_new_press(void) {
    (void)btn_frame(idle_at(BTN_CY), 0.0F);
    (void)btn_frame(press_at(BTN_CY), 0.0F);
    (void)btn_frame(hold_at(BTN_CY), 0.5F);

    nt_pointer_t out = hold_at(BTN_CY);
    out.x = BTN_X + BTN_W + 50.0F;
    nt_ui_interaction_t e = btn_frame(out, 0.25F);
    TEST_ASSERT_TRUE(e.pressed);
    TEST_ASSERT_FALSE(e.hovered);
    TEST_ASSERT_TRUE(float_near(e.hold_secs, 0.0F));

    e = btn_frame(hold_at(BTN_CY), 0.5F);
    TEST_ASSERT_TRUE(e.pressed);
    TEST_ASSERT_TRUE(e.hovered);
    TEST_ASSERT_TRUE(float_near(e.hold_secs, 0.0F));
    TEST_ASSERT_FALSE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));

    (void)btn_frame(release_at(BTN_CY), 0.0F);
    (void)btn_frame(press_at(BTN_CY), 0.0F);
    e = btn_frame(hold_at(BTN_CY), LONG_PRESS_SECS);
    TEST_ASSERT_TRUE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));
}

/* ---- the hold belongs to the capturing pointer: a second pointer over the widget does not keep it ---- */
static void test_other_pointer_over_widget_does_not_keep_hold(void) {
    nt_pointer_t pointers[2] = {idle_at(BTN_CY), idle_at(BTN_CY)};
    (void)pointers_frame(pointers, 2, 0.0F);
    pointers[0] = press_at(BTN_CY);
    (void)pointers_frame(pointers, 2, 0.0F);

    pointers[0] = hold_at(BTN_CY);
    pointers[0].x = BTN_X + BTN_W + 50.0F;
    nt_ui_interaction_t e = pointers_frame(pointers, 2, 0.25F);
    TEST_ASSERT_TRUE(e.pressed);
    TEST_ASSERT_TRUE(e.hovered);
    TEST_ASSERT_TRUE(float_near(e.hold_secs, 0.0F));

    /* Back over the widget, still down: leaving cancelled the hold despite the other pointer. */
    pointers[0] = hold_at(BTN_CY);
    e = pointers_frame(pointers, 2, LONG_PRESS_SECS);
    TEST_ASSERT_TRUE(float_near(e.hold_secs, 0.0F));
    TEST_ASSERT_FALSE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));
}

/* ---- a pointer past slot 0 holds the same way ---- */
static void test_hold_on_second_pointer_slot(void) {
    nt_pointer_t pointers[2] = {idle_at(BTN_CY), idle_at(BTN_CY)};
    (void)pointers_frame(pointers, 2, 0.0F);
    pointers[1] = press_at(BTN_CY);
    (void)pointers_frame(pointers, 2, 0.0F);
    pointers[1] = hold_at(BTN_CY);
    TEST_ASSERT_TRUE(float_near(pointers_frame(pointers, 2, 0.5F).hold_secs, 0.5F));
}

/* ---- the release frame reports no hold, even if its dt would cross; a new press re-arms ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_release_ends_hold_and_new_press_rearms(void) {
    (void)btn_frame(idle_at(BTN_CY), 0.0F);
    (void)btn_frame(press_at(BTN_CY), 0.0F);
    nt_ui_interaction_t e = btn_frame(release_at(BTN_CY), LONG_PRESS_SECS);
    TEST_ASSERT_TRUE(float_near(e.hold_secs, 0.0F));
    TEST_ASSERT_FALSE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));

    (void)btn_frame(press_at(BTN_CY), 0.0F);
    e = btn_frame(hold_at(BTN_CY), LONG_PRESS_SECS);
    TEST_ASSERT_TRUE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));
    (void)btn_frame(release_at(BTN_CY), 0.0F);

    e = btn_frame(press_at(BTN_CY), 0.0F);
    TEST_ASSERT_TRUE(e.pressed_now);
    TEST_ASSERT_TRUE(float_near(e.hold_secs, 0.0F));
    e = btn_frame(hold_at(BTN_CY), LONG_PRESS_SECS);
    TEST_ASSERT_TRUE(nt_ui_hold_crossed(&e, LONG_PRESS_SECS));
}

/* ---- a frame without a step drops the capture, so a hold never resumes without a new press ---- */
static void test_unstepped_frame_ends_hold(void) {
    (void)btn_frame(idle_at(BTN_CY), 0.0F);
    (void)btn_frame(press_at(BTN_CY), 0.0F);
    TEST_ASSERT_TRUE(float_near(btn_frame(hold_at(BTN_CY), 0.5F).hold_secs, 0.5F));

    unstepped_frame(hold_at(BTN_CY), 0.0F);

    const nt_ui_interaction_t resumed = btn_frame(hold_at(BTN_CY), LONG_PRESS_SECS);
    TEST_ASSERT_FALSE(resumed.pressed);
    TEST_ASSERT_TRUE(float_near(resumed.hold_secs, 0.0F));
}

/* ---- the window, the radius on both axes, the third press and the pair after it ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_double_click_window_radius_and_third_press(void) {
    (void)btn_frame(idle_at(BTN_CY), 0.0F);
    nt_ui_set_gesture_constants(s_fx.ctx, 0.25F, 8.0F);

    (void)btn_frame(press_at(BTN_CY), 0.0F);
    (void)btn_frame(release_at(BTN_CY), 0.125F);
    nt_ui_interaction_t e = btn_frame(press_at(BTN_CY), 0.25F);
    TEST_ASSERT_TRUE(e.pressed_now);
    TEST_ASSERT_FALSE(e.double_clicked);
    (void)btn_frame(release_at(BTN_CY), 0.0F);

    e = btn_frame(press_at(BTN_CY + 9.0F), 0.0F);
    TEST_ASSERT_TRUE(e.pressed_now);
    TEST_ASSERT_FALSE(e.double_clicked);
    (void)btn_frame(release_at(BTN_CY + 9.0F), 0.0F);

    nt_pointer_t far_x = press_at(BTN_CY + 9.0F);
    far_x.x += 9.0F;
    e = btn_frame(far_x, 0.0F);
    TEST_ASSERT_TRUE(e.pressed_now);
    TEST_ASSERT_FALSE(e.double_clicked);
    far_x.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_released = true};
    (void)btn_frame(far_x, 0.125F);

    nt_pointer_t on_edge = press_at(BTN_CY + 9.0F);
    on_edge.x = far_x.x + 8.0F;
    TEST_ASSERT_TRUE(btn_frame(on_edge, 0.125F).double_clicked);
    on_edge.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_released = true};
    (void)btn_frame(on_edge, 0.0F);

    on_edge.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    e = btn_frame(on_edge, 0.0F);
    TEST_ASSERT_TRUE(e.pressed_now);
    TEST_ASSERT_FALSE(e.double_clicked);
    on_edge.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_released = true};
    (void)btn_frame(on_edge, 0.0F);

    on_edge.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    TEST_ASSERT_TRUE(btn_frame(on_edge, 0.0F).double_clicked);
}

/* ---- frames where the widget is not stepped still age the previous press ---- */
static void test_unstepped_frames_count_toward_window(void) {
    (void)btn_frame(idle_at(BTN_CY), 0.0F);
    (void)btn_frame(press_at(BTN_CY), 0.0F);
    (void)btn_frame(release_at(BTN_CY), 0.05F);

    for (int i = 0; i < 600; ++i) {
        unstepped_frame(idle_at(BTN_CY), 1.0F / 60.0F);
    }
    (void)btn_frame(idle_at(BTN_CY), 0.0F); /* Re-register before the next press. */

    const nt_ui_interaction_t resumed = btn_frame(press_at(BTN_CY), 0.0F);
    TEST_ASSERT_TRUE(resumed.pressed_now);
    TEST_ASSERT_FALSE(resumed.double_clicked);
}

/* ---- only UI time separates presses: a short or dt==0 gap inside the window keeps the pair ---- */
static void test_gap_inside_window_keeps_pair(void) {
    (void)btn_frame(idle_at(BTN_CY), 0.0F);
    nt_ui_set_gesture_constants(s_fx.ctx, 0.25F, 8.0F);

    (void)btn_frame(press_at(BTN_CY), 0.0F);
    (void)btn_frame(release_at(BTN_CY), 0.0F);
    unstepped_frame(idle_at(BTN_CY), 0.125F);
    unstepped_frame(idle_at(BTN_CY), 0.0F);
    (void)btn_frame(idle_at(BTN_CY), 0.0F);

    TEST_ASSERT_TRUE(btn_frame(press_at(BTN_CY), 0.0F).double_clicked);
}

/* ---- A, B, A is not a pair, but the press on B replaced the record: B, B is ---- */
static nt_ui_interaction_t two_btn_frame(nt_pointer_t p, bool want_other) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &p, 1, 0);
    declare_btn_element();
    CLAY({.id = CLAY_ID("other"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y + BTN_H}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    const nt_ui_interaction_t other = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("other"));
    const nt_ui_interaction_t btn = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    return want_other ? other : btn;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_press_on_other_widget_replaces_record(void) {
    nt_ui_set_gesture_constants(s_fx.ctx, 10.0F, 1000.0F);
    (void)two_btn_frame(idle_at(OTHER_CY), true);

    TEST_ASSERT_TRUE(two_btn_frame(press_at(BTN_CY), false).pressed_now);
    (void)two_btn_frame(release_at(BTN_CY), false);
    TEST_ASSERT_TRUE(two_btn_frame(press_at(OTHER_CY), true).pressed_now);
    (void)two_btn_frame(release_at(OTHER_CY), true);

    nt_ui_interaction_t e = two_btn_frame(press_at(BTN_CY), false);
    TEST_ASSERT_TRUE(e.pressed_now);
    TEST_ASSERT_FALSE(e.double_clicked);
    (void)two_btn_frame(release_at(BTN_CY), false);

    TEST_ASSERT_TRUE(two_btn_frame(press_at(OTHER_CY), true).pressed_now);
    (void)two_btn_frame(release_at(OTHER_CY), true);
    TEST_ASSERT_TRUE(two_btn_frame(press_at(OTHER_CY), true).double_clicked);
}

/* ---- a quick re-tap can land in another pointer slot and must still pair ---- */
static void test_retap_in_other_pointer_slot_pairs(void) {
    nt_pointer_t pointers[2] = {idle_at(BTN_CY), idle_at(BTN_CY)};
    (void)pointers_frame(pointers, 2, 0.0F);
    pointers[0] = press_at(BTN_CY);
    (void)pointers_frame(pointers, 2, 0.0F);
    pointers[0] = release_at(BTN_CY);
    (void)pointers_frame(pointers, 2, 0.0F);

    pointers[0] = idle_at(BTN_CY);
    pointers[1] = press_at(BTN_CY);
    TEST_ASSERT_TRUE(pointers_frame(pointers, 2, 0.0F).double_clicked);
}

/* ---- double_clicked comes from the step only; a query never reports it, before or after ---- */
static nt_ui_interaction_t query_around_step(nt_pointer_t p, nt_ui_interaction_t *after) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &p, 1, 0);
    declare_btn_element();
    const nt_ui_interaction_t before = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
    const nt_ui_interaction_t st = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn"));
    TEST_ASSERT_FALSE(before.double_clicked);
    *after = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    return st;
}

static void test_query_never_reports_double_click(void) {
    nt_ui_interaction_t after;
    (void)btn_frame(idle_at(BTN_CY), 0.0F);

    /* Press and release inside one frame: the step's own record must not read back as a pair. */
    nt_pointer_t tap = press_at(BTN_CY);
    tap.buttons[NT_BUTTON_LEFT].is_down = false;
    tap.buttons[NT_BUTTON_LEFT].is_released = true;
    TEST_ASSERT_FALSE(query_around_step(tap, &after).double_clicked);
    TEST_ASSERT_FALSE(after.double_clicked);

    TEST_ASSERT_TRUE(query_around_step(press_at(BTN_CY), &after).double_clicked);
    TEST_ASSERT_FALSE(after.double_clicked);
}

/* ---- a query reads the same hold before and after the step ---- */
static void test_query_reads_hold(void) {
    (void)btn_frame(idle_at(BTN_CY), 0.0F);
    (void)btn_frame(press_at(BTN_CY), 0.0F);

    nt_pointer_t hold = hold_at(BTN_CY);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.25F, &hold, 1, 0);
    declare_btn_element();
    const nt_ui_interaction_t before = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
    (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn"));
    const nt_ui_interaction_t after = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(float_near(before.hold_secs, 0.25F));
    TEST_ASSERT_TRUE(float_near(after.hold_secs, 0.25F));
}

/* ---- edges lost while the game skipped nt_ui_begin: a new press edge starts a new hold ---- */
static void test_press_edge_on_held_capture_restarts_hold(void) {
    (void)btn_frame(idle_at(BTN_CY), 0.0F);
    (void)btn_frame(press_at(BTN_CY), 0.0F);
    TEST_ASSERT_TRUE(float_near(btn_frame(hold_at(BTN_CY), 0.75F).hold_secs, 0.75F));

    const nt_ui_interaction_t e = btn_frame(press_at(BTN_CY), 0.5F);
    TEST_ASSERT_TRUE(e.pressed_now);
    TEST_ASSERT_TRUE(float_near(e.hold_secs, 0.0F));
}

/* ---- a release lost the same way must not keep the pointer bound to the old widget ---- */
static void test_lost_release_frees_capture_for_next_press(void) {
    nt_ui_set_gesture_constants(s_fx.ctx, 0.0F, 0.0F);
    (void)two_btn_frame(idle_at(BTN_CY), false);
    TEST_ASSERT_TRUE(two_btn_frame(press_at(BTN_CY), false).pressed_now);

    (void)two_btn_frame(idle_at(OTHER_CY), true);
    TEST_ASSERT_TRUE(two_btn_frame(idle_at(OTHER_CY), true).hovered);
    TEST_ASSERT_TRUE(two_btn_frame(press_at(OTHER_CY), true).pressed_now);
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
    RUN_TEST(test_hold_ramp_and_one_shot_crossing);
    RUN_TEST(test_hold_progress_is_zero_without_a_hold);
    RUN_TEST(test_motion_inside_hitbox_keeps_hold);
    RUN_TEST(test_leaving_hitbox_cancels_hold_until_new_press);
    RUN_TEST(test_other_pointer_over_widget_does_not_keep_hold);
    RUN_TEST(test_hold_on_second_pointer_slot);
    RUN_TEST(test_release_ends_hold_and_new_press_rearms);
    RUN_TEST(test_unstepped_frame_ends_hold);
    RUN_TEST(test_double_click_window_radius_and_third_press);
    RUN_TEST(test_unstepped_frames_count_toward_window);
    RUN_TEST(test_gap_inside_window_keeps_pair);
    RUN_TEST(test_press_on_other_widget_replaces_record);
    RUN_TEST(test_retap_in_other_pointer_slot_pairs);
    RUN_TEST(test_query_never_reports_double_click);
    RUN_TEST(test_query_reads_hold);
    RUN_TEST(test_press_edge_on_held_capture_restarts_hold);
    RUN_TEST(test_lost_release_frees_capture_for_next_press);
    RUN_TEST(test_set_gesture_constants_rejects_bad_values);
    return UNITY_END();
}
