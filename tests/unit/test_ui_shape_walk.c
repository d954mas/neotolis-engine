#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "renderers/nt_ui_shape_renderer.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_gfx_fake.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui_shape.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;
static nt_material_t s_body_material;
static bool s_export_gpu_cases;

static const nt_ui_target_t k_screen = {.viewport = {0, 0, 800, 600}};

static nt_material_t make_shape_material(bool depth) {
    return nt_material_create(&(nt_material_create_desc_t){
        .program = nt_gfx_fake_make_program(NULL, 0),
        .blend = nt_blend_alpha_premultiplied(),
        .depth_test = depth,
        .depth_write = depth,
    });
}

void setUp(void) {
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    s_body_material = make_shape_material(false);
    nt_gfx_fake_draw_trace_reset(true);
    nt_ui_shape_renderer_test_reset();
}

void tearDown(void) {
    nt_material_destroy(s_body_material);
    ui_walker_fixture_shutdown(&s_fx);
}

static void begin_frame(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
}

static void end_and_walk_target(const nt_ui_target_t *target) {
    nt_ui_end(s_fx.ctx);
    nt_gfx_fake_draw_trace_reset(true);
    nt_ui_shape_renderer_test_reset();
    nt_ui_walk(s_fx.ctx, target);
    TEST_ASSERT_FALSE(nt_gfx_fake_draw_trace_overflowed());
}

static void end_and_walk(void) { end_and_walk_target(&k_screen); }

static nt_ui_shape_style_t box_style(void) {
    nt_ui_shape_style_t style = nt_ui_shape_style_defaults();
    style.material = s_body_material;
    style.box = (nt_ui_shape_radii_t){40.0F, 40.0F, 10.0F, 10.0F};
    return style;
}

static void emit_box(const nt_ui_shape_style_t *style, const nt_ui_element_data_t *data) {
    nt_ui_shape(s_fx.ctx, data, style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}});
}

static const nt_ui_shape_instance_t *emitted(uint32_t index) { return nt_ui_shape_renderer_test_emitted(index); }

/* Compares everything but the world placement, which the tests check where it matters. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void assert_instance(uint32_t index, const nt_ui_shape_instance_t *expected) {
    TEST_ASSERT_TRUE(index < nt_ui_shape_renderer_test_emit_count());
    const nt_ui_shape_instance_t *actual = emitted(index);
    TEST_ASSERT_TRUE(fabsf(expected->width - actual->width) <= 0.0001F);
    TEST_ASSERT_TRUE(fabsf(expected->height - actual->height) <= 0.0001F);
    TEST_ASSERT_TRUE(fabsf(expected->pad - actual->pad) <= 0.0001F);
    for (uint8_t i = 0; i < 4U; ++i) {
        TEST_ASSERT_TRUE(fabsf(expected->geometry[i] - actual->geometry[i]) <= 0.0001F);
        TEST_ASSERT_TRUE(fabsf(expected->widths[i] - actual->widths[i]) <= 0.0001F);
        TEST_ASSERT_TRUE(expected->user[i] == actual->user[i]);
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->color, actual->color, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->endpoint, actual->endpoint, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->border, actual->border, 4);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected->control, actual->control, 4);
}

static void assert_origin(uint32_t index, float x, float y) {
    const nt_ui_shape_instance_t *actual = emitted(index);
    TEST_ASSERT_TRUE(fabsf(actual->origin[0] - x) <= 0.0001F && fabsf(actual->origin[1] - y) <= 0.0001F);
}

/* One instanced draw carrying count instances. */
static void assert_one_shape_draw(uint32_t count) {
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(count, nt_gfx_fake_draw_trace_at(0).instance_count);
    TEST_ASSERT_EQUAL_UINT32(count, nt_ui_shape_renderer_test_emit_count());
}

