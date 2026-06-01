/* Phase 56 ext (REVIEW-2 followup): hit-test clip stack.
 *
 * nt_ui_push_clip / nt_ui_pop_clip pair the visual Clay clip with a hit-test
 * scissor so a click landing outside a clipping ancestor is a MISS even if the
 * widget's transformed bbox covers the point. Walking the clip stack BEFORE
 * the widget inverse-affine matches the visual scissor: ANY clip rejecting the
 * point shorts the test. Mirrors push_transform/pop_transform (explicit > implicit).
 *
 * Test geometry is deliberately asymmetric per AGENTS.md so an axis swap / sign
 * flip in the inverse-affine of the clip entry's accumulated affine would be
 * caught (button bbox 200x100 at non-origin, anisotropic clip rects, off-center
 * pivots for rotation tests). */

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

/* Button geometry shared by axis-aligned tests. Asymmetric on purpose. */
#define BBOX_X 100.0F
#define BBOX_Y 100.0F
#define BBOX_W 200.0F
#define BBOX_H 100.0F

#define SCREEN_W 800.0F
#define SCREEN_H 600.0F

#define DEG2RAD(d) ((d) * 3.14159265358979323846F / 180.0F)

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* Frame 1: declare the asymmetric button bbox so Clay stores it in the
 * persistent hashmap; the next frame's hit-test reads it via prev-frame
 * lookup (D-56-06). FIXED-size + ABSOLUTE floating element so the layout
 * lands exactly where the test expects -- the clip rects are sized against
 * this exact rect. */
