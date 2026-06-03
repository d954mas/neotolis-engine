/* Hit-test clip ancestry: each element's clip parent comes from Clay's native
 * `.clip` element config (layoutElementClipElementIds). Hit-test walks the chain
 * via hit_baked/hit_clip_parent_id, rejecting outside-clip points before the
 * widget's inverse-affine runs. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "clay.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define SCREEN_W 800.0F
#define SCREEN_H 600.0F
#define DEG2RAD(d) ((d) * 3.14159265358979323846F / 180.0F)

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* ---- Test 1+2: axis-aligned clip rect at (0,0,200,200) wraps a child button.
 *      Probe inside clip+button -> HIT. Probe inside button bbox but outside clip
 *      -> MISS. ---- */
/* Clay floating CHILDREN escape the clip-parent chain (clay.h:2101 — floating
 * inherits the floating-parent's clip ancestor, which for a top-level clip is 0).
 * Use a non-floating child positioned via padding inside the clip wrapper so the
 * layoutElementClipElementIds entry stays pointed at the wrapping clip. */
static void declare_clip_with_child(float clip_x, float clip_y, float clip_w, float clip_h, float btn_x, float btn_y, float btn_w, float btn_h, const nt_ui_transform_t *clip_xform) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    void *clip_ud = (clip_xform != NULL) ? (void *)NT_UI_DATA_XFORM(0U, clip_xform, 1.0F) : NULL;
    const float pad_l = btn_x - clip_x;
    const float pad_t = btn_y - clip_y;
    CLAY({.id = CLAY_ID("clip_outer"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = clip_x, .y = clip_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(clip_w), CLAY_SIZING_FIXED(clip_h)}, .padding = {.left = (uint16_t)pad_l, .top = (uint16_t)pad_t}},
          .clip = {.horizontal = true, .vertical = true},
          .userData = clip_ud}) {
        CLAY({.id = CLAY_ID("clipbtn"), .layout = {.sizing = {CLAY_SIZING_FIXED(btn_w), CLAY_SIZING_FIXED(btn_h)}}}) {}
    }
    nt_ui_end(s_fx.ctx);
}

static void test_clip_axis_aligned_misses_outside_clip(void) {
    declare_clip_with_child(0.0F, 0.0F, 200.0F, 200.0F, 100.0F, 100.0F, 200.0F, 100.0F, NULL);

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    nt_ui_bbox_t clip_b = nt_ui_get_bbox(s_fx.ctx, nt_ui_id("clip_outer"));
    nt_ui_bbox_t btn_b = nt_ui_get_bbox(s_fx.ctx, nt_ui_id("clipbtn"));
    TEST_ASSERT_TRUE_MESSAGE(clip_b.found, "clip_outer bbox missing");
    TEST_ASSERT_TRUE_MESSAGE(btn_b.found, "clipbtn bbox missing");
    /* If Clay's clip parent linkage holds, btn's clip chain is non-empty and the
     * probe at (250,150) outside clip_outer rejects. */
    const uint32_t id = nt_ui_id("clipbtn");
    const bool h = nt_ui_test_hit(s_fx.ctx, id, 250.0F, 150.0F);
    nt_ui_end(s_fx.ctx);
    (void)clip_b;
    (void)btn_b;
    TEST_ASSERT_FALSE_MESSAGE(h, "inside button bbox but past clip right edge must be MISS");
}

static void test_clip_axis_aligned_hits_inside_clip(void) {
    declare_clip_with_child(0.0F, 0.0F, 200.0F, 200.0F, 100.0F, 100.0F, 200.0F, 100.0F, NULL);

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    const uint32_t id = nt_ui_id("clipbtn");
    /* (150, 150): inside both clip and button. */
    const bool h = nt_ui_test_hit(s_fx.ctx, id, 150.0F, 150.0F);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(h, "inside both clip and button must be HIT");
}

/* ---- Test 3: rotated button inside an axis-aligned clip. ---- */
static void test_clip_with_rotated_button(void) {
    /* Button rotated 30° about its center; clip 400x400 covers the rotated extent. */
    const nt_ui_transform_t rot30 = {.offset_x = 0, .offset_y = 0, .rotation = DEG2RAD(30.0F), .scale_x = 1.0F, .scale_y = 1.0F};

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("clip_outer"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 0.0F, .y = 0.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(400.0F), CLAY_SIZING_FIXED(400.0F)}},
          .clip = {.horizontal = true, .vertical = true}}) {
        CLAY({.id = CLAY_ID("clipbtn"),
              .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .offset = {.x = 100.0F, .y = 100.0F}},
              .layout = {.sizing = {CLAY_SIZING_FIXED(200.0F), CLAY_SIZING_FIXED(100.0F)}},
              .userData = (void *)NT_UI_DATA_XFORM(0U, &rot30, 1.0F)}) {}
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    const uint32_t id = nt_ui_id("clipbtn");
    const bool h_center = nt_ui_test_hit(s_fx.ctx, id, 200.0F, 150.0F);
    /* (420, 250) > clip's 400 right edge → clip chain rejects. */
    const bool h_past_clip = nt_ui_test_hit(s_fx.ctx, id, 420.0F, 250.0F);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(h_center, "(rotated button) center inside clip + inside rotated bbox must be HIT");
    TEST_ASSERT_FALSE_MESSAGE(h_past_clip, "(rotated button) past clip right edge must be MISS regardless of rotated extent");
}

