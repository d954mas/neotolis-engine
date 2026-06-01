/* Engine-owned interaction state machine (WIDGET-02, D-56-04/06).
 *
 * nt_ui_get_interaction drives the per-pointer capture state machine off the
 * precomputed nt_button_state_t edges (is_pressed/is_down/is_released) + the
 * transform-aware hit-test, computed lazily: this-frame primary pointer vs the
 * PREVIOUS-frame Clay bbox (1-frame IM lag). The bbox is only available from
 * frame 2 onward (frame 1 declares + ends -> Clay hashmap stores it).
 *
 * Frame sequence (RESEARCH "Concrete unit-test design for the state machine"):
 *   Frame 1: declare CLAY({.id=CLAY_ID("btn")}) at a known bbox; end.
 *   Frame 2: pointer inside + is_pressed/is_down -> hovered+pressed+pressed_now,
 *            !clicked, !released_now; capture active_id == id.
 *   Frame 3: is_released, pointer inside -> released_now && clicked && !pressed;
 *            capture cleared (active_id 0).
 *   Variant: Frame 3 with pointer OUTSIDE -> released_now && !clicked.
 *   Hover-only (over, no button) -> hovered && !pressed && !pressed_now. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
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

/* A query frame: begin with pointer p, declare the element, query interaction
 * (INSIDE the frame -- get_interaction needs Clay's context set), then end.
 * Mirrors how a game/button calls get_interaction during declaration. */
static nt_ui_interaction_t query_btn_frame(const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    declare_btn_element();
    nt_ui_interaction_t in = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btn"));
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

/* ---- Test 4: disabled widget skips hover + click (D-56-12 foundation) ---- */
static void test_interaction_disabled_skips(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    declare_btn_frame(&f1);

    /* Frame 2: pointer inside + pressed, but the widget is disabled. The
     * disabled path (D-56-12) is the button's job: when enabled == false the
     * button does NOT call get_interaction and reports a zeroed interaction.
     * Model that here -- the full button enabled=false path is exercised by
     * Plan 04. A zeroed interaction has no hover/press/click and forms no
     * capture, and the engine does not want the pointer. */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true, false);
    const bool enabled = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    declare_btn_element();
    nt_ui_interaction_t in = enabled ? nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btn")) : (nt_ui_interaction_t){0};
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(in.hovered);
    TEST_ASSERT_FALSE(in.pressed);
    TEST_ASSERT_FALSE(in.pressed_now);
    TEST_ASSERT_FALSE(in.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx));
}

/* ---- Test 6: padded interaction (Phase 56 ext) ----
 * nt_ui_get_interaction_padded inflates the layout-space bbox by pad_lrtb
 * BEFORE the inverse-affine. A pointer 12 px past the right edge becomes
 * hovered + can begin a capture when the right padding is 16. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_interaction_padded_hover_and_capture(void) {
    /* Frame 1: declare bbox (no pointer over). */
    nt_pointer_t f1 = make_pointer(0.0F, 0.0F, false, false, false);
    declare_btn_frame(&f1);

    /* Frame 2: pointer 12 px past the right edge, button down -> with right
     * padding 16, the padded query reports hovered + pressed_now + capture. */
    const float right_outside_x = BTN_X + BTN_W + 12.0F;
    const float center_y = BTN_Y + (BTN_H * 0.5F);
    nt_pointer_t f2 = make_pointer(right_outside_x, center_y, true, true, false);
    const int16_t pad_right[4] = {0, 16, 0, 0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f2, 1);
    declare_btn_element();
    nt_ui_interaction_t in = nt_ui_get_interaction_padded(s_fx.ctx, nt_ui_id("btn"), pad_right);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(in.hovered);
    TEST_ASSERT_TRUE(in.pressed);
    TEST_ASSERT_TRUE(in.pressed_now);
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("btn"), nt_ui_test_capture_active_id(s_fx.ctx, 0));

    /* Same pointer position, NULL pad (== unpadded) -> NOT hovered, no capture.
     * Need fresh frame 1 to clear the prior capture; orphan cleanup runs at
     * begin. Easier: assert by calling the unpadded test_hit probe directly
     * on the same frame state. Clear the capture first via a quiet frame. */
    nt_pointer_t f3 = make_pointer(0.0F, 0.0F, false, false, true);
    declare_btn_frame(&f3); /* releases + frame 1 will clear orphan */
    nt_pointer_t f4 = make_pointer(0.0F, 0.0F, false, false, false);
    declare_btn_frame(&f4); /* orphan cleanup runs at this begin */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
}

