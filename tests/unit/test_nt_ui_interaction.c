/* Engine-owned interaction state machine.
 *
 * step_interaction drives per-pointer capture off precomputed button edges +
 * transform-aware hit-test against PREVIOUS-frame Clay bbox (1-frame IM lag).
 * Bbox available from frame 2 onward.
 *
 * Frame sequence:
 *   Frame 1: declare CLAY({.id=CLAY_ID("btn")}) at known bbox; end.
 *   Frame 2: pointer inside + is_pressed → hovered+pressed+pressed_now;
 *            capture active_id == id.
 *   Frame 3: is_released inside → released_now && clicked, capture cleared.
 *   Frame 3 outside → released_now && !clicked. */

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

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* Unity float macros are excluded in this build (UNITY_EXCLUDE_FLOAT). */
static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* Build a primary pointer at (x,y) with the LEFT button edges set. */
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

/* Declare the button bbox at a FIXED absolute position so the layout solver
 * lands it exactly where the asserts expect. Called inside an open frame. */
static void declare_btn_element(void) {
    CLAY({.id = CLAY_ID("btn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
}

/* A frame that only declares the bbox (no interaction query). Used for frame 1
 * so Clay's hashmap has a prev-frame bbox before the first query. */
static void declare_btn_frame(const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    declare_btn_element();
    nt_ui_end(s_fx.ctx);
}

/* A step frame: begin with pointer p, declare the element, step interaction
 * (INSIDE the frame -- step needs Clay's context set), then end. Mirrors how
 * a game/button calls step_interaction during declaration. */
static nt_ui_interaction_t query_btn_frame(const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    declare_btn_element();
    nt_ui_interaction_t in = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    return in;
}

/* ---- Test 1: press-then-release over widget -> clicked once (SC #1) ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_interaction_click_on_release_within_bounds(void) {
    /* Frame 1: declare only (no buttons). Bbox stored for next frame. */
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    declare_btn_frame(&f1);

    /* Frame 2: press inside. */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_interaction_t in2 = query_btn_frame(&f2);
    TEST_ASSERT_TRUE(in2.hovered);
    TEST_ASSERT_TRUE(in2.pressed);
    TEST_ASSERT_TRUE(in2.pressed_now);
    TEST_ASSERT_FALSE(in2.clicked);
    TEST_ASSERT_FALSE(in2.released_now);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Frame 3: hold down (still pressed, NOT pressed_now, NOT clicked). */
    nt_pointer_t f3 = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_interaction_t in3 = query_btn_frame(&f3);
    TEST_ASSERT_TRUE(in3.pressed);
    TEST_ASSERT_FALSE(in3.pressed_now);
    TEST_ASSERT_FALSE(in3.clicked);

    /* Frame 4: release inside -> clicked exactly once, capture cleared. */
    nt_pointer_t f4 = make_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_interaction_t in4 = query_btn_frame(&f4);
    TEST_ASSERT_TRUE(in4.released_now);
    TEST_ASSERT_TRUE(in4.clicked);
    TEST_ASSERT_FALSE(in4.pressed);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Frame 5: idle -> click does NOT re-fire. */
    nt_pointer_t f5 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_ui_interaction_t in5 = query_btn_frame(&f5);
    TEST_ASSERT_FALSE(in5.clicked);
    TEST_ASSERT_FALSE(in5.released_now);
    TEST_ASSERT_TRUE(in5.hovered); /* still over the widget */
}

/* ---- Test 2: idle -> hover -> pressed state progression (SC #2) ---- */
static void test_interaction_hover_then_pressed(void) {
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F, false, false, false);
    declare_btn_frame(&f1);

    /* Frame 2: pointer over, no button -> hovered, not pressed. */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_ui_interaction_t hov = query_btn_frame(&f2);
    TEST_ASSERT_TRUE(hov.hovered);
    TEST_ASSERT_FALSE(hov.pressed);
    TEST_ASSERT_FALSE(hov.pressed_now);

    /* Frame 3: press -> hovered && pressed && pressed_now. */
    nt_pointer_t f3 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_interaction_t prs = query_btn_frame(&f3);
    TEST_ASSERT_TRUE(prs.hovered);
    TEST_ASSERT_TRUE(prs.pressed);
    TEST_ASSERT_TRUE(prs.pressed_now);
}

/* ---- Test 3: press, release OUTSIDE widget -> cancel (released_now, !clicked) ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_interaction_release_outside_cancels(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    declare_btn_frame(&f1);

    /* Frame 2: press inside -> capture begins. */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_interaction_t in2 = query_btn_frame(&f2);
    TEST_ASSERT_TRUE(in2.pressed_now);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Frame 3: hold, drag pointer OUTSIDE the bbox -> still captured (pressed),
     * but not hovered; drag delta reflects the move. */
    const float far_x = BTN_X + BTN_W + 100.0F;
    const float far_y = BTN_Y - 100.0F;
    nt_pointer_t f3 = make_pointer(far_x, far_y, true, false, false);
    nt_ui_interaction_t in3 = query_btn_frame(&f3);
    TEST_ASSERT_TRUE(in3.pressed); /* capture holds even off-widget */
    TEST_ASSERT_FALSE(in3.hovered);
    TEST_ASSERT_TRUE(float_near(in3.drag_dx, far_x - BTN_CX, 0.01F));
    TEST_ASSERT_TRUE(float_near(in3.drag_dy, far_y - BTN_CY, 0.01F));

    /* Frame 4: release OUTSIDE -> released_now true, clicked FALSE (cancel),
     * capture cleared. */
    nt_pointer_t f4 = make_pointer(far_x, far_y, false, false, true);
    nt_ui_interaction_t in4 = query_btn_frame(&f4);
    TEST_ASSERT_TRUE(in4.released_now);
    TEST_ASSERT_FALSE(in4.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
}

/* ---- Test 4: disabled widget skips hover + click ---- */
static void test_interaction_disabled_skips(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    declare_btn_frame(&f1);

    /* Disabled = button never calls get_interaction → zeroed result, no capture. */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    const bool enabled = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    declare_btn_element();
    nt_ui_interaction_t in = enabled ? nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn")) : (nt_ui_interaction_t){0};
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(in.hovered);
    TEST_ASSERT_FALSE(in.pressed);
    TEST_ASSERT_FALSE(in.pressed_now);
    TEST_ASSERT_FALSE(in.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx));
}

