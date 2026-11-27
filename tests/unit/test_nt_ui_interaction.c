/* Engine-owned interaction state machine.
 *
 * nt_ui_step drives per-pointer capture off precomputed button edges +
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
#include "test_helpers/ui_fx_frame.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_popup.h"
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

/* Build a primary pointer at (x,y) with the LEFT button edges set. */
/* Declare the button bbox at a FIXED absolute position so the layout solver
 * lands it exactly where the asserts expect. Called inside an open frame. */
static void declare_btn_element(void) {
    CLAY({.id = CLAY_ID("btn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
}

/* A frame that only declares the bbox (no interaction query). Used for frame 1
 * so Clay's hashmap has a prev-frame bbox before the first query. */
static void declare_btn_frame(const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = p, .pointer_count = 1});
    declare_btn_element();
    nt_ui_end(s_fx.ctx);
}

/* A step frame: begin with pointer p, declare the element, step interaction
 * (INSIDE the frame -- step needs Clay's context set), then end. Mirrors how
 * a game/button calls nt_ui_step during declaration. */
static nt_ui_interaction_t query_btn_frame(const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = p, .pointer_count = 1});
    declare_btn_element();
    nt_ui_interaction_t in = nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL);
    nt_ui_end(s_fx.ctx);
    return in;
}

/* Warm-up frame: declare + step so btn is BOTH baked (bbox) and entered into the interactive
 * registry. Front-most arbitration reads the PREVIOUS frame's registry, so a widget reacts only from
 * the frame after its first step; the warm-up itself reacts to nothing (empty registry → hot==0). */
static void warm_btn_frame(const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = p, .pointer_count = 1});
    declare_btn_element();
    (void)nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL);
    nt_ui_end(s_fx.ctx);
}

/* Warm-up that registers btn with a specific pad (the resolve hit-tests the registered pad). */
static void warm_btn_frame_padded(const nt_pointer_t *p, const int16_t pad[4]) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = p, .pointer_count = 1});
    declare_btn_element();
    (void)nt_ui_step(s_fx.ctx, nt_ui_id("btn"), &(nt_ui_step_desc_t){.pad_lrtb = pad});
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 1: press-then-release over widget -> clicked once (SC #1) ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_interaction_click_on_release_within_bounds(void) {
    /* Frame 1: declare only (no buttons). Bbox stored for next frame. */
    nt_pointer_t f1 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Frame 2: press inside. */
    nt_pointer_t f2 = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_interaction_t in2 = query_btn_frame(&f2);
    TEST_ASSERT_TRUE(in2.hovered);
    TEST_ASSERT_TRUE(in2.pressed);
    TEST_ASSERT_TRUE(in2.pressed_now);
    TEST_ASSERT_FALSE(in2.clicked);
    TEST_ASSERT_FALSE(in2.released_now);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Frame 3: hold down (still pressed, NOT pressed_now, NOT clicked). */
    nt_pointer_t f3 = ui_fx_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_interaction_t in3 = query_btn_frame(&f3);
    TEST_ASSERT_TRUE(in3.pressed);
    TEST_ASSERT_FALSE(in3.pressed_now);
    TEST_ASSERT_FALSE(in3.clicked);

    /* Frame 4: release inside -> clicked exactly once, capture cleared. */
    nt_pointer_t f4 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_interaction_t in4 = query_btn_frame(&f4);
    TEST_ASSERT_TRUE(in4.released_now);
    TEST_ASSERT_TRUE(in4.clicked);
    TEST_ASSERT_FALSE(in4.pressed);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Frame 5: idle -> click does NOT re-fire. */
    nt_pointer_t f5 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_ui_interaction_t in5 = query_btn_frame(&f5);
    TEST_ASSERT_FALSE(in5.clicked);
    TEST_ASSERT_FALSE(in5.released_now);
    TEST_ASSERT_TRUE(in5.hovered); /* still over the widget */
}

/* ---- Test 2: idle -> hover -> pressed state progression (SC #2) ---- */
static void test_interaction_hover_then_pressed(void) {
    nt_pointer_t f1 = ui_fx_pointer(0.0F, 0.0F, false, false, false);
    warm_btn_frame(&f1);

    /* Frame 2: pointer over, no button -> hovered, not pressed. */
    nt_pointer_t f2 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_ui_interaction_t hov = query_btn_frame(&f2);
    TEST_ASSERT_TRUE(hov.hovered);
    TEST_ASSERT_FALSE(hov.pressed);
    TEST_ASSERT_FALSE(hov.pressed_now);

    /* Frame 3: press -> hovered && pressed && pressed_now. */
    nt_pointer_t f3 = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_interaction_t prs = query_btn_frame(&f3);
    TEST_ASSERT_TRUE(prs.hovered);
    TEST_ASSERT_TRUE(prs.pressed);
    TEST_ASSERT_TRUE(prs.pressed_now);
}

/* A gone pointer (a mouse that left, a lifted touch) keeps its last position but hovers nothing. */
static void test_interaction_inactive_pointer_no_hover(void) {
    nt_pointer_t f1 = ui_fx_pointer(0.0F, 0.0F, false, false, false);
    warm_btn_frame(&f1);
    nt_pointer_t f2 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    f2.active = false;
    nt_ui_interaction_t in = query_btn_frame(&f2);
    TEST_ASSERT_FALSE(in.hovered);
}

