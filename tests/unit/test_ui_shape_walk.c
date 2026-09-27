#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nt_pack_format.h"
#include "renderers/nt_sprite_renderer.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui_shape.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;
static nt_material_t s_body_material;
static nt_material_t s_shadow_material;
static bool s_export_gpu_cases;

static nt_material_t make_shape_material(nt_program_t program) {
    return nt_material_create(&(nt_material_create_desc_t){
        .vertex_layout = {.stride = 84,
                          .attr_count = 7,
                          .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
                                    {.location = 3, .type = NT_VERTEX_UINT16, .count = 2, .normalized = true, .offset = 12},
                                    {.location = 2, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 16},
                                    {.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 20},
                                    {.location = 5, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 36},
                                    {.location = 6, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 52},
                                    {.location = 7, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 68}}},
        .vertex_defaults = (const float[21]){0},
        .program = program,
        .textures = {{.name = "u_texture"}},
        .texture_count = 1,
        .attr_map = {{.stream_name = "a_shape_layout", .location = 4},
                     {.stream_name = "a_shape_geometry", .location = 5},
                     {.stream_name = "a_shape_paint", .location = 6},
                     {.stream_name = "a_shape_border", .location = 7}},
        .attr_map_count = 4,
    });
}

void setUp(void) {
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    s_body_material = make_shape_material(nt_material_get_info(s_fx.sprite_material)->program);
    const nt_program_t shadow_program = nt_gfx_fake_make_program((const char *const[]){"u_texture"}, 1);
    nt_gfx_fake_set_samplers(NULL, 0);
    s_shadow_material = make_shape_material(shadow_program);
    nt_gfx_fake_draw_trace_reset(true);
}

void tearDown(void) {
    nt_material_destroy(s_shadow_material);
    nt_material_destroy(s_body_material);
    ui_walker_fixture_shutdown(&s_fx);
}

static void begin_frame(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
}

static void end_and_walk(void) {
    nt_ui_end(s_fx.ctx);
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_FALSE(nt_gfx_fake_draw_trace_overflowed());
}

static nt_ui_shape_style_t box_style(void) {
    nt_ui_shape_style_t style = nt_ui_shape_style_defaults();
    style.material = s_body_material;
    style.box = (nt_ui_shape_radii_t){40.0F, 40.0F, 10.0F, 10.0F};
    return style;
}

static void emit_box(const nt_ui_shape_style_t *style, const nt_ui_element_data_t *data) {
    nt_ui_shape(s_fx.ctx, data, style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}});
}

static void assert_last_attrs(const float expected[16]) {
    TEST_ASSERT_EQUAL_UINT32(4, nt_sprite_renderer_test_last_emit_vertex_count());
    TEST_ASSERT_EQUAL_UINT32(6, nt_sprite_renderer_test_last_emit_index_count());
    for (uint32_t vertex = 0; vertex < 4; ++vertex) {
        float actual[16];
        nt_sprite_renderer_test_last_emit_radial(vertex, actual, 16);
        for (uint32_t field = 0; field < 16; ++field) {
            TEST_ASSERT_TRUE_MESSAGE(fabsf(expected[field] - actual[field]) <= 0.0001F, "shape attributes must be identical on all four vertices");
        }
    }
}

static void test_box_layout_and_asymmetric_radii(void) {
    const nt_ui_shape_style_t style = box_style();
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();

    nt_sprite_layout_info_t layout;
    nt_sprite_renderer_test_layout(s_body_material, &layout);
    TEST_ASSERT_EQUAL_UINT32(84, layout.stride);
    TEST_ASSERT_EQUAL_UINT32(7, layout.attr_count);
    for (uint32_t i = 3; i < 7; ++i) {
        TEST_ASSERT_EQUAL_UINT32(i + 1U, layout.locations[i]);
        TEST_ASSERT_EQUAL_UINT32(20U + ((i - 3U) * 16U), layout.offsets[i]);
    }
    const float expected[16] = {200, 60, 1, 0, 40, 40, 10, 10, 0, -16777216, 1, 65536};
    assert_last_attrs(expected);
    float top_left[3];
    float bottom_right[3];
    nt_sprite_renderer_test_last_emit_position(0, top_left);
    nt_sprite_renderer_test_last_emit_position(2, bottom_right);
    TEST_ASSERT_TRUE(top_left[0] == -1.0F && top_left[1] == 601.0F);
    TEST_ASSERT_TRUE(bottom_right[0] == 201.0F && bottom_right[1] == 539.0F);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_draw_trace_at(0).num_indices);
}

