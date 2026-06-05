/* Transform-aware hit-test.
 *
 * Test pattern: declare bbox with userData in frame 1; frame 2 probes hit-test
 * against PREV-frame hit_baked.
 *
 * Asymmetric data per AGENTS.md: NON-square bbox 200x60 at NON-origin, rotation
 * 30°, anisotropic scale (1.2 != 0.8), offset (10 != -25). Probes hit/miss both
 * the rotated quad and the axis-aligned bbox to prove inverse-affine ran. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

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

/* Forward transform mirrors compose_transform_level (Clay Y-down, non-negated rotation).
 * Used to MANUFACTURE probe points so the test is independent of the engine's inverse. */
static void forward_xform(float x, float y, float cx, float cy, float sx, float sy, float rot, float ox, float oy, float *out_x, float *out_y) {
    const float cr = cosf(rot);
    const float sr = sinf(rot);
    const float la = cr * sx;
    const float lb = -(sr * sy);
    const float lc = sr * sx;
    const float ld = cr * sy;
    const float ltx = cx - (la * cx) - (lb * cy) + ox;
    const float lty = cy - (lc * cx) - (ld * cy) + oy;
    *out_x = (x * la) + (y * lb) + ltx;
    *out_y = (x * lc) + (y * ld) + lty;
}

/* Declare the bbox WITH a transform attached via userData (NULL = identity). */
static void declare_bbox_with_xform(const nt_ui_transform_t *t) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    void *ud = (t != NULL) ? (void *)NT_UI_DATA_XFORM(0U, t, 1.0F) : NULL;
    CLAY({.id = CLAY_ID("htbtn"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}},
          .userData = ud}) {}
    nt_ui_end(s_fx.ctx);
}

/* Sanity-check Clay landed the bbox where the math expects. Tolerant of sub-px rounding. */
static void assert_bbox_as_declared(void) {
    nt_ui_bbox_t b = nt_ui_get_bbox(s_fx.ctx, nt_ui_id("htbtn"));
    TEST_ASSERT_TRUE_MESSAGE(b.found, "bbox not found after frame 1 (Clay hashmap miss)");
    TEST_ASSERT_TRUE(fabsf(b.x - BBOX_X) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(b.y - BBOX_Y) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(b.width - BBOX_W) <= 0.5F);
    TEST_ASSERT_TRUE(fabsf(b.height - BBOX_H) <= 0.5F);
}

/* Begin a probe frame (no declaration — hit-test reads PREV frame's tree_baked). */
static void begin_probe_frame(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    assert_bbox_as_declared();
}

static void end_probe_frame(void) { nt_ui_end(s_fx.ctx); }

/* ---- Test 1: untransformed AABB hit ---- */
static void test_hittest_axis_aligned_baseline(void) {
    declare_bbox_with_xform(NULL);

    begin_probe_frame();
    const uint32_t id = nt_ui_id("htbtn");
    TEST_ASSERT_TRUE(nt_ui_test_hit(s_fx.ctx, id, BBOX_X + (BBOX_W * 0.5F), BBOX_Y + (BBOX_H * 0.5F)));
    TEST_ASSERT_FALSE(nt_ui_test_hit(s_fx.ctx, id, BBOX_X - 5.0F, BBOX_Y - 5.0F));
    TEST_ASSERT_TRUE(nt_ui_test_hit(s_fx.ctx, id, BBOX_X + 1.0F, BBOX_Y + 1.0F));
    TEST_ASSERT_TRUE(nt_ui_test_hit(s_fx.ctx, id, BBOX_X + BBOX_W - 1.0F, BBOX_Y + BBOX_H - 1.0F));
    end_probe_frame();
}

