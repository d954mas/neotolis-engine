#include "graphics/nt_gfx.h"
#include "unity.h"
#include "window/nt_window.h"

#include <stddef.h>
#include <stdint.h>

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

    nt_gfx_bind_texture(nt_gfx_render_target_depth(target), 0);
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

static nt_pipeline_t make_test_pipeline(const char *vs_src, const char *fs_src, bool depth_write, const nt_vertex_layout_t *layout) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fs_src});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vs.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, fs.id);
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = vs,
        .fragment_shader = fs,
        .layout = *layout,
        .depth_test = depth_write,
        .depth_write = depth_write,
        .depth_func = NT_DEPTH_ALWAYS,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);
    return pip;
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
        .attrs = {{.location = NT_ATTR_POSITION, .format = NT_FORMAT_FLOAT3, .offset = 0}},
    };
    nt_vertex_layout_t fullscreen_layout = {
        .stride = sizeof(fullscreen_vertex_t),
        .attr_count = 2,
        .attrs =
            {
                {.location = NT_ATTR_POSITION, .format = NT_FORMAT_FLOAT2, .offset = 0},
                {.location = NT_ATTR_TEXCOORD0, .format = NT_FORMAT_FLOAT2, .offset = 8},
            },
    };
    nt_buffer_t depth_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = right_half_quad, .size = sizeof(right_half_quad)});
    nt_buffer_t fullscreen_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = fullscreen_tri, .size = sizeof(fullscreen_tri)});
    nt_pipeline_t depth_pip = make_test_pipeline(s_depth_vs, s_depth_fs, true, &depth_layout);
    nt_pipeline_t shadow_pip = make_test_pipeline(s_fullscreen_vs, s_shadow_fs, false, &fullscreen_layout);
    nt_pipeline_t raw_pip = make_test_pipeline(s_fullscreen_vs, s_raw_fs, false, &fullscreen_layout);

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
    nt_gfx_bind_vertex_buffer(depth_vbo);
    nt_gfx_draw(0, 6);
    nt_gfx_end_pass();

    /* The fullscreen triangle spreads v_uv.x across the viewport, so one row
       readback carries the whole shadow ramp. */
    uint8_t row[RAMP_WIDTH * 4] = {0};
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_gfx_set_viewport(0, 0, RAMP_WIDTH, (int)g_nt_window.fb_height);
    nt_gfx_bind_pipeline(shadow_pip);
    nt_gfx_bind_vertex_buffer(fullscreen_vbo);
    nt_gfx_bind_texture(depth_tex, 0);
    nt_gfx_bind_sampler(comparison, 0);
    nt_gfx_set_uniform_int("u_shadow", 0);
    nt_gfx_set_uniform_float("u_ref", 0.5F);
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
    nt_gfx_bind_vertex_buffer(fullscreen_vbo);
    nt_gfx_bind_texture(depth_tex, 0);
    nt_gfx_bind_sampler(raw, 0);
    nt_gfx_set_uniform_int("u_depth", 0);
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

    nt_gfx_bind_texture(nt_gfx_render_target_color(target), 0);
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

static void test_begin_pass_clears_depth_after_depth_writes_were_disabled(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0); }\n";
    static const char *fragment_source = "#ifdef GL_ES\n"
                                         "precision mediump float;\n"
                                         "#endif\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(1.0); }\n";

    nt_shader_t vertex_shader = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fragment_shader = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_pipeline_t no_depth_write_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = vertex_shader,
        .fragment_shader = fragment_shader,
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_render_target_resize_without_spare_texture_slots);
    RUN_TEST(test_depth_texture_uses_explicit_format_and_wrap);
    RUN_TEST(test_depth_comparison_sampler_blends_comparison_results);
    RUN_TEST(test_half_float_target_is_complete_and_keeps_values_above_one);
    RUN_TEST(test_depth_buffer_uses_explicit_format);
    RUN_TEST(test_begin_pass_clears_depth_after_depth_writes_were_disabled);
    return UNITY_END();
}