static void test_radii_share_css_adjacent_edge_scale(void) {
    nt_ui_shape_style_t style = box_style();
    style.box.top_left = 80.0F;
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();
    const float expected[16] = {200, 60, 1, 0, 160.0F / 3.0F, 80.0F / 3.0F, 20.0F / 3.0F, 20.0F / 3.0F, 0, -16777216, 1, 65536};
    assert_last_attrs(expected);
}

static void test_four_border_sides_keep_layout_order_and_full_precision(void) {
    nt_ui_shape_style_t style = box_style();
    style.paint.border_widths = (nt_ui_shape_border_widths_t){8, 1, 2, 4};
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();
    const float expected[16] = {200, 60, 1, 0, 40, 40, 10, 10, 0, -16777216, 1, 65536, 8, 1, 2, 4};
    assert_last_attrs(expected);
}

static void test_zero_sides_do_not_inherit_another_side(void) {
    nt_ui_shape_style_t style = box_style();
    style.paint.border_widths = (nt_ui_shape_border_widths_t){0, 0.1F, 0, 0.5F};
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();
    const float expected[16] = {200, 60, 1, 0, 40, 40, 10, 10, 0, -16777216, 1, 65536, 0, 0.1F, 0, 0.5F};
    assert_last_attrs(expected);
}

static void test_zero_height_interior_is_marked_empty(void) {
    nt_ui_shape_style_t style = box_style();
    style.box = (nt_ui_shape_radii_t){0};
    style.paint.border_widths = (nt_ui_shape_border_widths_t){20, 20, 20, 20};
    begin_frame();
    nt_ui_shape(s_fx.ctx, NULL, &style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(40)}});
    end_and_walk();
    const float expected[16] = {96, 40, 1, 0, 0, 0, 0, 0, 0, -16777216, 1, -65536, 20, 20, 20, 20};
    assert_last_attrs(expected);
}

static void test_overlapping_corner_constraints_can_empty_positive_inset(void) {
    nt_ui_shape_style_t style = box_style();
    style.box = (nt_ui_shape_radii_t){90, 10, 90, 10};
    style.paint.border_widths = (nt_ui_shape_border_widths_t){40, 40, 40, 40};
    begin_frame();
    nt_ui_shape(s_fx.ctx, NULL, &style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(100)}});
    end_and_walk();
    const float expected[16] = {100, 100, 1, 0, 90, 10, 90, 10, 0, -16777216, 1, -65536, 40, 40, 40, 40};
    assert_last_attrs(expected);
}

static void test_overlapping_corners_keep_nonempty_interior(void) {
    nt_ui_shape_style_t style = box_style();
    style.box = (nt_ui_shape_radii_t){90, 10, 90, 10};
    style.paint.border_widths = (nt_ui_shape_border_widths_t){30, 30, 30, 30};
    begin_frame();
    nt_ui_shape(s_fx.ctx, NULL, &style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(100)}});
    end_and_walk();
    const float expected[16] = {100, 100, 1, 0, 90, 10, 90, 10, 0, -16777216, 1, 65536, 30, 30, 30, 30};
    assert_last_attrs(expected);
}

