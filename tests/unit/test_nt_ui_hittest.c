/* Transform-aware hit-test (D-56-07, supersedes D-54-08).
 *
 * The hit-test inverse-transforms the pointer by the declaration-time
 * accumulated transform (Option A, ctx-resident accum stack maintained by
 * push/pop_transform), then point-in-rect against Clay's stable layout bbox
 * (prev-frame, via Clay_GetElementData). The test data is asymmetric per
 * AGENTS.md ("data that breaks on axis swap or flip"):
 *   - NON-square bbox 200x60 at a NON-origin position (x=150, y=80).
 *   - ASYMMETRIC transform: rotation = 30 deg, scale_x=1.2 != scale_y=0.8,
 *     offset_x=10 != offset_y=-25 (anisotropic scale + offset + rotation so an
 *     axis swap / sign flip is caught).
 *   - 4 probe points:
 *       (a) center                              -> inside
 *       (b) just-inside a rotated corner        -> inside
 *       (c) just-outside that corner            -> outside
 *       (d) inside the AXIS-ALIGNED bbox but
 *           outside the ROTATED one             -> outside  (proves inverse-affine)
 *   Hit-test stays in Clay Y-DOWN space with NON-negated rotation
 *   (the walker's Y-flip / -rotation are render-only -- do NOT copy them). */

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

/* Asymmetric probe geometry (see file header). */
#define BBOX_X 150.0F
#define BBOX_Y 80.0F
#define BBOX_W 200.0F
#define BBOX_H 60.0F
#define ROT_DEG 30.0F
#define ROT_RAD (ROT_DEG * 3.14159265358979323846F / 180.0F)
#define SCALE_X 1.2F
#define SCALE_Y 0.8F
#define OFFSET_X 10.0F
#define OFFSET_Y (-25.0F)

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* Forward transform of a layout-space point through ONE transform level about
 * center C, in Clay Y-down space (mirrors walker_recompute_transform's per-level
 * math, NON-negated rotation, NO Y-flip). Used to MANUFACTURE probe points so
 * the test is independent of the engine's inverse implementation. */
static void forward_xform(float x, float y, float cx, float cy, float *out_x, float *out_y) {
    const float sx = SCALE_X;
    const float sy = SCALE_Y;
    const float cr = cosf(ROT_RAD);
    const float sr = sinf(ROT_RAD);
    const float la = cr * sx;
    const float lb = -(sr * sy);
    const float lc = sr * sx;
    const float ld = cr * sy;
    const float ltx = cx - (la * cx) - (lb * cy) + OFFSET_X;
    const float lty = cy - (lc * cx) - (ld * cy) + OFFSET_Y;
    *out_x = (x * la) + (y * lb) + ltx;
    *out_y = (x * lc) + (y * ld) + lty;
}

/* Frame 1: declare the asymmetric bbox so Clay stores it in its persistent
 * hashmap. The id string is hashed into a uint32 via nt_ui_id. The bbox is
 * forced to (150,80,200,60) with a FIXED-size, ABSOLUTE-positioned floating
 * element so the layout solver lands it exactly where the test expects. */