/* ---- Test 3: press, release OUTSIDE widget -> cancel (released_now, !clicked) ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_interaction_release_outside_cancels(void) {
    nt_pointer_t f1 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Frame 2: press inside -> capture begins. */
    nt_pointer_t f2 = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_interaction_t in2 = query_btn_frame(&f2);
    TEST_ASSERT_TRUE(in2.pressed_now);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Frame 3: hold, drag pointer OUTSIDE the bbox -> still captured (pressed),
     * but not hovered; drag delta reflects the move. */
    const float far_x = BTN_X + BTN_W + 100.0F;
    const float far_y = BTN_Y - 100.0F;
    nt_pointer_t f3 = ui_fx_pointer(far_x, far_y, true, false, false);
    nt_ui_interaction_t in3 = query_btn_frame(&f3);
    TEST_ASSERT_TRUE(in3.pressed); /* capture holds even off-widget */
    TEST_ASSERT_FALSE(in3.hovered);
    TEST_ASSERT_TRUE(ui_fx_float_near(in3.drag_dx, far_x - BTN_CX, 0.01F));
    TEST_ASSERT_TRUE(ui_fx_float_near(in3.drag_dy, far_y - BTN_CY, 0.01F));

    /* Frame 4: release OUTSIDE -> released_now true, clicked FALSE (cancel),
     * capture cleared. */
    nt_pointer_t f4 = ui_fx_pointer(far_x, far_y, false, false, true);
    nt_ui_interaction_t in4 = query_btn_frame(&f4);
    TEST_ASSERT_TRUE(in4.released_now);
    TEST_ASSERT_FALSE(in4.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
}

/* ---- Test 4: disabled widget skips hover + click ---- */
static void test_interaction_disabled_skips(void) {
    nt_pointer_t f1 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Disabled = button never calls get_interaction → zeroed result, no capture. */
    nt_pointer_t f2 = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    const bool enabled = false;
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f2, .pointer_count = 1});
    declare_btn_element();
    nt_ui_interaction_t in = enabled ? nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL) : (nt_ui_interaction_t){0};
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(in.hovered);
    TEST_ASSERT_FALSE(in.pressed);
    TEST_ASSERT_FALSE(in.pressed_now);
    TEST_ASSERT_FALSE(in.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_wants_pointer(s_fx.ctx, 0U), "the press landed on last frame's button: the gesture stays the UI's");
}

/* ---- Test 6: padded interaction inflates bbox before inverse-affine ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_interaction_padded_hover_and_capture(void) {
    /* Frame 1: declare + step (padded) so btn registers with the SAME pad the interaction uses —
     * the next-frame resolve hit-tests the registered pad. */
    const int16_t pad_right[4] = {0, 16, 0, 0};
    nt_pointer_t f1 = ui_fx_pointer(0.0F, 0.0F, false, false, false);
    warm_btn_frame_padded(&f1, pad_right);

    /* Pointer 12 px past right edge + pad right 16 → hovered + pressed_now. */
    const float right_outside_x = BTN_X + BTN_W + 12.0F;
    const float center_y = BTN_Y + (BTN_H * 0.5F);
    nt_pointer_t f2 = ui_fx_pointer(right_outside_x, center_y, true, true, false);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f2, .pointer_count = 1});
    declare_btn_element();
    nt_ui_interaction_t in = nt_ui_step(s_fx.ctx, nt_ui_id("btn"), &(nt_ui_step_desc_t){.pad_lrtb = pad_right});
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(in.hovered);
    TEST_ASSERT_TRUE(in.pressed);
    TEST_ASSERT_TRUE(in.pressed_now);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Clear capture via a quiet frame; orphan cleanup runs at begin. */
    nt_pointer_t f3 = ui_fx_pointer(0.0F, 0.0F, false, false, true);
    declare_btn_frame(&f3); /* releases + frame 1 will clear orphan */
    nt_pointer_t f4 = ui_fx_pointer(0.0F, 0.0F, false, false, false);
    declare_btn_frame(&f4); /* orphan cleanup runs at this begin */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
}

/* ---- Test 7: padded query on a DISABLED widget stays non-hoverable ---- */
static void test_interaction_disabled_with_padding_stays_disabled(void) {
    nt_pointer_t f1 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Frame 2: pointer over center, pressed, but enabled = false. Large
     * padding {32,32,32,32} must NOT cause hover/press/capture. */
    nt_pointer_t f2 = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    const bool enabled = false;
    const int16_t big_pad[4] = {32, 32, 32, 32};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f2, .pointer_count = 1});
    declare_btn_element();
    /* Mirror the button's disabled branch: query not invoked, zeroed result. */
    nt_ui_interaction_t in = enabled ? nt_ui_step(s_fx.ctx, nt_ui_id("btn"), &(nt_ui_step_desc_t){.pad_lrtb = big_pad}) : (nt_ui_interaction_t){0};
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(in.hovered);
    TEST_ASSERT_FALSE(in.pressed);
    TEST_ASSERT_FALSE(in.pressed_now);
    TEST_ASSERT_FALSE(in.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_wants_pointer(s_fx.ctx, 0U), "the press landed on last frame's button: the gesture stays the UI's");
}

/* ---- Test 5: wants_pointer true on hover and while captured ---- */
static void test_interaction_wants_pointer(void) {
    nt_pointer_t f1 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Hover only → wants_pointer true (persists until next begin). */
    nt_pointer_t f2 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    (void)query_btn_frame(&f2);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx, 0U));

    /* Frame 3: pointer off-widget, not queried -> false. */
    nt_pointer_t f3 = ui_fx_pointer(0.0F, 0.0F, false, false, false);
    declare_btn_frame(&f3);
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx, 0U));
}

/* ---- A second button joining a gesture that began on the UI keeps it the UI's: the right press off the UI
 *      while the left still holds the button does not hand the rest to the game. A new press after both are
 *      up decides again. ---- */
