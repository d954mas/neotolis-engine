/* nt_ui_events — the single canonical mutating interaction step.
 *
 * Mirrors test_nt_ui_interaction's dt-driven frame idiom: drive ctx->frame_dt via the
 * nt_ui_begin dt arg, step across frames, assert latched edges/timers.
 *
 * Coverage:
 *   capture parity: nt_ui_events(cfg=NULL) == nt_ui_step_interaction for the same sequence.
 *   zero-alloc gate: cfg==NULL grows nt_ui_state_used_slots by zero.
 *   hold_progress ramp 0..1, one-shot long_pressed, leave-hitbox cancel, query idempotence. */

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

/* A frame that declares the bbox and steps via nt_ui_events (cfg). dt drives the gesture clock. */
static nt_ui_events_t events_btn_frame(const nt_pointer_t *p, float dt, const nt_ui_events_cfg_t *cfg) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, dt, p, 1);
    declare_btn_element();
    nt_ui_events_t e = nt_ui_events(s_fx.ctx, nt_ui_id("btn"), cfg);
    nt_ui_end(s_fx.ctx);
    return e;
}

/* Warm-up: register btn so next-frame arbitration sees it (front-most resolve reads prev frame). */
static void warm_btn_frame(const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    declare_btn_element();
    (void)nt_ui_events(s_fx.ctx, nt_ui_id("btn"), NULL);
    nt_ui_end(s_fx.ctx);
}

/* ---- capture parity — nt_ui_events(cfg=NULL) matches step_interaction edges ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_capture_parity_with_step_interaction(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Press inside. */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_events_t e2 = events_btn_frame(&f2, 0.0F, NULL);
    TEST_ASSERT_TRUE(e2.hovered);
    TEST_ASSERT_TRUE(e2.pressed);
    TEST_ASSERT_TRUE(e2.held); /* pressed && hovered */
    TEST_ASSERT_FALSE(e2.clicked);
    TEST_ASSERT_FALSE(e2.released);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Hold. */
    nt_pointer_t f3 = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_events_t e3 = events_btn_frame(&f3, 0.0F, NULL);
    TEST_ASSERT_TRUE(e3.pressed);
    TEST_ASSERT_TRUE(e3.held);
    TEST_ASSERT_FALSE(e3.clicked);

    /* Release inside -> clicked one-shot, capture cleared. */
    nt_pointer_t f4 = make_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_events_t e4 = events_btn_frame(&f4, 0.0F, NULL);
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
    (void)events_btn_frame(&f2, 0.0F, NULL);

    /* Drag off-widget, still down: capture holds (pressed) but not over widget (not held). */
    const float far_x = BTN_X + BTN_W + 200.0F;
    const float far_y = BTN_Y + BTN_H + 200.0F;
    nt_pointer_t f3 = make_pointer(far_x, far_y, true, false, false);
    nt_ui_events_t e3 = events_btn_frame(&f3, 0.0F, NULL);
    TEST_ASSERT_TRUE(e3.pressed);
    TEST_ASSERT_FALSE(e3.hovered);
    TEST_ASSERT_FALSE(e3.held);
    TEST_ASSERT_TRUE(float_near(e3.drag_dx, far_x - BTN_CX, 0.01F));
}

/* ---- zero-alloc gate — cfg==NULL must NOT touch the state pool ---- */
static void test_events_cfg_null_zero_alloc(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    const uint32_t before = nt_ui_state_used_slots(s_fx.ctx);

    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&f2, 0.0F, NULL);
    nt_pointer_t f3 = make_pointer(BTN_CX, BTN_CY, true, false, false);
    (void)events_btn_frame(&f3, 0.0F, NULL);

    const uint32_t after = nt_ui_state_used_slots(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(before, after); /* base capture lives in captures[], not the pool */
}

/* A cfg with neither gesture requested (long_press_secs<=0, double_click=false) also stays zero-alloc. */
static void test_events_cfg_no_gesture_zero_alloc(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    const uint32_t before = nt_ui_state_used_slots(s_fx.ctx);
    const nt_ui_events_cfg_t cfg = {.long_press_secs = 0.0F, .double_click = false};

    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&f2, 0.0F, &cfg);

    TEST_ASSERT_EQUAL_UINT32(before, nt_ui_state_used_slots(s_fx.ctx));
}

