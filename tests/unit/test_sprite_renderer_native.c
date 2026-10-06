/* Real-GL proof that sprite geometry in frame storage draws across stride changes:
 * one draw_list with a plain (20 B) and a custom-attr (36 B) run, and immediate emits
 * that switch between the two materials inside one frame. */

#include "atlas/nt_atlas.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "hash/nt_hash.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "math/nt_math.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_sprite_renderer.h"
#include "resource/nt_resource.h"
#include "sprite_comp/nt_sprite_comp.h"
#include "test_helpers/nt_gfx_test_desc.h"
#include "test_helpers/ui_atlas.h"
#include "transform_comp/nt_transform_comp.h"
#include "unity.h"
#include "window/nt_window.h"

#include <stdint.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

enum { FB_SIZE = 32, CAPTURE_BYTES = 512 * 64 * 4 };

static const char *s_plain_vs_src = "precision mediump float;\n"
                                    "layout(location = 0) in vec3 a_position;\n"
                                    "layout(location = 2) in vec4 a_color;\n"
                                    "out vec4 v_color;\n"
                                    "void main() { v_color = a_color; gl_Position = vec4(a_position, 1.0); }\n";

/* Reads position at stride 36 too: a wrong stride misplaces the quad, not only its color. */
static const char *s_tint_vs_src = "precision mediump float;\n"
                                   "layout(location = 0) in vec3 a_position;\n"
                                   "layout(location = 4) in vec4 a_tint;\n"
                                   "out vec4 v_color;\n"
                                   "void main() { v_color = a_tint; gl_Position = vec4(a_position, 1.0); }\n";

static const char *s_fs_src = "precision mediump float;\n"
                              "in vec4 v_color;\n"
                              "out vec4 frag_color;\n"
                              "void main() { frag_color = v_color; }\n";

static const uint32_t k_red = 0xFF0000FFU; /* 0xAABBGGRR */
static const uint32_t k_green = 0xFF00FF00U;

static minimal_ui_atlas_t s_atlas;
static nt_material_t s_plain;
static nt_material_t s_tint;
static uint8_t s_pixels[CAPTURE_BYTES];

static nt_program_t make_program(const char *vs_src) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src});
    nt_program_t program = nt_gfx_make_program(vs, fs);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, program.id);
    return program;
}

void setUp(void) {
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_gfx_init(&NT_GFX_TEST_DESC(.max_shaders = 8, .max_programs = 4, .max_pipelines = 8, .max_buffers = 16, .max_textures = 4, .max_meshes = 4, .max_vertex_inputs = 8, .max_render_targets = 2));
    nt_gfx_begin_frame();
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_atlas_init();
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 8});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 8});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 8});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = 8});
    nt_sprite_comp_init(&(nt_sprite_comp_desc_t){.capacity = 8});
    nt_material_init(&(nt_material_desc_t){.max_materials = 4});
    s_atlas = minimal_ui_atlas_create();

    /* Textureless: the renderer needs no page for them. */
    s_plain = nt_material_create(&(nt_material_create_desc_t){.program = make_program(s_plain_vs_src), .cull_mode = NT_CULL_NONE, .label = "native_sprite_plain"});
    s_tint = nt_material_create(&(nt_material_create_desc_t){
        .program = make_program(s_tint_vs_src),
        .attr_map = {{.stream_name = "a_tint", .location = 4, .default_value = {0.0F, 0.0F, 1.0F, 1.0F}}},
        .attr_map_count = 1,
        .has_attr_defaults = true,
        .cull_mode = NT_CULL_NONE,
        .label = "native_sprite_tint",
    });
}

void tearDown(void) {
    nt_sprite_renderer_shutdown();
    nt_material_shutdown();
    minimal_ui_atlas_destroy(&s_atlas);
    nt_sprite_comp_shutdown();
    nt_material_comp_shutdown();
    nt_drawable_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
    nt_atlas_test_reset();
    nt_resource_shutdown();
    nt_gfx_shutdown();
    nt_hash_shutdown();
}

static void begin_black_pass(void) { nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F}); }

/* Reads the frame and closes it before any assertion, so a failure never leaves a pass open. */
static void capture_and_end_frame(void) {
    const uint32_t w = g_nt_window.fb_width;
    const uint32_t h = g_nt_window.fb_height;
    const bool read = nt_gfx_read_pixels(0, 0, (int)w, (int)h, s_pixels, sizeof(s_pixels));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE_MESSAGE(read, "framebuffer larger than the capture buffer");
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
}

/* The window may be wider than requested (OS minimum width), so probes are in NDC. */
static void assert_ndc_pixel(float x, float y, uint8_t r, uint8_t g, uint8_t b) {
    const uint32_t w = g_nt_window.fb_width;
    const uint32_t h = g_nt_window.fb_height;
    const uint32_t px = (uint32_t)((x + 1.0F) * 0.5F * (float)w);
    const uint32_t py = (uint32_t)((1.0F - y) * 0.5F * (float)h); /* captured rows are top-left */
    const uint8_t expected[3] = {r, g, b};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, &s_pixels[((size_t)py * w + px) * 4U], 3);
}