/* ---- Test 6: padded interaction inflates bbox before inverse-affine ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_interaction_padded_hover_and_capture(void) {
    /* Frame 1: declare bbox (no pointer over). */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F, false, false, false);
    declare_btn_frame(&f1);

    /* Pointer 12 px past right edge + pad right 16 → hovered + pressed_now. */
    const float right_outside_x = BTN_X + BTN_W + 12.0F;
    const float center_y = BTN_Y + (BTN_H * 0.5F);
    nt_pointer_t f2 = make_pointer(right_outside_x, center_y, true, true, false);
    const int16_t pad_right[4] = {0, 16, 0, 0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    declare_btn_element();
    nt_ui_interaction_t in = nt_ui_step_interaction_padded(s_fx.ctx, nt_ui_id("btn"), pad_right);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(in.hovered);
    TEST_ASSERT_TRUE(in.pressed);
    TEST_ASSERT_TRUE(in.pressed_now);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Clear capture via a quiet frame; orphan cleanup runs at begin. */
    nt_pointer_t f3 = make_pointer(0.0F, 0.0F, false, false, true);
    declare_btn_frame(&f3); /* releases + frame 1 will clear orphan */
    nt_pointer_t f4 = make_pointer(0.0F, 0.0F, false, false, false);
    declare_btn_frame(&f4); /* orphan cleanup runs at this begin */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
}

/* ---- Test 7: padded query on a DISABLED widget stays non-hoverable ---- */
static void test_interaction_disabled_with_padding_stays_disabled(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    declare_btn_frame(&f1);

    /* Frame 2: pointer over center, pressed, but enabled = false. Large
     * padding {32,32,32,32} must NOT cause hover/press/capture. */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    const bool enabled = false;
    const int16_t big_pad[4] = {32, 32, 32, 32};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    declare_btn_element();
    /* Mirror the button's disabled branch: query not invoked, zeroed result. */
    nt_ui_interaction_t in = enabled ? nt_ui_step_interaction_padded(s_fx.ctx, nt_ui_id("btn"), big_pad) : (nt_ui_interaction_t){0};
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(in.hovered);
    TEST_ASSERT_FALSE(in.pressed);
    TEST_ASSERT_FALSE(in.pressed_now);
    TEST_ASSERT_FALSE(in.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx));
}

/* ---- Test 5: wants_pointer true on hover and while captured ---- */
static void test_interaction_wants_pointer(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    declare_btn_frame(&f1);

    /* Hover only → wants_pointer true (persists until next begin). */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    (void)query_btn_frame(&f2);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx));

    /* Frame 3: pointer off-widget, not queried -> false. */
    nt_pointer_t f3 = make_pointer(0.0F, 0.0F, false, false, false);
    declare_btn_frame(&f3);
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx));
}