static void test_interaction_second_button_keeps_the_gesture(void) {
    nt_pointer_t p = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&p);
    p = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)query_btn_frame(&p);
    p = ui_fx_pointer(600.0F, 500.0F, true, false, false);
    (void)query_btn_frame(&p);
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    (void)query_btn_frame(&p);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_wants_pointer(s_fx.ctx, 0U), "the right press joins the left's gesture");
    /* The left's release and the right's press can land in one poll too. */
    p = ui_fx_pointer(600.0F, 500.0F, false, false, true);
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_released = true};
    (void)query_btn_frame(&p);
    p = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)query_btn_frame(&p);
    p = ui_fx_pointer(600.0F, 500.0F, false, false, true);
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    (void)query_btn_frame(&p);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_wants_pointer(s_fx.ctx, 0U), "a right press in the poll of the left's release joins it");
    p = ui_fx_pointer(600.0F, 500.0F, false, false, true);
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_released = true};
    (void)query_btn_frame(&p);
    p = ui_fx_pointer(600.0F, 500.0F, true, true, false);
    (void)query_btn_frame(&p);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_wants_pointer(s_fx.ctx, 0U), "a press of its own off the UI is the game's");
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

    /* Frame 1: declare both + step both so they register for next-frame arbitration. */
    nt_pointer_t f1 = ui_fx_pointer(0.0F, 0.0F, false, false, false);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f1, .pointer_count = 1});
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {
        (void)nt_ui_step(s_fx.ctx, nt_ui_id("btnA"), NULL);
    }
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {
        (void)nt_ui_step(s_fx.ctx, nt_ui_id("btnB"), NULL);
    }
    nt_ui_end(s_fx.ctx);

    /* Frame 2: press inside A -> A begins capture. Query A and B in declaration
     * order (game declares both each frame regardless of where the pointer is). */
    nt_pointer_t f2 = ui_fx_pointer(a_cx, a_cy, true, true, false);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f2, .pointer_count = 1});
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inA_press = nt_ui_step(s_fx.ctx, nt_ui_id("btnA"), NULL);
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB_press = nt_ui_step(s_fx.ctx, nt_ui_id("btnB"), NULL);
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
    nt_pointer_t f3 = ui_fx_pointer(b_cx, b_cy, true, false, false);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f3, .pointer_count = 1});
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inA_drag = nt_ui_step(s_fx.ctx, nt_ui_id("btnA"), NULL);
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB_drag = nt_ui_step(s_fx.ctx, nt_ui_id("btnB"), NULL);
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
    nt_pointer_t f4 = ui_fx_pointer(b_cx, b_cy, false, false, true);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f4, .pointer_count = 1});
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inA_rel = nt_ui_step(s_fx.ctx, nt_ui_id("btnA"), NULL);
    /* A processed the release: released_now true, clicked false (off A). Capture cleared. */
    TEST_ASSERT_TRUE(inA_rel.released_now);
    TEST_ASSERT_FALSE(inA_rel.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    /* By the time B is stepped, A's call has cleared the capture, so B is
     * no longer exclusive_gated and reports hovered=true (pointer is over B). */
    nt_ui_interaction_t inB_rel = nt_ui_step(s_fx.ctx, nt_ui_id("btnB"), NULL);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(inB_rel.hovered);

    /* Frame 5: idle, pointer over B -> normal hover (no residual capture). */
    nt_pointer_t f5 = ui_fx_pointer(b_cx, b_cy, false, false, false);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f5, .pointer_count = 1});
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inA_drop = nt_ui_step(s_fx.ctx, nt_ui_id("btnA"), NULL);
    (void)inA_drop;
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB_idle = nt_ui_step(s_fx.ctx, nt_ui_id("btnB"), NULL);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(inB_idle.hovered);
    TEST_ASSERT_FALSE(inB_idle.pressed);
}

/* Query is pure — N calls return the SAME struct AND cap->active_id is unchanged. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_query_is_idempotent(void) {
    /* Frame 1: declare. */
    nt_pointer_t f1 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Frame 2: press inside (capture begins via step). */
    nt_pointer_t f2 = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    (void)query_btn_frame(&f2); /* uses step under the hood */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Frame 3: release inside. query is idempotent — N calls return identical
     * clicked=true and leave cap->active_id unchanged (no step → no commit). */
    nt_pointer_t f3 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f3, .pointer_count = 1});
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
    nt_pointer_t f1 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Frame 2: press_now. Pre-query first (capture still 0), then step. */
    nt_pointer_t f2 = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f2, .pointer_count = 1});
    declare_btn_element();
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    nt_ui_interaction_t qA = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
    /* Query did NOT mutate. */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    nt_ui_interaction_t qB = nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL);
    /* Step committed: capture begins. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    /* Returns are byte-identical. */
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&qA, &qB, sizeof(nt_ui_interaction_t), "query and step diverged on press_now frame");
    TEST_ASSERT_TRUE(qA.pressed_now);
    nt_ui_end(s_fx.ctx);

    /* Frame 3: release. Query first (capture still owned), then step. */
    nt_pointer_t f3 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f3, .pointer_count = 1});
    declare_btn_element();
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    nt_ui_interaction_t rA = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
    /* Query did NOT clear capture. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    nt_ui_interaction_t rB = nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL);
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
    rot.rotation_z = 30.0F * 0.017453292F;

    /* Frame 1: declare the rotated button + step it so prev-frame tree_baked carries it AND it
     * enters the interactive registry for next-frame arbitration. */
    nt_pointer_t f1 = ui_fx_pointer(0.0F, 0.0F, false, false, false);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f1, .pointer_count = 1});
    CLAY({.id = CLAY_ID("rotbtn"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &rot, 1.0F)}) {
        (void)nt_ui_step(s_fx.ctx, nt_ui_id("rotbtn"), NULL);
    }
    nt_ui_end(s_fx.ctx);

    /* Frame 2: pointer at the forward-rotated layout right-center. */
    const float c = 0.8660F;
    const float sn = 0.5F;
    const float dx = BTN_W * 0.5F;
    const float rx = BTN_CX + (c * dx);
    const float ry = BTN_CY + (sn * dx);
    TEST_ASSERT_TRUE(ry > BTN_Y + BTN_H);

    nt_pointer_t f2 = ui_fx_pointer(rx, ry, true, true, false);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f2, .pointer_count = 1});
    CLAY({.id = CLAY_ID("rotbtn"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &rot, 1.0F)}) {
        const nt_ui_interaction_t q = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("rotbtn"));
        const nt_ui_interaction_t s = nt_ui_step(s_fx.ctx, nt_ui_id("rotbtn"), NULL);
        TEST_ASSERT_TRUE_MESSAGE(q.hovered, "query inverse-affine missed the rotated quad");
        TEST_ASSERT_TRUE(q.pressed_now);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&q, &s, sizeof(nt_ui_interaction_t), "query and step diverged under rotation");
    }
    nt_ui_end(s_fx.ctx);
}