static void test_paint_alpha_stays_separate_from_inherited_opacity(void) {
    nt_ui_shape_style_t style = box_style();
    style.paint = (nt_ui_shape_paint_t){.color0 = 0x80204080U, .color1 = 0x00010203U, .border_color = 0x404080C0U, .border_widths = {0.1F, 0.1F, 0.1F, 0.1F}, .gradient = NT_UI_SHAPE_HORIZONTAL};
    const nt_ui_transform_t identity = nt_ui_transform_defaults();
    begin_frame();
    CLAY({.layout.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}, .userData = (void *)NT_UI_DATA_XFORM(0, &identity, 0.5F)}) { emit_box(&style, NT_UI_DATA_XFORM(0, &identity, 0.5F)); }
    end_and_walk();
    const float expected[16] = {200, 60, 1, 0, 40, 40, 10, 10, 0, -66052, -4227265, 4194433, 0.1F, 0.1F, 0.1F, 0.1F};
    assert_last_attrs(expected);
    for (uint32_t vertex = 0; vertex < 4; ++vertex) {
        uint8_t color[4];
        nt_sprite_renderer_test_last_emit_color(vertex, color);
        TEST_ASSERT_EQUAL_UINT8(128, color[0]);
        TEST_ASSERT_EQUAL_UINT8(64, color[1]);
        TEST_ASSERT_EQUAL_UINT8(32, color[2]);
        TEST_ASSERT_EQUAL_UINT8(64, color[3]);
    }
}

static void test_radial_parameters_and_vertical_gradient_reach_vertices(void) {
    nt_ui_shape_style_t style = nt_ui_shape_style_defaults();
    style.material = s_body_material;
    style.kind = NT_UI_SHAPE_RADIAL;
    style.radial.angle_start = 5.5F;
    style.radial.angle_end = 0.75F;
    style.radial.inner_radius_norm = 0.6F;
    style.paint.gradient = NT_UI_SHAPE_VERTICAL;
    style.paint.color1 = 0x00112233U;
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();
    const float expected[16] = {200, 60, 1, 0, 5.5F, 0.75F, 0.6F, 0, 0, 1122868.0F * 4294967296.0F, 4294967296.0F, 256};
    assert_last_attrs(expected);
}

static void test_shadow_precedes_body_with_distinct_materials(void) {
    nt_ui_shape_style_t style = box_style();
    style.shadow = (nt_ui_shape_shadow_t){.material = s_shadow_material, .color = 0x80402010U, .offset_x = 4, .offset_y = 8, .spread = 2, .softness = 3};
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(nt_material_get_info(s_shadow_material)->program.id, nt_gfx_fake_draw_trace_at(0).program.id);
    TEST_ASSERT_EQUAL_UINT32(nt_material_get_info(s_body_material)->program.id, nt_gfx_fake_draw_trace_at(1).program.id);
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_draw_trace_at(0).num_indices);
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_draw_trace_at(1).num_indices);
    const float expected[16] = {200, 60, 1, 0, 40, 40, 10, 10, 0, -16777216, 1, 65536};
    assert_last_attrs(expected);
}

static void test_transparent_body_keeps_only_visible_shadow(void) {
    nt_ui_shape_style_t style = box_style();
    style.paint.color0 = 0;
    style.shadow = (nt_ui_shape_shadow_t){.material = s_shadow_material, .color = 0x80402010U, .spread = 2, .softness = 3};
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_draw_trace_at(0).num_indices);
    TEST_ASSERT_EQUAL_UINT32(nt_material_get_info(s_shadow_material)->program.id, nt_gfx_fake_draw_trace_at(0).program.id);
    const float expected[16] = {200, 60, 6, 0, 40, 40, 10, 10, 0, -4294967296.0F, 1, 129, 2, 3, 5, 0};
    assert_last_attrs(expected);
}

static void test_transparent_shape_container_keeps_visible_children(void) {
    nt_ui_set_sprite_material(s_fx.ctx, s_body_material);
    nt_ui_shape_style_t style = box_style();
    style.paint.color0 = 0;
    begin_frame();
    nt_ui_shape_begin(s_fx.ctx, NULL, &style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}});
    CLAY({.layout.sizing = {CLAY_SIZING_FIXED(20), CLAY_SIZING_FIXED(20)}, .backgroundColor = {255, 0, 0, 128}}) {}
    nt_ui_shape_end(s_fx.ctx);
    end_and_walk();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_draw_trace_at(0).num_indices);
    const float expected[16] = {0};
    assert_last_attrs(expected);
    uint8_t color[4];
    nt_sprite_renderer_test_last_emit_color(0, color);
    TEST_ASSERT_EQUAL_UINT8(255, color[0]);
    TEST_ASSERT_EQUAL_UINT8(128, color[3]);
}

