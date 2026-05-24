#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"
#include "graphics/nt_gfx.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define MAX_TEST_CMDS 32
static Clay_RenderCommand s_test_cmds[MAX_TEST_CMDS];

void setUp(void) {
    nt_test_assert_install();
    memset(s_test_cmds, 0, sizeof s_test_cmds);
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void inject_frozen_cmds(int32_t count) {
    s_fx.ctx->frozen_cmds.internalArray = s_test_cmds;
    s_fx.ctx->frozen_cmds.length = count;
    s_fx.ctx->frozen_cmds.capacity = MAX_TEST_CMDS;
}

/* 8 nested SCISSOR_START / 8 SCISSOR_END must succeed (depth 8
 * is at the limit but not over). */
static void test_scissor_depth_8_ok(void) {
    for (int32_t i = 0; i < 8; ++i) {
        s_test_cmds[i].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
        s_test_cmds[i].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 800, .height = 600};
        s_test_cmds[i].renderData.clip.horizontal = true;
        s_test_cmds[i].renderData.clip.vertical = true;
    }
    for (int32_t i = 8; i < 16; ++i) {
        s_test_cmds[i].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    }
    inject_frozen_cmds(16);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_FALSE(nt_gfx_test_scissor_enabled());
}

/* Hard cap NT_UI_WALKER_SCISSOR_DEPTH_CAP push asserts on overflow. */
static void test_scissor_depth_cap_asserts(void) {
    /* MAX_TEST_CMDS may be smaller than CAP+1; use a heap array sized to CAP+1. */
    const int32_t over = NT_UI_WALKER_SCISSOR_DEPTH_CAP + 1;
    Clay_RenderCommand *cmds = (Clay_RenderCommand *)calloc((size_t)over, sizeof(Clay_RenderCommand));
    TEST_ASSERT_NOT_NULL(cmds);
    for (int32_t i = 0; i < over; ++i) {
        cmds[i].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
        cmds[i].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 800, .height = 600};
        cmds[i].renderData.clip.horizontal = true;
        cmds[i].renderData.clip.vertical = true;
    }
    s_fx.ctx->frozen_cmds.internalArray = cmds;
    s_fx.ctx->frozen_cmds.length = over;
    s_fx.ctx->frozen_cmds.capacity = over;

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
    free(cmds);
}

/* 2 SCISSOR_START + 1 SCISSOR_END leaves depth > 0
 * at walk exit -> the final NT_ASSERT(depth == 0) must fire. */
static void test_scissor_unbalanced_asserts_at_exit(void) {
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 800, .height = 600};
    s_test_cmds[0].renderData.clip.horizontal = true;
    s_test_cmds[0].renderData.clip.vertical = true;
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[1].boundingBox = (Clay_BoundingBox){.x = 0, .y = 0, .width = 800, .height = 600};
    s_test_cmds[1].renderData.clip.horizontal = true;
    s_test_cmds[1].renderData.clip.vertical = true;
    s_test_cmds[2].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(3);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* Clay scissor (x=100, y=100, w=200, h=200) inside an 800x600 viewport
 * must produce GL scissor at (100, 600 - 100 - 200 = 300, 200, 200). The
 * scissor is disabled at walk exit; we observe the LAST set_scissor rect via
 * the gfx test probe, which retains the most-recent values regardless of
 * the enabled flag. */
static void test_scissor_y_flip_top_left_to_gl_bottom_left(void) {
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 100.0F, .y = 100.0F, .width = 200.0F, .height = 200.0F};
    s_test_cmds[0].renderData.clip.horizontal = true;
    s_test_cmds[0].renderData.clip.vertical = true;
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    int rect[4];
    nt_gfx_test_scissor_rect(rect);
    /* GL bottom-left form: y_gl = fb_h - y - h = 600 - 100 - 200 = 300. */
    TEST_ASSERT_EQUAL_INT(100, rect[0]);
    TEST_ASSERT_EQUAL_INT(300, rect[1]);
    TEST_ASSERT_EQUAL_INT(200, rect[2]);
    TEST_ASSERT_EQUAL_INT(200, rect[3]);
}

/* Nested scissor intersects, not replaces. */
static void test_scissor_intersection_nested(void) {
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 0.0F, .y = 0.0F, .width = 100.0F, .height = 100.0F};
    s_test_cmds[0].renderData.clip.horizontal = true;
    s_test_cmds[0].renderData.clip.vertical = true;
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[1].boundingBox = (Clay_BoundingBox){.x = 50.0F, .y = 50.0F, .width = 150.0F, .height = 150.0F};
    s_test_cmds[1].renderData.clip.horizontal = true;
    s_test_cmds[1].renderData.clip.vertical = true;
    s_test_cmds[2].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    s_test_cmds[3].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(4);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    /* After pop-inner re-applies outer, walker exit only disables scissor
     * (does not overwrite the rect probe). So we read the outer rect:
     *   (0, 0, 100, 100) in Clay top-left -> Y-flip: y_gl = 600 - 100 = 500. */
    int rect[4];
    nt_gfx_test_scissor_rect(rect);
    TEST_ASSERT_EQUAL_INT(0, rect[0]);
    TEST_ASSERT_EQUAL_INT(500, rect[1]);
    TEST_ASSERT_EQUAL_INT(100, rect[2]);
    TEST_ASSERT_EQUAL_INT(100, rect[3]);
}