/* step is strictly in-frame; calling outside begin/end must assert. */
static void test_step_outside_frame_asserts(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &mouse, .pointer_count = 1});
    CLAY({.id = CLAY_ID("btn"), .layout = {.sizing = {CLAY_SIZING_FIXED(50), CLAY_SIZING_FIXED(50)}}}) {}
    nt_ui_end(s_fx.ctx);
    /* No begin in flight → step must trip the in-frame assert. */
    NT_TEST_EXPECT_ASSERT((void)nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL));
}

/* query is safe outside begin/end (snapshot/restore Clay ctx, like get_bbox). */
static void test_query_outside_frame_returns_prev_frame_state(void) {
    nt_pointer_t mouse = {.x = 25.0F, .y = 25.0F, .active = true};
    /* Two step frames: frame 1 registers btn, frame 2's resolve (prev = frame-1 registry) makes it
     * hot, so the outside query (which reuses that resolve) admits it past the front-most gate. */
    for (int i = 0; i < 2; ++i) {
        nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &mouse, .pointer_count = 1});
        CLAY({.id = CLAY_ID("btn"), .layout = {.sizing = {CLAY_SIZING_FIXED(50), CLAY_SIZING_FIXED(50)}}}) { (void)nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL); }
        nt_ui_end(s_fx.ctx);
    }
    /* Outside frame: query reads prev-frame bbox + pointer, returns without asserting. */
    const nt_ui_interaction_t in = nt_ui_query_interaction(s_fx.ctx, nt_ui_id("btn"));
    TEST_ASSERT_TRUE_MESSAGE(in.hovered, "query outside frame must read prev-frame bbox");
}

/* ---- Multitouch (α-semantics: single capture per widget) ---- */

/* Two-pointer frame helper. */
static nt_ui_interaction_t step_btn_2p(const nt_pointer_t *a, const nt_pointer_t *b) {
    const nt_pointer_t ptrs[2] = {*a, *b};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = ptrs, .pointer_count = 2});
    declare_btn_element();
    nt_ui_interaction_t in = nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL);
    nt_ui_end(s_fx.ctx);
    return in;
}

/* Two-pointer warm-up: declare + step so btn registers for next-frame arbitration. */
static void warm_btn_2p(const nt_pointer_t *a, const nt_pointer_t *b) {
    const nt_pointer_t ptrs[2] = {*a, *b};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = ptrs, .pointer_count = 2});
    declare_btn_element();
    (void)nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL);
    nt_ui_end(s_fx.ctx);
}

/* Test M1: finger 0 captures, finger 1's press on same widget is ignored.
 * Only finger 0's release fires clicked. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_multitouch_first_finger_wins_capture(void) {
    /* Frame 1: declare (no input) so bbox lands in Clay's hashmap. */
    nt_pointer_t idle = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_2p(&idle, &idle);

    /* Frame 2: finger 0 presses inside; finger 1 idle off-widget. */
    nt_pointer_t f0_press = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_pointer_t f1_off = ui_fx_pointer(0.0F, 0.0F, false, false, false);
    nt_ui_interaction_t in2 = step_btn_2p(&f0_press, &f1_off);
    TEST_ASSERT_TRUE(in2.pressed);
    TEST_ASSERT_TRUE(in2.pressed_now);
    /* captures[0] holds the widget; captures[1] empty. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 1));

    /* Frame 3: finger 0 holds; finger 1 ALSO presses inside the widget.
     * α: second finger ignored, captures[1] stays empty. */
    nt_pointer_t f0_hold = ui_fx_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_pointer_t f1_press = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_interaction_t in3 = step_btn_2p(&f0_hold, &f1_press);
    TEST_ASSERT_TRUE(in3.pressed);
    TEST_ASSERT_FALSE(in3.pressed_now); /* no NEW capture this frame */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 1));

    /* Frame 4: finger 0 releases over widget; finger 1 still pressing.
     * Click fires from finger 0; captures[0] cleared. captures[1] stays empty. */
    nt_pointer_t f0_release = ui_fx_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_pointer_t f1_hold = ui_fx_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_interaction_t in4 = step_btn_2p(&f0_release, &f1_hold);
    TEST_ASSERT_TRUE(in4.clicked);
    TEST_ASSERT_TRUE(in4.released_now);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 1));

    /* Frame 5: only finger 1 still pressed-down (no new edge). No second click. */
    nt_pointer_t f0_idle = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    nt_pointer_t f1_still = ui_fx_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_interaction_t in5 = step_btn_2p(&f0_idle, &f1_still);
    TEST_ASSERT_FALSE(in5.clicked);
    /* No edge on finger 1 (is_pressed = false), so no new capture either. */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 1));
}