static void declare_bbox_frame(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("htbtn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}}}) {}
    nt_ui_end(s_fx.ctx);
}

/* Verify the bbox landed where the test math assumes; otherwise every probe is
 * meaningless. MUST be called inside begin/end (Clay context set). Tolerant of
 * sub-pixel layout rounding. */
static void assert_bbox_as_declared(void) {
    nt_ui_bbox_t b = nt_ui_get_bbox(s_fx.ctx, nt_ui_id("htbtn"));
    TEST_ASSERT_TRUE_MESSAGE(b.found, "bbox not found after frame 1 (Clay hashmap miss)");
    TEST_ASSERT_TRUE(fabsf(b.x - BBOX_X) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(b.y - BBOX_Y) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(b.width - BBOX_W) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(b.height - BBOX_H) <= 0.5F);
}

/* ---- Test 1: untransformed AABB hit (baseline before transforms) ---- */
static void test_hittest_axis_aligned_baseline(void) {
    declare_bbox_frame();

    /* Frame 2: no transform pushed -> hit-test is a plain point-in-rect. */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    assert_bbox_as_declared();
    const uint32_t id = nt_ui_id("htbtn");
    /* center inside */
    TEST_ASSERT_TRUE(nt_ui_test_hit(s_fx.ctx, id, BBOX_X + (BBOX_W * 0.5F), BBOX_Y + (BBOX_H * 0.5F)));
    /* far corner outside */
    TEST_ASSERT_FALSE(nt_ui_test_hit(s_fx.ctx, id, BBOX_X - 5.0F, BBOX_Y - 5.0F));
    /* just inside each edge */
    TEST_ASSERT_TRUE(nt_ui_test_hit(s_fx.ctx, id, BBOX_X + 1.0F, BBOX_Y + 1.0F));
    TEST_ASSERT_TRUE(nt_ui_test_hit(s_fx.ctx, id, BBOX_X + BBOX_W - 1.0F, BBOX_Y + BBOX_H - 1.0F));
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 2: rotated + offset + anisotropic-scale, 4 asymmetric probes ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_hittest_rotated_asymmetric_probes(void) {
    declare_bbox_frame();

    const float cx = BBOX_X + (BBOX_W * 0.5F); /* 250 */
    const float cy = BBOX_Y + (BBOX_H * 0.5F); /* 110 */

    /* Frame 2: push the asymmetric transform around the bbox, then probe. */
    nt_ui_transform_t t = {.offset_x = OFFSET_X, .offset_y = OFFSET_Y, .rotation = ROT_RAD, .scale_x = SCALE_X, .scale_y = SCALE_Y};
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    assert_bbox_as_declared();
    nt_ui_push_transform(s_fx.ctx, &t);
    const uint32_t id = nt_ui_id("htbtn");

    /* (a) center of the widget, forward-transformed -> must be inside. */
    float pax = 0;
    float pay = 0;
    forward_xform(cx, cy, cx, cy, &pax, &pay);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, pax, pay), "(a) transformed center must be inside");

    /* (b) just-inside a corner: layout corner pulled 4px toward center, then
     *     forward-transformed -> must be inside the rotated quad. */
    float pbx = 0;
    float pby = 0;
    forward_xform(BBOX_X + 4.0F, BBOX_Y + 4.0F, cx, cy, &pbx, &pby);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, pbx, pby), "(b) just-inside corner must be inside");

    /* (c) just-outside that same corner: layout corner pushed 8px past the
     *     edge, forward-transformed -> must be outside. */
    float pcx = 0;
    float pcy = 0;
    forward_xform(BBOX_X - 8.0F, BBOX_Y - 8.0F, cx, cy, &pcx, &pcy);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, pcx, pcy), "(c) just-outside corner must be outside");

    /* (d) a point inside the AXIS-ALIGNED bbox but outside the ROTATED one.
     *     The untransformed layout corner (150,80) is inside the AABB; once
     *     the widget is rotated 30deg + anisotropically scaled + offset, the
     *     raw layout corner location is no longer covered by the rotated quad.
     *     A plain AABB test on the raw point would (wrongly) say inside; the
     *     inverse-affine must say OUTSIDE. */
    TEST_ASSERT_TRUE_MESSAGE(BBOX_X >= BBOX_X && BBOX_X <= BBOX_X + BBOX_W && BBOX_Y >= BBOX_Y && BBOX_Y <= BBOX_Y + BBOX_H, "(d) probe must be inside the axis-aligned bbox");
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, BBOX_X, BBOX_Y), "(d) inside-AABB / outside-rotated must be outside (proves inverse-affine ran, not plain AABB)");

    nt_ui_pop_transform(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 4: padded hit-test, asymmetric padding (touch-target inflation) ----
 * Phase 56 ext: nt_ui_test_hit_padded inflates the LAYOUT-space bbox by
 * pad_lrtb = {left, right, top, bottom} BEFORE the inverse-affine. With no
 * transform pushed, a point that sits 12 px OUTSIDE the bbox to the right
 * is OUTSIDE the unpadded hit zone but INSIDE the {0,16,0,0} padded one. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_hittest_padded_asymmetric(void) {
    declare_bbox_frame();

    /* Frame 2: no transform, asymmetric pad. */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    assert_bbox_as_declared();
    const uint32_t id = nt_ui_id("htbtn");

    const float right_outside_x = BBOX_X + BBOX_W + 12.0F;
    const float center_y = BBOX_Y + (BBOX_H * 0.5F);
    /* Unpadded: 12 px past the right edge -> outside. */
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, right_outside_x, center_y), "12 px right of bbox must be outside the unpadded hit zone");
    /* Padded right=16: 12 px past the right edge -> inside. */
    const int16_t pad_right[4] = {0, 16, 0, 0};
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, right_outside_x, center_y, pad_right), "12 px right of bbox must be inside with right padding 16");
    /* Asymmetric: pad_left=0 -> 12 px LEFT of bbox stays outside even when right padding is large. */
    const float left_outside_x = BBOX_X - 12.0F;
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, left_outside_x, center_y, pad_right), "12 px left of bbox must stay outside (left padding is 0)");
    /* {left=16}: now LEFT side becomes inside. Right side test recomputed below. */
    const int16_t pad_left[4] = {16, 0, 0, 0};
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, left_outside_x, center_y, pad_left), "12 px left of bbox must be inside with left padding 16");
    /* {top=8, bottom=0}: 4 px ABOVE bbox -> inside; 4 px BELOW -> outside. */
    const float center_x = BBOX_X + (BBOX_W * 0.5F);
    const int16_t pad_top[4] = {0, 0, 8, 0};
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, center_x, BBOX_Y - 4.0F, pad_top), "4 px above bbox must be inside with top padding 8");
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, center_x, BBOX_Y + BBOX_H + 4.0F, pad_top), "4 px below bbox must stay outside (bottom padding 0)");
    /* NULL pad_lrtb == unpadded. */
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, right_outside_x, center_y, NULL), "NULL pad_lrtb must behave like unpadded (12 px right -> outside)");

    nt_ui_end(s_fx.ctx);
}