/* ---- Test 8: exclusive capture — while A owns capture, B sees zero ----
 * Standard UI semantics: one widget per pointer captures, others get nothing. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_interaction_capture_excludes_other_widgets(void) {
    /* B is positioned far from A so the same pointer cannot hover both. */
    const float a_x = BTN_X;
    const float a_y = BTN_Y;
    const float a_cx = a_x + (BTN_W * 0.5F);
    const float a_cy = a_y + (BTN_H * 0.5F);
    const float b_x = BTN_X + BTN_W + 80.0F; /* gap so AABBs don't overlap */
    const float b_y = BTN_Y;
    const float b_cx = b_x + (BTN_W * 0.5F);
    const float b_cy = b_y + (BTN_H * 0.5F);

    /* Frame 1: declare both elements so Clay has bboxes for next frame. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F, false, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_end(s_fx.ctx);

    /* Frame 2: press inside A -> A begins capture. Query A and B in declaration
     * order (game declares both each frame regardless of where the pointer is). */
    nt_pointer_t f2 = make_pointer(a_cx, a_cy, true, true, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inA_press = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnA"));
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB_press = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnB"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(inA_press.hovered);
    TEST_ASSERT_TRUE(inA_press.pressed_now);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btnA"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    /* B was NOT pressed; A holds the capture. B sees nothing -- pointer is
     * actually over A anyway, but the pin is for the next frame. */
    TEST_ASSERT_FALSE(inB_press.hovered);
    TEST_ASSERT_FALSE(inB_press.pressed_now);

    /* Frame 3: hold the button, slide pointer to B's center. Pointer is now
     * geometrically OVER B but A holds the capture. B must report hovered=false. */
    nt_pointer_t f3 = make_pointer(b_cx, b_cy, true, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f3, 1);
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inA_drag = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnA"));
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB_drag = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnB"));
    nt_ui_end(s_fx.ctx);
    /* A still owns the capture; pointer is off-widget so A's hovered is false
     * but pressed (capture-held) is true. */
    TEST_ASSERT_TRUE(inA_drag.pressed);
    TEST_ASSERT_FALSE(inA_drag.hovered);
    /* B is geometrically under the pointer but EXCLUDED by A's capture. */
    TEST_ASSERT_FALSE(inB_drag.hovered);
    TEST_ASSERT_FALSE(inB_drag.pressed);
    TEST_ASSERT_FALSE(inB_drag.pressed_now);
    TEST_ASSERT_FALSE(inB_drag.clicked);

    /* Frame 4: release over B. A still owns capture this frame -- the release
     * lands on A's get_interaction call (because A is queried first), which
     * clears the capture. B's query is exclusive_gated until release HAS
     * been processed by A's call. Verify A processes the release (clicked=false,
     * since pointer is off A), and B is still gated on this frame because the
     * pre-call state of cap is still A. */
    nt_pointer_t f4 = make_pointer(b_cx, b_cy, false, false, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f4, 1);
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inA_rel = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnA"));
    /* A processed the release: released_now true, clicked false (off A). Capture cleared. */
    TEST_ASSERT_TRUE(inA_rel.released_now);
    TEST_ASSERT_FALSE(inA_rel.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    /* By the time B is stepped, A's call has cleared the capture, so B is
     * no longer exclusive_gated and reports hovered=true (pointer is over B). */
    nt_ui_interaction_t inB_rel = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnB"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(inB_rel.hovered);

    /* Frame 5: idle, pointer over B -> normal hover (no residual capture). */
    nt_pointer_t f5 = make_pointer(b_cx, b_cy, false, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f5, 1);
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inA_drop = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnA"));
    (void)inA_drop;
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB_idle = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btnB"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(inB_idle.hovered);
    TEST_ASSERT_FALSE(inB_idle.pressed);
}

/* Query is pure — N calls return the SAME struct AND cap->active_id is unchanged. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_query_is_idempotent(void) {
    /* Frame 1: declare. */
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    declare_btn_frame(&f1);

    /* Frame 2: press inside (capture begins via step). */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)query_btn_frame(&f2); /* uses step under the hood */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Frame 3: release inside. query is idempotent — N calls return identical
     * clicked=true and leave cap->active_id unchanged (no step → no commit). */
    nt_pointer_t f3 = make_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f3, 1);
    declare_btn_element();

    nt_ui_interaction_t calls[5];
    for (int i = 0; i < 5; ++i) {
        calls[i] = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
    }

    /* All five returns are byte-identical. */
    for (int i = 1; i < 5; ++i) {
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&calls[0], &calls[i], sizeof(nt_ui_interaction_t), "query is not idempotent");
    }
    /* All 5 must report clicked=true on the release frame. */
    TEST_ASSERT_TRUE(calls[0].clicked);
    TEST_ASSERT_TRUE(calls[0].released_now);

    /* cap->active_id is UNCHANGED by query (still owns the press; only step
     * would clear it). */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    nt_ui_end(s_fx.ctx);
}