/* ---- Test 2: rotated + offset + anisotropic scale ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_hittest_rotated_asymmetric_probes(void) {
    nt_ui_transform_t t = {.offset_x = OFFSET_X, .offset_y = OFFSET_Y, .rotation_z = ROT_RAD, .scale_x = SCALE_X, .scale_y = SCALE_Y, .scale_z = 1.0F};
    declare_bbox_with_xform(&t);

    const float cx = BBOX_X + (BBOX_W * 0.5F);
    const float cy = BBOX_Y + (BBOX_H * 0.5F);

    begin_probe_frame();
    const uint32_t id = nt_ui_id("htbtn");

    float pax;
    float pay;
    forward_xform(cx, cy, cx, cy, SCALE_X, SCALE_Y, ROT_RAD, OFFSET_X, OFFSET_Y, &pax, &pay);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, pax, pay), "(a) transformed center must be inside");

    float pbx;
    float pby;
    forward_xform(BBOX_X + 4.0F, BBOX_Y + 4.0F, cx, cy, SCALE_X, SCALE_Y, ROT_RAD, OFFSET_X, OFFSET_Y, &pbx, &pby);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, pbx, pby), "(b) just-inside corner must be inside");

    float pcx;
    float pcy;
    forward_xform(BBOX_X - 8.0F, BBOX_Y - 8.0F, cx, cy, SCALE_X, SCALE_Y, ROT_RAD, OFFSET_X, OFFSET_Y, &pcx, &pcy);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, pcx, pcy), "(c) just-outside corner must be outside");

    /* Inside axis-aligned bbox but outside rotated -> proves inverse-affine ran. */
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, BBOX_X, BBOX_Y), "(d) inside-AABB / outside-rotated must be outside");

    end_probe_frame();
}

/* ---- Test 3: hit-test stays in Clay Y-down space ---- */
static void test_hittest_no_render_y_flip(void) {
    nt_ui_transform_t t = {.offset_x = 0, .offset_y = 40.0F, .rotation_z = 0, .scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F};
    declare_bbox_with_xform(&t);

    begin_probe_frame();
    const uint32_t id = nt_ui_id("htbtn");
    const float midx = BBOX_X + (BBOX_W * 0.5F);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, midx, BBOX_Y + BBOX_H + 20.0F), "down-shift: below original bottom must be inside");
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, midx, BBOX_Y - 20.0F), "down-shift: above original top must be outside");
    end_probe_frame();
}

/* ---- Test 4: padded hit-test, no transform ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_hittest_padded_asymmetric(void) {
    declare_bbox_with_xform(NULL);

    begin_probe_frame();
    const uint32_t id = nt_ui_id("htbtn");
    const float right_outside_x = BBOX_X + BBOX_W + 12.0F;
    const float center_y = BBOX_Y + (BBOX_H * 0.5F);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, right_outside_x, center_y), "12 px right must be outside unpadded");
    const int16_t pad_right[4] = {0, 16, 0, 0};
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, right_outside_x, center_y, pad_right), "12 px right must be inside with right padding 16");
    const float left_outside_x = BBOX_X - 12.0F;
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, left_outside_x, center_y, pad_right), "12 px left must stay outside (left pad=0)");
    const int16_t pad_left[4] = {16, 0, 0, 0};
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, left_outside_x, center_y, pad_left), "12 px left must be inside with left padding 16");
    const float center_x = BBOX_X + (BBOX_W * 0.5F);
    const int16_t pad_top[4] = {0, 0, 8, 0};
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, center_x, BBOX_Y - 4.0F, pad_top), "4 px above must be inside with top padding 8");
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, center_x, BBOX_Y + BBOX_H + 4.0F, pad_top), "4 px below must stay outside");
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, right_outside_x, center_y, NULL), "NULL pad must behave like unpadded");
    end_probe_frame();
}

/* ---- Test 5: padded hit-test + rotation ---- */
static void test_hittest_padded_with_rotation(void) {
    nt_ui_transform_t t = {.offset_x = 0, .offset_y = 0, .rotation_z = ROT_RAD, .scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F};
    declare_bbox_with_xform(&t);

    const float cx = BBOX_X + (BBOX_W * 0.5F);
    const float cy = BBOX_Y + (BBOX_H * 0.5F);
    begin_probe_frame();
    const uint32_t id = nt_ui_id("htbtn");

    const float layout_x = BBOX_X + BBOX_W + 12.0F;
    const float layout_y = cy;
    const float cr = cosf(ROT_RAD);
    const float sr = sinf(ROT_RAD);
    const float dx = layout_x - cx;
    const float dy = layout_y - cy;
    const float world_x = cx + (cr * dx) - (sr * dy);
    const float world_y = cy + (sr * dx) + (cr * dy);

    TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, world_x, world_y), "rotated: 12 px past right must be outside unpadded");
    const int16_t pad_right[4] = {0, 16, 0, 0};
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, world_x, world_y, pad_right), "rotated: 12 px past right must be inside with padding 16");
    end_probe_frame();
}

