/* A custom-attr base material: the game's base block rides every base emit, so plain widgets and
 * nt_ui_image_custom share one material and one batch. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"
#include "math/nt_math.h"
#include "renderers/nt_sprite_renderer.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_debug_hit_zones.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_radial.h"
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

/* One batch from the walker's entry flush: every staged vertex so far must carry the base block. */
static void assert_whole_batch_carries_base_block(void) {
    const uint32_t total = nt_sprite_renderer_test_last_emit_first_vertex() + nt_sprite_renderer_test_last_emit_vertex_count();
    TEST_ASSERT_GREATER_THAN_UINT32(0U, total);
    for (uint32_t v = 0; v < total; ++v) {
        float got[4] = {0};
        nt_sprite_renderer_test_batch_custom(v, got, 4);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(k_base_block, got, sizeof k_base_block, "base emit carries the game's base block");
    }
}

/* The rect/border paths that build geometry instead of one screen quad: rounded rect, rounded border,
 * and a square border under rotation (its mesh path, not the four-quad fast path). */
static void test_base_block_rides_geometry_rect_and_border_paths(void) {
    nt_ui_set_sprite_material(s_fx.ctx, make_one_attr_material(), k_base_block, sizeof k_base_block);
    static nt_ui_transform_t s_rot;
    s_rot = nt_ui_transform_defaults();
    s_rot.rotation_z = 0.3F;
    const Clay_Sizing box = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(30)};
    const Clay_BorderElementConfig border = {.color = {0, 255, 0, 255}, .width = {.left = 2, .right = 2, .top = 2, .bottom = 2}};

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        CLAY({.id = CLAY_ID("rounded_rect"), .layout = {.sizing = box}, .backgroundColor = {255, 0, 0, 255}, .cornerRadius = CLAY_CORNER_RADIUS(6)}) {}
        CLAY({.id = CLAY_ID("rounded_border"), .layout = {.sizing = box}, .cornerRadius = CLAY_CORNER_RADIUS(6), .border = border}) {}
        CLAY({.id = CLAY_ID("rotated_border"), .layout = {.sizing = box}, .border = border, .userData = (void *)NT_UI_DATA_XFORM(0U, &s_rot, 1.0F)}) {}
    }
    nt_ui_end(s_fx.ctx);

    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_get_last_walk_border_command_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_get_last_walk_draw_calls(s_fx.ctx));
    TEST_ASSERT_GREATER_THAN_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count()); /* rotated border = one mesh */
    assert_whole_batch_carries_base_block();
}

#define MAX_TEST_CMDS 4
static Clay_RenderCommand s_cmds[MAX_TEST_CMDS];
static nt_ui_image_payload_t s_payload;

/* One raw IMAGE command on the white region whose payload names `material` (0 = base). */
static void inject_image(nt_resource_t atlas, nt_material_t material, const nt_ui_image_custom_block_t *custom) {
    memset(s_cmds, 0, sizeof s_cmds);
    s_payload = (nt_ui_image_payload_t){.atlas = atlas, .region_index = s_fx.atlas.white_region_idx, .slice9_scale = 1.0F, .material = material, .custom = custom};
    s_cmds[0].commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
    s_cmds[0].boundingBox = (Clay_BoundingBox){.x = 10, .y = 10, .width = 32, .height = 32};
    s_cmds[0].renderData.image.imageData = &s_payload;
    ui_walker_fixture_inject_cmds(&s_fx, s_cmds, 1, MAX_TEST_CMDS);
}

/* Equal-handle rule: an explicit per-element override that names the base handle gets the block. */
static void test_override_equal_to_base_gets_block(void) {
    const nt_material_t mat = make_one_attr_material();
    nt_ui_set_sprite_material(s_fx.ctx, mat, k_base_block, sizeof k_base_block);
    inject_image(s_fx.atlas.handle, mat, NULL);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32(4U, nt_sprite_renderer_test_last_emit_vertex_count());
    assert_whole_batch_carries_base_block();
}

static const float k_radial_base_block[8] = {9.0F, 8.0F, 7.0F, 6.0F, 5.0F, 4.0F, 3.0F, 2.0F};

