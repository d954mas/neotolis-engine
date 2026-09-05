#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"
#include "window/nt_window.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

static void assert_rgba(const uint8_t *pixels, uint32_t pixel_count, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (uint32_t i = 0; i < pixel_count; i++) {
        const uint8_t *pixel = &pixels[(size_t)i * 4U];
        TEST_ASSERT_UINT8_WITHIN(1, r, pixel[0]);
        TEST_ASSERT_UINT8_WITHIN(1, g, pixel[1]);
        TEST_ASSERT_UINT8_WITHIN(1, b, pixel[2]);
        TEST_ASSERT_UINT8_WITHIN(1, a, pixel[3]);
    }
}

void setUp(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){
        .max_dpr = 1.0F,
        .resizable = false,
        .width = 64,
        .height = 64,
    };
    nt_window_init();

    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.max_textures = 2;
    desc.max_render_targets = 1;
    nt_gfx_init(&desc);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

void tearDown(void) {
    nt_gfx_shutdown();
    nt_window_shutdown();
}

static void test_render_target_resize_without_spare_texture_slots(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_TEXTURE,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH24,
        .depth_texture_min_filter = NT_FILTER_NEAREST,
        .depth_texture_mag_filter = NT_FILTER_NEAREST,
        .depth_texture_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .depth_texture_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .label = "native_rt_smoke",
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(target));

    uint8_t first_pixels[4U * 4U * 4U] = {0};
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = target,
        .clear_color = {0.25F, 0.5F, 0.75F, 1.0F},
        .clear_depth = 1.0F,
    });
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 4, 4, first_pixels, sizeof(first_pixels)));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    assert_rgba(first_pixels, 16, 64, 128, 191, 255);

    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(target, 7, 5));
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(target));

    uint8_t resized_pixels[7U * 5U * 4U] = {0};
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = target,
        .clear_color = {1.0F, 0.25F, 0.5F, 1.0F},
        .clear_depth = 1.0F,
    });
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 7, 5, resized_pixels, sizeof(resized_pixels)));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    assert_rgba(resized_pixels, 35, 255, 64, 128, 255);

    nt_gfx_destroy_render_target(target);
}

static void test_depth_texture_uses_explicit_format_and_wrap(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_TEXTURE,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH16,
        .depth_texture_min_filter = NT_FILTER_NEAREST,
        .depth_texture_mag_filter = NT_FILTER_NEAREST,
        .depth_texture_wrap_u = NT_WRAP_REPEAT,
        .depth_texture_wrap_v = NT_WRAP_MIRRORED_REPEAT,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(target, 6, 5));

    const nt_texture_t depth_texture = nt_gfx_render_target_depth(target);
    nt_gfx_backend_bind_texture(nt_gfx_test_texture_backend_id(depth_texture), 0);
    nt_gfx_backend_bind_sampler(nt_gfx_test_sampler_backend_id(nt_gfx_get_texture_default_sampler(depth_texture)), 0);
    GLint value = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &value);
    TEST_ASSERT_EQUAL_INT(GL_DEPTH_COMPONENT16, value);
    GLint sampler = 0;
    glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
    TEST_ASSERT_NOT_EQUAL_INT(0, sampler);
    glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_MIN_FILTER, &value);
    TEST_ASSERT_EQUAL_INT(GL_NEAREST, value);
    glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_MAG_FILTER, &value);
    TEST_ASSERT_EQUAL_INT(GL_NEAREST, value);
    glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_WRAP_S, &value);
    TEST_ASSERT_EQUAL_INT(GL_REPEAT, value);
    glGetSamplerParameteriv((GLuint)sampler, GL_TEXTURE_WRAP_T, &value);
    TEST_ASSERT_EQUAL_INT(GL_MIRRORED_REPEAT, value);

    nt_gfx_destroy_render_target(target);
}

/* Depth step written by geometry: the left texel keeps the pass clear, the
   right one takes the quad's depth. */
static const char *s_depth_vs = "layout(location = 0) in vec3 a_position;\n"
                                "void main() { gl_Position = vec4(a_position, 1.0); }\n";
static const char *s_depth_fs = "precision mediump float;\n"
                                "out vec4 frag_color;\n"
                                "void main() { frag_color = vec4(1.0); }\n";
static const char *s_fullscreen_vs = "layout(location = 0) in vec2 a_position;\n"
                                     "layout(location = 3) in vec2 a_uv;\n"
                                     "out vec2 v_uv;\n"
                                     "void main() {\n"
                                     "    v_uv = a_uv;\n"
                                     "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                                     "}\n";
static const char *s_shadow_fs = "precision highp float;\n"
                                 "uniform highp sampler2DShadow u_shadow;\n"
                                 "uniform float u_ref;\n"
                                 "in vec2 v_uv;\n"
                                 "out vec4 frag_color;\n"
                                 "void main() {\n"
                                 "    float lit = texture(u_shadow, vec3(v_uv, u_ref));\n"
                                 "    frag_color = vec4(lit, lit, lit, 1.0);\n"
                                 "}\n";
static const char *s_raw_fs = "precision highp float;\n"
                              "uniform highp sampler2D u_depth;\n"
                              "in vec2 v_uv;\n"
                              "out vec4 frag_color;\n"
                              "void main() {\n"
                              "    float d = texture(u_depth, v_uv).r;\n"
                              "    frag_color = vec4(d, d, d, 1.0);\n"
                              "}\n";

typedef struct {
    float pos[2];
    float uv[2];
} fullscreen_vertex_t;

/* The window framebuffer is whatever the OS grants a hidden window, so the
   shadow passes pin their own width and sample the ramp at fixed columns. */
#define RAMP_WIDTH 64
#define RAMP_NEAR 4
#define RAMP_EDGE 32
#define RAMP_FAR 60

static uint8_t ramp_at(const uint8_t *row, int column) { return row[(size_t)(unsigned)column * 4U]; }

static nt_pipeline_t make_test_pipeline(const char *vs_src, const char *fs_src, bool depth_write) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fs_src});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vs.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, fs.id);
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
        .depth_test = depth_write,
        .depth_write = depth_write,
        .depth_func = NT_DEPTH_ALWAYS,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);
    return pip;
}

static nt_vertex_input_t make_test_vertex_input(const nt_vertex_layout_t *layout, nt_buffer_t vbo) {
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = *layout, .vertex_buffer = vbo});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vi.id);
    return vi;
}

/* gl_VertexID draws still need a bound vertex input: the empty one. */
static void bind_empty_vertex_input(void) { nt_gfx_bind_vertex_input(nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0})); }