static void test_uber_batches_shadow_shapes_and_plain_rect_without_attr_leak(void) {
    nt_ui_set_sprite_material(s_fx.ctx, s_body_material);
    nt_ui_shape_style_t style = box_style();
    style.shadow = (nt_ui_shape_shadow_t){.material = s_body_material, .color = 0xFF000000U, .spread = 2, .softness = 3};
    begin_frame();
    CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .sizing = {CLAY_SIZING_FIXED(800), CLAY_SIZING_FIXED(100)}}}) {
        emit_box(&style, NULL);
        style.shadow.color = 0;
        style.paint.border_widths = (nt_ui_shape_border_widths_t){0.5F, 0.5F, 0.5F, 0.5F};
        emit_box(&style, NULL);
        CLAY({.layout.sizing = {CLAY_SIZING_FIXED(20), CLAY_SIZING_FIXED(20)}, .backgroundColor = {255, 0, 0, 128}}) {}
    }
    end_and_walk();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(24, nt_gfx_fake_draw_trace_at(0).num_indices);
    const float expected[16] = {0};
    assert_last_attrs(expected);
    uint8_t color[4];
    nt_sprite_renderer_test_last_emit_color(0, color);
    TEST_ASSERT_EQUAL_UINT8(255, color[0]);
    TEST_ASSERT_EQUAL_UINT8(128, color[3]);
}

static void test_offscreen_layout_transformed_into_view_still_draws(void) {
    const nt_ui_shape_style_t style = box_style();
    nt_ui_transform_t transform = nt_ui_transform_defaults();
    transform.offset_y = -650.0F;
    begin_frame();
    CLAY({.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = {CLAY_SIZING_FIXED(800), CLAY_SIZING_FIXED(1000)}}}) {
        CLAY({.layout.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(700)}}) {}
        emit_box(&style, NT_UI_DATA_XFORM(0, &transform, 1.0F));
    }
    end_and_walk();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    float position[3];
    nt_sprite_renderer_test_last_emit_position(0, position);
    TEST_ASSERT_TRUE(position[0] == -1.0F && position[1] == 551.0F);
}

static void assert_skipped_custom_image_preserves_plain_defaults(nt_resource_t atlas, uint8_t geom_mode, float width) {
    nt_ui_set_sprite_material(s_fx.ctx, s_body_material);
    const nt_ui_image_custom_block_t block = {.custom_attrs = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}, .custom_bytes = 64, .geom_mode = geom_mode};
    const nt_ui_image_payload_t payload = {.atlas = atlas, .region_index = s_fx.atlas.white_region_idx, .slice9_scale = 1.0F, .material = s_body_material, .custom = &block};
    Clay_RenderCommand commands[2] = {
        {.commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE, .boundingBox = {10, 10, width, 20}, .renderData.image.imageData = (void *)&payload},
        {.commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE, .boundingBox = {40, 10, 20, 20}, .renderData.rectangle.backgroundColor = {255, 0, 0, 128}},
    };
    ui_walker_fixture_inject_cmds(&s_fx, commands, 2, 2);
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_draw_trace_at(0).num_indices);
    const float expected[16] = {0};
    assert_last_attrs(expected);
    uint8_t color[4];
    nt_sprite_renderer_test_last_emit_color(0, color);
    TEST_ASSERT_EQUAL_UINT8(255, color[0]);
    TEST_ASSERT_EQUAL_UINT8(128, color[3]);
}

static void test_zero_width_custom_geometry_does_not_leak_attrs_to_plain_rect(void) { assert_skipped_custom_image_preserves_plain_defaults(s_fx.atlas.handle, NT_UI_IMAGE_GEOM_GEOMETRY, 0.0F); }

static void test_unready_custom_region_does_not_leak_attrs_to_plain_rect(void) {
    const nt_resource_t atlas = nt_resource_request((nt_hash64_t){.value = 0x518BADU}, NT_ASSET_ATLAS);
    TEST_ASSERT_NOT_EQUAL(0, atlas.id);
    TEST_ASSERT_FALSE(nt_resource_is_ready(atlas));
    assert_skipped_custom_image_preserves_plain_defaults(atlas, NT_UI_IMAGE_GEOM_REGION, 20.0F);
}