/* Test M2: finger 1 presses widget while finger 0 holds NOTHING — finger 1 captures. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_multitouch_secondary_finger_captures_when_primary_idle(void) {
    nt_pointer_t idle = ui_fx_pointer(0.0F, 0.0F, false, false, false);
    warm_btn_2p(&idle, &idle);

    /* Frame 2: finger 0 off, finger 1 presses inside widget → captures[1] holds. */
    nt_pointer_t f0_off = ui_fx_pointer(0.0F, 0.0F, false, false, false);
    nt_pointer_t f1_press = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_interaction_t in2 = step_btn_2p(&f0_off, &f1_press);
    TEST_ASSERT_TRUE(in2.pressed_now);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 1));

    /* Frame 3: finger 1 releases over widget → clicked, captures[1] cleared. */
    nt_pointer_t f1_release = ui_fx_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_interaction_t in3 = step_btn_2p(&f0_off, &f1_release);
    TEST_ASSERT_TRUE(in3.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 1));
}

/* ---- Disabled widget blocks input to widgets behind it (modal-overlay correctness) ----
 * A disabled widget A calls nt_ui_block_pointer instead of step. It must win topmost-z hot
 * arbitration over an OVERLAPPING enabled widget B underneath, so B never hovers/clicks and A
 * itself never clicks. The pointer can't leak through the disabled overlay. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_disabled_widget_blocks_input_behind(void) {
    /* A and B occupy the SAME bbox (full overlap). B declared FIRST (behind), A SECOND (front). */
    const float ox = BTN_X;
    const float oy = BTN_Y;
    const float cx = ox + (BTN_W * 0.5F);
    const float cy = oy + (BTN_H * 0.5F);

    /* Frame 1: declare both. B steps (enabled). A blocks (disabled). Both register for next frame. */
    nt_pointer_t f1 = ui_fx_pointer(cx, cy, false, false, false);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f1, .pointer_count = 1});
    CLAY({.id = CLAY_ID("behindB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = ox, .y = oy}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {
        (void)nt_ui_step(s_fx.ctx, nt_ui_id("behindB"), NULL);
    }
    CLAY({.id = CLAY_ID("frontA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = ox, .y = oy}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {
        nt_ui_block_pointer(s_fx.ctx, nt_ui_id("frontA"), NULL);
    }
    nt_ui_end(s_fx.ctx);

    /* Frame 2: press inside the overlap. A (front) is the hot widget; B must see nothing. */
    nt_pointer_t f2 = ui_fx_pointer(cx, cy, true, true, false);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f2, .pointer_count = 1});
    CLAY({.id = CLAY_ID("behindB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = ox, .y = oy}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB = nt_ui_step(s_fx.ctx, nt_ui_id("behindB"), NULL);
    CLAY({.id = CLAY_ID("frontA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = ox, .y = oy}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_block_pointer(s_fx.ctx, nt_ui_id("frontA"), NULL);
    /* A wins front-most arbitration; the disabled overlay swallows the pointer. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("frontA"), nt_ui_pointer_hot(s_fx.ctx, 0).id);
    nt_ui_end(s_fx.ctx);
    /* B got no hover, no press, no capture, no click — the leak is plugged. */
    TEST_ASSERT_FALSE(inB.hovered);
    TEST_ASSERT_FALSE(inB.pressed);
    TEST_ASSERT_FALSE(inB.pressed_now);
    TEST_ASSERT_FALSE(inB.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Frame 3: release inside. A never clicks (inert), B still blocked → no click leak. */
    nt_pointer_t f3 = ui_fx_pointer(cx, cy, false, false, true);
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &f3, .pointer_count = 1});
    CLAY({.id = CLAY_ID("behindB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = ox, .y = oy}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB3 = nt_ui_step(s_fx.ctx, nt_ui_id("behindB"), NULL);
    CLAY({.id = CLAY_ID("frontA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = ox, .y = oy}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_block_pointer(s_fx.ctx, nt_ui_id("frontA"), NULL);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(inB3.clicked);
    TEST_ASSERT_FALSE(inB3.hovered);
}

/* ---- press -> drag OFF -> drag BACK -> release inside = clicked (button) ----
 * Capture survives the off-widget excursion; the release lands over the widget so it clicks. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_press_drag_off_then_back_clicks(void) {
    const float far_x = BTN_X + BTN_W + 200.0F; /* well outside the bbox */
    const float far_y = BTN_Y + BTN_H + 200.0F;

    nt_pointer_t f1 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&f1);

    /* Frame 2: press inside -> capture begins. */
    nt_pointer_t f2 = ui_fx_pointer(BTN_CX, BTN_CY, true, true, false);
    nt_ui_interaction_t in2 = query_btn_frame(&f2);
    TEST_ASSERT_TRUE(in2.pressed_now);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Frame 3: drag OFF the widget, still held. Capture persists, hovered false, no click. */
    nt_pointer_t f3 = ui_fx_pointer(far_x, far_y, true, false, false);
    nt_ui_interaction_t in3 = query_btn_frame(&f3);
    TEST_ASSERT_TRUE(in3.pressed);
    TEST_ASSERT_FALSE(in3.hovered);
    TEST_ASSERT_FALSE(in3.clicked);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Frame 4: drag BACK over the widget, still held. Hover returns, still no click yet. */
    nt_pointer_t f4 = ui_fx_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_interaction_t in4 = query_btn_frame(&f4);
    TEST_ASSERT_TRUE(in4.pressed);
    TEST_ASSERT_TRUE(in4.hovered);
    TEST_ASSERT_FALSE(in4.clicked);

    /* Frame 5: release INSIDE -> clicked (the round-trip excursion did not cancel). */
    nt_pointer_t f5 = ui_fx_pointer(BTN_CX, BTN_CY, false, false, true);
    nt_ui_interaction_t in5 = query_btn_frame(&f5);
    TEST_ASSERT_TRUE(in5.clicked);
    TEST_ASSERT_TRUE(in5.released_now);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
}

/* ---- two fingers press two DIFFERENT widgets same frame -> independent captures, both click ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_multitouch_two_widgets_independent_capture(void) {
    /* A and B are non-overlapping so each finger hits exactly one. */
    const float a_x = BTN_X;
    const float a_y = BTN_Y;
    const float a_cx = a_x + (BTN_W * 0.5F);
    const float a_cy = a_y + (BTN_H * 0.5F);
    const float b_x = BTN_X + BTN_W + 80.0F;
    const float b_y = BTN_Y;
    const float b_cx = b_x + (BTN_W * 0.5F);
    const float b_cy = b_y + (BTN_H * 0.5F);

    /* Frame 1: declare + step both so they register for next-frame arbitration. */
    nt_pointer_t idle = ui_fx_pointer(0.0F, 0.0F, false, false, false);
    const nt_pointer_t f1[2] = {idle, idle};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = f1, .pointer_count = 2});
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {
        (void)nt_ui_step(s_fx.ctx, nt_ui_id("btnA"), NULL);
    }
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {
        (void)nt_ui_step(s_fx.ctx, nt_ui_id("btnB"), NULL);
    }
    nt_ui_end(s_fx.ctx);

    /* Frame 2: finger 0 presses A, finger 1 presses B — SAME frame. Each captures its own widget. */
    nt_pointer_t f0_press = ui_fx_pointer(a_cx, a_cy, true, true, false);
    nt_pointer_t f1_press = ui_fx_pointer(b_cx, b_cy, true, true, false);
    const nt_pointer_t f2[2] = {f0_press, f1_press};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = f2, .pointer_count = 2});
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inA = nt_ui_step(s_fx.ctx, nt_ui_id("btnA"), NULL);
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB = nt_ui_step(s_fx.ctx, nt_ui_id("btnB"), NULL);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(inA.pressed_now);
    TEST_ASSERT_TRUE(inB.pressed_now);
    /* Independent captures: finger 0 -> A, finger 1 -> B. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btnA"), nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btnB"), nt_ui_test_capture_active_id(s_fx.ctx, 1));

    /* Frame 3: both release over their own widget -> both click, both captures cleared. */
    nt_pointer_t f0_rel = ui_fx_pointer(a_cx, a_cy, false, false, true);
    nt_pointer_t f1_rel = ui_fx_pointer(b_cx, b_cy, false, false, true);
    const nt_pointer_t f3[2] = {f0_rel, f1_rel};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = f3, .pointer_count = 2});
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inA_rel = nt_ui_step(s_fx.ctx, nt_ui_id("btnA"), NULL);
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB_rel = nt_ui_step(s_fx.ctx, nt_ui_id("btnB"), NULL);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(inA_rel.clicked);
    TEST_ASSERT_TRUE(inB_rel.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 1));
}

/* ---- Input ownership: what a game reads per pointer ---- */

static nt_pointer_t buttons_at(float x, float y, uint8_t down, uint8_t pressed, uint8_t released) {
    nt_pointer_t p = {.x = x, .y = y, .active = true};
    for (int b = 0; b < NT_BUTTON_MAX; ++b) {
        p.buttons[b] = (nt_button_state_t){.is_down = ((down >> b) & 1U) != 0U, .is_pressed = ((pressed >> b) & 1U) != 0U, .is_released = ((released >> b) & 1U) != 0U};
    }
    return p;
}

#define BIT_L 1U
#define BIT_R 2U
#define BIT_M 4U

/* Each pointer has its own answer: one over the button is the UI's, one over nothing is the game's. */
static void test_wants_pointer_is_per_pointer(void) {
    const nt_pointer_t on = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    const nt_pointer_t off = ui_fx_pointer(600.0F, 500.0F, false, false, false);
    warm_btn_2p(&on, &off);
    (void)step_btn_2p(&on, &off);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx, 0U));
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx, 1U));
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_wants_pointer(s_fx.ctx, 5U), "a slot the frame did not pass");
}