/* ---- Test 6: combinatorial padded + offset + anisotropic + rotation ---- */
#define COMBO_ROT_DEG 25.0F
#define COMBO_ROT_RAD (COMBO_ROT_DEG * 3.14159265358979323846F / 180.0F)
#define COMBO_SCALE_X 1.4F
#define COMBO_SCALE_Y 0.7F
#define COMBO_OFFSET_X 10.0F
#define COMBO_OFFSET_Y (-25.0F)

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_hittest_padded_combinatorial_worst_case(void) {
    nt_ui_transform_t t = {.offset_x = COMBO_OFFSET_X, .offset_y = COMBO_OFFSET_Y, .rotation_z = COMBO_ROT_RAD, .scale_x = COMBO_SCALE_X, .scale_y = COMBO_SCALE_Y, .scale_z = 1.0F};
    declare_bbox_with_xform(&t);

    const float cx = BBOX_X + (BBOX_W * 0.5F);
    const float cy = BBOX_Y + (BBOX_H * 0.5F);
    begin_probe_frame();
    const uint32_t id = nt_ui_id("htbtn");

    const int16_t pad_combo[4] = {8, 16, 4, 12};
    {
        const float layout_x = 146.0F;
        const float layout_y = 78.0F;
        const bool in_unpadded = (layout_x >= BBOX_X) && (layout_x <= BBOX_X + BBOX_W) && (layout_y >= BBOX_Y) && (layout_y <= BBOX_Y + BBOX_H);
        const bool in_padded = (layout_x >= BBOX_X - 8.0F) && (layout_x <= BBOX_X + BBOX_W + 16.0F) && (layout_y >= BBOX_Y - 4.0F) && (layout_y <= BBOX_Y + BBOX_H + 12.0F);
        TEST_ASSERT_FALSE_MESSAGE(in_unpadded, "(A) sanity: must be outside unpadded");
        TEST_ASSERT_TRUE_MESSAGE(in_padded, "(A) sanity: must be inside padded");
        float world_x;
        float world_y;
        forward_xform(layout_x, layout_y, cx, cy, COMBO_SCALE_X, COMBO_SCALE_Y, COMBO_ROT_RAD, COMBO_OFFSET_X, COMBO_OFFSET_Y, &world_x, &world_y);
        TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, world_x, world_y), "(A) combinatorial: inside padded only -> outside unpadded");
        TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, world_x, world_y, pad_combo), "(A) combinatorial: inside padded only -> inside padded");
    }
    {
        const float layout_x = 100.0F;
        const float layout_y = 30.0F;
        const bool in_padded = (layout_x >= BBOX_X - 8.0F) && (layout_x <= BBOX_X + BBOX_W + 16.0F) && (layout_y >= BBOX_Y - 4.0F) && (layout_y <= BBOX_Y + BBOX_H + 12.0F);
        TEST_ASSERT_FALSE_MESSAGE(in_padded, "(B) sanity: must be outside padded");
        float world_x;
        float world_y;
        forward_xform(layout_x, layout_y, cx, cy, COMBO_SCALE_X, COMBO_SCALE_Y, COMBO_ROT_RAD, COMBO_OFFSET_X, COMBO_OFFSET_Y, &world_x, &world_y);
        TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, world_x, world_y), "(B) combinatorial: outside both -> outside unpadded");
        TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit_padded(s_fx.ctx, id, world_x, world_y, pad_combo), "(B) combinatorial: outside both -> outside padded");
    }
    end_probe_frame();
}