/* A gesture cfg DOES allocate exactly one gesture cell. */
static void test_events_gesture_cfg_allocs_one_cell(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    const uint32_t before = nt_ui_state_used_slots(s_fx.ctx);
    const nt_ui_events_cfg_t cfg = {.long_press_secs = 0.5F, .double_click = false};

    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&f2, 0.0F, &cfg);

    TEST_ASSERT_EQUAL_UINT32(before + 1U, nt_ui_state_used_slots(s_fx.ctx));
}

/* ---- hold_progress rises linearly to 1.0, long_pressed fires once at the top ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_hold_progress_ramp_and_one_shot_long_press(void) {
    const float lp = 1.0F; /* long-press at 1.0s */
    const nt_ui_events_cfg_t cfg = {.long_press_secs = lp, .double_click = false};

    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Press inside; clock starts at 0 -> progress 0. */
    nt_pointer_t fp = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_events_t e0 = events_btn_frame(&fp, 0.0F, &cfg);
    TEST_ASSERT_TRUE(e0.held);
    TEST_ASSERT_TRUE(float_near(e0.hold_progress, 0.0F, 0.001F));
    TEST_ASSERT_FALSE(e0.long_pressed);

    /* Hold for 0.25s -> ~0.25 progress, no long-press yet. */
    nt_pointer_t fh = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_events_t e1 = events_btn_frame(&fh, 0.25F, &cfg);
    TEST_ASSERT_TRUE(float_near(e1.hold_progress, 0.25F, 0.01F));
    TEST_ASSERT_FALSE(e1.long_pressed);

    /* Hold to 0.50s -> ~0.50, monotone rising. */
    nt_ui_events_t e2 = events_btn_frame(&fh, 0.25F, &cfg);
    TEST_ASSERT_TRUE(e2.hold_progress > e1.hold_progress);
    TEST_ASSERT_TRUE(float_near(e2.hold_progress, 0.50F, 0.01F));
    TEST_ASSERT_FALSE(e2.long_pressed);

    /* Cross 1.0s -> progress clamps to 1.0 AND long_pressed fires this frame. */
    nt_ui_events_t e3 = events_btn_frame(&fh, 0.60F, &cfg);
    TEST_ASSERT_TRUE(float_near(e3.hold_progress, 1.0F, 0.001F));
    TEST_ASSERT_TRUE(e3.long_pressed);

    /* Keep holding -> long_pressed does NOT re-fire (one-shot per hold); progress stays 1.0. */
    nt_ui_events_t e4 = events_btn_frame(&fh, 0.20F, &cfg);
    TEST_ASSERT_FALSE(e4.long_pressed);
    TEST_ASSERT_TRUE(float_near(e4.hold_progress, 1.0F, 0.001F));
}

/* ---- an in-box jitter past the old move_radius does NOT cancel the hold (it tracks the hitbox) ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_inbox_jitter_keeps_hold(void) {
    const float lp = 1.0F;
    const nt_ui_events_cfg_t cfg = {.long_press_secs = lp, .double_click = false};

    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Press inside, hold to 0.5s. */
    nt_pointer_t fp = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&fp, 0.0F, &cfg);
    nt_pointer_t fh = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_events_t e_mid = events_btn_frame(&fh, 0.5F, &cfg);
    TEST_ASSERT_TRUE(e_mid.hold_progress > 0.0F);

    /* Jitter +40px (well past the old 16px radius) but STILL inside the bbox -> hold KEEPS rising. */
    nt_pointer_t fj = make_pointer(BTN_CX + 40.0F, BTN_CY, true, false, false);
    nt_ui_events_t e_jit = events_btn_frame(&fj, 0.2F, &cfg);
    TEST_ASSERT_TRUE(e_jit.held);
    TEST_ASSERT_TRUE(e_jit.hold_progress > e_mid.hold_progress);

    /* Cross long_press_secs while jittering in-box -> long_pressed DOES fire. */
    nt_ui_events_t e_lp = events_btn_frame(&fj, 0.5F, &cfg);
    TEST_ASSERT_TRUE(e_lp.long_pressed);
}