/* The white region is the unit quad at origin 0,0: scale and position place it in NDC. */
static nt_render_item_t make_sprite_item(nt_material_t material, float x, float y, float w, float h, uint32_t color) {
    nt_entity_t e = nt_entity_create();
    TEST_ASSERT_TRUE(nt_transform_comp_add(e));
    TEST_ASSERT_TRUE(nt_drawable_comp_add(e));
    TEST_ASSERT_TRUE(nt_material_comp_add(e));
    TEST_ASSERT_TRUE(nt_sprite_comp_add(e));
    *nt_material_comp_handle(e) = material;
    nt_drawable_comp_set_color(e, color);
    nt_transform_comp_set_position(e, x, y, 0.0F);
    nt_transform_comp_set_scale(e, w, h, 1.0F);
    nt_transform_comp_update();
    nt_sprite_comp_set_region(e, s_atlas.handle, (uint16_t)s_atlas.white_region_idx);
    const nt_sprite_comp_view_t sv = nt_sprite_comp_view();
    const uint16_t s_idx = sv.sparse_indices[nt_entity_index(e)];
    TEST_ASSERT_BITS_HIGH(NT_SPRITE_FLAG_RESOLVED, sv.flags[s_idx]);
    return (nt_render_item_t){.entity = e.id, .batch_key = nt_sprite_renderer_batch_key(material, sv.resolved[s_idx].page_resource)};
}

static void test_draw_list_with_a_stride_change_draws_both_runs(void) {
    /* B's own color is white: the right half must show the tint material's attr default. */
    const nt_render_item_t items[2] = {
        make_sprite_item(s_plain, -0.9F, -0.8F, 0.8F, 1.6F, k_red),
        make_sprite_item(s_tint, 0.1F, -0.8F, 0.8F, 1.6F, 0xFFFFFFFFU),
    };
    begin_black_pass();
    const uint32_t draws = nt_gfx_draw_calls(&g_nt_gfx.counters);
    nt_sprite_renderer_draw_list(items, 2);
    const uint32_t list_draws = nt_gfx_draw_calls(&g_nt_gfx.counters) - draws;
    capture_and_end_frame();
    TEST_ASSERT_EQUAL_UINT32(2, list_draws);
    assert_ndc_pixel(-0.5F, 0.0F, 255, 0, 0);
    assert_ndc_pixel(0.5F, 0.0F, 0, 0, 255);
    assert_ndc_pixel(0.0F, 0.0F, 0, 0, 0);
    assert_ndc_pixel(-0.5F, 0.95F, 0, 0, 0);
}

/* Returns the emit's first vertex in frame storage. */
static uint32_t emit_quad(nt_material_t material, float x0, float x1, uint32_t color, const float *custom, uint8_t custom_bytes) {
    const float positions[4][2] = {{x0, -0.5F}, {x1, -0.5F}, {x1, 0.5F}, {x0, 0.5F}};
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    nt_sprite_renderer_set_material(material);
    nt_sprite_renderer_emit_geometry(s_atlas.handle, s_atlas.white_region_idx, positions, 4, indices, 6, NT_MATH_MAT4_IDENTITY, color, custom, custom_bytes);
    nt_sprite_test_emit_t emit;
    nt_sprite_renderer_test_last_emit(&emit);
    return emit.first_vertex;
}

static void test_immediate_emits_across_material_switches_draw_each_quad(void) {
    const float yellow[4] = {1.0F, 1.0F, 0.0F, 1.0F};
    begin_black_pass();
    const uint32_t first_vertex[3] = {
        emit_quad(s_plain, -0.95F, -0.4F, k_red, NULL, 0),
        emit_quad(s_tint, -0.25F, 0.25F, 0xFFFFFFFFU, yellow, (uint8_t)sizeof(yellow)),
        emit_quad(s_plain, 0.4F, 0.95F, k_green, NULL, 0),
    };
    capture_and_end_frame();
    for (uint32_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_UINT32(0, first_vertex[i] % 4U);
    }
    assert_ndc_pixel(-0.675F, 0.0F, 255, 0, 0);
    assert_ndc_pixel(0.0F, 0.0F, 255, 255, 0);
    assert_ndc_pixel(0.675F, 0.0F, 0, 255, 0);
    assert_ndc_pixel(0.0F, 0.9F, 0, 0, 0);
}

int main(void) {
    /* One hidden window and GL context serve every test; setUp/tearDown reset only engine state. */
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){.max_dpr = 1.0F, .resizable = false, .width = FB_SIZE, .height = FB_SIZE};
    nt_window_init();
    UNITY_BEGIN();
    RUN_TEST(test_draw_list_with_a_stride_change_draws_both_runs);
    RUN_TEST(test_immediate_emits_across_material_switches_draw_each_quad);
    int failures = UNITY_END();
    nt_window_shutdown();
    return failures;
}