// #region gpu fixtures
/* Emits every instance of the last walk for tests/browser/shape_shader.spec.ts. */
static void export_case(const char *name, const float view_proj[16], const nt_ui_target_t *target, bool visible, bool depth_test, const char *pixel_checks) {
    if (!s_export_gpu_cases) {
        return;
    }
    printf("UI_SHAPE_GPU_CASE {\"schemaVersion\":3,\"name\":\"%s\",\"expectedPixelCoverage\":\"%s\",\"viewProj\":[", name, visible ? "nonempty" : "empty");
    for (int i = 0; i < 16; ++i) {
        printf("%s%.9g", i == 0 ? "" : ",", (double)view_proj[i]);
    }
    printf("],\"viewport\":[%.9g,%.9g,%.9g,%.9g],\"fbSize\":[%.9g,%.9g],\"fbOffset\":[%.9g,%.9g],\"depthTest\":%s,\"instances\":[", (double)target->viewport[0], (double)target->viewport[1],
           (double)target->viewport[2], (double)target->viewport[3], (double)target->fb_size[0], (double)target->fb_size[1], (double)target->fb_offset[0], (double)target->fb_offset[1],
           depth_test ? "true" : "false");
    for (uint32_t index = 0; index < nt_ui_shape_renderer_test_emit_count(); ++index) {
        printf("%s[", index == 0U ? "" : ",");
        const uint8_t *bytes = (const uint8_t *)emitted(index);
        for (size_t i = 0; i < sizeof(nt_ui_shape_instance_t); ++i) {
            printf("%s%u", i == 0U ? "" : ",", (unsigned)bytes[i]);
        }
        printf("]");
    }
    printf("]");
    if (pixel_checks != NULL) {
        printf(",\"pixelChecks\":%s", pixel_checks);
    }
    printf("}\n");
}

static const float k_screen_projection[16] = {2.0F / 800.0F, 0, 0, 0, 0, 2.0F / 600.0F, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1};
// #endregion

// #region screen instances
static void test_box_instance_carries_layout_paint_and_placement(void) {
    const nt_ui_shape_style_t style = box_style();
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();
    assert_one_shape_draw(1);
    const nt_ui_shape_instance_t expected = {
        .width = 200, .height = 60, .pad = 1, .geometry = {40, 40, 10, 10}, .color = {255, 255, 255, 255}, .endpoint = {255, 255, 255, 255}, .control = {255, 1, 0, 0}};
    assert_instance(0, &expected);
    /* Screen Y flips: layout (0,0) lands at the viewport top and layout +y points down. */
    assert_origin(0, 0.0F, 600.0F);
    TEST_ASSERT_TRUE(emitted(0)->axis_x[0] == 1.0F && emitted(0)->axis_y[1] == -1.0F);
}

static void test_leaf_without_declaration_fills_parent(void) {
    const nt_ui_shape_style_t style = box_style();
    begin_frame();
    CLAY({.layout.sizing = {CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(90)}}) { nt_ui_shape(s_fx.ctx, NULL, &style, NULL); }
    end_and_walk();
    assert_one_shape_draw(1);
    TEST_ASSERT_TRUE(emitted(0)->width == 160.0F && emitted(0)->height == 90.0F);
}

static void test_radii_share_css_adjacent_edge_scale(void) {
    nt_ui_shape_style_t style = box_style();
    style.box.top_left = 80.0F;
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();
    const nt_ui_shape_instance_t expected = {.width = 200,
                                             .height = 60,
                                             .pad = 1,
                                             .geometry = {160.0F / 3.0F, 80.0F / 3.0F, 20.0F / 3.0F, 20.0F / 3.0F},
                                             .color = {255, 255, 255, 255},
                                             .endpoint = {255, 255, 255, 255},
                                             .control = {255, 1, 0, 0}};
    assert_instance(0, &expected);
}

/* Border sides keep their own widths; the interior flag follows the inset contour. */
static void test_border_widths_and_empty_interior(void) {
    static const struct {
        nt_ui_shape_radii_t radii;
        nt_ui_shape_border_widths_t widths;
        float w, h;
        uint8_t empty;
    } k_cases[] = {
        {{40, 40, 10, 10}, {8, 1, 2, 4}, 200, 60, 0},      {{40, 40, 10, 10}, {0, 0.1F, 0, 0.5F}, 200, 60, 0}, {{0, 0, 0, 0}, {20, 20, 20, 20}, 96, 40, 1},
        {{90, 10, 90, 10}, {40, 40, 40, 40}, 100, 100, 1}, {{90, 10, 90, 10}, {30, 30, 30, 30}, 100, 100, 0},
    };
    for (uint32_t i = 0; i < sizeof k_cases / sizeof k_cases[0]; ++i) {
        nt_ui_shape_style_t style = box_style();
        style.box = k_cases[i].radii;
        style.paint.border_widths = k_cases[i].widths;
        begin_frame();
        nt_ui_shape(s_fx.ctx, NULL, &style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(k_cases[i].w), CLAY_SIZING_FIXED(k_cases[i].h)}});
        end_and_walk();
        const nt_ui_shape_border_widths_t *bw = &k_cases[i].widths;
        const nt_ui_shape_instance_t expected = {.width = k_cases[i].w,
                                                 .height = k_cases[i].h,
                                                 .pad = 1,
                                                 .geometry = {k_cases[i].radii.top_left, k_cases[i].radii.top_right, k_cases[i].radii.bottom_right, k_cases[i].radii.bottom_left},
                                                 .widths = {bw->left, bw->top, bw->right, bw->bottom},
                                                 .color = {255, 255, 255, 255},
                                                 .endpoint = {255, 255, 255, 255},
                                                 .control = {255, 1, 0, k_cases[i].empty}};
        assert_instance(0, &expected);
    }
}