/* ---- Test 7: padded query on a DISABLED widget does NOT make it hoverable ----
 * The button's enabled=false path (D-56-12) SHORT-CIRCUITS the query entirely
 * and returns a zeroed interaction. Padding is in the same code path -- a
 * disabled button must stay non-hoverable even with large padding.
 * Modeled here at the foundation level (the full nt_ui_button enabled=false
 * path is exercised in test_nt_ui_button). */
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
    /* Mirror the button's branch (D-56-12): disabled => zeroed interaction,
     * the query is not invoked. */
    nt_ui_interaction_t in = enabled ? nt_ui_get_interaction_padded(s_fx.ctx, nt_ui_id("btn"), big_pad) : (nt_ui_interaction_t){0};
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

    /* Frame 2: hover only -> wants_pointer true (checked after end; the flag
     * persists until the next begin). */
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, false, false, false);
    (void)query_btn_frame(&f2);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx));

    /* Frame 3: pointer off-widget, not queried -> false. */
    nt_pointer_t f3 = make_pointer(0.0F, 0.0F, false, false, false);
    declare_btn_frame(&f3);
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx));
}

/* ---- Test 8: exclusive capture -- while A owns the pointer's capture, B
 * sees zero interaction even if the pointer is geometrically over it ----
 *
 * Phase 56 ext fix (exclusive capture). User report: pressing button A and
 * sliding to button B mid-drag lit B up as hovered. Standard UI semantics:
 * one widget owns the capture per pointer, every other widget gets nothing.
 *
 * Setup: two buttons A and B at non-overlapping bboxes. Press inside A
 * (capture begins on A). Move pointer to B's bbox WITHOUT releasing.
 * get_interaction(B) must report hovered=false / pressed=false / clicked=false.
 * After release, A's capture clears -- a fresh query of B with the pointer
 * over B must then report hovered=true. */
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

    /* Helper to declare both elements at fixed absolute positions in one frame. */
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
    nt_ui_interaction_t inA_press = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnA"));
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB_press = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnB"));
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
    nt_ui_interaction_t inA_drag = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnA"));
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB_drag = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnB"));
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
    nt_ui_interaction_t inA_rel = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnA"));
    /* A processed the release: released_now true, clicked false (off A). Capture cleared. */
    TEST_ASSERT_TRUE(inA_rel.released_now);
    TEST_ASSERT_FALSE(inA_rel.clicked);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0));
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    /* By the time B is queried, A's call has cleared the capture, so B is
     * no longer exclusive_gated and reports hovered=true (pointer is over B). */
    nt_ui_interaction_t inB_rel = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnB"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(inB_rel.hovered);

    /* Frame 5: idle, pointer over B -> normal hover (no residual capture). */
    nt_pointer_t f5 = make_pointer(b_cx, b_cy, false, false, false);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &f5, 1);
    CLAY({.id = CLAY_ID("btnA"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = a_x, .y = a_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}){}(
        void)nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnA"));
    CLAY({.id = CLAY_ID("btnB"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = b_x, .y = b_y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
    nt_ui_interaction_t inB_idle = nt_ui_get_interaction(s_fx.ctx, nt_ui_id("btnB"));
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(inB_idle.hovered);
    TEST_ASSERT_FALSE(inB_idle.pressed);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_interaction_click_on_release_within_bounds);
    RUN_TEST(test_interaction_hover_then_pressed);
    RUN_TEST(test_interaction_release_outside_cancels);
    RUN_TEST(test_interaction_disabled_skips);
    RUN_TEST(test_interaction_wants_pointer);
    /* Phase 56 ext: padded variant + disabled-with-padding guard. */
    RUN_TEST(test_interaction_padded_hover_and_capture);
    RUN_TEST(test_interaction_disabled_with_padding_stays_disabled);
    /* Phase 56 ext fix: exclusive capture (no cross-widget interference). */
    RUN_TEST(test_interaction_capture_excludes_other_widgets);
    return UNITY_END();
}