/* walker exit always disables scissor, even if scissor
 * was enabled mid-walk. */
static void test_walker_exit_disables_scissor(void) {
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 10, .y = 10, .width = 100, .height = 100};
    s_test_cmds[0].renderData.clip.horizontal = true;
    s_test_cmds[0].renderData.clip.vertical = true;
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_FALSE(nt_gfx_test_scissor_enabled());
}

/* Clay's bounding box is target-local. With a non-zero
 * viewport offset (split-screen pane, sub-FBO render-target), the GL scissor
 * MUST add viewport.x to x and use a Y-flip relative to viewport.y, not the
 * raw framebuffer height. Without this, a split-screen pane's clip rectangle
 * lands on the wrong physical pixels (clipping the wrong pane). */
static void test_scissor_respects_viewport_offset(void) {
    /* Local scissor at (10, 10) within a 400x300 viewport that itself
     * starts at framebuffer offset (100, 50). */
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 10.0F, .y = 10.0F, .width = 100.0F, .height = 100.0F};
    s_test_cmds[0].renderData.clip.horizontal = true;
    s_test_cmds[0].renderData.clip.vertical = true;
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {100.0F, 50.0F, 400.0F, 300.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    int rect[4];
    nt_gfx_test_scissor_rect(rect);

    /* Expected (GL coords, bottom-left origin):
     *   x_gl = viewport.x + local_x          = 100 + 10           = 110
     *   y_gl = viewport.y + viewport.h - local_y - local_h
     *        = 50 + 300 - 10 - 100                                = 240
     *   w    = local_w                                            = 100
     *   h    = local_h                                            = 100
     */
    TEST_ASSERT_EQUAL_INT_MESSAGE(110, rect[0], "scissor x must include viewport offset");
    TEST_ASSERT_EQUAL_INT_MESSAGE(240, rect[1], "scissor y must Y-flip within viewport, not framebuffer");
    TEST_ASSERT_EQUAL_INT(100, rect[2]);
    TEST_ASSERT_EQUAL_INT(100, rect[3]);
}

/* clip.horizontal=true, clip.vertical=false: only x is clipped. y/h
 * should span the full viewport. */
static void test_scissor_horizontal_only(void) {
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 50.0F, .y = 100.0F, .width = 200.0F, .height = 50.0F};
    s_test_cmds[0].renderData.clip.horizontal = true;
    s_test_cmds[0].renderData.clip.vertical = false;
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    int rect[4];
    nt_gfx_test_scissor_rect(rect);
    /* x = 50, w = 200 (clipped). y = 0, h = 600 (unclipped -> full viewport).
     * GL Y-flip: y_gl = 0 + 600 - 0 - 600 = 0. */
    TEST_ASSERT_EQUAL_INT_MESSAGE(50, rect[0], "x must be clipped");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rect[1], "y must span full viewport (Y-flip of 0..600)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, rect[2], "w must be clipped");
    TEST_ASSERT_EQUAL_INT_MESSAGE(600, rect[3], "h must span full viewport");
}

/* clip.horizontal=false, clip.vertical=true: only y is clipped. */
static void test_scissor_vertical_only(void) {
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 50.0F, .y = 100.0F, .width = 200.0F, .height = 50.0F};
    s_test_cmds[0].renderData.clip.horizontal = false;
    s_test_cmds[0].renderData.clip.vertical = true;
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    int rect[4];
    nt_gfx_test_scissor_rect(rect);
    /* x = 0, w = 800 (unclipped). y = 100, h = 50 (clipped).
     * GL Y-flip: y_gl = 600 - 100 - 50 = 450. */
    TEST_ASSERT_EQUAL_INT(0, rect[0]);
    TEST_ASSERT_EQUAL_INT(450, rect[1]);
    TEST_ASSERT_EQUAL_INT(800, rect[2]);
    TEST_ASSERT_EQUAL_INT(50, rect[3]);
}

/* Both axes false + bbox set is Clay's floating clipTo=ATTACHED_PARENT marker
 * (clay.h:2695-2701). Walker must clip to bbox on BOTH axes -- not full viewport. */