/* While a gesture is the game's the pointer has no target: the UI never hovers or clicks under its drag. */
static void test_game_gesture_has_no_target(void) {
    nt_pointer_t p = ui_fx_pointer(600.0F, 500.0F, false, false, false);
    warm_btn_frame(&p);
    p = ui_fx_pointer(600.0F, 500.0F, true, true, false);
    (void)query_btn_frame(&p);
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx, 0U));
    p = ui_fx_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_interaction_t in = query_btn_frame(&p);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_pointer_hot(s_fx.ctx, 0U).id);
    TEST_ASSERT_FALSE(in.hovered);
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx, 0U));
    p = ui_fx_pointer(BTN_CX, BTN_CY, false, false, true);
    in = query_btn_frame(&p);
    TEST_ASSERT_FALSE(in.clicked || in.released_now || in.hovered);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_wants_pointer(s_fx.ctx, 0U), "the release frame is still the game's");
    p = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    in = query_btn_frame(&p);
    TEST_ASSERT_TRUE_MESSAGE(in.hovered, "a free pointer targets the button again");
}

/* Only the gesture's first press is a press: a left press joining a right press on the button captures nothing. */
static void test_joined_press_is_no_press(void) {
    nt_pointer_t p = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&p);
    p = buttons_at(BTN_CX, BTN_CY, BIT_R, BIT_R, 0U);
    (void)query_btn_frame(&p);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx, 0U));
    p = buttons_at(BTN_CX, BTN_CY, BIT_R | BIT_L, BIT_L, 0U);
    nt_ui_interaction_t in = query_btn_frame(&p);
    TEST_ASSERT_FALSE(in.pressed_now);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
    p = buttons_at(BTN_CX, BTN_CY, BIT_R, 0U, BIT_L);
    in = query_btn_frame(&p);
    TEST_ASSERT_FALSE(in.clicked);
}