static void test_paint_alpha_stays_separate_from_inherited_opacity(void) {
    nt_ui_shape_style_t style = box_style();
    style.paint = (nt_ui_shape_paint_t){.color0 = 0x80204080U, .color1 = 0x00010203U, .border_color = 0x404080C0U, .border_widths = {0.1F, 0.1F, 0.1F, 0.1F}, .gradient = NT_UI_SHAPE_HORIZONTAL};
    const nt_ui_transform_t identity = nt_ui_transform_defaults();
    begin_frame();
    CLAY({.layout.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}, .userData = (void *)NT_UI_DATA_XFORM(0, &identity, 0.5F)}) { emit_box(&style, NT_UI_DATA_XFORM(0, &identity, 0.5F)); }
    end_and_walk();
    const nt_ui_shape_instance_t expected = {.width = 200,
                                             .height = 60,
                                             .pad = 1,
                                             .geometry = {40, 40, 10, 10},
                                             .widths = {0.1F, 0.1F, 0.1F, 0.1F},
                                             .color = {128, 64, 32, 64},
                                             .endpoint = {3, 2, 1, 0},
                                             .border = {192, 128, 64, 64},
                                             .control = {128, 1, 1, 0}};
    assert_instance(0, &expected);
}

static void test_radial_parameters_vertical_gradient_and_user(void) {
    nt_ui_shape_style_t style = nt_ui_shape_style_defaults();
    style.material = s_body_material;
    style.kind = NT_UI_SHAPE_RADIAL;
    style.radial.angle_start = 5.5F;
    style.radial.angle_end = 0.75F;
    style.radial.inner_radius_norm = 0.6F;
    style.paint.gradient = NT_UI_SHAPE_VERTICAL;
    style.paint.color1 = 0x00112233U;
    style.user[0] = 7.0F;
    style.user[3] = -2.5F;
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();
    const nt_ui_shape_instance_t expected = {
        .width = 200, .height = 60, .pad = 1, .geometry = {5.5F, 0.75F, 0.6F, 0}, .user = {7.0F, 0, 0, -2.5F}, .color = {255, 255, 255, 255}, .endpoint = {51, 34, 17, 0}, .control = {255, 2, 2, 0}};
    assert_instance(0, &expected);
}

static void test_shadow_precedes_body_in_one_draw(void) {
    nt_ui_shape_style_t style = box_style();
    style.shadow = (nt_ui_shape_shadow_t){.color = 0x80402010U, .offset_x = 4, .offset_y = 8, .spread = 2, .softness = 3};
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();
    assert_one_shape_draw(2);
    const nt_ui_shape_instance_t shadow = {.width = 200, .height = 60, .pad = 6, .geometry = {40, 40, 10, 10}, .widths = {2, 3, 5, 0}, .color = {16, 32, 64, 255}, .control = {128, 3, 0, 0}};
    assert_instance(0, &shadow);
    assert_origin(0, 4.0F, 592.0F);
    TEST_ASSERT_EQUAL_UINT8(1, emitted(1)->control[1]);
}

static void test_transparent_body_keeps_only_visible_shadow(void) {
    nt_ui_shape_style_t style = box_style();
    style.paint.color0 = 0;
    style.shadow = (nt_ui_shape_shadow_t){.color = 0x80402010U, .spread = 2, .softness = 3};
    begin_frame();
    emit_box(&style, NULL);
    end_and_walk();
    assert_one_shape_draw(1);
    TEST_ASSERT_EQUAL_UINT8(3, emitted(0)->control[1]);
}

static void test_transparent_container_draws_only_children(void) {
    nt_ui_shape_style_t style = box_style();
    style.paint.color0 = 0;
    begin_frame();
    nt_ui_shape_begin(s_fx.ctx, NULL, &style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}});
    CLAY({.layout.sizing = {CLAY_SIZING_FIXED(20), CLAY_SIZING_FIXED(20)}, .backgroundColor = {255, 0, 0, 128}}) {}
    nt_ui_shape_end(s_fx.ctx);
    end_and_walk();
    TEST_ASSERT_EQUAL_UINT32(0, nt_ui_shape_renderer_test_emit_count());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_ui_shape_renderer_test_draw_count());
}