/* ---- leaving the hitbox (still pressed) cancels the hold: progress resets, long_pressed never fires ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_events_leave_hitbox_cancels_hold(void) {
    const float lp = 1.0F;
    const nt_ui_events_cfg_t cfg = {.long_press_secs = lp, .double_click = false};

    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Press inside, hold to 0.5s. */
    nt_pointer_t fp = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&fp, 0.0F, &cfg);
    nt_pointer_t fh = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_events_t e_mid = events_btn_frame(&fh, 0.5F, &cfg);
    TEST_ASSERT_TRUE(e_mid.hold_progress > 0.0F);

    /* Drag OUTSIDE the bbox, still pressed -> held drops -> hold cancels, progress resets. */
    nt_pointer_t fo = make_pointer(BTN_X + BTN_W + 50.0F, BTN_CY, true, false, false);
    nt_ui_events_t e_out = events_btn_frame(&fo, 0.2F, &cfg);
    TEST_ASSERT_FALSE(e_out.held);
    TEST_ASSERT_TRUE(float_near(e_out.hold_progress, 0.0F, 0.001F));

    /* Keep holding past long_press_secs -> long_pressed must NEVER fire (candidate cancelled). */
    nt_ui_events_t e_after = events_btn_frame(&fo, 1.5F, &cfg);
    TEST_ASSERT_FALSE(e_after.long_pressed);
    TEST_ASSERT_TRUE(float_near(e_after.hold_progress, 0.0F, 0.001F));
}

/* ---- query_events is idempotent — N calls identical, no capture/timer/pool advance ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_query_events_idempotent(void) {
    const nt_ui_events_cfg_t cfg = {.long_press_secs = 1.0F, .double_click = true};

    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Press inside via events (creates the gesture cell + capture). */
    nt_pointer_t fp = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&fp, 0.0F, &cfg);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Release frame: query_events 3x must return identical results AND advance nothing. */
    nt_pointer_t fr = make_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.5F, &fr, 1);
    declare_btn_element();

    const uint32_t slots_before = nt_ui_state_used_slots(s_fx.ctx);
    nt_ui_events_t calls[3];
    for (int i = 0; i < 3; ++i) {
        calls[i] = nt_ui_query_events(s_fx.ctx, nt_ui_id("btn"));
    }
    for (int i = 1; i < 3; ++i) {
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&calls[0], &calls[i], sizeof(nt_ui_events_t), "query_events is not idempotent");
    }
    /* Capture still owned (only nt_ui_events would clear it on release); pool unchanged. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(slots_before, nt_ui_state_used_slots(s_fx.ctx));
    TEST_ASSERT_TRUE(calls[0].clicked);
    TEST_ASSERT_TRUE(calls[0].released);

    nt_ui_end(s_fx.ctx);
}

/* ---- query_events surfaces the gestures the mutating step computed this frame ----
 * The button->query_events contract: the game runs the mutating nt_ui_events (via the button) and
 * reads gestures back via nt_ui_query_events. Prove double_clicked, long_pressed, and a ramping
 * hold_progress all reach query_events (they were hard-zeroed before the cell-latch fix). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_query_events_surfaces_gestures_after_mutating_step(void) {
    const float lp = 1.0F;
    const nt_ui_events_cfg_t cfg = {.long_press_secs = lp, .double_click = true};

    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Press inside (progress is 0 on the press frame: press_clock == clock). */
    nt_pointer_t fp = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&fp, 0.0F, &cfg);

    /* Hold frame: mutating step then query in the same frame -> ramping hold_progress surfaced. */
    nt_pointer_t fh = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.25F, &fh, 1);
    declare_btn_element();
    (void)nt_ui_events(s_fx.ctx, nt_ui_id("btn"), &cfg);
    nt_ui_events_t q0 = nt_ui_query_events(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(q0.held);
    TEST_ASSERT_TRUE(q0.hold_progress > 0.0F); /* ramping, not the old hard-zero */
    TEST_ASSERT_TRUE(float_near(q0.hold_progress, 0.25F, 0.01F));
    TEST_ASSERT_FALSE(q0.long_pressed);

    /* Hold past long_press_secs: query must surface the one-shot long_pressed + full hold_progress. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F, &fh, 1);
    declare_btn_element();
    (void)nt_ui_events(s_fx.ctx, nt_ui_id("btn"), &cfg);
    nt_ui_events_t q1 = nt_ui_query_events(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(q1.long_pressed);
    TEST_ASSERT_TRUE(float_near(q1.hold_progress, 1.0F, 0.001F));

    /* Release, then warm a no-press frame to reset the gesture clock window cleanly. */
    nt_pointer_t fr = make_pointer(BTN_CX, BTN_CY, false, false, true);
    (void)events_btn_frame(&fr, 0.0F, &cfg);

    /* Double-click: two quick presses within the window/radius -> query surfaces double_clicked. */
    nt_pointer_t fp1 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&fp1, 0.0F, &cfg);
    nt_pointer_t fr1 = make_pointer(BTN_CX, BTN_CY, false, false, true);
    (void)events_btn_frame(&fr1, 0.05F, &cfg);

    nt_pointer_t fp2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.05F, &fp2, 1);
    declare_btn_element();
    nt_ui_events_t mut = nt_ui_events(s_fx.ctx, nt_ui_id("btn"), &cfg);
    nt_ui_events_t q2 = nt_ui_query_events(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(mut.double_clicked); /* mutating result unchanged */
    TEST_ASSERT_TRUE(q2.double_clicked);  /* and now surfaced via query_events */
}

