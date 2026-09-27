#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>

#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_shape.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;
static nt_material_t s_shape_material;

void setUp(void) {
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    const nt_material_create_desc_t desc = {
        .program = nt_material_get_info(s_fx.sprite_material)->program,
        .attr_map = {{.stream_name = "a_shape_layout", .location = 4},
                     {.stream_name = "a_shape_geometry", .location = 5},
                     {.stream_name = "a_shape_paint", .location = 6},
                     {.stream_name = "a_shape_border", .location = 7}},
        .attr_map_count = 4,
    };
    s_shape_material = nt_material_create(&desc);
}

void tearDown(void) {
    nt_material_destroy(s_shape_material);
    ui_walker_fixture_shutdown(&s_fx);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- Unity assertions expand to control flow.
static void test_shape_copies_style_and_keeps_children(void) {
    nt_ui_shape_style_t style = nt_ui_shape_style_defaults();
    style.material = s_shape_material;
    style.box.top_left = 40.0F;
    style.paint.border_widths = (nt_ui_shape_border_widths_t){8, 1, 2, 4};
    style.paint.color0 = 0x00112233U;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_shape_begin(s_fx.ctx, NULL, &style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}});
    CLAY({.id = CLAY_ID("child"), .layout.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10)}, .backgroundColor = {255, 0, 0, 255}}) {}
    nt_ui_shape_end(s_fx.ctx);
    style.box.top_left = 0.0F;
    style.paint.border_widths = (nt_ui_shape_border_widths_t){0};
    style.paint.color0 = 0xFFFFFFFFU;
    nt_ui_end(s_fx.ctx);
    int images = 0;
    int children = 0;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *cmd = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE) {
            const nt_ui_image_payload_t *payload = cmd->renderData.image.imageData;
            TEST_ASSERT_EQUAL_UINT32(0x00112233U, payload->shape->style.paint.color0);
            TEST_ASSERT_TRUE(payload->shape->style.box.top_left == 40.0F);
            TEST_ASSERT_TRUE(payload->shape->style.paint.border_widths.left == 8.0F);
            TEST_ASSERT_TRUE(payload->shape->style.paint.border_widths.top == 1.0F);
            TEST_ASSERT_TRUE(payload->shape->style.paint.border_widths.right == 2.0F);
            TEST_ASSERT_TRUE(payload->shape->style.paint.border_widths.bottom == 4.0F);
            TEST_ASSERT_TRUE(cmd->boundingBox.width == 200.0F && cmd->boundingBox.height == 60.0F);
            ++images;
        }
        if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
            ++children;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, images);
    TEST_ASSERT_EQUAL_INT(1, children);
}

static void test_shape_offscreen_logical_box_reaches_walker(void) {
    nt_ui_shape_style_t style = nt_ui_shape_style_defaults();
    style.material = s_shape_material;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = {CLAY_SIZING_FIXED(800), CLAY_SIZING_FIXED(1000)}}}) {
        CLAY({.layout.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(700)}}) {}
        nt_ui_shape(s_fx.ctx, NULL, &style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}});
    }
    nt_ui_end(s_fx.ctx);
    int images = 0;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *cmd = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE) {
            TEST_ASSERT_TRUE(cmd->boundingBox.y >= 600.0F);
            ++images;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, images);
}

static void test_each_border_side_rejects_invalid_width(void) {
    const nt_ui_shape_border_widths_t invalid[] = {{-1, 0, 0, 0}, {0, -1, 0, 0}, {0, 0, -1, 0}, {0, 0, 0, -1}, {NAN, 0, 0, 0}, {0, INFINITY, 0, 0}, {0, 0, NAN, 0}, {0, 0, 0, INFINITY}};
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    for (uint32_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        nt_ui_shape_style_t style = nt_ui_shape_style_defaults();
        style.material = s_shape_material;
        style.paint.border_widths = invalid[i];
        NT_TEST_EXPECT_ASSERT(nt_ui_shape(s_fx.ctx, NULL, &style, NULL));
    }
    nt_ui_end(s_fx.ctx);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_shape_copies_style_and_keeps_children);
    RUN_TEST(test_shape_offscreen_logical_box_reaches_walker);
    RUN_TEST(test_each_border_side_rejects_invalid_width);
    return UNITY_END();
}