/* Declaration order survives the renderer switch; adjacent shapes share one draw across materials. */
static void test_shapes_and_sprites_keep_declaration_order(void) {
    const nt_ui_shape_style_t style = box_style();
    nt_ui_shape_style_t other = box_style();
    other.material = make_shape_material(false);
    begin_frame();
    CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .sizing = {CLAY_SIZING_FIXED(800), CLAY_SIZING_FIXED(100)}}}) {
        emit_box(&style, NULL);
        emit_box(&other, NULL);
        CLAY({.layout.sizing = {CLAY_SIZING_FIXED(20), CLAY_SIZING_FIXED(20)}, .backgroundColor = {255, 0, 0, 255}}) {}
        emit_box(&style, NULL);
    }
    end_and_walk();
    const nt_program_t sprite = nt_material_get_info(s_fx.sprite_material)->program;
    TEST_ASSERT_EQUAL_UINT32(4, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(3, nt_ui_shape_renderer_test_draw_count());
    TEST_ASSERT_NOT_EQUAL(sprite.id, nt_gfx_fake_draw_trace_at(0).program.id);
    TEST_ASSERT_NOT_EQUAL(sprite.id, nt_gfx_fake_draw_trace_at(1).program.id);
    TEST_ASSERT_EQUAL_UINT32(sprite.id, nt_gfx_fake_draw_trace_at(2).program.id);
    TEST_ASSERT_NOT_EQUAL(sprite.id, nt_gfx_fake_draw_trace_at(3).program.id);
    nt_material_destroy(other.material);
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
    assert_one_shape_draw(1);
    assert_origin(0, 0.0F, 550.0F);
}

static void test_shadow_only_visible_keeps_outset_and_offset(void) {
    nt_ui_shape_style_t style = box_style();
    style.shadow = (nt_ui_shape_shadow_t){.color = 0x80402010U, .offset_x = 4, .offset_y = -150, .spread = 2, .softness = 3};
    begin_frame();
    CLAY({.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = {CLAY_SIZING_FIXED(800), CLAY_SIZING_FIXED(1000)}}}) {
        CLAY({.layout.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(700)}}) {}
        emit_box(&style, NULL);
    }
    end_and_walk();
    assert_one_shape_draw(1);
    TEST_ASSERT_EQUAL_UINT8(3, emitted(0)->control[1]);
    TEST_ASSERT_TRUE(emitted(0)->pad == 6.0F);
    assert_origin(0, 4.0F, 50.0F);
}

static void test_transformed_paint_outside_view_is_culled(void) {
    nt_ui_shape_style_t style = box_style();
    style.shadow = (nt_ui_shape_shadow_t){.color = 0xFF000000U, .spread = 2, .softness = 3};
    nt_ui_transform_t transform = nt_ui_transform_defaults();
    transform.offset_x = 1000.0F;
    begin_frame();
    emit_box(&style, NT_UI_DATA_XFORM(0, &transform, 1.0F));
    end_and_walk();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_draw_trace_count());
}

/* The cull and the Y flip share the viewport-local projection, whatever the viewport origin. */
static void test_screen_cull_and_flip_use_viewport_local_projection(void) {
    const nt_ui_shape_style_t style = box_style();
    nt_ui_transform_t transform = nt_ui_transform_defaults();
    transform.offset_y = 500.0F;
    begin_frame();
    nt_ui_shape(s_fx.ctx, NT_UI_DATA_XFORM(0, &transform, 1.0F), &style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(20)}});
    const nt_ui_target_t target = {.viewport = {101, 47, 640, 480}};
    end_and_walk_target(&target);
    assert_one_shape_draw(1);
    assert_origin(0, 0.0F, 27.0F);
}

/* The AA pad covers one physical pixel after nonuniform scale and the rounded framebuffer offset. */
static void test_affine_guard_covers_one_physical_pixel(void) {
    static const struct {
        float fb_offset[2];
        float pad;
    } k_cases[] = {{{0, 0}, 2.0F}, {{0.6F, 2.6F}, 800.0F / (0.25F * 1598.0F)}};
    for (uint32_t i = 0; i < 2U; ++i) {
        const nt_ui_shape_style_t style = box_style();
        nt_ui_transform_t transform = nt_ui_transform_defaults();
        transform.scale_x = 0.25F;
        transform.scale_y = 2.0F;
        begin_frame();
        emit_box(&style, NT_UI_DATA_XFORM(0, &transform, 1.0F));
        const nt_ui_target_t target = {.viewport = {0, 0, 800, 600}, .fb_size = {1600, 1200}, .fb_offset = {k_cases[i].fb_offset[0], k_cases[i].fb_offset[1]}};
        end_and_walk_target(&target);
        assert_one_shape_draw(1);
        TEST_ASSERT_TRUE(fabsf(k_cases[i].pad - emitted(0)->pad) <= 0.0000005F);
    }
}