static void test_custom_blend_state_reaches_gl_unchanged(void) {
    nt_blend_state_t blend = nt_blend_alpha();
    blend.constant_color[0] = 0.2F;
    blend.constant_color[1] = 0.3F;
    blend.constant_color[2] = 0.4F;
    blend.constant_color[3] = 0.5F;
    blend.src_rgb = NT_BLEND_CONSTANT_COLOR;
    blend.dst_rgb = NT_BLEND_ONE_MINUS_DST_COLOR;
    blend.src_alpha = NT_BLEND_SRC_ALPHA;
    blend.dst_alpha = NT_BLEND_ONE_MINUS_DST_ALPHA;
    blend.op_rgb = NT_BLEND_OP_SUBTRACT;
    blend.op_alpha = NT_BLEND_OP_MAX;

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_depth_vs});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_depth_fs});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
        .blend = blend,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pipeline.id);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0, 0, 0, 0}});
    nt_gfx_bind_pipeline(pipeline);

    GLint value = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &value);
    TEST_ASSERT_EQUAL_INT(GL_CONSTANT_COLOR, value);
    glGetIntegerv(GL_BLEND_DST_RGB, &value);
    TEST_ASSERT_EQUAL_INT(GL_ONE_MINUS_DST_COLOR, value);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &value);
    TEST_ASSERT_EQUAL_INT(GL_SRC_ALPHA, value);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &value);
    TEST_ASSERT_EQUAL_INT(GL_ONE_MINUS_DST_ALPHA, value);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &value);
    TEST_ASSERT_EQUAL_INT(GL_FUNC_SUBTRACT, value);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &value);
    TEST_ASSERT_EQUAL_INT(GL_MAX, value);
    float constant[4] = {0};
    glGetFloatv(GL_BLEND_COLOR, constant);
    TEST_ASSERT_INT_WITHIN(1, 200, (int)(constant[0] * 1000.0F));
    TEST_ASSERT_INT_WITHIN(1, 300, (int)(constant[1] * 1000.0F));
    TEST_ASSERT_INT_WITHIN(1, 400, (int)(constant[2] * 1000.0F));
    TEST_ASSERT_INT_WITHIN(1, 500, (int)(constant[3] * 1000.0F));

    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_destroy_pipeline(pipeline);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

static void test_all_public_blend_enums_reach_gl(void) {
    static const struct {
        nt_blend_factor_t factor;
        GLenum expected;
    } factor_cases[] = {
        {NT_BLEND_ZERO, GL_ZERO},
        {NT_BLEND_ONE, GL_ONE},
        {NT_BLEND_SRC_COLOR, GL_SRC_COLOR},
        {NT_BLEND_ONE_MINUS_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR},
        {NT_BLEND_DST_COLOR, GL_DST_COLOR},
        {NT_BLEND_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_DST_COLOR},
        {NT_BLEND_SRC_ALPHA, GL_SRC_ALPHA},
        {NT_BLEND_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA},
        {NT_BLEND_DST_ALPHA, GL_DST_ALPHA},
        {NT_BLEND_ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA},
        {NT_BLEND_CONSTANT_COLOR, GL_CONSTANT_COLOR},
        {NT_BLEND_ONE_MINUS_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR},
        {NT_BLEND_CONSTANT_ALPHA, GL_CONSTANT_ALPHA},
        {NT_BLEND_ONE_MINUS_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA},
        {NT_BLEND_SRC_ALPHA_SATURATE, GL_SRC_ALPHA_SATURATE},
    };
    static const struct {
        nt_blend_op_t op;
        GLenum expected;
    } op_cases[] = {
        {NT_BLEND_OP_ADD, GL_FUNC_ADD}, {NT_BLEND_OP_SUBTRACT, GL_FUNC_SUBTRACT}, {NT_BLEND_OP_REVERSE_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT}, {NT_BLEND_OP_MIN, GL_MIN}, {NT_BLEND_OP_MAX, GL_MAX},
    };
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_depth_vs});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_depth_fs});
    nt_program_t prog = nt_gfx_make_program(vs, fs);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0, 0, 0, 0}});
    for (size_t i = 0; i < sizeof(factor_cases) / sizeof(factor_cases[0]); i++) {
        nt_blend_state_t blend = nt_blend_alpha();
        blend.src_rgb = factor_cases[i].factor;
        blend.dst_rgb = NT_BLEND_ZERO;
        nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
            .program = prog,
            .blend = blend,
        });
        TEST_ASSERT_NOT_EQUAL_UINT32(0, pipeline.id);
        nt_gfx_bind_pipeline(pipeline);
        GLint actual = 0;
        glGetIntegerv(GL_BLEND_SRC_RGB, &actual);
        TEST_ASSERT_EQUAL_INT((GLint)factor_cases[i].expected, actual);
        nt_gfx_destroy_pipeline(pipeline);
    }
    for (size_t i = 0; i < sizeof(op_cases) / sizeof(op_cases[0]); i++) {
        nt_blend_state_t blend = nt_blend_alpha();
        blend.op_rgb = op_cases[i].op;
        nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
            .program = prog,
            .blend = blend,
        });
        TEST_ASSERT_NOT_EQUAL_UINT32(0, pipeline.id);
        nt_gfx_bind_pipeline(pipeline);
        GLint actual = 0;
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &actual);
        TEST_ASSERT_EQUAL_INT((GLint)op_cases[i].expected, actual);
        nt_gfx_destroy_pipeline(pipeline);
    }
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

static void test_multiply_blend_multiplies_rgb_and_preserves_destination_alpha(void) {
    static const char *fragment_source = "precision mediump float;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(0.5, 0.25, 0.75, 0.25); }\n";
    static const float fullscreen_tri[6] = {-1.0F, -1.0F, 3.0F, -1.0F, -1.0F, 3.0F};
    const nt_vertex_layout_t layout = {
        .stride = sizeof(float) * 2,
        .attr_count = 1,
        .attrs = {{.location = NT_ATTR_POSITION, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 0}},
    };
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_fullscreen_vs});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
        .blend = nt_blend_multiply(),
    });
    nt_buffer_t vertices = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = fullscreen_tri,
        .size = sizeof(fullscreen_tri),
    });
    nt_vertex_input_t vertex_input = make_test_vertex_input(&layout, vertices);
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_NONE,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pipeline.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vertices.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);

    uint8_t pixels[4U * 4U * 4U] = {0};
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_color = {0.2F, 0.4F, 0.8F, 0.6F}});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vertex_input);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 4, 4, pixels, sizeof(pixels)));
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    assert_rgba(pixels, 16, 26, 26, 153, 153);
    nt_gfx_destroy_render_target(target);
    nt_gfx_destroy_vertex_input(vertex_input);
    nt_gfx_destroy_buffer(vertices);
    nt_gfx_destroy_pipeline(pipeline);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

/* Hardware comparison filters the 0/1 results, so the shadow edge lands between
   them. The blend weights are implementation-dependent, hence a bounds check
   rather than an exact midpoint. */
