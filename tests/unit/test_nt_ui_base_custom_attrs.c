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
#include "ui/nt_ui_debug_hit_zones.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_inspector.h"
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

#if NT_UI_DEBUG_TOOLS
static nt_ui_transform_t s_xform;

/* Two frames of one selected interactive element under the custom-attr base material: frame 1 bakes
 * the layout (and transform), frame 2 records its hit zone. The overlays read both afterwards. */
static void select_debug_element(bool is_3d, bool transformed) {
    nt_ui_set_sprite_material(s_fx.ctx, make_one_attr_material(), k_base_block, sizeof k_base_block);
    s_fx.ctx->use_raycast_input = is_3d;
    nt_ui_inspector_set_active(s_fx.ctx, true);
    nt_ui_debug_set_recording(s_fx.ctx, true);
    s_xform = nt_ui_transform_defaults();
    if (transformed) {
        s_xform.offset_x = 40.0F;
        s_xform.rotation_z = 0.4F;
        s_xform.scale_x = 1.2F;
    }
    const float identity_vp[16] = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    for (int frame = 0; frame < 2; ++frame) {
        nt_pointer_t mouse = {0};
        nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
        if (is_3d) {
            nt_ui_set_view_proj(s_fx.ctx, identity_vp);
        }
        CLAY({.id = CLAY_ID("dbg_target"),
              .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 120.0F, .y = 160.0F}},
              .layout = {.sizing = {CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(48)}},
              .userData = transformed ? (void *)NT_UI_DATA_XFORM(0U, &s_xform, 1.0F) : NULL}) {
            (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("dbg_target"));
        }
        s_fx.ctx->inspector_selected_id = nt_ui_id("dbg_target");
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("dbg_target"), s_fx.ctx->inspector_highlight_id);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_debug_get_zone_count(s_fx.ctx));
}

/* The fixture re-inits the renderer per test, so a 4-vertex last emit is the overlay's own outline edge. */
static void assert_overlay_emitted_with_base_block(void) {
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    for (uint32_t v = 0; v < 4U; ++v) {
        float got[4] = {0};
        nt_sprite_renderer_test_last_emit_radial(v, got, 4);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(k_base_block, got, sizeof k_base_block, "overlay emit on the base material carries the base block");
    }
}

static void draw_hit_zones(void) {
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_debug_draw_hit_zones(s_fx.ctx, &target, NT_UI_DEBUG_HIT_ALL, NT_FONT_INVALID, 0.0F);
    nt_sprite_renderer_flush();
}

static void draw_inspector_overlay(void) {
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_inspector_overlay_draw(s_fx.ctx, &target, NT_FONT_INVALID, 0.0F);
    nt_sprite_renderer_flush();
}

static void test_hit_zones_carry_base_block_2d(void) {
    select_debug_element(false, true);
    draw_hit_zones();
    assert_overlay_emitted_with_base_block();
}

static void test_hit_zones_carry_base_block_3d(void) {
    select_debug_element(true, true);
    draw_hit_zones();
    assert_overlay_emitted_with_base_block();
}

/* The whole inspector frame: game walk, sidebar walk on the base material, then the highlight. */
static void test_inspector_highlight_carries_base_block_2d_transformed(void) {
    select_debug_element(false, true);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    nt_ui_debug_inspector_walk(s_fx.ctx, &target);
    draw_inspector_overlay();
    assert_overlay_emitted_with_base_block();
}

static void test_inspector_highlight_carries_base_block_2d_axis_aligned(void) {
    select_debug_element(false, false);
    draw_inspector_overlay();
    assert_overlay_emitted_with_base_block();
}

static void test_inspector_highlight_carries_base_block_3d(void) {
    select_debug_element(true, true);
    draw_inspector_overlay();
    assert_overlay_emitted_with_base_block();
}

/* A plain inspector overlay material takes the 3D overlays, and the inspector walk, without the block. */
static void test_plain_inspector_material_stages_no_block_3d(void) {
    select_debug_element(true, true);
    nt_ui_inspector_set_materials(s_fx.ctx, s_fx.sprite_material, s_fx.text_material);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_debug_inspector_walk(s_fx.ctx, &target);
    draw_hit_zones();
    draw_inspector_overlay();
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT8(sizeof k_base_block, s_fx.ctx->base_custom_bytes);
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_base_block_rides_every_base_emit_in_one_batch);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_set_asserts_on_stride_mismatch);
#endif
#if NT_UI_DEBUG_TOOLS
    RUN_TEST(test_hit_zones_carry_base_block_2d);
    RUN_TEST(test_hit_zones_carry_base_block_3d);
    RUN_TEST(test_inspector_highlight_carries_base_block_2d_transformed);
    RUN_TEST(test_inspector_highlight_carries_base_block_2d_axis_aligned);
    RUN_TEST(test_inspector_highlight_carries_base_block_3d);
    RUN_TEST(test_plain_inspector_material_stages_no_block_3d);
#endif
    return UNITY_END();
}