/* The radial's [a_radial, a_layout] layout as the base material, so plain UI and radials share it. */
static nt_material_t make_radial_layout_material(void) {
    nt_material_create_desc_t desc;
    memset(&desc, 0, sizeof desc);
    desc.program = nt_gfx_fake_make_program((const char *const[]){"u_texture"}, 1);
    nt_gfx_fake_set_samplers(NULL, 0);
    desc.cull_mode = NT_CULL_NONE;
    desc.color_mode = NT_COLOR_MODE_NONE;
    desc.textures[0].name = "u_texture";
    desc.texture_count = 1;
    desc.attr_map[0].stream_name = "a_radial";
    desc.attr_map[0].location = 4;
    desc.attr_map[1].stream_name = "a_layout";
    desc.attr_map[1].location = 7;
    desc.attr_map_count = 2;
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
    nt_ui_set_sprite_material(s_fx.ctx, mat, k_radial_base_block, sizeof k_radial_base_block);
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
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(k_radial_base_block, got, sizeof k_radial_base_block, "base emit carries the base block");
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

static void test_plain_setter_takes_no_block(void) {
    nt_ui_set_sprite_material(s_fx.ctx, make_one_attr_material(), k_base_block, sizeof k_base_block);
    nt_ui_set_sprite_material(s_fx.ctx, s_fx.sprite_material, NULL, 0U);
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->base_custom_bytes);
}

/* The ctx keeps a copy: the game's array may be a temporary. */
static void test_setter_copies_the_block(void) {
    float block[4];
    memcpy(block, k_base_block, sizeof block);
    const nt_material_t mat = make_one_attr_material();
    nt_ui_set_sprite_material(s_fx.ctx, mat, block, sizeof block);
    memset(block, 0, sizeof block);
    inject_image(s_fx.atlas.handle, NT_MATERIAL_INVALID, NULL);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    assert_whole_batch_carries_base_block();
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
static void test_set_asserts_on_stride_mismatch(void) {
    const nt_material_t mat = make_one_attr_material();
    NT_TEST_EXPECT_ASSERT(nt_ui_set_sprite_material(s_fx.ctx, mat, k_base_block, 8U));
    NT_TEST_EXPECT_ASSERT(nt_ui_set_sprite_material(s_fx.ctx, mat, k_base_block, 0U));
    NT_TEST_EXPECT_ASSERT(nt_ui_set_sprite_material(s_fx.ctx, mat, NULL, 0U));
    NT_TEST_EXPECT_ASSERT(nt_ui_set_sprite_material(s_fx.ctx, s_fx.sprite_material, k_base_block, 0U));
    NT_TEST_EXPECT_ASSERT(nt_ui_set_sprite_material(s_fx.ctx, s_fx.sprite_material, k_base_block, sizeof k_base_block));
}

/* An override that names a different custom-attr material gets no base block. */
static void test_other_custom_override_without_block_asserts(void) {
    nt_ui_set_sprite_material(s_fx.ctx, make_one_attr_material(), k_base_block, sizeof k_base_block);
    inject_image(s_fx.atlas.handle, make_one_attr_material(), NULL);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

/* A block staged for an image that never drew (atlas not ready) must not satisfy a later emit. */
static void test_unconsumed_block_does_not_outlive_a_bind(void) {
    const nt_material_t mat = make_one_attr_material();
    nt_ui_set_sprite_material(s_fx.ctx, mat, k_base_block, sizeof k_base_block);
    inject_image((nt_resource_t){.id = 0xDEADBEEFU}, NT_MATERIAL_INVALID, NULL);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);

    nt_sprite_renderer_set_material(mat);
    const float quad[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
    NT_TEST_EXPECT_ASSERT(nt_sprite_renderer_emit_geometry(s_fx.atlas.handle, s_fx.atlas.white_region_idx, quad, 4, idx, 6, NT_MATH_MAT4_IDENTITY, 0xFFFFFFFFU));
}

static nt_ui_image_payload_t s_payloads[2];

/* A custom widget that draws nothing, then a blockless override on the same material: no rebind
 * between them, so the second emit must assert instead of baking the first widget's block. */
static void expect_blockless_override_asserts_after(nt_resource_t first_atlas, Clay_BoundingBox first_bb, const nt_ui_image_custom_block_t *first_custom) {
    nt_ui_set_sprite_material(s_fx.ctx, make_one_attr_material(), k_base_block, sizeof k_base_block);
    const nt_material_t other = make_one_attr_material();
    memset(s_cmds, 0, sizeof s_cmds);
    s_payloads[0] = (nt_ui_image_payload_t){.atlas = first_atlas, .region_index = s_fx.atlas.white_region_idx, .slice9_scale = 1.0F, .material = other, .custom = first_custom};
    s_payloads[1] = (nt_ui_image_payload_t){.atlas = s_fx.atlas.handle, .region_index = s_fx.atlas.white_region_idx, .slice9_scale = 1.0F, .material = other};
    for (uint32_t i = 0; i < 2U; ++i) {
        s_cmds[i].commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE;
        s_cmds[i].boundingBox = (Clay_BoundingBox){.x = 10, .y = 10, .width = 32, .height = 32};
        s_cmds[i].renderData.image.imageData = &s_payloads[i];
    }
    s_cmds[0].boundingBox = first_bb;
    ui_walker_fixture_inject_cmds(&s_fx, s_cmds, 2, MAX_TEST_CMDS);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &target));
}