static void test_depth_comparison_sampler_blends_comparison_results(void) {
    nt_render_target_t shadow_map = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 2,
        .height = 1,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_TEXTURE,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH24,
        .depth_texture_min_filter = NT_FILTER_NEAREST,
        .depth_texture_mag_filter = NT_FILTER_NEAREST,
        .depth_texture_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .depth_texture_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .label = "shadow_map_probe",
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, shadow_map.id);
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(shadow_map));
    /* Reading outside the drawable is undefined, so refuse to guess its width. */
    TEST_ASSERT_TRUE_MESSAGE(g_nt_window.fb_width >= RAMP_WIDTH, "drawable narrower than the sampled ramp");

    /* z_ndc 0.6 maps to window depth 0.8; the pass clear leaves 0.2 on the left. */
    static const float right_half_quad[18] = {
        0.0F, -1.0F, 0.6F, 1.0F, -1.0F, 0.6F, 1.0F, 1.0F, 0.6F, 0.0F, -1.0F, 0.6F, 1.0F, 1.0F, 0.6F, 0.0F, 1.0F, 0.6F,
    };
    static const fullscreen_vertex_t fullscreen_tri[3] = {
        {{-1.0F, -1.0F}, {0.0F, 0.0F}},
        {{3.0F, -1.0F}, {2.0F, 0.0F}},
        {{-1.0F, 3.0F}, {0.0F, 2.0F}},
    };
    nt_vertex_layout_t depth_layout = {
        .stride = sizeof(float) * 3,
        .attr_count = 1,
        .attrs = {{.location = NT_ATTR_POSITION, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0}},
    };
    nt_vertex_layout_t fullscreen_layout = {
        .stride = sizeof(fullscreen_vertex_t),
        .attr_count = 2,
        .attrs =
            {
                {.location = NT_ATTR_POSITION, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 0},
                {.location = NT_ATTR_TEXCOORD0, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 8},
            },
    };
    nt_buffer_t depth_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = right_half_quad, .size = sizeof(right_half_quad)});
    nt_buffer_t fullscreen_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = fullscreen_tri, .size = sizeof(fullscreen_tri)});
    nt_pipeline_t depth_pip = make_test_pipeline(s_depth_vs, s_depth_fs, true);
    nt_pipeline_t shadow_pip = make_test_pipeline(s_fullscreen_vs, s_shadow_fs, false);
    nt_pipeline_t raw_pip = make_test_pipeline(s_fullscreen_vs, s_raw_fs, false);
    nt_vertex_input_t depth_vi = make_test_vertex_input(&depth_layout, depth_vbo);
    nt_vertex_input_t fullscreen_vi = make_test_vertex_input(&fullscreen_layout, fullscreen_vbo);

    nt_sampler_t comparison = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .compare_func = NT_COMPARE_LEQUAL,
    });
    nt_sampler_t raw = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, comparison.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(comparison.id, raw.id);

    nt_texture_t depth_tex = nt_gfx_render_target_depth(shadow_map);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = shadow_map, .clear_color = {0, 0, 0, 1}, .clear_depth = 0.2F});
    nt_gfx_bind_pipeline(depth_pip);
    nt_gfx_bind_vertex_input(depth_vi);
    nt_gfx_draw(0, 6);
    nt_gfx_end_pass();

    /* The fullscreen triangle spreads v_uv.x across the viewport, so one row
       readback carries the whole shadow ramp. */
    uint8_t row[RAMP_WIDTH * 4] = {0};
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_gfx_set_viewport(0, 0, RAMP_WIDTH, (int)g_nt_window.fb_height);
    nt_gfx_bind_pipeline(shadow_pip);
    nt_gfx_bind_vertex_input(fullscreen_vi);
    const nt_gfx_texture_binding_t shadow_binding = {.name = nt_hash32_str("u_shadow"), .texture = depth_tex, .sampler = comparison};
    nt_gfx_apply_texture_bindings(&shadow_binding, 1);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
    nt_gfx_set_uniform_float(nt_hash32_str("u_ref"), 0.5F);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, RAMP_WIDTH, 1, row, sizeof(row)));
    nt_gfx_end_pass();

    GLint sampler_binding = 0;
    glGetIntegerv(GL_SAMPLER_BINDING, &sampler_binding);
    GLint compare_state = 0;
    glGetSamplerParameteriv((GLuint)sampler_binding, GL_TEXTURE_COMPARE_MODE, &compare_state);
    TEST_ASSERT_EQUAL_INT(GL_COMPARE_REF_TO_TEXTURE, compare_state);
    glGetSamplerParameteriv((GLuint)sampler_binding, GL_TEXTURE_COMPARE_FUNC, &compare_state);
    TEST_ASSERT_EQUAL_INT(GL_LEQUAL, compare_state);

    /* u < 0.25 samples only the near texel, u > 0.75 only the far one. */
    TEST_ASSERT_EQUAL_UINT8(0, ramp_at(row, RAMP_NEAR));
    TEST_ASSERT_EQUAL_UINT8(255, ramp_at(row, RAMP_FAR));
    TEST_ASSERT_TRUE_MESSAGE(ramp_at(row, RAMP_EDGE) > 0 && ramp_at(row, RAMP_EDGE) < 255,
                             "shadow edge did not blend comparison results — both specs allow a driver to compare a single texel, so suspect driver latitude before the sampler path");

    /* Same texture, plain sampler: raw depth for a debug view. */
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_gfx_set_viewport(0, 0, RAMP_WIDTH, (int)g_nt_window.fb_height);
    nt_gfx_bind_pipeline(raw_pip);
    nt_gfx_bind_vertex_input(fullscreen_vi);
    const nt_gfx_texture_binding_t raw_binding = {.name = nt_hash32_str("u_depth"), .texture = depth_tex, .sampler = raw};
    nt_gfx_apply_texture_bindings(&raw_binding, 1);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
    nt_gfx_draw(0, 3);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, RAMP_WIDTH, 1, row, sizeof(row)));
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_UINT8_WITHIN(2, 51, ramp_at(row, RAMP_NEAR));
    TEST_ASSERT_UINT8_WITHIN(2, 204, ramp_at(row, RAMP_FAR));

    nt_gfx_destroy_render_target(shadow_map);
}

/* An over-range clear must survive the round trip — clamping to white would
   mean the target has no headroom and only the format name changed. */
static void test_half_float_target_is_complete_and_keeps_values_above_one(void) {
    TEST_ASSERT_TRUE(g_nt_gfx.gpu_caps.has_float_render_target);

    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA16F,
        .color_min_filter = NT_FILTER_LINEAR,
        .color_mag_filter = NT_FILTER_LINEAR,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_NONE,
        .depth_format = NT_TEXTURE_FORMAT_INVALID,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(target));

    nt_gfx_backend_bind_texture(nt_gfx_test_texture_backend_id(nt_gfx_render_target_color(target)), 0);
    GLint internal_format = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal_format);
    TEST_ASSERT_EQUAL_INT(GL_RGBA16F, internal_format);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_color = {3.5F, 0.25F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    float pixels[4 * 4 * 4] = {0};
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_FLOAT, pixels);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    /* Unity float asserts are disabled in this build; compare in millis. */
    TEST_ASSERT_INT_WITHIN(10, 3500, (int)(pixels[0] * 1000.0F));
    TEST_ASSERT_INT_WITHIN(10, 250, (int)(pixels[1] * 1000.0F));

    nt_gfx_destroy_render_target(target);
}

static void test_depth_buffer_uses_explicit_format(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_BUFFER,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH32F,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_depth = 1.0F});
    GLint object_type = 0;
    GLint renderbuffer = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &object_type);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &renderbuffer);
    TEST_ASSERT_EQUAL_INT(GL_RENDERBUFFER, object_type);
    TEST_ASSERT_NOT_EQUAL_INT(0, renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)renderbuffer);
    GLint format = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
    TEST_ASSERT_EQUAL_INT(GL_DEPTH_COMPONENT32F, format);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_render_target(target);
}