/* The middle button starts a gesture too: a left press while the game's middle drag holds joins the game's. */
static void test_middle_press_starts_a_gesture(void) {
    nt_pointer_t p = ui_fx_pointer(600.0F, 500.0F, false, false, false);
    warm_btn_frame(&p);
    p = buttons_at(600.0F, 500.0F, BIT_M, BIT_M, 0U);
    (void)query_btn_frame(&p);
    p = buttons_at(BTN_CX, BTN_CY, BIT_M, 0U, 0U);
    (void)query_btn_frame(&p);
    p = buttons_at(BTN_CX, BTN_CY, BIT_M | BIT_L, BIT_L, 0U);
    const nt_ui_interaction_t in = query_btn_frame(&p);
    TEST_ASSERT_FALSE(in.pressed_now || in.hovered);
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx, 0U));
}

/* A button held that the UI saw up (its press fell in a frame without nt_ui_begin) starts a gesture by the same
 * rule, without a press edge: on the button it is the UI's, captures nothing and clicks nothing. */
static void test_held_unseen_press_starts_a_gesture_without_an_edge(void) {
    nt_pointer_t p = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&p);
    p = ui_fx_pointer(BTN_CX, BTN_CY, true, false, false);
    nt_ui_interaction_t in = query_btn_frame(&p);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx, 0U));
    TEST_ASSERT_FALSE(in.pressed_now || in.pressed);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
    p = ui_fx_pointer(BTN_CX, BTN_CY, false, false, true);
    in = query_btn_frame(&p);
    TEST_ASSERT_FALSE(in.clicked);
}

/* A slot whose contact id changed lost a lift and a landing to frames without nt_ui_begin: the new contact's
 * held press is a new gesture, so the old contact's capture ends and the new one, off the UI, is the game's. */
static void test_contact_change_starts_a_new_gesture(void) {
    nt_pointer_t p = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    p.type = NT_POINTER_TOUCH;
    p.id = 7U;
    warm_btn_frame(&p);
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true};
    (void)query_btn_frame(&p);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0U));
    p = ui_fx_pointer(600.0F, 500.0F, true, false, false);
    p.type = NT_POINTER_TOUCH;
    p.id = 9U;
    (void)query_btn_frame(&p);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx, 0U));
}

/* A 2D context takes the occlusion too: 0 hides the pointer, so the front context keeps it. */
static void test_occlusion_zero_hides_a_2d_pointer(void) {
    nt_pointer_t p = ui_fx_pointer(BTN_CX, BTN_CY, false, false, false);
    warm_btn_frame(&p);
    const float hidden[1] = {0.0F};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &p, .pointer_count = 1, .occlusion = hidden});
    declare_btn_element();
    const nt_ui_interaction_t in = nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(in.hovered);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_pointer_hot(s_fx.ctx, 0U).id);
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx, 0U));
}

/* ---- Input ownership: every reader reads the target ---- */

#define COVER_ID 0x1C0001U
#define CHILD_ID 0x1C0002U
#define POPUP_ID 0x1C0006U

/* The button, then a later-declared record over its right half. */
static nt_ui_interaction_t covered_frame(nt_pointer_t p) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = 0.25F, .pointers = &p, .pointer_count = 1});
    declare_btn_element();
    const nt_ui_interaction_t in = nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL);
    CLAY({.id = (Clay_ElementId){.id = COVER_ID},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_CX, .y = BTN_Y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_block_pointer(s_fx.ctx, COVER_ID, NULL);
    nt_ui_end(s_fx.ctx);
    return in;
}

/* A holder dragged under a later record is neither hovered nor holding, and its release there clicks nothing. */
static void test_holder_under_a_later_record_is_not_hovered(void) {
    (void)covered_frame(ui_fx_pointer(BTN_X + 20.0F, BTN_CY, false, false, false));
    (void)covered_frame(ui_fx_pointer(BTN_X + 20.0F, BTN_CY, true, true, false));
    nt_ui_interaction_t in = covered_frame(ui_fx_pointer(BTN_X + 20.0F, BTN_CY, true, false, false));
    TEST_ASSERT_TRUE(in.hovered && in.hold_secs > 0.0F);
    in = covered_frame(ui_fx_pointer(BTN_CX + 20.0F, BTN_CY, true, false, false));
    TEST_ASSERT_TRUE(in.pressed);
    TEST_ASSERT_FALSE(in.hovered);
    TEST_ASSERT_TRUE(in.hold_secs == 0.0F);
    in = covered_frame(ui_fx_pointer(BTN_CX + 20.0F, BTN_CY, false, false, true));
    TEST_ASSERT_TRUE(in.released_now);
    TEST_ASSERT_FALSE(in.clicked);
}

/* The button, then optionally an open popup over the screen. */
static nt_ui_interaction_t btn_popup_frame(nt_pointer_t p, bool popup) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    const nt_ui_popup_anchor_t anc = {.x = 500.0F, .y = 400.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &p, .pointer_count = 1});
    declare_btn_element();
    const nt_ui_interaction_t in = nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL);
    if (popup && nt_ui_popup_begin(s_fx.ctx, POPUP_ID, &st, &anc, NULL)) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(40)}}}) {}
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    return in;
}