static void test_typed_paint_with_asymmetric_widths_and_gradient(void) {
    const nt_ui_transform_t identity = nt_ui_transform_defaults();
    for (uint8_t gradient = 1; gradient <= 2U; ++gradient) {
        nt_ui_shape_style_t style = box_style();
        style.box = (nt_ui_shape_radii_t){3, 17, 11, 5};
        style.paint = (nt_ui_shape_paint_t){.color0 = 0x804020FFU,
                                            .color1 = gradient == 1U ? 0xFF80FF00U : 0x0080FF00U,
                                            .border_color = 0x7FFF0080U,
                                            .border_widths = {1.25F, 7.5F, 0, 3.125F},
                                            .gradient = (nt_ui_shape_gradient_t)gradient};
        begin_frame();
        emit_box(&style, NT_UI_DATA_XFORM(0, &identity, 0.5F));
        end_and_walk();
        const nt_ui_shape_instance_t expected = {.width = 200,
                                                 .height = 60,
                                                 .pad = 1,
                                                 .geometry = {3, 17, 11, 5},
                                                 .widths = {1.25F, 7.5F, 0, 3.125F},
                                                 .color = {255, 32, 64, 128},
                                                 .endpoint = {0, 255, 128, gradient == 1U ? 255 : 0},
                                                 .border = {128, 0, 255, 127},
                                                 .control = {128, 1, gradient, 0}};
        assert_instance(0, &expected);
        export_case(gradient == 1U ? "typed-horizontal-paint" : "typed-vertical-transparent-paint", k_screen_projection, &k_screen, true, false, NULL);
    }
}

static void test_screen_offset_viewports_render_translated_body_and_shadow(void) {
    const float projection[16] = {2.0F / 640.0F, 0, 0, 0, 0, 2.0F / 480.0F, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1};
    nt_ui_transform_t transform = nt_ui_transform_defaults();
    transform.offset_y = 500.0F;
    const char *names[2][2] = {{"screen-direct-y-offset", "screen-direct-y-shadow"}, {"screen-scaled-y-offset", "screen-scaled-y-shadow"}};
    for (uint8_t scaled = 0; scaled < 2U; ++scaled) {
        for (uint8_t shadow_only = 0; shadow_only < 2U; ++shadow_only) {
            nt_ui_shape_style_t style = nt_ui_shape_style_defaults();
            style.material = s_body_material;
            if (shadow_only != 0U) {
                style.paint.color0 = 0;
                style.shadow = (nt_ui_shape_shadow_t){.color = UINT32_MAX, .spread = 2};
            }
            begin_frame();
            nt_ui_shape(s_fx.ctx, NT_UI_DATA_XFORM(0, &transform, 1.0F), &style, &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(20)}});
            nt_ui_target_t target = {.viewport = {101, 47, 640, 480}};
            if (scaled != 0U) {
                target.fb_size[0] = 1280;
                target.fb_size[1] = 960;
                target.fb_offset[0] = 0.6F;
                target.fb_offset[1] = 2.6F;
            }
            end_and_walk_target(&target);
            assert_one_shape_draw(1);
            export_case(names[scaled][shadow_only], projection, &target, true, false, NULL);
        }
    }
}
// #endregion

// #region renderer
/* A full staging buffer flushes and reopens the same material: 256 + 44 instances, two draws. */
static void test_renderer_overflow_flushes_and_keeps_material(void) {
    const nt_ui_shape_instance_t instance = {.width = 1, .height = 1, .control = {255, 1, 0, 0}};
    nt_ui_shape_renderer_set_material(s_body_material);
    for (uint32_t i = 0; i < 300U; ++i) {
        nt_ui_shape_renderer_emit(&instance);
    }
    nt_ui_shape_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(2, nt_gfx_fake_draw_trace_count());
    TEST_ASSERT_EQUAL_UINT32(256, nt_gfx_fake_draw_trace_at(0).instance_count);
    TEST_ASSERT_EQUAL_UINT32(44, nt_gfx_fake_draw_trace_at(1).instance_count);
    TEST_ASSERT_TRUE(nt_gfx_fake_draw_trace_at(0).pipeline.id == nt_gfx_fake_draw_trace_at(1).pipeline.id);
}