/* The clear runs with the depth mask forced on and leaves it on; the pipeline
 * that wants it off must be re-bound inside the new pass. */
static void test_begin_pass_clears_depth_after_depth_writes_were_disabled(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0); }\n";
    static const char *fragment_source = "#ifdef GL_ES\n"
                                         "precision mediump float;\n"
                                         "#endif\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(1.0); }\n";

    nt_shader_t vertex_shader = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fragment_shader = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t prog = nt_gfx_make_program(vertex_shader, fragment_shader);
    nt_pipeline_t no_depth_write_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
        .depth_write = false,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, no_depth_write_pipeline.id);

    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_BUFFER,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH24,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_depth = 0.25F});
    nt_gfx_bind_pipeline(no_depth_write_pipeline);
    nt_gfx_end_pass();

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_depth = 0.75F});
    float depth = 0.0F;
    glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    uint32_t depth_milli = (uint32_t)((depth * 1000.0F) + 0.5F);
    TEST_ASSERT_UINT32_WITHIN(1, 750, depth_milli);
    nt_gfx_bind_pipeline(no_depth_write_pipeline);
    GLboolean depth_write_enabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write_enabled);
    TEST_ASSERT_EQUAL_INT(GL_FALSE, depth_write_enabled);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_render_target(target);
    nt_gfx_destroy_pipeline(no_depth_write_pipeline);
    nt_gfx_destroy_shader(fragment_shader);
    nt_gfx_destroy_shader(vertex_shader);
}

/* Real GL must record the registered block binding when the program links. */
static void test_global_block_registered_before_link_binds_in_the_program(void) {
    static const char *vertex_source = "layout(std140) uniform Globals { vec4 g_offset; };\n"
                                       "void main() { gl_Position = vec4(g_offset.xy, 0.0, 1.0); }\n";
    static const char *fragment_source = "#ifdef GL_ES\n"
                                         "precision mediump float;\n"
                                         "#endif\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(1.0); }\n";

    nt_gfx_register_global_block("Globals", 3);

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    TEST_ASSERT_TRUE(nt_gfx_program_ready(prog));

    /* Bind a pipeline so the program becomes current, then read the binding
     * GL actually recorded for the block. */
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);

    GLint current_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    TEST_ASSERT_NOT_EQUAL_INT(0, current_program);
    GLuint block_index = glGetUniformBlockIndex((GLuint)current_program, "Globals");
    TEST_ASSERT_NOT_EQUAL_UINT32(GL_INVALID_INDEX, block_index);
    GLint binding = -1;
    glGetActiveUniformBlockiv((GLuint)current_program, block_index, GL_UNIFORM_BLOCK_BINDING, &binding);
    TEST_ASSERT_EQUAL_INT(3, binding);

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_pipeline(pip);
    nt_gfx_destroy_program(prog);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

/* Uniform values live on the program, not the pipeline: two pipelines on one
 * program see the same values, and binding the second does not reset them. */
static void test_uniform_values_are_shared_by_pipelines_on_one_program(void) {
    /* Attribute-less full-screen triangle: the pipelines carry no layout. */
    static const char *vertex_source = "void main() {\n"
                                       "    float x = float((gl_VertexID << 1) & 2);\n"
                                       "    float y = float(gl_VertexID & 2);\n"
                                       "    gl_Position = vec4((vec2(x, y) * 2.0) - 1.0, 0.0, 1.0);\n"
                                       "}\n";
    static const char *fragment_source = "#ifdef GL_ES\n"
                                         "precision mediump float;\n"
                                         "#endif\n"
                                         "uniform vec4 u_color;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = u_color; }\n";

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t prog = nt_gfx_make_program(vs, fs);

    /* Two pipelines differing only in fixed-function state. */
    nt_pipeline_t pip_a = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog, .depth_test = false});
    nt_pipeline_t pip_b = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog, .depth_test = true, .depth_func = NT_DEPTH_ALWAYS});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip_a.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip_b.id);

    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_BUFFER,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH24,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    /* Value written while A is bound; the draw happens through B without
     * setting it again. */
    nt_gfx_bind_pipeline(pip_a);
    const float green[4] = {0.0F, 1.0F, 0.0F, 1.0F};
    nt_gfx_set_uniform_vec4(nt_hash32_str("u_color"), green);
    nt_gfx_bind_pipeline(pip_b);
    bind_empty_vertex_input();
    nt_gfx_draw(0, 3);

    uint8_t pixels[4 * 4 * 4] = {0};
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 4, 4, pixels, sizeof(pixels)));
    assert_rgba(pixels, 16, 0, 255, 0, 255);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_render_target(target);
    nt_gfx_destroy_pipeline(pip_b);
    nt_gfx_destroy_pipeline(pip_a);
    nt_gfx_destroy_program(prog);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

/* Distinct output colors expose a pipeline binding the wrong program. */
static void test_each_pipeline_binds_its_own_program(void) {
    static const char *vertex_source = "void main() {\n"
                                       "    float x = float((gl_VertexID << 1) & 2);\n"
                                       "    float y = float(gl_VertexID & 2);\n"
                                       "    gl_Position = vec4((vec2(x, y) * 2.0) - 1.0, 0.0, 1.0);\n"
                                       "}\n";
    static const char *red_source = "#ifdef GL_ES\n"
                                    "precision mediump float;\n"
                                    "#endif\n"
                                    "out vec4 frag_color;\n"
                                    "void main() { frag_color = vec4(1.0, 0.0, 0.0, 1.0); }\n";
    static const char *blue_source = "#ifdef GL_ES\n"
                                     "precision mediump float;\n"
                                     "#endif\n"
                                     "out vec4 frag_color;\n"
                                     "void main() { frag_color = vec4(0.0, 0.0, 1.0, 1.0); }\n";

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t red_fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = red_source});
    nt_shader_t blue_fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = blue_source});
    nt_program_t red = nt_gfx_make_program(vs, red_fs);
    nt_program_t blue = nt_gfx_make_program(vs, blue_fs);
    nt_pipeline_t pip_red = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = red});
    nt_pipeline_t pip_blue = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = blue});

    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_BUFFER,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH24,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);

    uint8_t pixels[4 * 4 * 4] = {0};
    nt_gfx_begin_frame();

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip_red);
    bind_empty_vertex_input();
    nt_gfx_draw(0, 3);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 4, 4, pixels, sizeof(pixels)));
    assert_rgba(pixels, 16, 255, 0, 0, 255);
    nt_gfx_end_pass();

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip_blue);
    bind_empty_vertex_input();
    nt_gfx_draw(0, 3);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 4, 4, pixels, sizeof(pixels)));
    assert_rgba(pixels, 16, 0, 0, 255, 255);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_render_target(target);
    nt_gfx_destroy_pipeline(pip_blue);
    nt_gfx_destroy_pipeline(pip_red);
    nt_gfx_destroy_program(blue);
    nt_gfx_destroy_program(red);
    nt_gfx_destroy_shader(blue_fs);
    nt_gfx_destroy_shader(red_fs);
    nt_gfx_destroy_shader(vs);
}

