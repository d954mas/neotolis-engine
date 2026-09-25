/* A custom-attr base material: the game's base block rides every base emit, so plain widgets and
 * nt_ui_image_custom share one material and one batch. */

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "renderers/nt_sprite_renderer.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Distinct from each other and from zero, so a missing or swapped block cannot pass. */
static const float k_base_block[4] = {0.125F, 0.25F, 0.5F, 1.0F};
static const float k_widget_block[4] = {3.0F, 5.0F, 7.0F, 11.0F};

/* One game attr the walker never injects (not a_layout/a_uvrect): the bytes must arrive verbatim. */
static nt_material_t make_one_attr_material(void) {
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.program = nt_gfx_fake_make_program((const char *const[]){"u_texture"}, 1);
    nt_gfx_fake_set_samplers(NULL, 0);
    desc.depth_test = false;
    desc.depth_write = false;
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.textures[0].name = "u_texture";
    desc.texture_count = 1;
    desc.attr_map[0].stream_name = "a_game";
    desc.attr_map[0].location = 4;
    desc.attr_map_count = 1;
    desc.label = "base_custom_material";
    return nt_material_create(&desc);
}

void setUp(void) { ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL); }

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* RECTANGLE + BORDER + IMAGE + slice9 IMAGE + nt_ui_image_custom REGION under one custom-attr base
 * material: one draw, every base vertex carries the base block, the widget's vertices its own. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_base_block_rides_every_base_emit_in_one_batch(void) {
    const nt_material_t mat = make_one_attr_material();
    nt_ui_set_sprite_material(s_fx.ctx, mat, k_base_block, sizeof k_base_block);

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
            .geom_mode = NT_UI_IMAGE_GEOM_REGION,
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

    /* The custom REGION is the last emit; everything staged before it in the batch is a base emit:
     * rect (4) + border + plain image (4) + slice9 (16). */
    const uint32_t first_widget_vertex = nt_sprite_renderer_test_last_emit_first_vertex();
    TEST_ASSERT_GREATER_THAN_UINT32(4U + 4U + 16U, first_widget_vertex);
    for (uint32_t v = 0; v < first_widget_vertex; ++v) {
        float got[4] = {0};
        nt_sprite_renderer_test_batch_custom(v, got, 4);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(k_base_block, got, sizeof k_base_block, "base emit carries the game's base block");
    }
    const uint32_t widget_vertex_count = nt_sprite_renderer_test_last_emit_vertex_count();
    TEST_ASSERT_EQUAL_UINT32(4U, widget_vertex_count);
    for (uint32_t v = 0; v < widget_vertex_count; ++v) {
        float got[4] = {0};
        nt_sprite_renderer_test_batch_custom(first_widget_vertex + v, got, 4);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(k_widget_block, got, sizeof k_widget_block, "custom widget keeps its own block");
    }
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
static void test_set_asserts_on_stride_mismatch(void) {
    const nt_material_t mat = make_one_attr_material();
    NT_TEST_EXPECT_ASSERT(nt_ui_set_sprite_material(s_fx.ctx, mat, k_base_block, 8U));
    NT_TEST_EXPECT_ASSERT(nt_ui_set_sprite_material(s_fx.ctx, mat, NULL, 0U));
    NT_TEST_EXPECT_ASSERT(nt_ui_set_sprite_material(s_fx.ctx, s_fx.sprite_material, k_base_block, sizeof k_base_block));
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_base_block_rides_every_base_emit_in_one_batch);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_set_asserts_on_stride_mismatch);
#endif
    return UNITY_END();
}