/* Staged instances reference the lost buffer; restore drops them and draws again afterwards. */
static void test_renderer_restore_drops_staged_instances(void) {
    const nt_ui_shape_instance_t instance = {.width = 1, .height = 1, .control = {255, 1, 0, 0}};
    nt_ui_shape_renderer_set_material(s_body_material);
    nt_ui_shape_renderer_emit(&instance);
    TEST_ASSERT_EQUAL(NT_OK, nt_ui_shape_renderer_restore_gpu());
    nt_ui_shape_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_draw_trace_count());
    nt_ui_shape_renderer_set_material(s_body_material);
    nt_ui_shape_renderer_emit(&instance);
    nt_ui_shape_renderer_flush();
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_fake_draw_trace_count());
}
// #endregion

// #region world instances
static void setup_world_context(void) {
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

static void walk_world(const nt_ui_shape_style_t *style, const float view_proj[16], const nt_ui_transform_t *transform, const nt_ui_target_t *target) {
    setup_world_context();
    begin_frame();
    nt_ui_set_view_proj(s_fx.ctx, view_proj);
    emit_box(style, transform != NULL ? NT_UI_DATA_XFORM(0, transform, 1.0F) : NULL);
    end_and_walk_target(target);
}

/* Projected pad = layout units covering one physical pixel; here 1 / (0.005 * 320) along x. */
static void test_world_guard_uses_projection_and_physical_viewport(void) {
    const float vp[16] = {0.005F, 0, 0, 0, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.5F, 0.6F, 0, 1};
    const nt_ui_target_t target = {.viewport = {101, 47, 640, 480}};
    const nt_ui_shape_style_t style = box_style();
    walk_world(&style, vp, NULL, &target);
    assert_one_shape_draw(1);
    TEST_ASSERT_TRUE(fabsf(emitted(0)->pad - 0.625F) <= 0.00001F);
    export_case("viewport-offset", vp, &target, true, false, NULL);
}

/* A plane the world XY projection collapses is still seen face-on through this camera. */
static void test_world_plane_seen_face_on_through_rotated_axes(void) {
    const float vp[16] = {0, 0, 0.001F, 0, 0, -0.02F, 0, 0, -0.005F, 0, 0, 0, 0, 0.6F, -0.1F, 1};
    nt_ui_transform_t transform = nt_ui_transform_defaults();
    transform.rotation_y = 1.5707963267948966F;
    const nt_ui_shape_style_t style = box_style();
    walk_world(&style, vp, &transform, &k_screen);
    assert_one_shape_draw(1);
    TEST_ASSERT_TRUE(fabsf(emitted(0)->pad - 0.5F) <= 0.00001F);
    export_case("world-xy-singular", vp, &k_screen, true, false, NULL);
}

static void test_world_support_behind_camera_is_culled(void) {
    const float vp[16] = {0.005F, 0, 0, 0, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.5F, 0.6F, 0, -1};
    const nt_ui_shape_style_t style = box_style();
    walk_world(&style, vp, NULL, &k_screen);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_draw_trace_count());
    export_case("behind-camera", vp, &k_screen, false, false, NULL);
}

/* GPU clipping owns near, far and camera-plane crossings; the walker emits the whole quad. */
static void test_world_clip_crossings_emit_whole_quad(void) {
    static const struct {
        const char *name;
        float vp[16];
        bool visible;
    } k_cases[] = {
        {"near-far-crossing", {0.005F, 0, 0.012F, 0, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.5F, 0.6F, -1.2F, 1}, true},
        {"camera-crossing", {0.005F, 0, 0, 0.01F, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.3F, 0.6F, 0, -0.5F}, true},
        {"beyond-far", {0.005F, 0, 0, 0, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.5F, 0.6F, 2, 1}, false},
    };
    for (uint32_t i = 0; i < sizeof k_cases / sizeof k_cases[0]; ++i) {
        const nt_ui_shape_style_t style = box_style();
        walk_world(&style, k_cases[i].vp, NULL, &k_screen);
        assert_one_shape_draw(1);
        assert_origin(0, 0.0F, 0.0F);
        export_case(k_cases[i].name, k_cases[i].vp, &k_screen, k_cases[i].visible, false, NULL);
    }
}