/* Destroying one pipeline must leave its shared program usable by the other. */
static void test_destroying_one_pipeline_leaves_the_shared_program_alive(void) {
    static const char *vertex_source = "void main() {\n"
                                       "    float x = float((gl_VertexID << 1) & 2);\n"
                                       "    float y = float(gl_VertexID & 2);\n"
                                       "    gl_Position = vec4((vec2(x, y) * 2.0) - 1.0, 0.0, 1.0);\n"
                                       "}\n";
    static const char *fragment_source = "#ifdef GL_ES\n"
                                         "precision mediump float;\n"
                                         "#endif\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(0.0, 1.0, 0.0, 1.0); }\n";

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_pipeline_t pip_a = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog, .depth_test = false});
    nt_pipeline_t pip_b = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog, .depth_test = true});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip_a.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip_b.id);

    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = 4,
        .height = 4,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_BUFFER,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH24,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, target.id);

    nt_gfx_destroy_pipeline(pip_a);
    TEST_ASSERT_TRUE(nt_gfx_program_ready(prog));

    uint8_t pixels[4 * 4 * 4] = {0};
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip_b);
    bind_empty_vertex_input();
    nt_gfx_draw(0, 3);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 4, 4, pixels, sizeof(pixels)));
    assert_rgba(pixels, 16, 0, 255, 0, 255);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_render_target(target);
    nt_gfx_destroy_pipeline(pip_b);
    nt_gfx_destroy_program(prog);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

/* Late block registration must reach programs linked before the game registered the binding. */
static void test_global_block_registered_after_link_binds_in_that_program(void) {
    static const char *vertex_source = "layout(std140) uniform Globals { vec4 g_offset; };\n"
                                       "void main() { gl_Position = vec4(g_offset.xy, 0.0, 1.0); }\n";
    static const char *fragment_source = "#ifdef GL_ES\n"
                                         "precision mediump float;\n"
                                         "#endif\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(1.0); }\n";

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});

    /* Registration must update this already-linked program. */
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    TEST_ASSERT_TRUE(nt_gfx_program_ready(prog));
    nt_gfx_register_global_block("Globals", 5);

    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);

    GLint current_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    TEST_ASSERT_NOT_EQUAL_INT(0, current_program);
    GLuint block_index = glGetUniformBlockIndex((GLuint)current_program, "Globals");
    TEST_ASSERT_NOT_EQUAL_UINT32(GL_INVALID_INDEX, block_index);
    GLint binding = -1;
    glGetActiveUniformBlockiv((GLuint)current_program, block_index, GL_UNIFORM_BLOCK_BINDING, &binding);
    TEST_ASSERT_EQUAL_INT(5, binding);

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_pipeline(pip);
    nt_gfx_destroy_program(prog);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

static GLuint bind_uniform_test_program(const char *fragment_source) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t program = nt_gfx_make_program(vs, fs);
    TEST_ASSERT_TRUE(nt_gfx_program_ready(program));
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    GLint current_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    TEST_ASSERT_NOT_EQUAL_INT(0, current_program);
    return (GLuint)current_program;
}

static void assert_uniform_float(GLuint program, const char *name, int expected) {
    GLint location = glGetUniformLocation(program, name);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, location);
    GLfloat value = 0.0F;
    glGetUniformfv(program, location, &value);
    TEST_ASSERT_EQUAL_INT(expected, (int)value);
}

static void test_uniform_cache_addresses_all_sixteen_array_elements(void) {
    static const char *fragment_source = "layout(std140) uniform Globals { vec4 g_values[17]; };\n"
                                         "uniform float u_values[16];\n"
                                         "out vec4 frag_color;\n"
                                         "void main() {\n"
                                         "    float sum = 0.0;\n"
                                         "    for (int i = 0; i < 16; i++) { sum += u_values[i]; }\n"
                                         "    frag_color = vec4(sum + g_values[16].x);\n"
                                         "}\n";
    GLuint program = bind_uniform_test_program(fragment_source);
    for (int i = 0; i < 16; i++) {
        char name[32];
        (void)snprintf(name, sizeof(name), "u_values[%d]", i);
        nt_gfx_set_uniform_float(nt_hash32_str(name), (float)(i + 1));
        assert_uniform_float(program, name, i + 1);
    }
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

#define LONG_UNIFORM_NAME "u_uniform_with_a_name_that_is_longer_than_sixty_four_characters_and_must_not_be_truncated"

static void test_uniform_cache_preserves_long_names(void) {
    static const char *fragment_source = "uniform float " LONG_UNIFORM_NAME ";\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(" LONG_UNIFORM_NAME "); }\n";
    GLuint program = bind_uniform_test_program(fragment_source);
    nt_gfx_set_uniform_float(nt_hash32_str(LONG_UNIFORM_NAME), 7.0F);
    assert_uniform_float(program, LONG_UNIFORM_NAME, 7);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_uniform_cache_expands_only_terminal_struct_array_indices(void) {
    static const char *fragment_source = "struct Light { float weights[2]; };\n"
                                         "uniform Light u_lights[2];\n"
                                         "out vec4 frag_color;\n"
                                         "void main() {\n"
                                         "    frag_color = vec4(u_lights[0].weights[0] + u_lights[0].weights[1]\n"
                                         "                    + u_lights[1].weights[0] + u_lights[1].weights[1]);\n"
                                         "}\n";
    GLuint program = bind_uniform_test_program(fragment_source);
    nt_gfx_set_uniform_float(nt_hash32_str("u_lights[0].weights[1]"), 3.0F);
    nt_gfx_set_uniform_float(nt_hash32_str("u_lights[1].weights[1]"), 5.0F);
    assert_uniform_float(program, "u_lights[0].weights[1]", 3);
    assert_uniform_float(program, "u_lights[1].weights[1]", 5);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_uniform_cache_asserts_when_array_elements_exceed_capacity(void) {
    static const char *fragment_source = "uniform float u_values[17];\n"
                                         "out vec4 frag_color;\n"
                                         "void main() {\n"
                                         "    float sum = 0.0;\n"
                                         "    for (int i = 0; i < 17; i++) { sum += u_values[i]; }\n"
                                         "    frag_color = vec4(sum);\n"
                                         "}\n";
    NT_TEST_EXPECT_ASSERT((void)bind_uniform_test_program(fragment_source));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "NT_MAX_CACHED_UNIFORMS"));
}

static void test_uniform_cache_asserts_when_scalar_uniforms_exceed_capacity(void) {
    static const char *fragment_source = "uniform float u0, u1, u2, u3, u4, u5, u6, u7, u8;\n"
                                         "uniform float u9, u10, u11, u12, u13, u14, u15, u16;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() {\n"
                                         "    frag_color = vec4(u0 + u1 + u2 + u3 + u4 + u5 + u6 + u7 + u8\n"
                                         "                    + u9 + u10 + u11 + u12 + u13 + u14 + u15 + u16);\n"
                                         "}\n";
    NT_TEST_EXPECT_ASSERT((void)bind_uniform_test_program(fragment_source));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "NT_MAX_CACHED_UNIFORMS"));
}

// #region program-owned sampler units
static nt_program_t make_sampler_program(const char *vertex_source, const char *fragment_source) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    TEST_ASSERT_TRUE(nt_gfx_program_ready(prog));
    return prog;
}