static void test_shadow_only_visible_keeps_outset_and_offset(void) {
    nt_ui_shape_style_t style = box_style();
    style.shadow = (nt_ui_shape_shadow_t){.material = s_shadow_material, .color = 0x80402010U, .offset_x = 4, .offset_y = -150, .spread = 2, .softness = 3};
    begin_frame();
    CLAY({.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = {CLAY_SIZING_FIXED(800), CLAY_SIZING_FIXED(1000)}}}) {
        CLAY({.layout.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(700)}}) {}
        emit_box(&style, NULL);
    }
    end_and_walk();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(nt_material_get_info(s_shadow_material)->program.id, nt_gfx_fake_draw_trace_at(0).program.id);
    const float expected[16] = {200, 60, 6, 0, 40, 40, 10, 10, 0, -4294967296.0F, 1, 129, 2, 3, 5, 0};
    assert_last_attrs(expected);
    float position[3];
    nt_sprite_renderer_test_last_emit_position(0, position);
    TEST_ASSERT_TRUE(position[0] == -2.0F && position[1] == 56.0F);
    uint8_t color[4];
    nt_sprite_renderer_test_last_emit_color(0, color);
    TEST_ASSERT_EQUAL_UINT8(16, color[0]);
    TEST_ASSERT_EQUAL_UINT8(32, color[1]);
    TEST_ASSERT_EQUAL_UINT8(64, color[2]);
    TEST_ASSERT_EQUAL_UINT8(255, color[3]);
}

static void test_transformed_paint_outside_view_is_culled(void) {
    nt_ui_shape_style_t style = box_style();
    style.shadow = (nt_ui_shape_shadow_t){.material = s_shadow_material, .color = 0xFF000000U, .spread = 2, .softness = 3};
    nt_ui_transform_t transform = nt_ui_transform_defaults();
    transform.offset_x = 1000.0F;
    begin_frame();
    emit_box(&style, NT_UI_DATA_XFORM(0, &transform, 1.0F));
    end_and_walk();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_draw_trace_count());
}

static void test_aa_guard_accounts_for_nonuniform_scale_and_framebuffer_density(void) {
    const nt_ui_shape_style_t style = box_style();
    nt_ui_transform_t transform = nt_ui_transform_defaults();
    transform.scale_x = 0.25F;
    transform.scale_y = 2.0F;
    begin_frame();
    emit_box(&style, NT_UI_DATA_XFORM(0, &transform, 1.0F));
    nt_ui_end(s_fx.ctx);
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}, .fb_size = {1600, 1200}};
    nt_ui_walk(s_fx.ctx, &target);
    const float expected[16] = {200, 60, 2, 0, 40, 40, 10, 10, 0, -16777216, 1, 65536};
    assert_last_attrs(expected);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
}

static void setup_projective_context(void) {
    nt_ui_destroy_context(s_fx.ctx);
    nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    desc.use_raycast_input = true;
    s_fx.ctx = nt_ui_create_context(s_arena, sizeof s_arena, &desc);
    TEST_ASSERT_NOT_NULL(s_fx.ctx);
    nt_ui_set_font(s_fx.ctx, 0U, s_fx.stub_font);
    nt_ui_set_atlas_white_region(s_fx.ctx, s_fx.atlas.handle, s_fx.atlas.white_region_idx);
    nt_ui_set_sprite_material(s_fx.ctx, s_fx.sprite_material);
    nt_ui_set_text_material(s_fx.ctx, s_fx.text_material);
}

static void test_affine_guard_matches_rounded_physical_framebuffer_offset(void) {
    const nt_ui_shape_style_t style = box_style();
    nt_ui_transform_t transform = nt_ui_transform_defaults();
    transform.scale_x = 0.25F;
    transform.scale_y = 2.0F;
    begin_frame();
    emit_box(&style, NT_UI_DATA_XFORM(0, &transform, 1.0F));
    nt_ui_end(s_fx.ctx);
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}, .fb_size = {1600, 1200}, .fb_offset = {0.6F, 2.6F}};
    nt_ui_walk(s_fx.ctx, &target);
    float attrs[16];
    nt_sprite_renderer_test_last_emit_radial(0, attrs, 16);
    TEST_ASSERT_TRUE(fabsf((800.0F / (0.25F * 1598.0F)) - attrs[2]) <= 0.0000005F);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
}