/* ---- Frame-guard: a gesture latch written one frame must NOT leak into a later frame's query when the
 *      widget stops requesting gestures (cfg==NULL). The one-shots read false and hold_progress reads 0. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_query_events_no_stale_latch_across_frames(void) {
    const nt_ui_events_cfg_t cfg = {.long_press_secs = 1.0F, .double_click = false};

    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Press + hold past long_press_secs WITH a gesture cfg -> latch fires (long_pressed + full ramp). */
    nt_pointer_t fp = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)events_btn_frame(&fp, 0.0F, &cfg);
    nt_pointer_t fh = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.5F, &fh, 1);
    declare_btn_element();
    (void)nt_ui_events(s_fx.ctx, nt_ui_id("btn"), &cfg);
    nt_ui_events_t q_live = nt_ui_query_events(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(q_live.long_pressed);         /* fired this frame */
    TEST_ASSERT_TRUE(q_live.hold_progress > 0.0F); /* ramp surfaced this frame */

    /* Next frame: NO mutating gesture step (cfg==NULL), still holding. query must NOT replay the latch. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.5F, &fh, 1);
    declare_btn_element();
    (void)nt_ui_events(s_fx.ctx, nt_ui_id("btn"), NULL); /* base step only, no gesture write */
    nt_ui_events_t q_stale = nt_ui_query_events(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE_MESSAGE(q_stale.long_pressed, "stale long_pressed must not leak into a later frame");
    TEST_ASSERT_TRUE_MESSAGE(float_near(q_stale.hold_progress, 0.0F, 0.001F), "stale hold_progress must read 0 on a later frame");
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
    RUN_TEST(test_events_cfg_null_zero_alloc);
    RUN_TEST(test_events_cfg_no_gesture_zero_alloc);
    RUN_TEST(test_events_gesture_cfg_allocs_one_cell);
    RUN_TEST(test_events_hold_progress_ramp_and_one_shot_long_press);
    RUN_TEST(test_events_inbox_jitter_keeps_hold);
    RUN_TEST(test_events_leave_hitbox_cancels_hold);
    RUN_TEST(test_query_events_idempotent);
    RUN_TEST(test_query_events_surfaces_gestures_after_mutating_step);
    RUN_TEST(test_query_events_no_stale_latch_across_frames);
    RUN_TEST(test_set_gesture_constants_rejects_bad_values);
    return UNITY_END();
}