/* Reflection order is the driver's business, so only the unit set is pinned. */
static void assert_distinct_units_in_range(const int *units, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, units[i]);
        TEST_ASSERT_LESS_THAN_INT((int)count, units[i]);
        for (uint32_t j = i + 1; j < count; j++) {
            TEST_ASSERT_NOT_EQUAL_INT(units[i], units[j]);
        }
    }
}

static void test_supported_sampler_types_retain_their_classes(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform sampler2D u_color;\n"
                                         "uniform highp usampler2D u_ids;\n"
                                         "uniform highp sampler2DShadow u_shadow;\n"
                                         "uniform float u_scale;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() {\n"
                                         "    vec4 c = texture(u_color, vec2(0.5));\n"
                                         "    uvec4 i = texture(u_ids, vec2(0.5));\n"
                                         "    float s = texture(u_shadow, vec3(0.5));\n"
                                         "    frag_color = (c + vec4(i) + vec4(s)) * u_scale;\n"
                                         "}\n";
    nt_program_t prog = make_sampler_program(vertex_source, fragment_source);

    nt_gfx_sampler_info_t infos[3] = {0};
    TEST_ASSERT_TRUE(nt_gfx_test_program_sampler_info(prog, nt_hash32_str("u_color"), &infos[0]));
    TEST_ASSERT_TRUE(nt_gfx_test_program_sampler_info(prog, nt_hash32_str("u_ids"), &infos[1]));
    TEST_ASSERT_TRUE(nt_gfx_test_program_sampler_info(prog, nt_hash32_str("u_shadow"), &infos[2]));
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_SAMPLER_CLASS_FLOAT, infos[0].sampler_class);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_SAMPLER_CLASS_UINT, infos[1].sampler_class);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_SAMPLER_CLASS_SHADOW, infos[2].sampler_class);

    const int units[3] = {
        infos[0].unit,
        infos[1].unit,
        infos[2].unit,
    };
    assert_distinct_units_in_range(units, 3);
    TEST_ASSERT_EQUAL_UINT32(0x7U, nt_gfx_test_program_sampler_mask(prog));
    TEST_ASSERT_FALSE(nt_gfx_test_program_sampler_info(prog, nt_hash32_str("u_scale"), &infos[0]));
    TEST_ASSERT_FALSE(nt_gfx_test_program_sampler_info(prog, nt_hash32_str("u_missing"), &infos[0]));
}

static void test_signed_sampler_type_asserts_at_link(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform highp isampler2D u_signed_ids;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(texture(u_signed_ids, vec2(0.5))); }\n";
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    NT_TEST_EXPECT_ASSERT((void)nt_gfx_make_program(vs, fs));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "signed integer texture format"));
}

static void test_active_sampler_hash_collision_asserts_at_link(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform sampler2D attr4040;\n"
                                         "uniform sampler2D attr168680;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = texture(attr4040, vec2(0.5)) + texture(attr168680, vec2(0.5)); }\n";
    TEST_ASSERT_EQUAL_UINT32(nt_hash32_str("attr4040").value, nt_hash32_str("attr168680").value);
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    NT_TEST_EXPECT_ASSERT((void)nt_gfx_make_program(vs, fs));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "active uniform name hash collision"));
}

static void test_sampler_and_value_uniform_hash_collision_asserts_at_link(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform sampler2D attr4040;\n"
                                         "uniform int attr168680;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = texture(attr4040, vec2(0.5)) + vec4(float(attr168680)); }\n";
    TEST_ASSERT_EQUAL_UINT32(nt_hash32_str("attr4040").value, nt_hash32_str("attr168680").value);
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    NT_TEST_EXPECT_ASSERT((void)nt_gfx_make_program(vs, fs));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "active uniform name hash collision"));
}

static void test_value_uniform_hash_collision_asserts_at_link(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform float attr4040;\n"
                                         "uniform int attr168680;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(attr4040 + float(attr168680)); }\n";
    TEST_ASSERT_EQUAL_UINT32(nt_hash32_str("attr4040").value, nt_hash32_str("attr168680").value);
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    NT_TEST_EXPECT_ASSERT((void)nt_gfx_make_program(vs, fs));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "active uniform name hash collision"));
}

static void test_vertex_stage_and_array_samplers_get_distinct_units(void) {
    static const char *vertex_source = "uniform sampler2D u_heights;\n"
                                       "void main() { gl_Position = texture(u_heights, vec2(0.5)); }\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform sampler2D u_arr[2];\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = texture(u_arr[0], vec2(0.5)) + texture(u_arr[1], vec2(0.5)); }\n";
    nt_program_t prog = make_sampler_program(vertex_source, fragment_source);

    const int units[3] = {
        nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_heights")),
        nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_arr[0]")),
        nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_arr[1]")),
    };
    assert_distinct_units_in_range(units, 3);
    TEST_ASSERT_EQUAL_UINT32(0x7U, nt_gfx_test_program_sampler_mask(prog));
}

static void test_reflection_reports_active_uniforms_with_glsl_declarations(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "#define COUNT 2\n"
                                         "// uniform samplerCube u_comment;\n"
                                         "uniform sampler2D u_unused;\n"
                                         "uniform sampler2D u_arr [ COUNT ], u_b;\n"
                                         "uniform sampler2D a_very_long_sampler_name_that_goes_well_past_eighty_characters_to_check_the_buffer_limit_x;\n"
                                         "vec4 read_tex(sampler2D tex, vec2 uv) { return texture(tex, uv); }\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = read_tex(u_arr[0], vec2(0.5)) + read_tex(u_arr[1], vec2(0.5))\n"
                                         "    + read_tex(u_b, vec2(0.5))\n"
                                         "    + read_tex(a_very_long_sampler_name_that_goes_well_past_eighty_characters_to_check_the_buffer_limit_x, vec2(0.5)); }\n";
    nt_program_t prog = make_sampler_program(vertex_source, fragment_source);
    const int units[] = {
        nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_arr[0]")),
        nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_arr[1]")),
        nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_b")),
        nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("a_very_long_sampler_name_that_goes_well_past_eighty_characters_to_check_the_buffer_limit_x")),
    };
    assert_distinct_units_in_range(units, 4);
    TEST_ASSERT_EQUAL_UINT32(0xFU, nt_gfx_test_program_sampler_mask(prog));
    TEST_ASSERT_EQUAL_INT(-1, nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_comment")));
    TEST_ASSERT_EQUAL_INT(-1, nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_unused")));
    TEST_ASSERT_EQUAL_INT(-1, nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("tex")));
    TEST_ASSERT_EQUAL_INT(-1, nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_arr")));
}

/* An unsupported sampler type has no texture-unit story, so it is rejected at link
 * rather than bound silently wrong. The GL program leaks: the assert is the contract. */
static void test_program_with_an_unsupported_sampler_type_asserts(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform samplerCube u_env;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = texture(u_env, vec3(0.0, 0.0, 1.0)); }\n";
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    NT_TEST_EXPECT_ASSERT((void)nt_gfx_make_program(vs, fs));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "unsupported sampler type"));
}