static void test_block_of_undrawn_region_widget_does_not_leak(void) {
    static const nt_ui_image_custom_block_t blk = {.custom_attrs = {3.0F, 5.0F, 7.0F, 11.0F}, .custom_bytes = 16, .geom_mode = NT_UI_IMAGE_GEOM_REGION};
    expect_blockless_override_asserts_after((nt_resource_t){.id = 0xDEADBEEFU}, (Clay_BoundingBox){.x = 10, .y = 10, .width = 32, .height = 32}, &blk);
}

static void test_block_of_empty_geometry_widget_does_not_leak(void) {
    static const nt_ui_image_custom_block_t blk = {.custom_attrs = {3.0F, 5.0F, 7.0F, 11.0F}, .custom_bytes = 16, .geom_mode = NT_UI_IMAGE_GEOM_GEOMETRY};
    expect_blockless_override_asserts_after(s_fx.atlas.handle, (Clay_BoundingBox){.x = 10, .y = 10, .width = 0, .height = 32}, &blk);
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
    const uint32_t first_before = nt_sprite_renderer_test_last_emit_first_vertex();
    float pos_before[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos_before);
    draw_inspector_overlay();
    float pos_after[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos_after);
    const bool drew = first_before != nt_sprite_renderer_test_last_emit_first_vertex() || pos_before[0] != pos_after[0] || pos_before[1] != pos_after[1];
    TEST_ASSERT_TRUE_MESSAGE(drew, "the highlight emitted after the walks");
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

/* The inspector walk draws its panel on the plain inspector material without the block, then hands
 * the game's material and block back to the next game walk. */
static void test_inspector_walk_swaps_the_block_out_and_back(void) {
    select_debug_element(false, false);
    nt_ui_inspector_set_materials(s_fx.ctx, s_fx.sprite_material, s_fx.text_material);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    nt_ui_debug_inspector_walk(s_fx.ctx, &target);
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0U, nt_ui_get_last_walk_rect_command_count(s_fx.ctx), "the inspector walk draws on the swapped material");
    nt_sprite_renderer_flush();
    nt_ui_walk(s_fx.ctx, &target);
    assert_whole_batch_carries_base_block();
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
/* The inspector passes stage no block, so their own material must be plain. */
static void test_custom_inspector_material_asserts(void) { NT_TEST_EXPECT_ASSERT(nt_ui_inspector_set_materials(s_fx.ctx, make_one_attr_material(), s_fx.text_material)); }
#endif
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_base_block_rides_every_base_emit_in_one_batch);
    RUN_TEST(test_base_block_rides_geometry_rect_and_border_paths);
    RUN_TEST(test_override_equal_to_base_gets_block);
    RUN_TEST(test_geometry_widget_shares_base_batch_aligned);
    RUN_TEST(test_plain_setter_takes_no_block);
    RUN_TEST(test_setter_copies_the_block);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_set_asserts_on_stride_mismatch);
    RUN_TEST(test_other_custom_override_without_block_asserts);
    RUN_TEST(test_unconsumed_block_does_not_outlive_a_bind);
    RUN_TEST(test_block_of_undrawn_region_widget_does_not_leak);
    RUN_TEST(test_block_of_empty_geometry_widget_does_not_leak);
#endif
#if NT_UI_DEBUG_TOOLS
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_custom_inspector_material_asserts);
#endif
    RUN_TEST(test_hit_zones_carry_base_block_2d);
    RUN_TEST(test_hit_zones_carry_base_block_3d);
    RUN_TEST(test_inspector_highlight_carries_base_block_2d_transformed);
    RUN_TEST(test_inspector_highlight_carries_base_block_2d_axis_aligned);
    RUN_TEST(test_inspector_highlight_carries_base_block_3d);
    RUN_TEST(test_plain_inspector_material_stages_no_block_3d);
    RUN_TEST(test_inspector_walk_swaps_the_block_out_and_back);
#endif
    return UNITY_END();
}