static void export_projective_case(const char *name, const float view_proj[16], const nt_ui_target_t *target, bool visible) {
    if (!s_export_gpu_cases) {
        return;
    }
    printf("UI_SHAPE_GPU_CASE {\"name\":\"%s\",\"expectedPixelCoverage\":\"%s\",\"emittedQuads\":%u,\"viewProj\":[", name, visible ? "nonempty" : "empty", (unsigned)nt_gfx_fake_draw_trace_count());
    for (int i = 0; i < 16; ++i) {
        printf("%s%.9g", i == 0 ? "" : ",", (double)view_proj[i]);
    }
    printf("],\"viewport\":[%.9g,%.9g,%.9g,%.9g],\"fbSize\":[%.9g,%.9g],\"fbOffset\":[%.9g,%.9g],\"vertices\":[", (double)target->viewport[0], (double)target->viewport[1], (double)target->viewport[2],
           (double)target->viewport[3], (double)target->fb_size[0], (double)target->fb_size[1], (double)target->fb_offset[0], (double)target->fb_offset[1]);
    if (nt_gfx_fake_draw_trace_count() > 0U) {
        for (uint32_t vertex = 0; vertex < 4U; ++vertex) {
            float position[3];
            float attrs[16];
            uint8_t color[4];
            nt_sprite_renderer_test_last_emit_position(vertex, position);
            nt_sprite_renderer_test_last_emit_radial(vertex, attrs, 16);
            nt_sprite_renderer_test_last_emit_color(vertex, color);
            printf("%s{\"position\":[%.9g,%.9g,%.9g],\"color\":[%u,%u,%u,%u],\"attrs\":[", vertex == 0U ? "" : ",", (double)position[0], (double)position[1], (double)position[2], (unsigned)color[0],
                   (unsigned)color[1], (unsigned)color[2], (unsigned)color[3]);
            for (int i = 0; i < 16; ++i) {
                printf("%s%.9g", i == 0 ? "" : ",", (double)attrs[i]);
            }
            printf("]}");
        }
    }
    printf("],\"indices\":[0,1,2,0,2,3]}\n");
}

static void walk_projective_box(const char *name, const float view_proj[16], const nt_ui_transform_t *transform, const nt_ui_target_t *target, bool visible) {
    setup_projective_context();
    begin_frame();
    nt_ui_set_view_proj(s_fx.ctx, view_proj);
    const nt_ui_shape_style_t style = box_style();
    emit_box(&style, transform != NULL ? NT_UI_DATA_XFORM(0, transform, 1.0F) : NULL);
    nt_ui_end(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, target);
    export_projective_case(name, view_proj, target, visible);
}

static void assert_projective_emit(void) {
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(6, nt_gfx_fake_draw_trace_at(0).num_indices);
    for (uint32_t vertex = 0; vertex < 4U; ++vertex) {
        float attrs[16];
        nt_sprite_renderer_test_last_emit_radial(vertex, attrs, 16);
        TEST_ASSERT_TRUE(isfinite(attrs[2]) && attrs[2] > 1.0F);
        TEST_ASSERT_TRUE(isfinite(attrs[3]) && fabsf(attrs[3]) <= 1.01F);
        TEST_ASSERT_TRUE(isfinite(attrs[8]) && fabsf(attrs[8]) <= 1.01F);
        TEST_ASSERT_TRUE(attrs[11] == (65536.0F * 4294967296.0F));
    }
}

static void test_camera_projection_keeps_world_xy_singular_plane_visible(void) {
    const float vp[16] = {0, 0, 0.001F, 0, 0, -0.02F, 0, 0, -0.005F, 0, 0, 0, 0, 0.6F, -0.1F, 1};
    nt_ui_transform_t transform = nt_ui_transform_defaults();
    transform.rotation_y = 1.5707963267948966F;
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    walk_projective_box("world-xy-singular", vp, &transform, &target, true);
    assert_projective_emit();
    float attrs[16];
    nt_sprite_renderer_test_last_emit_radial(0, attrs, 16);
    TEST_ASSERT_TRUE(fabsf((1.0F + (1.5F / 180.0F)) - attrs[2]) <= 0.00001F);
    float position[3];
    nt_sprite_renderer_test_last_emit_position(0, position);
    TEST_ASSERT_TRUE(fabsf((100.0F) - position[0]) <= 0.0001F);
    TEST_ASSERT_TRUE(fabsf((0.0F) - position[1]) <= 0.0001F);
    TEST_ASSERT_TRUE(fabsf((100.0F) - position[2]) <= 0.0001F);
}