/* ---- Test 4: rotated clip parent. Clip 200x200 at (100,100) rotated 45° about
 *      its center (200,200) → diamond. Axis-aligned button inside. A point inside
 *      the bounding AABB of the diamond but outside the diamond itself MUST MISS. ---- */
static void test_clip_rotated_parent(void) {
    /* Big floating root so the diamond's screen-space extent fits. Clip 200x200
     * positioned at (100,100), rotated 45° about its own center. Button is axis-
     * aligned 100x100 centered inside the clip — it's reachable only through the
     * rotated clip. */
    const nt_ui_transform_t rot45 = {.offset_x = 0, .offset_y = 0, .rotation = DEG2RAD(45.0F), .scale_x = 1.0F, .scale_y = 1.0F};

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("clip_outer"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 100.0F, .y = 100.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(200.0F), CLAY_SIZING_FIXED(200.0F)}},
          .clip = {.horizontal = true, .vertical = true},
          .userData = (void *)NT_UI_DATA_XFORM(0U, &rot45, 1.0F)}) {
        CLAY({.id = CLAY_ID("clipbtn"),
              .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .offset = {.x = 50.0F, .y = 50.0F}},
              .layout = {.sizing = {CLAY_SIZING_FIXED(100.0F), CLAY_SIZING_FIXED(100.0F)}}}) {}
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    const uint32_t id = nt_ui_id("clipbtn");
    /* Clip's rotation center is (200,200). Point (200,200) is at the diamond center → HIT. */
    const bool h_center = nt_ui_test_hit(s_fx.ctx, id, 200.0F, 200.0F);
    /* (350,350) lands inside the diamond's AABB but well past its rotated extent → MISS. */
    const bool h_outside = nt_ui_test_hit(s_fx.ctx, id, 350.0F, 350.0F);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(h_center, "(rotated clip) center of rotated clip must be HIT");
    TEST_ASSERT_FALSE_MESSAGE(h_outside, "(rotated clip) probe past diamond corner must be MISS (proves inverse-affine ran)");
}

/* ---- Test 5: nested clips intersect. Outer 300x300, inner 100x100 at (50,50).
 *      A point inside the outer but outside the inner MUST MISS. ---- */
static void test_nested_clips_intersect(void) {
    /* Use non-floating children so Clay's openClipElementStack chain links each
     * descendant to its lexical clip parent (floating breaks this — see
     * declare_clip_with_child comment). Padding positions inner inside outer. */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("clip_outer"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 0.0F, .y = 0.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(300.0F), CLAY_SIZING_FIXED(300.0F)}, .padding = {.left = 50U, .top = 50U}},
          .clip = {.horizontal = true, .vertical = true}}) {
        CLAY({.id = CLAY_ID("clip_inner"), .layout = {.sizing = {CLAY_SIZING_FIXED(100.0F), CLAY_SIZING_FIXED(100.0F)}}, .clip = {.horizontal = true, .vertical = true}}) {
            CLAY({.id = CLAY_ID("clipbtn"), .layout = {.sizing = {CLAY_SIZING_FIXED(300.0F), CLAY_SIZING_FIXED(300.0F)}}}) {}
        }
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &mouse, 1);
    const uint32_t id = nt_ui_id("clipbtn");
    /* (75, 75): inside both clips → HIT. */
    const bool h_in_both = nt_ui_test_hit(s_fx.ctx, id, 75.0F, 75.0F);
    /* (200, 200): inside outer (300x300), outside inner (50..150) → MISS via inner. */
    const bool h_in_outer_only = nt_ui_test_hit(s_fx.ctx, id, 200.0F, 200.0F);
    /* (10, 10): inside outer, above inner (10 < 50) → MISS via inner. */
    const bool h_above_inner = nt_ui_test_hit(s_fx.ctx, id, 10.0F, 10.0F);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(h_in_both, "nested clips: inside both must be HIT");
    TEST_ASSERT_FALSE_MESSAGE(h_in_outer_only, "nested clips: inside outer but outside inner must be MISS");
    TEST_ASSERT_FALSE_MESSAGE(h_above_inner, "nested clips: above-left of inner clip must be MISS");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_clip_axis_aligned_misses_outside_clip);
    RUN_TEST(test_clip_axis_aligned_hits_inside_clip);
    RUN_TEST(test_clip_with_rotated_button);
    RUN_TEST(test_clip_rotated_parent);
    RUN_TEST(test_nested_clips_intersect);
    return UNITY_END();
}
