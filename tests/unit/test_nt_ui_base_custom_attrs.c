/* A custom-attr base material with attr defaults: every base emit bakes the defaults, so plain widgets
 * and nt_ui_image_custom share one material and one batch. */

#include <stdalign.h>
#include <stdbool.h>
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
#include "ui/nt_ui_radial.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Distinct from each other and from zero, so a missing or swapped block cannot pass. */
static const float k_defaults[4] = {0.125F, 0.25F, 0.5F, 1.0F};
static const float k_widget_block[4] = {3.0F, 5.0F, 7.0F, 11.0F};

/* One game attr the walker never injects (not a_layout/a_uvrect): the bytes must arrive verbatim. */
static nt_material_t make_one_attr_material(bool has_defaults) {
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.program = nt_gfx_fake_make_program((const char *const[]){"u_texture"}, 1);
    nt_gfx_fake_set_samplers(NULL, 0);
    desc.cull_mode = NT_CULL_NONE;
    desc.textures[0].name = "u_texture";
    desc.texture_count = 1;
    desc.attr_map[0].stream_name = "a_game";
    desc.attr_map[0].location = 4;
    memcpy(desc.attr_map[0].default_value, k_defaults, sizeof k_defaults);
    desc.attr_map_count = 1;
    desc.has_attr_defaults = has_defaults;
    desc.label = "base_custom_material";
    return nt_material_create(&desc);
}

void setUp(void) { ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL); }

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static void assert_batch_range_carries_defaults(uint32_t first, uint32_t end) {
    TEST_ASSERT_GREATER_THAN_UINT32(first, end);
    for (uint32_t v = first; v < end; ++v) {
        float got[4] = {0};
        nt_sprite_renderer_test_batch_custom(v, got, 4);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(k_defaults, got, sizeof k_defaults, "base emit bakes the material defaults");
    }
}

/* RECTANGLE + BORDER + IMAGE + slice9 IMAGE + nt_ui_image_custom REGION under one custom-attr base
 * material: one draw, every base vertex carries the defaults, the widget's vertices its own block. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_defaults_ride_every_base_emit_in_one_batch(void) {
    const nt_material_t mat = make_one_attr_material(true);
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

    /* The custom REGION is the last emit; everything staged before it is a base emit:
     * rect (4) + border + plain image (4) + slice9 (16). */
    const uint32_t first_widget_vertex = nt_sprite_renderer_test_last_emit_first_vertex();
    TEST_ASSERT_GREATER_THAN_UINT32(4U + 4U + 16U, first_widget_vertex);
    assert_batch_range_carries_defaults(0U, first_widget_vertex);
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    for (uint32_t v = 0; v < 4U; ++v) {
        float got[4] = {0};
        nt_sprite_renderer_test_batch_custom(first_widget_vertex + v, got, 4);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(k_widget_block, got, sizeof k_widget_block, "custom widget keeps its own block");
    }
}

static const float k_radial_defaults[8] = {9.0F, 8.0F, 7.0F, 6.0F, 5.0F, 4.0F, 3.0F, 2.0F};

/* The radial's [a_radial, a_layout] layout as the base material, so plain UI and radials share it. */
static nt_material_t make_radial_layout_material(void) {
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.program = nt_gfx_fake_make_program((const char *const[]){"u_texture"}, 1);
    nt_gfx_fake_set_samplers(NULL, 0);
    desc.cull_mode = NT_CULL_NONE;
    desc.textures[0].name = "u_texture";
    desc.texture_count = 1;
    desc.attr_map[0].stream_name = "a_radial";
    desc.attr_map[0].location = 4;
    memcpy(desc.attr_map[0].default_value, &k_radial_defaults[0], 4 * sizeof(float));
    desc.attr_map[1].stream_name = "a_layout";
    desc.attr_map[1].location = 7;
    memcpy(desc.attr_map[1].default_value, &k_radial_defaults[4], 4 * sizeof(float));
    desc.attr_map_count = 2;
    desc.has_attr_defaults = true;
    desc.label = "radial_layout_base_material";
    return nt_material_create(&desc);
}