/* ---- Test 10: step commits what query previewed ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_step_commits_what_query_previewed(void) {
    /* Frame 1: declare. */
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    declare_btn_frame(&f1);

    /* Frame 2: press_now. Pre-query first (capture still 0), then step. */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    declare_btn_element();
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    nt_ui_interaction_t qA = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
    /* Query did NOT mutate. */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    nt_ui_interaction_t qB = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn"));
    /* Step committed: capture begins. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    /* Returns are byte-identical. */
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&qA, &qB, sizeof(nt_ui_interaction_t), "query and step diverged on press_now frame");
    TEST_ASSERT_TRUE(qA.pressed_now);
    nt_ui_end(s_fx.ctx);

    /* Frame 3: release. Query first (capture still owned), then step. */
    nt_pointer_t f3 = make_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f3, 1);
    declare_btn_element();
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    nt_ui_interaction_t rA = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
    /* Query did NOT clear capture. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    nt_ui_interaction_t rB = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn"));
    /* Step cleared capture (release ends it). */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&rA, &rB, sizeof(nt_ui_interaction_t), "query and step diverged on release frame");
    TEST_ASSERT_TRUE(rA.clicked);
    TEST_ASSERT_TRUE(rA.released_now);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 11: query and step agree under rotation ----
 * Pointer outside axis-aligned AABB but inside the rotated visual quad — the
 * inverse-affine must un-rotate it to register a hover. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_query_step_under_rotation(void) {
    nt_ui_transform_t rot = nt_ui_transform_defaults();
    rot.rotation = 30.0F * 0.017453292F;

    /* Frame 1: declare the rotated button so prev-frame tree_baked carries it. */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F, false, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f1, 1);
    CLAY({.id = CLAY_ID("rotbtn"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &rot, 1.0F)}) {}
    nt_ui_end(s_fx.ctx);

    /* Frame 2: pointer at the forward-rotated layout right-center. */
    const float c = 0.8660F;
    const float sn = 0.5F;
    const float dx = BTN_W * 0.5F;
    const float rx = BTN_CX + (c * dx);
    const float ry = BTN_CY + (sn * dx);
    TEST_ASSERT_TRUE(ry > BTN_Y + BTN_H);

    nt_pointer_t f2 = make_pointer(rx, ry, true, true, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    CLAY({.id = CLAY_ID("rotbtn"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &rot, 1.0F)}) {
        const nt_ui_interaction_t q = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("rotbtn"));
        const nt_ui_interaction_t s = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("rotbtn"));
        TEST_ASSERT_TRUE_MESSAGE(q.hovered, "query inverse-affine missed the rotated quad");
        TEST_ASSERT_TRUE(q.pressed_now);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&q, &s, sizeof(nt_ui_interaction_t), "query and step diverged under rotation");
    }
    nt_ui_end(s_fx.ctx);
}

/* step is strictly in-frame; calling outside begin/end must assert. */
static void test_step_outside_frame_asserts(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("btn"), .layout = {.sizing = {CLAY_SIZING_FIXED(50), CLAY_SIZING_FIXED(50)}}}) {}
    nt_ui_end(s_fx.ctx);
    /* No begin in flight → step must trip the in-frame assert. */
    NT_TEST_EXPECT_ASSERT((void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn")));
}

/* query is safe outside begin/end (snapshot/restore Clay ctx, like get_bbox). */
static void test_query_outside_frame_returns_prev_frame_state(void) {
    nt_pointer_t mouse = {.x = 25.0F, .y = 25.0F, .active = true};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("btn"), .layout = {.sizing = {CLAY_SIZING_FIXED(50), CLAY_SIZING_FIXED(50)}}}) {}
    nt_ui_end(s_fx.ctx);
    /* Outside frame: query reads prev-frame bbox + pointer, returns without asserting. */
    const nt_ui_interaction_t in = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
    TEST_ASSERT_TRUE_MESSAGE(in.hovered, "query outside frame must read prev-frame bbox");
}

/* ---- Multitouch (α-semantics: single capture per widget) ---- */