/* One sampler past the unit budget is a link failure, not a sampler on unit 0. */
static void test_program_with_more_samplers_than_units_asserts(void) {
    _Static_assert(NT_GFX_MAX_TEXTURE_SLOTS == 8, "shader below declares NT_GFX_MAX_TEXTURE_SLOTS + 1 samplers");
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform sampler2D u0, u1, u2, u3, u4, u5, u6, u7, u8;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() {\n"
                                         "    vec2 uv = vec2(0.5);\n"
                                         "    frag_color = texture(u0, uv) + texture(u1, uv) + texture(u2, uv) + texture(u3, uv) + texture(u4, uv)\n"
                                         "               + texture(u5, uv) + texture(u6, uv) + texture(u7, uv) + texture(u8, uv);\n"
                                         "}\n";
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    NT_TEST_EXPECT_ASSERT((void)nt_gfx_make_program(vs, fs));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "NT_GFX_MAX_TEXTURE_SLOTS"));
}

static nt_texture_t make_unit_test_texture(const uint8_t rgba[4]) {
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 1,
        .height = 1,
        .data = rgba,
        .format = NT_TEXTURE_FORMAT_RGBA8,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
    });
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(tex));
    return tex;
}

/* No set_uniform_int anywhere: each sampler reads the unit the link wrote. */
static void test_samplers_read_their_link_time_units_without_uniform_writes(void) {
    static const char *vertex_source = "out vec2 v_uv;\n"
                                       "void main() {\n"
                                       "    float x = float((gl_VertexID << 1) & 2);\n"
                                       "    float y = float(gl_VertexID & 2);\n"
                                       "    v_uv = vec2(x, y);\n"
                                       "    gl_Position = vec4((vec2(x, y) * 2.0) - 1.0, 0.0, 1.0);\n"
                                       "}\n";
    /* Red through u_a and green through u_b make yellow; two samplers sharing
       one unit would read one texture and show red or green. */
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform sampler2D u_a;\n"
                                         "uniform sampler2D u_b;\n"
                                         "in vec2 v_uv;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(texture(u_a, v_uv).r, texture(u_b, v_uv).g, 0.0, 1.0); }\n";
    nt_program_t prog = make_sampler_program(vertex_source, fragment_source);
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);

    static const uint8_t red[4] = {255, 0, 0, 255};
    static const uint8_t green[4] = {0, 255, 0, 255};
    nt_texture_t tex_red = make_unit_test_texture(red);
    nt_texture_t tex_green = make_unit_test_texture(green);

    const int unit_a = nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_a"));
    const int unit_b = nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_b"));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, unit_a);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, unit_b);
    TEST_ASSERT_NOT_EQUAL_INT(unit_a, unit_b);

    uint8_t pixel[4] = {0};
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 1.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    bind_empty_vertex_input();
    const nt_gfx_texture_binding_t bindings[] = {
        {.name = nt_hash32_str("u_a"), .texture = tex_red, .sampler = NT_SAMPLER_DEFAULT},
        {.name = nt_hash32_str("u_b"), .texture = tex_green, .sampler = NT_SAMPLER_DEFAULT},
    };
    nt_gfx_apply_texture_bindings(bindings, 2);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
    nt_gfx_draw(0, 3);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 1, 1, pixel, sizeof(pixel)));
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    assert_rgba(pixel, 1, 255, 255, 0, 255);
}

/* Two draws through one program bind at the units it reports: rebinding one unit
 * between them leaves the other's texture alone. */
static void test_two_draws_on_one_program_bind_at_their_queried_units(void) {
    static const char *vertex_source = "out vec2 v_uv;\n"
                                       "void main() {\n"
                                       "    float x = float((gl_VertexID << 1) & 2);\n"
                                       "    float y = float(gl_VertexID & 2);\n"
                                       "    v_uv = vec2(x, y);\n"
                                       "    gl_Position = vec4((vec2(x, y) * 2.0) - 1.0, 0.0, 1.0);\n"
                                       "}\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform sampler2D u_a;\n"
                                         "uniform sampler2D u_b;\n"
                                         "in vec2 v_uv;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(texture(u_a, v_uv).r, texture(u_b, v_uv).g, 0.0, 1.0); }\n";
    nt_program_t prog = make_sampler_program(vertex_source, fragment_source);
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);

    static const uint8_t red[4] = {255, 0, 0, 255};
    static const uint8_t green[4] = {0, 255, 0, 255};
    nt_texture_t tex_red = make_unit_test_texture(red);
    nt_texture_t tex_green = make_unit_test_texture(green);

    const int unit_a = nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_a"));
    const int unit_b = nt_gfx_test_program_sampler_unit(prog, nt_hash32_str("u_b"));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, unit_a);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, unit_b);

    uint8_t first[4] = {0};
    uint8_t second[4] = {0};
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 1.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    bind_empty_vertex_input();

    /* First draw: red through u_a, green through u_b. */
    nt_gfx_texture_binding_t bindings[] = {
        {.name = nt_hash32_str("u_a"), .texture = tex_red, .sampler = NT_SAMPLER_DEFAULT},
        {.name = nt_hash32_str("u_b"), .texture = tex_green, .sampler = NT_SAMPLER_DEFAULT},
    };
    nt_gfx_apply_texture_bindings(bindings, 2);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
    nt_gfx_draw(0, 3);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 1, 1, first, sizeof(first)));

    /* Second draw puts red on u_b's unit only: u_a must keep reading red (red has no green). */
    bindings[1].texture = tex_red;
    nt_gfx_apply_texture_bindings(bindings, 2);
    TEST_ASSERT_EQUAL_UINT8(NT_GFX_TEXTURE_SET_APPLIED, nt_gfx_test_texture_set_state());
    nt_gfx_draw(0, 3);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 1, 1, second, sizeof(second)));
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    assert_rgba(first, 1, 255, 255, 0, 255);
    assert_rgba(second, 1, 255, 0, 0, 255);
}

/* The link fixed the unit; a later write would silently redirect every material. */
static void test_set_uniform_int_on_a_sampler_asserts(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform sampler2D u_a;\n"
                                         "uniform int u_mode;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = texture(u_a, vec2(0.5)) * float(u_mode); }\n";
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = make_sampler_program(vertex_source, fragment_source)});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_set_uniform_int(nt_hash32_str("u_mode"), 1); /* a plain int still writes */
    NT_TEST_EXPECT_ASSERT(nt_gfx_set_uniform_int(nt_hash32_str("u_a"), 1));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "apply_texture_bindings"));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* Writing the units must leave the bound pipeline's program current. */