static void test_world_radial_and_shadow_only(void) {
    const float vp[16] = {0.005F, 0, 0, 0.001F, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.5F, 0.6F, 0, 1};
    nt_ui_shape_style_t radial = nt_ui_shape_style_defaults();
    radial.material = s_body_material;
    radial.kind = NT_UI_SHAPE_RADIAL;
    radial.radial.angle_start = -0.4F;
    radial.radial.angle_end = 4.7F;
    radial.radial.inner_radius_norm = 0.45F;
    walk_world(&radial, vp, NULL, &k_screen);
    assert_one_shape_draw(1);
    TEST_ASSERT_EQUAL_UINT8(2, emitted(0)->control[1]);
    export_case("radial-perspective", vp, &k_screen, true, false, NULL);

    const float far_left[16] = {0.005F, 0, 0, 0.001F, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -3, 0.6F, 0, 1};
    nt_ui_shape_style_t shadow = box_style();
    shadow.shadow = (nt_ui_shape_shadow_t){.color = 0x80402010U, .offset_x = 400, .spread = 2, .softness = 3};
    walk_world(&shadow, far_left, NULL, &k_screen);
    TEST_ASSERT_EQUAL_UINT32(2, nt_ui_shape_renderer_test_emit_count());
    TEST_ASSERT_EQUAL_UINT8(3, emitted(0)->control[1]);
    assert_origin(0, 400.0F, 0.0F);
    export_case("shadow-only-perspective", far_left, &k_screen, true, false, NULL);
}

static void declare_depth_shapes(bool translucent) {
    nt_ui_shape_style_t styles[3];
    const uint32_t colors[3] = {0xFF00C800U, translucent ? 0x800000FFU : 0xFF0000FFU, 0xFFFF0000U};
    for (uint32_t i = 0; i < 3U; ++i) {
        styles[i] = nt_ui_shape_style_defaults();
        styles[i].material = s_body_material;
        styles[i].box = (nt_ui_shape_radii_t){8, 8, 8, 8};
        styles[i].paint.color0 = colors[i];
        styles[i].shadow = (nt_ui_shape_shadow_t){.color = i == 0U ? 0xFF202020U : 0xFF000000U, .spread = 8};
    }
    nt_ui_transform_t sibling = nt_ui_transform_defaults();
    sibling.offset_x = -40;
    nt_ui_shape_begin(s_fx.ctx, NULL, &styles[0],
                      &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(100)}, .padding = {20, 20, 20, 20}, .layoutDirection = CLAY_LEFT_TO_RIGHT}});
    nt_ui_shape(s_fx.ctx, NULL, &styles[1], &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(60)}});
    nt_ui_shape(s_fx.ctx, NT_UI_DATA_XFORM(0, &sibling, 1.0F), &styles[2], &(Clay_ElementDeclaration){.layout.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(60)}});
    nt_ui_shape_end(s_fx.ctx);
}