/* ---- Test 7: zero-padding == unpadded ---- */
static void test_hittest_padded_zero_equals_unpadded(void) {
    declare_bbox_with_xform(NULL);

    begin_probe_frame();
    const uint32_t id = nt_ui_id("htbtn");
    const float cx = BBOX_X + (BBOX_W * 0.5F);
    const float cy = BBOX_Y + (BBOX_H * 0.5F);
    const int16_t zero_pad[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(nt_ui_test_hit_padded(s_fx.ctx, id, cx, cy, zero_pad) == nt_ui_test_hit(s_fx.ctx, id, cx, cy));
    TEST_ASSERT_TRUE(nt_ui_test_hit_padded(s_fx.ctx, id, BBOX_X - 5.0F, BBOX_Y - 5.0F, zero_pad) == nt_ui_test_hit(s_fx.ctx, id, BBOX_X - 5.0F, BBOX_Y - 5.0F));
    TEST_ASSERT_TRUE(nt_ui_test_hit_padded(s_fx.ctx, id, BBOX_X + 1.0F, BBOX_Y + 1.0F, zero_pad) == nt_ui_test_hit(s_fx.ctx, id, BBOX_X + 1.0F, BBOX_Y + 1.0F));
    end_probe_frame();
}

/* ---- Test 8: hit-test indexed by Clay hashmap slot survives a tree-structure
 *      change between frames (a NEW element appearing earlier shifts every
 *      subsequent layout idx by +1; hit-test must still resolve "htbtn"
 *      against ITS prev-frame composed affine, not whatever element ends up
 *      at the shifted layout slot). ---- */
static void test_hittest_survives_layout_idx_shift_between_frames(void) {
    nt_ui_transform_t rot = {.offset_x = 0, .offset_y = 0, .rotation_z = ROT_RAD, .scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F};
    /* Frame 1: declare htbtn alone — it lands at some layout idx (likely 1 after the root). */
    declare_bbox_with_xform(&rot);

    /* Frame 2: declare a NEW element BEFORE htbtn, shifting htbtn's layout idx. */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("intruder"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 500.0F, .y = 500.0F}}, .layout = {.sizing = {CLAY_SIZING_FIXED(10.0F), CLAY_SIZING_FIXED(10.0F)}}}) {
    }
    CLAY({.id = CLAY_ID("htbtn"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BBOX_X, .y = BBOX_Y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(BBOX_W), CLAY_SIZING_FIXED(BBOX_H)}},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &rot, 1.0F)}) {
        /* step_interaction AFTER htbtn's CLAY — layout idx shifted, but hashmap
         * slot for "htbtn" is stable, so hit_baked still has frame 1's rotation. */
        const uint32_t id = nt_ui_id("htbtn");
        const float cx = BBOX_X + (BBOX_W * 0.5F);
        const float cy = BBOX_Y + (BBOX_H * 0.5F);
        float pax;
        float pay;
        forward_xform(cx, cy, cx, cy, 1.0F, 1.0F, ROT_RAD, 0.0F, 0.0F, &pax, &pay);
        TEST_ASSERT_TRUE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, pax, pay), "rotated center must HIT after layout idx shift (hit_baked is slot-indexed)");
        TEST_ASSERT_FALSE_MESSAGE(nt_ui_test_hit(s_fx.ctx, id, BBOX_X, BBOX_Y), "axis-aligned corner under rotation must MISS (proves the rotation actually fired, not stale identity)");
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 9: get_bbox rejects ids Clay's hashmap still holds but that
 *      weren't re-declared this frame (stale generation). Mirrors the
 *      hit-test rejection so the two APIs agree on "in last frame". ---- */
static void test_get_bbox_rejects_stale_id(void) {
    /* Frame 1: declare htbtn. */
    declare_bbox_with_xform(NULL);
    /* Sanity: bbox found in the immediately-following frame. */
    {
        nt_pointer_t mouse = {0};
        nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
        nt_ui_bbox_t b = nt_ui_get_bbox(s_fx.ctx, nt_ui_id("htbtn"));
        TEST_ASSERT_TRUE_MESSAGE(b.found, "bbox of just-declared id must be found");
        nt_ui_end(s_fx.ctx);
    }
    /* Frame 2: do NOT declare htbtn. Clay's hashmap still holds it but
     * hit_generation[slot] != current_generation -> get_bbox rejects. */
    {
        nt_pointer_t mouse = {0};
        nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
        nt_ui_bbox_t b = nt_ui_get_bbox(s_fx.ctx, nt_ui_id("htbtn"));
        TEST_ASSERT_FALSE_MESSAGE(b.found, "stale id must report found=false");
        TEST_ASSERT_TRUE(b.x == 0.0F && b.y == 0.0F && b.width == 0.0F && b.height == 0.0F);
        nt_ui_end(s_fx.ctx);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hittest_axis_aligned_baseline);
    RUN_TEST(test_hittest_rotated_asymmetric_probes);
    RUN_TEST(test_hittest_no_render_y_flip);
    RUN_TEST(test_hittest_padded_asymmetric);
    RUN_TEST(test_hittest_padded_with_rotation);
    RUN_TEST(test_hittest_padded_combinatorial_worst_case);
    RUN_TEST(test_hittest_padded_zero_equals_unpadded);
    RUN_TEST(test_hittest_survives_layout_idx_shift_between_frames);
    RUN_TEST(test_get_bbox_rejects_stale_id);
    return UNITY_END();
}