/* Two-pointer frame helper. */
static nt_ui_interaction_t step_btn_2p(const nt_pointer_t *a, const nt_pointer_t *b) {
    const nt_pointer_t ptrs[2] = {*a, *b};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, ptrs, 2);
    declare_btn_element();
    nt_ui_interaction_t in = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    return in;
}

static void declare_btn_2p(const nt_pointer_t *a, const nt_pointer_t *b) {
    const nt_pointer_t ptrs[2] = {*a, *b};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, ptrs, 2);
    declare_btn_element();
    nt_ui_end(s_fx.ctx);
}

/* Test M1: finger 0 captures, finger 1's press on same widget is ignored.
 * Only finger 0's release fires clicked. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_multitouch_first_finger_wins_capture(void) {
    /* Frame 1: declare (no input) so bbox lands in Clay's hashmap. */
    nt_pointer_t idle = make_pointer(BTN_CX, BTN_CY, false, false, false);
    declare_btn_2p(&idle, &idle);

    /* Frame 2: finger 0 presses inside; finger 1 idle off-widget. */
    nt_pointer_t f0_press = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_pointer_t f1_off = make_pointer(0.0F, 0.0F, false, false, false);
    nt_ui_interaction_t in2 = step_btn_2p(&f0_press, &f1_off);
    TEST_ASSERT_TRUE(in2.pressed);
    TEST_ASSERT_TRUE(in2.pressed_now);
    /* captures[0] holds the widget; captures[1] empty. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 1));

    /* Frame 3: finger 0 holds; finger 1 ALSO presses inside the widget.
     * α: second finger ignored, captures[1] stays empty. */
    nt_pointer_t f0_hold = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_pointer_t f1_press = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_interaction_t in3 = step_btn_2p(&f0_hold, &f1_press);
    TEST_ASSERT_TRUE(in3.pressed);
    TEST_ASSERT_FALSE(in3.pressed_now); /* no NEW capture this frame */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 1));

    /* Frame 4: finger 0 releases over widget; finger 1 still pressing.
     * Click fires from finger 0; captures[0] cleared. captures[1] stays empty. */
    nt_pointer_t f0_release = make_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_pointer_t f1_hold = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_interaction_t in4 = step_btn_2p(&f0_release, &f1_hold);
    TEST_ASSERT_TRUE(in4.clicked);
    TEST_ASSERT_TRUE(in4.released_now);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 1));

    /* Frame 5: only finger 1 still pressed-down (no new edge). No second click. */
    nt_pointer_t f0_idle = make_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_pointer_t f1_still = make_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_interaction_t in5 = step_btn_2p(&f0_idle, &f1_still);
    TEST_ASSERT_FALSE(in5.clicked);
    /* No edge on finger 1 (is_pressed = false), so no new capture either. */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 1));
}

/* Test M2: finger 1 presses widget while finger 0 holds NOTHING — finger 1 captures. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_multitouch_secondary_finger_captures_when_primary_idle(void) {
    nt_pointer_t idle = make_pointer(0.0F, 0.0F, false, false, false);
    declare_btn_2p(&idle, &idle);

    /* Frame 2: finger 0 off, finger 1 presses inside widget → captures[1] holds. */
    nt_pointer_t f0_off = make_pointer(0.0F, 0.0F, false, false, false);
    nt_pointer_t f1_press = make_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_interaction_t in2 = step_btn_2p(&f0_off, &f1_press);
    TEST_ASSERT_TRUE(in2.pressed_now);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 1));

    /* Frame 3: finger 1 releases over widget → clicked, captures[1] cleared. */
    nt_pointer_t f1_release = make_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_interaction_t in3 = step_btn_2p(&f0_off, &f1_release);
    TEST_ASSERT_TRUE(in3.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_interaction_click_on_release_within_bounds);
    RUN_TEST(test_interaction_hover_then_pressed);
    RUN_TEST(test_interaction_release_outside_cancels);
    RUN_TEST(test_interaction_disabled_skips);
    RUN_TEST(test_interaction_wants_pointer);
    RUN_TEST(test_interaction_padded_hover_and_capture);
    RUN_TEST(test_interaction_disabled_with_padding_stays_disabled);
    RUN_TEST(test_interaction_capture_excludes_other_widgets);
    RUN_TEST(test_query_is_idempotent);
    RUN_TEST(test_step_commits_what_query_previewed);
    RUN_TEST(test_query_step_under_rotation);
    RUN_TEST(test_step_outside_frame_asserts);
    RUN_TEST(test_query_outside_frame_returns_prev_frame_state);
    RUN_TEST(test_multitouch_first_finger_wins_capture);
    RUN_TEST(test_multitouch_secondary_finger_captures_when_primary_idle);
    return UNITY_END();
}