static void test_scissor_neither_axis_clips_to_bbox(void) {
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 50.0F, .y = 100.0F, .width = 200.0F, .height = 50.0F};
    s_test_cmds[0].renderData.clip.horizontal = false;
    s_test_cmds[0].renderData.clip.vertical = false;
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(2);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);
    int rect[4];
    nt_gfx_test_scissor_rect(rect);
    /* bbox=(50,100,200x50); GL Y-flip: y_gl = 600 - 100 - 50 = 450. */
    TEST_ASSERT_EQUAL_INT(50, rect[0]);
    TEST_ASSERT_EQUAL_INT(450, rect[1]);
    TEST_ASSERT_EQUAL_INT(200, rect[2]);
    TEST_ASSERT_EQUAL_INT(50, rect[3]);
}

/* Scaled mode: viewport is LOGICAL (Y top-down). fb_size + fb_offset describe
 * physical placement. Scissor coordinates scale logical->physical and Y-flip
 * against fb height for GL bottom-left. */
static void test_scissor_scaled_letterbox(void) {
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    /* Logical bbox: (10,20) 30x40 in a 640x480 logical viewport. */
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 10.0F, .y = 20.0F, .width = 30.0F, .height = 40.0F};
    s_test_cmds[0].renderData.clip.horizontal = true;
    s_test_cmds[0].renderData.clip.vertical = true;
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(2);

    /* Logical 640x480 inside fb 1600x600 with letterbox: 2x bars on x.
     * scale_x = (1600 - 2*320) / 640 = 1.5; scale_y = 600 / 480 = 1.25. */
    nt_ui_target_t target = {
        .mode = NT_UI_TARGET_SCALED,
        .viewport = {0.0F, 0.0F, 640.0F, 480.0F},
        .fb_size = {1600.0F, 600.0F},
        .fb_offset = {320.0F, 0.0F},
    };
    nt_ui_walk(s_fx.ctx, &target);

    int rect[4];
    nt_gfx_test_scissor_rect(rect);
    /* phys_x = 320 + 1.5 * (0 + 10)            = 335
     * phys_w = 1.5 * 30                        = 45
     * phys_h = 1.25 * 40                       = 50
     * phys_y_top = 0 + 1.25 * (0 + 20)         = 25
     * phys_y_gl = 600 - 25 - 50                = 525 */
    TEST_ASSERT_EQUAL_INT(335, rect[0]);
    TEST_ASSERT_EQUAL_INT(525, rect[1]);
    TEST_ASSERT_EQUAL_INT(45, rect[2]);
    TEST_ASSERT_EQUAL_INT(50, rect[3]);
}

/* EXPAND mode (no letterbox, scale_x == scale_y, no offset). */
static void test_scissor_scaled_expand(void) {
    s_test_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    s_test_cmds[0].boundingBox = (Clay_BoundingBox){.x = 100.0F, .y = 50.0F, .width = 200.0F, .height = 100.0F};
    s_test_cmds[0].renderData.clip.horizontal = true;
    s_test_cmds[0].renderData.clip.vertical = true;
    s_test_cmds[1].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    inject_frozen_cmds(2);

    /* Logical 640x480, fb 1280x960 (uniform 2x), no offset. */
    nt_ui_target_t target = {
        .mode = NT_UI_TARGET_SCALED,
        .viewport = {0.0F, 0.0F, 640.0F, 480.0F},
        .fb_size = {1280.0F, 960.0F},
        .fb_offset = {0.0F, 0.0F},
    };
    nt_ui_walk(s_fx.ctx, &target);

    int rect[4];
    nt_gfx_test_scissor_rect(rect);
    /* phys_x = 0 + 2 * (0 + 100)               = 200
     * phys_w = 2 * 200                         = 400
     * phys_h = 2 * 100                         = 200
     * phys_y_top = 0 + 2 * (0 + 50)            = 100
     * phys_y_gl = 960 - 100 - 200              = 660 */
    TEST_ASSERT_EQUAL_INT(200, rect[0]);
    TEST_ASSERT_EQUAL_INT(660, rect[1]);
    TEST_ASSERT_EQUAL_INT(400, rect[2]);
    TEST_ASSERT_EQUAL_INT(200, rect[3]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_scissor_depth_8_ok);
    RUN_TEST(test_scissor_depth_cap_asserts);
    RUN_TEST(test_scissor_unbalanced_asserts_at_exit);
    RUN_TEST(test_scissor_y_flip_top_left_to_gl_bottom_left);
    RUN_TEST(test_scissor_intersection_nested);
    RUN_TEST(test_walker_exit_disables_scissor);
    RUN_TEST(test_scissor_respects_viewport_offset);
    RUN_TEST(test_scissor_horizontal_only);
    RUN_TEST(test_scissor_vertical_only);
    RUN_TEST(test_scissor_neither_axis_clips_to_bbox);
    RUN_TEST(test_scissor_scaled_letterbox);
    RUN_TEST(test_scissor_scaled_expand);
    return UNITY_END();
}