static void test_linking_a_program_keeps_the_bound_pipelines_program_current(void) {
    static const char *vertex_source = "void main() {\n"
                                       "    float x = float((gl_VertexID << 1) & 2);\n"
                                       "    float y = float(gl_VertexID & 2);\n"
                                       "    gl_Position = vec4((vec2(x, y) * 2.0) - 1.0, 0.0, 1.0);\n"
                                       "}\n";
    static const char *color_fs = "precision highp float;\n"
                                  "uniform vec4 u_color;\n"
                                  "out vec4 frag_color;\n"
                                  "void main() { frag_color = u_color; }\n";
    static const char *sampler_fs = "precision highp float;\n"
                                    "uniform sampler2D u_a;\n"
                                    "out vec4 frag_color;\n"
                                    "void main() { frag_color = texture(u_a, vec2(0.5)); }\n";
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = make_sampler_program(vertex_source, color_fs)});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);

    uint8_t pixel[4] = {0};
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 1.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    bind_empty_vertex_input();

    nt_program_t linked_under_the_bind = make_sampler_program(vertex_source, sampler_fs);
    TEST_ASSERT_EQUAL_INT(0, nt_gfx_test_program_sampler_unit(linked_under_the_bind, nt_hash32_str("u_a")));

    const float green[4] = {0.0F, 1.0F, 0.0F, 1.0F};
    nt_gfx_set_uniform_vec4(nt_hash32_str("u_color"), green);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 1, 1, pixel, sizeof(pixel)));
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    assert_rgba(pixel, 1, 0, 255, 0, 255);
}
// #endregion

static PFNGLGETPROGRAMIVPROC s_get_program_iv;
static PFNGLGETACTIVEUNIFORMPROC s_get_active_uniform;
static GLenum s_skipped_program_query;
static GLuint s_failed_reflection_program;

static void GLAD_API_PTR skip_program_query(GLuint program, GLenum query, GLint *value) {
    if (query == s_skipped_program_query) {
        s_failed_reflection_program = program;
        return;
    }
    s_get_program_iv(program, query, value);
}

static void GLAD_API_PTR skip_second_uniform_query(GLuint program, GLuint index, GLsizei capacity, GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
    if (s_skipped_program_query == 0 && index == 1) {
        s_failed_reflection_program = program;
        return;
    }
    s_get_active_uniform(program, index, capacity, length, size, type, name);
}

static void assert_reflection_query_failure_retries(GLenum skipped_query) {
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.max_programs = 2;
    nt_gfx_init(&desc);

    uint32_t vs = nt_gfx_backend_create_shader(&(nt_shader_desc_t){
        .type = NT_SHADER_VERTEX,
        .source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n",
    });
    uint32_t fs = nt_gfx_backend_create_shader(&(nt_shader_desc_t){
        .type = NT_SHADER_FRAGMENT,
        .source = "uniform float u_first, u_second;\n"
                  "out vec4 frag_color;\n"
                  "void main() { frag_color = vec4(u_first + u_second); }\n",
    });
    uint32_t existing = nt_gfx_backend_create_program(vs, fs);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, existing);

    s_get_program_iv = glad_glGetProgramiv;
    s_get_active_uniform = glad_glGetActiveUniform;
    s_skipped_program_query = skipped_query;
    s_failed_reflection_program = 0;
    glad_glGetProgramiv = skip_program_query;
    glad_glGetActiveUniform = skip_second_uniform_query;
    nt_test_assert_install();
    nt_test_assert_last_expr[0] = '\0';
    nt_test_assert_armed = true;
    volatile uint32_t failed = UINT32_MAX;
    if (setjmp(nt_test_assert_jmp) == 0) {
        failed = nt_gfx_backend_create_program(vs, fs);
    }
    nt_test_assert_armed = false;
    glad_glGetProgramiv = s_get_program_iv;
    glad_glGetActiveUniform = s_get_active_uniform;
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", nt_test_assert_last_expr, "incomplete reflection must return failure without asserting");
    TEST_ASSERT_EQUAL_UINT32(0, failed);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, s_failed_reflection_program);
    TEST_ASSERT_FALSE(glIsProgram(s_failed_reflection_program));

    uint32_t retry = nt_gfx_backend_create_program(vs, fs);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, retry);
    nt_gfx_backend_destroy_program(retry);
    nt_gfx_backend_destroy_program(existing);
    nt_gfx_backend_destroy_shader(fs);
    nt_gfx_backend_destroy_shader(vs);
}

static void test_missing_active_uniform_count_releases_program_for_retry(void) { assert_reflection_query_failure_retries(GL_ACTIVE_UNIFORMS); }

static void test_missing_uniform_name_length_releases_program_for_retry(void) { assert_reflection_query_failure_retries(GL_ACTIVE_UNIFORM_MAX_LENGTH); }

static void test_missing_uniform_details_releases_partial_cache_for_retry(void) { assert_reflection_query_failure_retries(0); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_render_target_resize_without_spare_texture_slots);
    RUN_TEST(test_depth_texture_uses_explicit_format_and_wrap);
    RUN_TEST(test_custom_blend_state_reaches_gl_unchanged);
    RUN_TEST(test_all_public_blend_enums_reach_gl);
    RUN_TEST(test_multiply_blend_multiplies_rgb_and_preserves_destination_alpha);
    RUN_TEST(test_depth_comparison_sampler_blends_comparison_results);
    RUN_TEST(test_half_float_target_is_complete_and_keeps_values_above_one);
    RUN_TEST(test_depth_buffer_uses_explicit_format);
    RUN_TEST(test_begin_pass_clears_depth_after_depth_writes_were_disabled);
    RUN_TEST(test_global_block_registered_before_link_binds_in_the_program);
    RUN_TEST(test_global_block_registered_after_link_binds_in_that_program);
    RUN_TEST(test_uniform_values_are_shared_by_pipelines_on_one_program);
    RUN_TEST(test_each_pipeline_binds_its_own_program);
    RUN_TEST(test_destroying_one_pipeline_leaves_the_shared_program_alive);
    RUN_TEST(test_uniform_cache_addresses_all_sixteen_array_elements);
    RUN_TEST(test_uniform_cache_preserves_long_names);
    RUN_TEST(test_uniform_cache_expands_only_terminal_struct_array_indices);
    RUN_TEST(test_uniform_cache_asserts_when_array_elements_exceed_capacity);
    RUN_TEST(test_uniform_cache_asserts_when_scalar_uniforms_exceed_capacity);
    RUN_TEST(test_supported_sampler_types_retain_their_classes);
    RUN_TEST(test_signed_sampler_type_asserts_at_link);
    RUN_TEST(test_active_sampler_hash_collision_asserts_at_link);
    RUN_TEST(test_sampler_and_value_uniform_hash_collision_asserts_at_link);
    RUN_TEST(test_value_uniform_hash_collision_asserts_at_link);
    RUN_TEST(test_vertex_stage_and_array_samplers_get_distinct_units);
    RUN_TEST(test_reflection_reports_active_uniforms_with_glsl_declarations);
    RUN_TEST(test_program_with_an_unsupported_sampler_type_asserts);
    RUN_TEST(test_program_with_more_samplers_than_units_asserts);
    RUN_TEST(test_samplers_read_their_link_time_units_without_uniform_writes);
    RUN_TEST(test_two_draws_on_one_program_bind_at_their_queried_units);
    RUN_TEST(test_set_uniform_int_on_a_sampler_asserts);
    RUN_TEST(test_linking_a_program_keeps_the_bound_pipelines_program_current);
    RUN_TEST(test_missing_active_uniform_count_releases_program_for_retry);
    RUN_TEST(test_missing_uniform_name_length_releases_program_for_retry);
    RUN_TEST(test_missing_uniform_details_releases_partial_cache_for_retry);
    return UNITY_END();
}