static void test_projective_guard_uses_physical_viewport_extent_with_offset(void) {
    const float vp[16] = {0.005F, 0, 0, 0, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.5F, 0.6F, 0, 1};
    const nt_ui_target_t target = {.viewport = {101, 47, 640, 480}};
    walk_projective_box("viewport-offset", vp, NULL, &target, true);
    assert_projective_emit();
    float attrs[16];
    nt_sprite_renderer_test_last_emit_radial(0, attrs, 16);
    TEST_ASSERT_TRUE(fabsf((1.0F + (1.5F / 144.0F)) - attrs[2]) <= 0.00001F);
    TEST_ASSERT_TRUE(fabsf((0.0F) - attrs[3]) <= 0.00001F);
    TEST_ASSERT_TRUE(fabsf((0.0F) - attrs[8]) <= 0.00001F);
}

static void test_near_and_far_crossings_emit_original_body_bounds(void) {
    const float vp[16] = {0.005F, 0, 0.012F, 0, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.5F, 0.6F, -1.2F, 1};
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    walk_projective_box("near-far-crossing", vp, NULL, &target, true);
    assert_projective_emit();
    float position[3];
    nt_sprite_renderer_test_last_emit_position(0, position);
    TEST_ASSERT_TRUE(position[0] == (0.0F));
    TEST_ASSERT_TRUE(position[1] == (0.0F));
    nt_sprite_renderer_test_last_emit_position(2, position);
    TEST_ASSERT_TRUE(position[0] == (200.0F));
    TEST_ASSERT_TRUE(position[1] == (60.0F));
}

static void test_camera_plane_crossing_emits_finite_projective_metadata(void) {
    const float vp[16] = {0.005F, 0, 0, 0.01F, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.3F, 0.6F, 0, -0.5F};
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    walk_projective_box("camera-crossing", vp, NULL, &target, true);
    assert_projective_emit();
}

static void test_far_clipping_is_left_to_fragment_depth(void) {
    const float vp[16] = {0.005F, 0, 0, 0, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.5F, 0.6F, 2, 1};
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    walk_projective_box("beyond-far", vp, NULL, &target, false);
    assert_projective_emit();
}

static void test_entire_support_behind_camera_is_culled(void) {
    const float vp[16] = {0.005F, 0, 0, 0, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.5F, 0.6F, 0, -1};
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    walk_projective_box("behind-camera", vp, NULL, &target, false);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_draw_trace_count());
}

static void test_projective_radial_preserves_signed_sweep_and_inner_radius(void) {
    const float vp[16] = {0.005F, 0, 0, 0.001F, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.5F, 0.6F, 0, 1};
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    setup_projective_context();
    begin_frame();
    nt_ui_set_view_proj(s_fx.ctx, vp);
    nt_ui_shape_style_t style = nt_ui_shape_style_defaults();
    style.material = s_body_material;
    style.kind = NT_UI_SHAPE_RADIAL;
    style.radial.angle_start = -0.4F;
    style.radial.angle_end = 4.7F;
    style.radial.inner_radius_norm = 0.45F;
    emit_box(&style, NULL);
    nt_ui_end(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, &target);
    export_projective_case("radial-perspective", vp, &target, true);
    assert_projective_emit();
    float attrs[16];
    nt_sprite_renderer_test_last_emit_radial(0, attrs, 16);
    TEST_ASSERT_TRUE(attrs[4] == (-0.4F));
    TEST_ASSERT_TRUE(attrs[5] == (4.7F));
    TEST_ASSERT_TRUE(attrs[6] == (0.45F));
    TEST_ASSERT_TRUE(attrs[9] == (16777216.0F * 4294967296.0F));
}