/* ---- Test 5: padded hit-test + rotation 30 deg ---- */
/* Padding rotates with the widget (it lives in LAYOUT space, before the
 * inverse-affine). A point on the rotated-RIGHT normal that lands 12 px past
 * the rotated right edge is OUTSIDE unpadded but INSIDE with {0,16,0,0}. */
static void test_hittest_padded_with_rotation(void) {
    declare_bbox_frame();

    const float cx = BBOX_X + (BBOX_W * 0.5F);
    const float cy = BBOX_Y + (BBOX_H * 0.5F);

    nt_ui_transform_t t = {.offset_x = 0, .offset_y = 0, .rotation = ROT_RAD, .scale_x = 1.0F, .scale_y = 1.0F};
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    assert_bbox_as_declared();
    nt_ui_push_transform(s_fx.ctx, &t);
    const uint32_t id = nt_ui_id("htbtn");

    /* Forward-transform a layout-space point 12 px past the RIGHT edge along
     * the y=center axis. Unpadded: outside; padded right=16: inside. Use the
     * file-local forward_xform helper but with the test's rotation only
     * (scale=1, offset=0) to isolate the rotation case. */
    const float layout_x = BBOX_X + BBOX_W + 12.0F;
    const float layout_y = cy;
    /* Forward through rotation-only level (offset=0, scale=1). About center C. */
    const float cr = cosf(ROT_RAD);
    const float sr = sinf(ROT_RAD);
    const float dx = layout_x - cx;
    const float dy = layout_y - cy;
    const float world_x = cx + (cr * dx) - (sr * dy);
    const float world_y = cy + (sr * dx) + (cr * dy);

    /* Unpadded -> outside (point is past the rotated right edge). */
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, world_x, world_y), "rotated: 12 px past right must be outside unpadded");
    /* Padded right=16 -> inside (padding rotates with the widget). */
    const int16_t pad_right[4] = {0, 16, 0, 0};
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, world_x, world_y, pad_right), "rotated: 12 px past right must be inside with right padding 16 (padding rotates with widget)");

    nt_ui_pop_transform(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 6: zero-padding {0,0,0,0} is identical to unpadded ---- */
static void test_hittest_padded_zero_equals_unpadded(void) {
    declare_bbox_frame();

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    assert_bbox_as_declared();
    const uint32_t id = nt_ui_id("htbtn");

    /* Several probes: zero padding must match the unpadded API exactly. */
    const float cx = BBOX_X + (BBOX_W * 0.5F);
    const float cy = BBOX_Y + (BBOX_H * 0.5F);
    const int16_t zero_pad[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(nt_ui_test_hit_padded(s_fx.ctx, id, cx, cy, zero_pad) == nt_ui_test_hit(s_fx.ctx, id, cx, cy));
    TEST_ASSERT_TRUE(nt_ui_test_hit_padded(s_fx.ctx, id, BBOX_X - 5.0F, BBOX_Y - 5.0F, zero_pad) == nt_ui_test_hit(s_fx.ctx, id, BBOX_X - 5.0F, BBOX_Y - 5.0F));
    TEST_ASSERT_TRUE(nt_ui_test_hit_padded(s_fx.ctx, id, BBOX_X + 1.0F, BBOX_Y + 1.0F, zero_pad) == nt_ui_test_hit(s_fx.ctx, id, BBOX_X + 1.0F, BBOX_Y + 1.0F));

    nt_ui_end(s_fx.ctx);
}

/* ---- Test 3: hit-test stays in Clay Y-down space (no walker Y-flip) ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_hittest_no_render_y_flip(void) {
    declare_bbox_frame();

    /* offset_y-only transform: shifts the widget DOWN in Clay Y-down by +40.
     * A point 30px below the original bottom edge becomes covered (it now sits
     * inside the shifted box); a point 30px above the original top edge does
     * NOT. If the hit-test mistakenly applied the walker's GL Y-up flip, the
     * sign of the y-shift would invert and these two asserts would swap. */
    nt_ui_transform_t t = {.offset_x = 0, .offset_y = 40.0F, .rotation = 0, .scale_x = 1.0F, .scale_y = 1.0F};
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    assert_bbox_as_declared();
    nt_ui_push_transform(s_fx.ctx, &t);
    const uint32_t id = nt_ui_id("htbtn");

    const float midx = BBOX_X + (BBOX_W * 0.5F);
    /* 20px below original bottom edge -> inside the down-shifted box. */
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, midx, BBOX_Y + BBOX_H + 20.0F), "down-shift: below original bottom must be inside (Clay Y-down)");
    /* 20px above original top edge -> outside (box moved down, not up). */
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, midx, BBOX_Y - 20.0F), "down-shift: above original top must be outside (no GL Y-up flip)");

    nt_ui_pop_transform(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hittest_axis_aligned_baseline);
    RUN_TEST(test_hittest_rotated_asymmetric_probes);
    RUN_TEST(test_hittest_no_render_y_flip);
    /* Phase 56 ext: touch-target padding (nt_ui_get_interaction_padded). */
    RUN_TEST(test_hittest_padded_asymmetric);
    RUN_TEST(test_hittest_padded_with_rotation);
    RUN_TEST(test_hittest_padded_zero_equals_unpadded);
    return UNITY_END();
}