/* A capture nt_ui_begin drops (an overlay over it) reports lost once to its holder's step, no release and no click. */
static void test_dropped_capture_reports_lost_once(void) {
    (void)btn_popup_frame(ui_fx_pointer(BTN_CX, BTN_CY, false, false, false), false);
    (void)btn_popup_frame(ui_fx_pointer(BTN_CX, BTN_CY, true, true, false), false);
    nt_ui_popup_open(s_fx.ctx, POPUP_ID);
    (void)btn_popup_frame(ui_fx_pointer(BTN_CX, BTN_CY, true, false, false), true);
    nt_ui_interaction_t in = btn_popup_frame(ui_fx_pointer(BTN_CX, BTN_CY, true, false, false), true);
    TEST_ASSERT_TRUE_MESSAGE(in.lost, "the overlay took the capture");
    TEST_ASSERT_FALSE(in.pressed || in.clicked || in.released_now);
    in = btn_popup_frame(ui_fx_pointer(BTN_CX, BTN_CY, true, false, false), true);
    TEST_ASSERT_FALSE_MESSAGE(in.lost, "once");
    in = btn_popup_frame(ui_fx_pointer(BTN_CX, BTN_CY, false, false, true), true);
    TEST_ASSERT_FALSE(in.released_now || in.clicked || in.lost);
}

/* The button with a child record over its left half; both step. */
static void parent_child_frame(nt_pointer_t p, nt_ui_interaction_t *parent, nt_ui_interaction_t *child) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = &p, .pointer_count = 1});
    CLAY({.id = CLAY_ID("btn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {
        CLAY({.id = (Clay_ElementId){.id = CHILD_ID}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W * 0.5F), CLAY_SIZING_FIXED(BTN_H)}}}){} *child = nt_ui_step(s_fx.ctx, CHILD_ID, NULL);
    }
    *parent = nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL);
    nt_ui_end(s_fx.ctx);
}

static nt_pointer_t right_at(float x, bool press, bool release) {
    nt_pointer_t p = ui_fx_pointer(x, BTN_CY, false, false, false);
    p.buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_down = press, .is_pressed = press, .is_released = release};
    return p;
}

/* right_clicked: the release of a gesture whose first press was the right button, on its target only. */
static void test_right_click_goes_to_the_target(void) {
    nt_ui_interaction_t parent;
    nt_ui_interaction_t child;
    const float on_parent = BTN_X + (BTN_W * 0.75F);
    const float on_child = BTN_X + 20.0F;
    parent_child_frame(right_at(on_parent, false, false), &parent, &child);
    parent_child_frame(right_at(on_parent, true, false), &parent, &child);
    TEST_ASSERT_FALSE(parent.right_clicked || parent.pressed_now);
    parent_child_frame(right_at(on_parent, false, true), &parent, &child);
    TEST_ASSERT_TRUE(parent.right_clicked);
    TEST_ASSERT_FALSE(parent.clicked || child.right_clicked);
    parent_child_frame(right_at(on_child, true, false), &parent, &child);
    parent_child_frame(right_at(on_child, false, true), &parent, &child);
    TEST_ASSERT_TRUE(child.right_clicked);
    TEST_ASSERT_FALSE_MESSAGE(parent.right_clicked, "the child record takes it");
    parent_child_frame(right_at(on_parent, true, false), &parent, &child);
    nt_pointer_t cancel = right_at(on_parent, false, true);
    cancel.cancelled = true;
    parent_child_frame(cancel, &parent, &child);
    TEST_ASSERT_FALSE_MESSAGE(parent.right_clicked, "a cancelled release clicks nothing");
}

/* The mouse hovers the button and a pen right-clicks it elsewhere: the right click reports the pen and its point. */
static void test_right_click_carries_its_own_pointer(void) {
    const float mouse_x = BTN_X + (BTN_W * 0.6F);
    const float pen_x = BTN_X + (BTN_W * 0.9F);
    nt_pointer_t ps[2] = {ui_fx_pointer(mouse_x, BTN_CY, false, false, false), {.x = pen_x, .y = BTN_CY, .active = true, .id = 7U, .type = NT_POINTER_PEN}};
    nt_ui_interaction_t parent = {0};
    for (int f = 0; f < 3; ++f) {
        ps[1].buttons[NT_BUTTON_RIGHT] = (nt_button_state_t){.is_down = f == 1, .is_pressed = f == 1, .is_released = f == 2};
        nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .pointers = ps, .pointer_count = 2});
        declare_btn_element();
        parent = nt_ui_step(s_fx.ctx, nt_ui_id("btn"), NULL);
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_TRUE(parent.right_clicked);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(7U, parent.pointer_id, "the pen right-clicked");
    TEST_ASSERT_EQUAL_INT((int)pen_x, (int)parent.pos[0]);
    TEST_ASSERT_EQUAL_INT((int)pen_x, (int)parent.press_pos[0]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_interaction_click_on_release_within_bounds);
    RUN_TEST(test_interaction_hover_then_pressed);
    RUN_TEST(test_interaction_inactive_pointer_no_hover);
    RUN_TEST(test_interaction_release_outside_cancels);
    RUN_TEST(test_interaction_disabled_skips);
    RUN_TEST(test_interaction_wants_pointer);
    RUN_TEST(test_wants_pointer_is_per_pointer);
    RUN_TEST(test_game_gesture_has_no_target);
    RUN_TEST(test_joined_press_is_no_press);
    RUN_TEST(test_middle_press_starts_a_gesture);
    RUN_TEST(test_held_unseen_press_starts_a_gesture_without_an_edge);
    RUN_TEST(test_contact_change_starts_a_new_gesture);
    RUN_TEST(test_occlusion_zero_hides_a_2d_pointer);
    RUN_TEST(test_holder_under_a_later_record_is_not_hovered);
    RUN_TEST(test_dropped_capture_reports_lost_once);
    RUN_TEST(test_right_click_goes_to_the_target);
    RUN_TEST(test_right_click_carries_its_own_pointer);
    RUN_TEST(test_interaction_second_button_keeps_the_gesture);
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
    RUN_TEST(test_disabled_widget_blocks_input_behind);
    RUN_TEST(test_press_drag_off_then_back_clicks);
    RUN_TEST(test_multitouch_two_widgets_independent_capture);
    return UNITY_END();
}
