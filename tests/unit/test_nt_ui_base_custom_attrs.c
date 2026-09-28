/* A custom-attr base material: every base emit writes a zero tail, so plain widgets
 * and nt_ui_image_custom share one material and one batch. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "renderers/nt_sprite_renderer.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Nonzero, so a missing block or a stale tail cannot pass. */
static const float k_widget_block[4] = {3.0F, 5.0F, 7.0F, 11.0F};

/* One game attr the walker never writes: the bytes must arrive verbatim. */
static nt_material_t make_one_attr_material(void) {
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.program = nt_gfx_fake_make_program((const char *const[]){"u_texture"}, 1);
    nt_gfx_fake_set_samplers(NULL, 0);
    desc.cull_mode = NT_CULL_NONE;
    desc.textures[0].name = "u_texture";
    desc.texture_count = 1;
    desc.vertex_layout = nt_material_get_info(s_fx.sprite_material)->vertex_layout;
    desc.vertex_layout.stride = 36;
    desc.vertex_layout.attrs[3] = (nt_vertex_attr_t){.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 20};
    desc.vertex_layout.attr_count = 4;
    desc.attr_map[0].stream_name = "a_game";
    desc.attr_map[0].location = 4;
    desc.attr_map_count = 1;
    desc.label = "base_custom_material";
    return nt_material_create(&desc);
}

void setUp(void) { ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL); }

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* RECTANGLE + BORDER + IMAGE + slice9 IMAGE + nt_ui_image_custom REGION under one custom-attr base
 * material: one draw, base vertices carry a zero tail, the widget's vertices its own block. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_zero_tail_rides_every_base_emit_in_one_batch(void) {
    const nt_material_t mat = make_one_attr_material();
    nt_ui_set_sprite_material(s_fx.ctx, mat);

    nt_atlas_region_ref_t plain_ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    nt_atlas_region_ref_t slice_ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.packed_region_idx);
    const nt_ui_image_style_t plain = nt_ui_image_style_defaults();
    nt_ui_image_style_t sliced = nt_ui_image_style_defaults();
    sliced.flags |= NT_UI_IMAGE_SLICE9_OVERRIDE;
    sliced.slice9_lrtb[0] = 2;
    sliced.slice9_lrtb[1] = 2;
    sliced.slice9_lrtb[2] = 2;
    sliced.slice9_lrtb[3] = 2;
    const Clay_ElementDeclaration box = {.layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(30)}}};

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        CLAY({.id = CLAY_ID("rect"), .layout = box.layout, .backgroundColor = {255, 0, 0, 255}}) {}
        CLAY({.id = CLAY_ID("border"), .layout = box.layout, .border = {.color = {0, 255, 0, 255}, .width = {.left = 2, .right = 2, .top = 2, .bottom = 2}}}) {}
        nt_ui_image(s_fx.ctx, NULL, &plain_ref, &plain, &box);
        nt_ui_image(s_fx.ctx, NULL, &slice_ref, &sliced, &box);
        const nt_ui_image_custom_t img = {
            .atlas = s_fx.atlas.handle,
            .region_index = s_fx.atlas.white_region_idx,
            .material = mat,
            .custom_attrs = k_widget_block,
            .custom_bytes = (uint8_t)sizeof k_widget_block,
            .origin_x = 0.5F,
            .origin_y = 0.5F,
            .slice9_scale = 1.0F,
            .color_packed = 0xFFFFFFFFU,
        };
        nt_ui_image_custom(s_fx.ctx, NULL, &img, &box);
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_border_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(3U, nt_ui_get_last_walk_image_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_ui_get_last_walk_draw_calls(s_fx.ctx), "base and custom widget share one batch");

    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    for (uint32_t v = 0; v < 4U; ++v) {
        float got[4] = {0};
        nt_sprite_renderer_test_last_emit_attrs(v, got, sizeof(got));
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(k_widget_block, got, sizeof k_widget_block, "custom widget keeps its own block");
    }

    /* The widget-only walk leaves its block in staging slot 0; the plain rect must overwrite it. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    const nt_ui_image_custom_t first = {
        .atlas = s_fx.atlas.handle,
        .region_index = s_fx.atlas.white_region_idx,
        .material = mat,
        .custom_attrs = k_widget_block,
        .custom_bytes = (uint8_t)sizeof k_widget_block,
        .origin_x = 0.5F,
        .origin_y = 0.5F,
        .slice9_scale = 1.0F,
        .color_packed = 0xFFFFFFFFU,
    };
    nt_ui_image_custom(s_fx.ctx, NULL, &first, &box);
    nt_ui_end(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, &target);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("plain-only"), .layout = box.layout, .backgroundColor = {255, 255, 255, 255}}) {}
    nt_ui_end(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, &target);
    const float zero[4] = {0};
    float got[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    nt_sprite_renderer_test_last_emit_attrs(0, got, sizeof(got));
    TEST_ASSERT_EQUAL_MEMORY(zero, got, sizeof(got));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_tail_rides_every_base_emit_in_one_batch);
    return UNITY_END();
}