/* Shadows sit half an element-bias step behind their body: parents, children and siblings keep their order. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_world_shadow_half_step_preserves_depth_hierarchy(void) {
    setup_world_context();
    nt_material_destroy(s_body_material);
    s_body_material = make_shape_material(true);
    float vp[16] = {0.006F, 0, 0.0008F, 0.0015F, 0, -0.012F, 0.0003F, 0.001F, 0, 0, 0.1F, 0, -0.6F, 0.6F, 0, 1};
    const char *names[3] = {"depth-shadow-hierarchy", "depth-shadow-translucent", "depth-shadow-strong-perspective"};
    const char *checks[3] = {("[{\"x\":491,\"y\":203,\"rgba\":[0,200,0,255],\"tolerance\":2},{\"x\":269,\"y\":332,\"rgba\":[255,0,0,255],\"tolerance\":2},"
                              "{\"x\":337,\"y\":331,\"rgba\":[255,0,0,255],\"tolerance\":2},{\"x\":448,\"y\":329,\"rgba\":[0,0,255,255],\"tolerance\":2}]"),
                             "[{\"x\":269,\"y\":332,\"rgba\":[128,0,0,255],\"tolerance\":2},{\"x\":337,\"y\":331,\"rgba\":[128,0,0,255],\"tolerance\":2}]",
                             "[{\"x\":390,\"y\":249,\"rgba\":[0,0,0,255],\"tolerance\":2},{\"x\":312,\"y\":321,\"rgba\":[255,0,0,255],\"tolerance\":2}]"};
    nt_ui_set_element_depth_bias(s_fx.ctx, 0.004F);
    for (uint32_t scenario = 0; scenario < 3U; ++scenario) {
        vp[3] = scenario == 2U ? 0.015F : 0.0015F;
        begin_frame();
        nt_ui_set_view_proj(s_fx.ctx, vp);
        declare_depth_shapes(scenario == 1U);
        end_and_walk_target(&k_screen);
        assert_one_shape_draw(6);
        for (uint32_t i = 0; i < 6U; ++i) {
            TEST_ASSERT_EQUAL_UINT8((i & 1U) == 0U ? 3U : 1U, emitted(i)->control[1]);
        }
        TEST_ASSERT_TRUE(emitted(0)->origin[2] != emitted(1)->origin[2]);
        export_case(names[scenario], vp, &k_screen, true, true, checks[scenario]);
    }
}

static void test_world_shadow_depth_half_step_near_far(void) {
    setup_world_context();
    nt_ui_set_element_depth_bias(s_fx.ctx, 0.04F);
    const float depths[4] = {-0.97F, -0.99F, 1.01F, 1.03F};
    const char *names[4] = {"depth-shadow-near-visible", "depth-shadow-near-clipped", "depth-shadow-far-visible", "depth-shadow-far-clipped"};
    for (uint32_t i = 0; i < 4U; ++i) {
        const float vp[16] = {0.005F, 0, 0, 0, 0, -0.02F, 0, 0, 0, 0, 0.1F, 0, -0.5F, 0.6F, depths[i], 1};
        begin_frame();
        nt_ui_set_view_proj(s_fx.ctx, vp);
        nt_ui_shape_style_t style = box_style();
        style.paint.color0 = 0;
        style.shadow = (nt_ui_shape_shadow_t){.color = UINT32_MAX, .spread = 2};
        emit_box(&style, NULL);
        end_and_walk_target(&k_screen);
        assert_one_shape_draw(1);
        TEST_ASSERT_TRUE(fabsf(emitted(0)->origin[2] + 0.2F) < 0.00001F);
        export_case(names[i], vp, &k_screen, (i & 1U) == 0U, false, NULL);
    }
}

/* Without a bias the shadow ties with its body and wins the depth test. */
static void test_depth_writing_shadow_without_bias_asserts(void) {
    nt_material_destroy(s_body_material);
    s_body_material = make_shape_material(true);
    nt_ui_shape_style_t style = box_style();
    style.shadow = (nt_ui_shape_shadow_t){.color = 0xFF000000U, .spread = 2};
    begin_frame();
    emit_box(&style, NULL);
    nt_ui_end(s_fx.ctx);
    NT_TEST_EXPECT_ASSERT(nt_ui_walk(s_fx.ctx, &k_screen));
}
// #endregion

int main(int argc, char **argv) {
    s_export_gpu_cases = argc == 2 && strcmp(argv[1], "--gpu-fixtures") == 0;
    UNITY_BEGIN();
    RUN_TEST(test_box_instance_carries_layout_paint_and_placement);
    RUN_TEST(test_leaf_without_declaration_fills_parent);
    RUN_TEST(test_radii_share_css_adjacent_edge_scale);
    RUN_TEST(test_border_widths_and_empty_interior);
    RUN_TEST(test_paint_alpha_stays_separate_from_inherited_opacity);
    RUN_TEST(test_radial_parameters_vertical_gradient_and_user);
    RUN_TEST(test_shadow_precedes_body_in_one_draw);
    RUN_TEST(test_transparent_body_keeps_only_visible_shadow);
    RUN_TEST(test_transparent_container_draws_only_children);
    RUN_TEST(test_shapes_and_sprites_keep_declaration_order);
    RUN_TEST(test_offscreen_layout_transformed_into_view_still_draws);
    RUN_TEST(test_shadow_only_visible_keeps_outset_and_offset);
    RUN_TEST(test_transformed_paint_outside_view_is_culled);
    RUN_TEST(test_screen_cull_and_flip_use_viewport_local_projection);
    RUN_TEST(test_affine_guard_covers_one_physical_pixel);
    RUN_TEST(test_typed_paint_with_asymmetric_widths_and_gradient);
    RUN_TEST(test_screen_offset_viewports_render_translated_body_and_shadow);
    RUN_TEST(test_renderer_overflow_flushes_and_keeps_material);
    RUN_TEST(test_renderer_restore_drops_staged_instances);
    RUN_TEST(test_world_guard_uses_projection_and_physical_viewport);
    RUN_TEST(test_world_plane_seen_face_on_through_rotated_axes);
    RUN_TEST(test_world_support_behind_camera_is_culled);
    RUN_TEST(test_world_clip_crossings_emit_whole_quad);
    RUN_TEST(test_world_radial_and_shadow_only);
    RUN_TEST(test_world_shadow_half_step_preserves_depth_hierarchy);
    RUN_TEST(test_world_shadow_depth_half_step_near_far);
    RUN_TEST(test_depth_writing_shadow_without_bias_asserts);
    return UNITY_END();
}