static void test_projective_shadow_can_be_visible_without_body(void) {
    const float vp[16] = {0.005F, 0, 0, 0.001F, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -3, 0.6F, 0, 1};
    const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    setup_projective_context();
    begin_frame();
    nt_ui_set_view_proj(s_fx.ctx, vp);
    nt_ui_shape_style_t style = box_style();
    style.shadow = (nt_ui_shape_shadow_t){.material = s_shadow_material, .color = 0x80402010U, .offset_x = 400, .spread = 2, .softness = 3};
    emit_box(&style, NULL);
    nt_ui_end(s_fx.ctx);
    nt_ui_walk(s_fx.ctx, &target);
    export_projective_case("shadow-only-perspective", vp, &target, true);
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(nt_material_get_info(s_shadow_material)->program.id, nt_gfx_fake_draw_trace_at(0).program.id);
    float attrs[16];
    nt_sprite_renderer_test_last_emit_radial(0, attrs, 16);
    TEST_ASSERT_TRUE(isfinite(attrs[2]) && attrs[2] > 1.0F);
    TEST_ASSERT_TRUE(attrs[9] == (-4294967296.0F));
    TEST_ASSERT_TRUE(attrs[11] == (129.0F * 4294967296.0F));
    TEST_ASSERT_TRUE(attrs[12] == (2.0F));
    TEST_ASSERT_TRUE(attrs[13] == (3.0F));
    TEST_ASSERT_TRUE(attrs[14] == (5.0F));
    float position[3];
    nt_sprite_renderer_test_last_emit_position(0, position);
    TEST_ASSERT_TRUE(position[0] == (395.0F));
    TEST_ASSERT_TRUE(position[1] == (-5.0F));
}

int main(int argc, char **argv) {
    s_export_gpu_cases = argc == 2 && strcmp(argv[1], "--gpu-fixtures") == 0;
    UNITY_BEGIN();
    RUN_TEST(test_box_layout_and_asymmetric_radii);
    RUN_TEST(test_radii_share_css_adjacent_edge_scale);
    RUN_TEST(test_four_border_sides_keep_layout_order_and_full_precision);
    RUN_TEST(test_zero_sides_do_not_inherit_another_side);
    RUN_TEST(test_zero_height_interior_is_marked_empty);
    RUN_TEST(test_overlapping_corner_constraints_can_empty_positive_inset);
    RUN_TEST(test_overlapping_corners_keep_nonempty_interior);
    RUN_TEST(test_paint_alpha_stays_separate_from_inherited_opacity);
    RUN_TEST(test_radial_parameters_and_vertical_gradient_reach_vertices);
    RUN_TEST(test_shadow_precedes_body_with_distinct_materials);
    RUN_TEST(test_transparent_body_keeps_only_visible_shadow);
    RUN_TEST(test_transparent_shape_container_keeps_visible_children);
    RUN_TEST(test_uber_batches_shadow_shapes_and_plain_rect_without_attr_leak);
    RUN_TEST(test_zero_width_custom_geometry_does_not_leak_attrs_to_plain_rect);
    RUN_TEST(test_unready_custom_region_does_not_leak_attrs_to_plain_rect);
    RUN_TEST(test_offscreen_layout_transformed_into_view_still_draws);
    RUN_TEST(test_shadow_only_visible_keeps_outset_and_offset);
    RUN_TEST(test_transformed_paint_outside_view_is_culled);
    RUN_TEST(test_aa_guard_accounts_for_nonuniform_scale_and_framebuffer_density);
    RUN_TEST(test_affine_guard_matches_rounded_physical_framebuffer_offset);
    RUN_TEST(test_camera_projection_keeps_world_xy_singular_plane_visible);
    RUN_TEST(test_projective_guard_uses_physical_viewport_extent_with_offset);
    RUN_TEST(test_near_and_far_crossings_emit_original_body_bounds);
    RUN_TEST(test_camera_plane_crossing_emits_finite_projective_metadata);
    RUN_TEST(test_far_clipping_is_left_to_fragment_depth);
    RUN_TEST(test_entire_support_behind_camera_is_culled);
    RUN_TEST(test_projective_radial_preserves_signed_sweep_and_inner_radius);
    RUN_TEST(test_projective_shadow_can_be_visible_without_body);
    return UNITY_END();
}