/* Base emits of 6 (polygon), 16 (slice9) and fan-sized (rounded rect) vertices, then optionally a radial. */
static void declare_mixed_frame(nt_material_t mat, bool with_radial) {
    nt_atlas_region_ref_t poly_ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.polygon_region_idx);
    nt_atlas_region_ref_t slice_ref = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.packed_region_idx);
    const nt_ui_image_style_t plain = nt_ui_image_style_defaults();
    nt_ui_image_style_t sliced = nt_ui_image_style_defaults();
    sliced.flags |= NT_UI_IMAGE_SLICE9_OVERRIDE;
    sliced.slice9_lrtb[0] = 2;
    sliced.slice9_lrtb[1] = 2;
    sliced.slice9_lrtb[2] = 2;
    sliced.slice9_lrtb[3] = 2;
    nt_ui_radial_style_t radial = nt_ui_radial_style_defaults();
    radial.material = mat;
    const Clay_ElementDeclaration box = {.layout = {.sizing = {CLAY_SIZING_FIXED(64), CLAY_SIZING_FIXED(32)}}};

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        CLAY({.id = CLAY_ID("rounded"), .layout = box.layout, .backgroundColor = {255, 0, 0, 255}, .cornerRadius = CLAY_CORNER_RADIUS(6)}) {}
        nt_ui_image(s_fx.ctx, NULL, &slice_ref, &sliced, &box);
        nt_ui_image(s_fx.ctx, NULL, &poly_ref, &plain, &box);
        if (with_radial) {
            nt_ui_radial(s_fx.ctx, NULL, 0.25F, 1.75F, &radial, &box);
        }
    }
    nt_ui_end(s_fx.ctx);
}

/* A GEOMETRY widget after base emits of any vertex count stays in the batch, padded to a quad boundary. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_geometry_widget_shares_base_batch_aligned(void) {
    const nt_material_t mat = make_radial_layout_material();
    nt_ui_set_sprite_material(s_fx.ctx, mat);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};

    declare_mixed_frame(mat, false);
    nt_ui_walk(s_fx.ctx, &target);
    const uint32_t base_end = nt_sprite_renderer_test_last_emit_first_vertex() + nt_sprite_renderer_test_last_emit_vertex_count();
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0U, base_end % 4U, "the base emits leave the batch off a quad boundary");

    declare_mixed_frame(mat, true);
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
    const uint32_t first = nt_sprite_renderer_test_last_emit_first_vertex();
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(0U, first % 4U);
    TEST_ASSERT_TRUE_MESSAGE(first > base_end && first < base_end + 4U, "padding is 1..3 vertices");
    for (uint32_t v = 0; v < base_end; ++v) {
        float got[8] = {0};
        nt_sprite_renderer_test_batch_custom(v, got, 8);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(k_radial_defaults, got, sizeof k_radial_defaults, "base emit bakes the material defaults");
    }

    /* Corner k of the quad is vertex first+k: TL, TR, BR, BL (GL Y-up), each with the radial's block. */
    float pos[4][3];
    for (uint32_t k = 0; k < 4U; ++k) {
        nt_sprite_renderer_test_last_emit_position(k, pos[k]);
        float got[8] = {0};
        nt_sprite_renderer_test_last_emit_radial(k, got, 8);
        TEST_ASSERT_TRUE(got[0] == 0.25F);
        TEST_ASSERT_TRUE(got[1] == 1.75F);
        TEST_ASSERT_TRUE(got[4] == 2.0F); /* a_layout.x = 64/32 */
    }
    TEST_ASSERT_TRUE(pos[0][0] < pos[1][0] && pos[0][1] == pos[1][1]);
    TEST_ASSERT_TRUE(pos[2][0] == pos[1][0] && pos[2][1] < pos[1][1]);
    TEST_ASSERT_TRUE(pos[3][0] == pos[0][0] && pos[3][1] == pos[2][1]);
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
/* Base emits pass no block, so a custom-attr base without defaults is rejected where it is set. */
static void test_custom_attr_base_without_defaults_asserts(void) { NT_TEST_EXPECT_ASSERT(nt_ui_set_sprite_material(s_fx.ctx, make_one_attr_material(false))); }
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_ride_every_base_emit_in_one_batch);
    RUN_TEST(test_geometry_widget_shares_base_batch_aligned);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_custom_attr_base_without_defaults_asserts);
#endif
    return UNITY_END();
}