static void declare_bbox_frame(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY(
        {.id = CLAY_ID("clipbtn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}}) {
    }
    nt_ui_end(s_fx.ctx);
}

/* Verify the button landed where the test math assumes; if not, every clip
 * probe is meaningless. Tolerant of sub-pixel rounding. */
static void assert_bbox_as_declared(void) {
    nt_ui_bbox_t b = nt_ui_get_bbox(s_fx.ctx, nt_ui_id("clipbtn"));
    TEST_ASSERT_TRUE_MESSAGE(b.found, "bbox not found after frame 1 (Clay hashmap miss)");
    TEST_ASSERT_TRUE(fabsf(b.x - BBOX_X) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(b.y - BBOX_Y) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(b.width - BBOX_W) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(b.height - BBOX_H) <= 0.5F);
}

/* ---- Test 1: axis-aligned button + axis-aligned clip; pointer inside button
 *      bbox but OUTSIDE the clip is a MISS. Proves the clip walk runs before
 *      the widget inverse-affine and short-circuits the hit. ---- */
static void test_clip_axis_aligned_misses_outside_clip(void) {
    declare_bbox_frame();

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    assert_bbox_as_declared();
    const uint32_t id = nt_ui_id("clipbtn");

    /* Clip covers x in [0, 200], y in [0, 200]. Button covers x in [100, 300]. */
    nt_ui_push_clip(s_fx.ctx, 0.0F, 0.0F, 200.0F, 200.0F);
    /* (250, 150): inside button (100<=250<=300, 100<=150<=200) but outside
     *             clip (250 > 200 right edge). MUST miss. */
    const bool h = nt_ui_test_hit(s_fx.ctx, id, 250.0F, 150.0F);
    nt_ui_pop_clip(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE_MESSAGE(h, "clipped: inside button bbox but past clip right edge must be MISS");
}

/* ---- Test 2: same setup, pointer inside BOTH clip and button -> HIT. ---- */
static void test_clip_axis_aligned_hits_inside_clip(void) {
    declare_bbox_frame();

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    assert_bbox_as_declared();
    const uint32_t id = nt_ui_id("clipbtn");

    nt_ui_push_clip(s_fx.ctx, 0.0F, 0.0F, 200.0F, 200.0F);
    /* (150, 150): inside both clip (150 < 200) and button (100 <= 150 <= 300). */
    const bool h = nt_ui_test_hit(s_fx.ctx, id, 150.0F, 150.0F);
    nt_ui_pop_clip(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(h, "clipped: inside both clip and button must be HIT");
}

/* ---- Test 3: clip 400x400 at origin + button rotated 30 deg about its
 *      center. Pointer inside both clip and rotated button -> HIT. Pointer
 *      OUTSIDE the clip (even if it would land inside the rotated quad
 *      extending past the clip) -> MISS. Proves the clip walk applies to the
 *      transformed widget case too.
 *
 *      Defensive pattern (applies to all rotated-clip tests): query all
 *      hit-tests BEFORE the TEST_ASSERTs. A failed Unity assertion longjmps
 *      out, leaving the frame mid-flight; tearDown's destroy then asserts
 *      !in_frame and traps the process. ---- */
static void test_clip_with_rotated_button(void) {
    declare_bbox_frame();

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    assert_bbox_as_declared();
    const uint32_t id = nt_ui_id("clipbtn");

    /* Clip 400x400 at origin -- covers the whole button bbox plus margin. */
    nt_ui_push_clip(s_fx.ctx, 0.0F, 0.0F, 400.0F, 400.0F);
    /* Rotate the BUTTON (not the clip) 30 deg about its center. */
    const nt_ui_transform_t t = {.offset_x = 0, .offset_y = 0, .rotation = DEG2RAD(30.0F), .scale_x = 1.0F, .scale_y = 1.0F};
    nt_ui_push_transform(s_fx.ctx, &t);

    /* (200, 150): center of the button, inside clip + inside rotated bbox. HIT. */
    const bool h_center = nt_ui_test_hit(s_fx.ctx, id, 200.0F, 150.0F);
    /* (420, 250): outside clip (420 > 400). Clip walk must reject BEFORE the
     * widget inverse-affine even runs. */
    const bool h_past_clip = nt_ui_test_hit(s_fx.ctx, id, 420.0F, 250.0F);

    nt_ui_pop_transform(s_fx.ctx);
    nt_ui_pop_clip(s_fx.ctx);
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_TRUE_MESSAGE(h_center, "(rotated button) center inside both clip and rotated bbox must be HIT");
    TEST_ASSERT_FALSE_MESSAGE(h_past_clip, "(rotated button) probe past clip right edge must be MISS regardless of rotated bbox extent");
}

/* ---- Test 4: ROTATED CLIP PARENT.
 *      push_transform rotation 45 deg, then push_clip 200x200 at (100,100)
 *      INSIDE the rotated frame. Button (declared axis-aligned in F1) does NOT
 *      participate in the rotation here (no transform around it at hit-test
 *      time -- pop_transform happens before the probe). A pointer that lands
 *      in the AXIS-ALIGNED clip rect (100..300, 100..300) but OUTSIDE the
 *      rotated quad must MISS. Proves the clip stores the affine accum from
 *      push time and applies the inverse to incoming pointers. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_clip_rotated_parent(void) {
    /* Re-declare the button BIG enough to cover the rotated-clip diamond corners
     * (which extend to screen x = 341.4 / y = 341.4 with a 200x200 clip at
     * (100,100) rotated 45 deg about center (200,200)). Otherwise the button's
     * own axis-aligned bbox limits the test, not the clip. */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("clipbtn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 0.0F, .y = 0.0F}}, .layout = {.sizing = {CLAY_SIZING_FIXED(500.0F), CLAY_SIZING_FIXED(500.0F)}}}) {}
    nt_ui_end(s_fx.ctx);

    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    const uint32_t id = nt_ui_id("clipbtn");

    /* Rotate 45 deg. push_clip captures the affine AT THE CLIP CENTER (200,200).
     * The clip is a 200x200 square; rotated 45 deg about (200,200) it becomes a
     * diamond with screen-space corners at (200, 58.6), (341.4, 200), (200,
     * 341.4), (58.6, 200) -- the AABB extends FARTHER along axes than the
     * original square, not closer (the diagonal is now axis-aligned). */
    const nt_ui_transform_t rot45 = {.offset_x = 0, .offset_y = 0, .rotation = DEG2RAD(45.0F), .scale_x = 1.0F, .scale_y = 1.0F};
    nt_ui_push_transform(s_fx.ctx, &rot45);
    nt_ui_push_clip(s_fx.ctx, 100.0F, 100.0F, 200.0F, 200.0F);
    /* Pop transform now -- the clip captured its affine; the widget itself stays
     * axis-aligned (we want only the clip to rotate). */
    nt_ui_pop_transform(s_fx.ctx);

    /* Query all hit-tests BEFORE asserting -- a failed Unity assertion longjmps
     * out of the test body, leaving the frame mid-flight. tearDown's destroy
     * then asserts !in_frame and crashes the process. Pop + end FIRST, assert
     * LAST. */
    const bool h_center = nt_ui_test_hit(s_fx.ctx, id, 200.0F, 200.0F);
    /* (350, 350): clearly outside the diamond (the diamond's corners reach only
     * 341.4 along each axis, and (350, 350) is past the diagonal corner at
     * (341.4, 341.4)-ish). Inverse-transformed: lands at local x > 300, outside
     * clip rect [100, 300]. */
    const bool h_outside = nt_ui_test_hit(s_fx.ctx, id, 350.0F, 350.0F);

    nt_ui_pop_clip(s_fx.ctx);
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_TRUE_MESSAGE(h_center, "(rotated clip) center of rotated clip + inside button must be HIT");
    TEST_ASSERT_FALSE_MESSAGE(h_outside, "(rotated clip) probe past rotated diamond corner must be MISS (proves inverse-affine ran)");
}

/* ---- Test 5: NESTED CLIPS, intersect semantics.
 *      Outer clip (0,0,300,300), inner clip (50,50,100,100) -- their
 *      intersection is (50,50,100,100). A point (200,200) sits inside the outer
 *      but outside the inner -> MUST MISS. Proves ALL clips on the stack are
 *      walked, not just the top. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_nested_clips_intersect(void) {
    /* Re-declare the button at a larger size so its bbox spans the whole outer
     * clip (otherwise the button's own bbox limits the test). */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("clipbtn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 0.0F, .y = 0.0F}}, .layout = {.sizing = {CLAY_SIZING_FIXED(300.0F), CLAY_SIZING_FIXED(300.0F)}}}) {}
    nt_ui_end(s_fx.ctx);

    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    const uint32_t id = nt_ui_id("clipbtn");
    nt_ui_bbox_t b = nt_ui_get_bbox(s_fx.ctx, id);
    TEST_ASSERT_TRUE(b.found);
    TEST_ASSERT_TRUE(fabsf(b.width - 300.0F) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(b.height - 300.0F) <= 0.5F);

    /* Outer clip 300x300 -- entire button is inside it. */
    nt_ui_push_clip(s_fx.ctx, 0.0F, 0.0F, 300.0F, 300.0F);
    /* Inner clip 100x100 at (50,50). */
    nt_ui_push_clip(s_fx.ctx, 50.0F, 50.0F, 100.0F, 100.0F);

    /* (75, 75): inside both clips and the button -> HIT. */
    const bool h_in_both = nt_ui_test_hit(s_fx.ctx, id, 75.0F, 75.0F);
    /* (200, 200): inside outer + button, outside inner (200 > 50+100=150). MISS. */
    const bool h_in_outer_only = nt_ui_test_hit(s_fx.ctx, id, 200.0F, 200.0F);
    /* (10, 10): inside outer + button, outside inner (10 < 50). MISS via inner. */
    const bool h_above_inner = nt_ui_test_hit(s_fx.ctx, id, 10.0F, 10.0F);

    nt_ui_pop_clip(s_fx.ctx);
    nt_ui_pop_clip(s_fx.ctx);
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_TRUE_MESSAGE(h_in_both, "nested clips: inside both must be HIT");
    TEST_ASSERT_FALSE_MESSAGE(h_in_outer_only, "nested clips: inside outer but outside inner must be MISS");
    TEST_ASSERT_FALSE_MESSAGE(h_above_inner, "nested clips: above-left of inner clip must be MISS");
}

/* ---- Test 6a: underflow assert -- pop_clip without a matching push.
 *      After the trap, clip_depth is still 0 (assert fires BEFORE decrement),
 *      so nt_ui_end's balance assert passes and tearDown's destroy_context is
 *      safe. ---- */
static void test_clip_stack_pop_underflow_asserts(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    NT_TEST_EXPECT_ASSERT(nt_ui_pop_clip(s_fx.ctx));
    /* clip_depth still 0 after the trap -> end's balance assert passes. */
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 6b: overflow assert -- push beyond NT_UI_CLIP_STACK_CAP.
 *      After the trap, clip_depth = CAP (push asserts BEFORE increment), so we
 *      manually pop CAP times to satisfy nt_ui_end's balance assert. ---- */
static void test_clip_stack_push_overflow_asserts(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    for (uint32_t k = 0; k < NT_UI_CLIP_STACK_CAP; ++k) {
        nt_ui_push_clip(s_fx.ctx, 0.0F, 0.0F, 100.0F, 100.0F);
    }
    NT_TEST_EXPECT_ASSERT(nt_ui_push_clip(s_fx.ctx, 0.0F, 0.0F, 100.0F, 100.0F));
    /* Pop the CAP pushes that DID succeed so end's balance assert passes. */
    for (uint32_t k = 0; k < NT_UI_CLIP_STACK_CAP; ++k) {
        nt_ui_pop_clip(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 7: clip stack resets each nt_ui_begin.
 *      Frame F1 declares the bbox; F2 pushes a clip that would block the
 *      (250,150) probe and pops it; F3 begins with NO push_clip and the same
 *      probe must HIT -- proving the depth is reset to 0. ---- */
static void test_clip_stack_reset_each_begin(void) {
    declare_bbox_frame();

    nt_pointer_t mouse = {0};

    /* F2: push + immediate pop + balanced end. */
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    assert_bbox_as_declared();
    const uint32_t id = nt_ui_id("clipbtn");
    nt_ui_push_clip(s_fx.ctx, 0.0F, 0.0F, 200.0F, 200.0F);
    const bool h_clipped = nt_ui_test_hit(s_fx.ctx, id, 250.0F, 150.0F);
    nt_ui_pop_clip(s_fx.ctx);
    const bool h_after_pop = nt_ui_test_hit(s_fx.ctx, id, 250.0F, 150.0F);
    nt_ui_end(s_fx.ctx);

    /* F3: no clip at all. */
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    const bool h_new_frame = nt_ui_test_hit(s_fx.ctx, id, 250.0F, 150.0F);
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_FALSE_MESSAGE(h_clipped, "during clip: probe outside clip must MISS");
    TEST_ASSERT_TRUE_MESSAGE(h_after_pop, "after pop_clip: probe inside button must HIT (clip removed)");
    TEST_ASSERT_TRUE_MESSAGE(h_new_frame, "new frame with no clip: probe inside button must HIT (clip stack reset to 0)");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_clip_axis_aligned_misses_outside_clip);
    RUN_TEST(test_clip_axis_aligned_hits_inside_clip);
    RUN_TEST(test_clip_with_rotated_button);
    RUN_TEST(test_clip_rotated_parent);
    RUN_TEST(test_nested_clips_intersect);
    RUN_TEST(test_clip_stack_pop_underflow_asserts);
    RUN_TEST(test_clip_stack_push_overflow_asserts);
    RUN_TEST(test_clip_stack_reset_each_begin);
    return UNITY_END();
}
