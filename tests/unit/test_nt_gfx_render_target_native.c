#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_frame.h"
#include "graphics/nt_gfx_internal.h"
#include "log/nt_log.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_gfx_test_frame.h"
#include "unity.h"
#include "window/nt_window.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include "test_helpers/nt_gfx_test_gl_read.h"

static PFNGLINVALIDATEFRAMEBUFFERPROC s_invalidate;
static uint32_t s_invalidate_count;
static GLenum s_invalidated[3];
static GLsizei s_invalidated_count;
static GLint s_invalidated_fbo;

static void GLAD_API_PTR record_invalidate(GLenum target, GLsizei count, const GLenum *attachments) {
    s_invalidate_count++;
    s_invalidated_count = count;
    memcpy(s_invalidated, attachments, (size_t)count * sizeof(GLenum));
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s_invalidated_fbo);
    if (s_invalidate != NULL) {
        s_invalidate(target, count, attachments);
    }
}

static void assert_rgba(const uint8_t *pixels, uint32_t pixel_count, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (uint32_t i = 0; i < pixel_count; i++) {
        const uint8_t *pixel = &pixels[(size_t)i * 4U];
        TEST_ASSERT_UINT8_WITHIN(1, r, pixel[0]);
        TEST_ASSERT_UINT8_WITHIN(1, g, pixel[1]);
        TEST_ASSERT_UINT8_WITHIN(1, b, pixel[2]);
        TEST_ASSERT_UINT8_WITHIN(1, a, pixel[3]);
    }
}

/* The public readback needs an ended frame, so this closes the pass and the frame, reads, and opens the next frame. */
static void read_after_frame(nt_render_target_t src, int width, int height, uint8_t *out, uint32_t out_cap) {
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(src, 0, 0, width, height, out, out_cap));
    nt_gfx_begin_frame();
}

void setUp(void) {
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.max_textures = 3;
    desc.max_render_targets = 2;
    desc.max_pipelines = 32; /* the blend-enum sweep makes 20 states on one program */
    desc.game_blocks[0] = "Frame";
    desc.frame_capacity[NT_GFX_FRAME_UNIFORM] = 8U * 1024U; /* a Draw block per probe draw */
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
    s_invalidate = glad_glInvalidateFramebuffer;
    s_invalidate_count = 0;
}

void tearDown(void) {
    glad_glInvalidateFramebuffer = s_invalidate;
    nt_gfx_shutdown();
}

/* Local drivers return short single-line strings: truncation and CR/LF mapping stay unexercised here. */
static void test_device_identity_survives_shutdown(void) {
    const nt_gfx_device_info_t *info = nt_gfx_device_info();
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT32(3, g_nt_gfx.last_frame.gl[NT_GFX_GL_glGetString]);
    TEST_ASSERT_EQUAL_STRING((const char *)glGetString(GL_VENDOR), info->vendor);
    TEST_ASSERT_EQUAL_STRING((const char *)glGetString(GL_RENDERER), info->renderer);
    TEST_ASSERT_EQUAL_STRING((const char *)glGetString(GL_VERSION), info->api_version);
    char caps[sizeof(info->caps)];
    (void)snprintf(caps, sizeof(caps), "max_texture=%u ubo_align=%u", (unsigned)g_nt_gfx.gpu_caps.max_texture_size, (unsigned)g_nt_gfx.gpu_caps.uniform_buffer_offset_alignment);
    TEST_ASSERT_NOT_NULL(strstr(info->caps, caps));
    const nt_gfx_device_info_t saved = *info;
    nt_gfx_shutdown();
    TEST_ASSERT_EQUAL_PTR(info, nt_gfx_device_info());
    TEST_ASSERT_EQUAL_MEMORY(&saved, info, sizeof(saved));
}

typedef struct {
    nt_texture_t color;
    nt_texture_t depth;
    nt_render_target_t target;
} test_target_t;

/* The game owns the attachment textures; an INVALID format leaves that attachment out. */
static nt_texture_t make_attachment(nt_texture_format_t format, uint16_t width, uint16_t height) {
    if (format == NT_TEXTURE_FORMAT_INVALID) {
        return (nt_texture_t){0};
    }
    return nt_gfx_make_texture(&(nt_texture_desc_t){.width = width, .height = height, .format = format});
}

static test_target_t make_test_target(uint16_t width, uint16_t height, nt_texture_format_t color_format, nt_texture_format_t depth_format) {
    test_target_t t = {.color = make_attachment(color_format, width, height), .depth = make_attachment(depth_format, width, height)};
    t.target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = t.color, .depth = t.depth});
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(t.target));
    return t;
}

/* Destroying the textures destroys the target with them. */
static void destroy_test_target(const test_target_t *t) {
    if (t->color.id != 0) {
        nt_gfx_destroy_texture(t->color);
    }
    if (t->depth.id != 0) {
        nt_gfx_destroy_texture(t->depth);
    }
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(t->target));
}

/* setUp leaves no spare texture or target slot, so the new size reuses the freed ones. */
static void test_render_target_recreate_at_new_size_without_spare_slots(void) {
    test_target_t target = make_test_target(4, 4, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);
    nt_texture_t filler = make_attachment(NT_TEXTURE_FORMAT_RGBA8, 1, 1);
    nt_render_target_t second = nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = target.color, .depth = target.depth});
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(filler));
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(second));

    uint8_t first_pixels[4U * 4U * 4U] = {0};
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = target.target,
        .clear_color = {0.25F, 0.5F, 0.75F, 1.0F},
        .clear_depth = 1.0F,
    });
    read_after_frame(target.target, 4, 4, first_pixels, sizeof(first_pixels));
    assert_rgba(first_pixels, 16, 64, 128, 191, 255);

    destroy_test_target(&target);
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(second));
    target = make_test_target(7, 5, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);

    uint8_t second_pixels[7U * 5U * 4U] = {0};
    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = target.target,
        .clear_color = {1.0F, 0.25F, 0.5F, 1.0F},
        .clear_depth = 1.0F,
    });
    read_after_frame(target.target, 7, 5, second_pixels, sizeof(second_pixels));
    assert_rgba(second_pixels, 35, 255, 64, 128, 255);

    destroy_test_target(&target);
    nt_gfx_destroy_texture(filler);
}

static void test_depth_texture_uses_explicit_format(void) {
    static const struct {
        nt_texture_format_t format;
        GLint internal_format;
    } cases[] = {
        {NT_TEXTURE_FORMAT_DEPTH16, GL_DEPTH_COMPONENT16},
        {NT_TEXTURE_FORMAT_DEPTH24, GL_DEPTH_COMPONENT24},
        {NT_TEXTURE_FORMAT_DEPTH32F, GL_DEPTH_COMPONENT32F},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        test_target_t target = make_test_target(4, 4, NT_TEXTURE_FORMAT_RGBA8, cases[i].format);

        nt_gfx_backend_bind_texture_unit(nt_gfx_test_texture_backend_id(target.depth), nt_gfx_test_sampler_backend_id(nt_gfx_get_texture_default_sampler(target.depth)), 0);
        GLint value = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &value);
        TEST_ASSERT_EQUAL_INT(cases[i].internal_format, value);

        destroy_test_target(&target);
    }
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
                                 "layout(std140) uniform Draw { vec4 u_ref; };\n"
                                 "in vec2 v_uv;\n"
                                 "out vec4 frag_color;\n"
                                 "void main() {\n"
                                 "    float lit = texture(u_shadow, vec3(v_uv, u_ref.x));\n"
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
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(prog);
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

static void test_all_public_depth_functions_reach_gl(void) {
    static const struct {
        nt_depth_func_t function;
        GLenum expected;
    } cases[] = {
        {NT_DEPTH_LESS, GL_LESS},   {NT_DEPTH_LEQUAL, GL_LEQUAL},     {NT_DEPTH_ALWAYS, GL_ALWAYS}, {NT_DEPTH_GEQUAL, GL_GEQUAL}, {NT_DEPTH_GREATER, GL_GREATER},
        {NT_DEPTH_EQUAL, GL_EQUAL}, {NT_DEPTH_NOTEQUAL, GL_NOTEQUAL}, {NT_DEPTH_NEVER, GL_NEVER},   {NT_DEPTH_LESS, GL_LESS},
    };
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_depth_vs});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_depth_fs});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(program);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 0.0F});
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program, .depth_test = true, .depth_func = cases[i].function});
        TEST_ASSERT_NOT_EQUAL_UINT32(0, pipeline.id);
        nt_gfx_bind_pipeline(pipeline);
        nt_gfx_frame_execute();
        GLint actual = 0;
        glGetIntegerv(GL_DEPTH_FUNC, &actual);
        TEST_ASSERT_EQUAL_INT(cases[i].expected, actual);
        TEST_ASSERT_TRUE(glIsEnabled(GL_DEPTH_TEST));
        TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    }
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_destroy_program(program);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

/* Each sampler comparison reaches the GL sampler object as its literal function. */
static void test_all_sampler_compare_functions_reach_gl(void) {
    static const struct {
        nt_compare_func_t function;
        GLint expected;
    } cases[] = {
        {NT_COMPARE_LEQUAL, GL_LEQUAL}, {NT_COMPARE_LESS, GL_LESS},         {NT_COMPARE_GEQUAL, GL_GEQUAL}, {NT_COMPARE_GREATER, GL_GREATER},
        {NT_COMPARE_EQUAL, GL_EQUAL},   {NT_COMPARE_NOTEQUAL, GL_NOTEQUAL}, {NT_COMPARE_ALWAYS, GL_ALWAYS}, {NT_COMPARE_NEVER, GL_NEVER},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const nt_sampler_t sampler = nt_gfx_make_sampler(&(nt_sampler_desc_t){.min_filter = NT_FILTER_LINEAR, .mag_filter = NT_FILTER_LINEAR, .compare_func = cases[i].function});
        const GLuint name = (GLuint)nt_gfx_test_sampler_backend_id(sampler);
        GLint mode = 0;
        GLint function = 0;
        glGetSamplerParameteriv(name, GL_TEXTURE_COMPARE_MODE, &mode);
        glGetSamplerParameteriv(name, GL_TEXTURE_COMPARE_FUNC, &function);
        TEST_ASSERT_EQUAL_INT(GL_COMPARE_REF_TO_TEXTURE, mode);
        TEST_ASSERT_EQUAL_INT(cases[i].expected, function);
    }
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
}

static void test_gequal_depth_accepts_equal_and_nearer_fragments(void) {
    static const char *vertex_source = "layout(std140) uniform Draw { vec4 u_depth; vec4 u_color; };\n"
                                       "void main() {\n"
                                       "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                                       "    gl_Position = vec4(p * 2.0 - 1.0, u_depth.x, 1.0);\n"
                                       "}\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "layout(std140) uniform Draw { vec4 u_depth; vec4 u_color; };\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = u_color; }\n";
    /* Clip depths map to window depths 0, 0.5, 0.5, 0.25, 0.75. */
    static const struct {
        float clip_depth;
        float color[4];
        uint8_t expected[4];
    } draws[] = {
        {-1.0F, {1, 0, 0, 1}, {255, 0, 0, 255}}, {0.0F, {0, 1, 0, 1}, {0, 255, 0, 255}},     {0.0F, {0, 0, 1, 1}, {0, 0, 255, 255}},
        {-0.5F, {1, 0, 0, 1}, {0, 0, 255, 255}}, {0.5F, {1, 1, 1, 1}, {255, 255, 255, 255}},
    };
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(program);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program, .depth_test = true, .depth_write = true, .depth_func = NT_DEPTH_GEQUAL});
    nt_vertex_input_t input = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    test_target_t target = make_test_target(4, 4, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH32F);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pipeline.id);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {0, 0, 0, 1}, .clear_depth = 0.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(input);
    nt_gfx_apply_texture_bindings(NULL, 0);
    for (size_t i = 0; i < sizeof(draws) / sizeof(draws[0]); i++) {
        float block[2][4] = {{draws[i].clip_depth}};
        memcpy(block[1], draws[i].color, sizeof(block[1]));
        nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, block, sizeof(block));
        nt_gfx_draw(0, 3);
        uint8_t pixels[4U * 4U * 4U] = {0};
        nt_test_gl_read_in_pass(0, 0, 4, 4, pixels);
        assert_rgba(pixels, 16, draws[i].expected[0], draws[i].expected[1], draws[i].expected[2], draws[i].expected[3]);
    }
    float depths[16] = {0};
    glReadPixels(0, 0, 4, 4, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
    for (uint32_t i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_UINT32(750, (uint32_t)(depths[i] * 1000.0F));
    }
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_destroy_vertex_input(input);
    nt_gfx_destroy_program(program);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
    destroy_test_target(&target);
}

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
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(prog);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
        .blend = blend,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pipeline.id);

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0, 0, 0, 0}});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_frame_execute();

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
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

/* Binds a pipeline with this blend in its own frame and reads one GL blend state mid-pass. */
static GLint blend_value_in_pass(nt_program_t prog, nt_blend_state_t blend, GLenum query) {
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
        .blend = blend,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pipeline.id);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0, 0, 0, 0}});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_frame_execute();
    GLint actual = 0;
    glGetIntegerv(query, &actual);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    return actual;
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
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(prog);

    for (size_t i = 0; i < sizeof(factor_cases) / sizeof(factor_cases[0]); i++) {
        nt_blend_state_t blend = nt_blend_alpha();
        blend.src_rgb = factor_cases[i].factor;
        blend.dst_rgb = NT_BLEND_ZERO;
        TEST_ASSERT_EQUAL_INT((GLint)factor_cases[i].expected, blend_value_in_pass(prog, blend, GL_BLEND_SRC_RGB));
    }
    for (size_t i = 0; i < sizeof(op_cases) / sizeof(op_cases[0]); i++) {
        nt_blend_state_t blend = nt_blend_alpha();
        blend.op_rgb = op_cases[i].op;
        TEST_ASSERT_EQUAL_INT((GLint)op_cases[i].expected, blend_value_in_pass(prog, blend, GL_BLEND_EQUATION_RGB));
    }

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
        .attrs = {{.location = NT_LOC_POSITION, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 0}},
    };
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_fullscreen_vs});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(prog);
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
    test_target_t target = make_test_target(4, 4, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_INVALID);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pipeline.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vertices.id);

    uint8_t pixels[4U * 4U * 4U] = {0};
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {0.2F, 0.4F, 0.8F, 0.6F}});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vertex_input);
    nt_gfx_draw(0, 3);
    read_after_frame(target.target, 4, 4, pixels, sizeof(pixels));

    assert_rgba(pixels, 16, 26, 26, 153, 153);
    destroy_test_target(&target);
    nt_gfx_destroy_vertex_input(vertex_input);
    nt_gfx_destroy_buffer(vertices);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

/* Hardware comparison filters the 0/1 results, so the shadow edge lands between
   them. The blend weights are implementation-dependent, hence a bounds check
   rather than an exact midpoint. */
static void test_depth_comparison_sampler_blends_comparison_results(void) {
    test_target_t shadow_map = make_test_target(2, 1, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);
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
        .attrs = {{.location = NT_LOC_POSITION, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0}},
    };
    nt_vertex_layout_t fullscreen_layout = {
        .stride = sizeof(fullscreen_vertex_t),
        .attr_count = 2,
        .attrs =
            {
                {.location = NT_LOC_POSITION, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 0},
                {.location = NT_LOC_UV0, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 8},
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

    nt_texture_t depth_tex = shadow_map.depth;

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = shadow_map.target, .clear_color = {0, 0, 0, 1}, .clear_depth = 0.2F});
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
    TEST_ASSERT_TRUE(nt_gfx_test_texture_set_applied());
    const float ref[4] = {0.5F};
    nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, ref, sizeof(ref));
    nt_gfx_draw(0, 3);
    read_after_frame((nt_render_target_t){0}, RAMP_WIDTH, 1, row, sizeof(row));

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
    TEST_ASSERT_TRUE(nt_gfx_test_texture_set_applied());
    nt_gfx_draw(0, 3);
    read_after_frame((nt_render_target_t){0}, RAMP_WIDTH, 1, row, sizeof(row));

    TEST_ASSERT_UINT8_WITHIN(2, 51, ramp_at(row, RAMP_NEAR));
    TEST_ASSERT_UINT8_WITHIN(2, 204, ramp_at(row, RAMP_FAR));

    destroy_test_target(&shadow_map);
}

typedef struct {
    nt_pipeline_t depth_pip;
    nt_pipeline_t shadow_pip;
    nt_vertex_input_t depth_vi;
    nt_vertex_input_t fullscreen_vi;
    nt_sampler_t comparison;
} shadow_probe_t;

static shadow_probe_t make_shadow_probe(void) {
    static const float right_half_quad[18] = {
        0.0F, -1.0F, 0.6F, 1.0F, -1.0F, 0.6F, 1.0F, 1.0F, 0.6F, 0.0F, -1.0F, 0.6F, 1.0F, 1.0F, 0.6F, 0.0F, 1.0F, 0.6F,
    };
    static const fullscreen_vertex_t fullscreen_tri[3] = {
        {{-1.0F, -1.0F}, {0.0F, 0.0F}},
        {{3.0F, -1.0F}, {2.0F, 0.0F}},
        {{-1.0F, 3.0F}, {0.0F, 2.0F}},
    };
    const nt_vertex_layout_t depth_layout = {
        .stride = sizeof(float) * 3,
        .attr_count = 1,
        .attrs = {{.location = NT_LOC_POSITION, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0}},
    };
    const nt_vertex_layout_t fullscreen_layout = {
        .stride = sizeof(fullscreen_vertex_t),
        .attr_count = 2,
        .attrs =
            {
                {.location = NT_LOC_POSITION, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 0},
                {.location = NT_LOC_UV0, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 8},
            },
    };
    nt_buffer_t depth_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = right_half_quad, .size = sizeof(right_half_quad)});
    nt_buffer_t fullscreen_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = fullscreen_tri, .size = sizeof(fullscreen_tri)});
    return (shadow_probe_t){
        .depth_pip = make_test_pipeline(s_depth_vs, s_depth_fs, true),
        .shadow_pip = make_test_pipeline(s_fullscreen_vs, s_shadow_fs, false),
        .depth_vi = make_test_vertex_input(&depth_layout, depth_vbo),
        .fullscreen_vi = make_test_vertex_input(&fullscreen_layout, fullscreen_vbo),
        .comparison = nt_gfx_make_sampler(&(nt_sampler_desc_t){.min_filter = NT_FILTER_LINEAR, .mag_filter = NT_FILTER_LINEAR, .compare_func = NT_COMPARE_LEQUAL}),
    };
}

/* Pass 1 writes depth 0.8 into the right half of the map over a 0.2 clear;
   pass 2 compares against 0.5 across the drawable and reads back one row. */
static void render_shadow_ramp(const shadow_probe_t *probe, const test_target_t *shadow_map, uint8_t row[RAMP_WIDTH * 4]) {
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = shadow_map->target, .clear_depth = 0.2F});
    nt_gfx_frame_execute();
    GLint draw_buffer = -1;
    GLint read_buffer = -1;
    glGetIntegerv(GL_DRAW_BUFFER0, &draw_buffer);
    glGetIntegerv(GL_READ_BUFFER, &read_buffer);
    TEST_ASSERT_EQUAL_INT(GL_NONE, draw_buffer);
    TEST_ASSERT_EQUAL_INT(GL_NONE, read_buffer);
    nt_gfx_bind_pipeline(probe->depth_pip);
    nt_gfx_bind_vertex_input(probe->depth_vi);
    nt_gfx_draw(0, 6);
    nt_gfx_end_pass();

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_gfx_set_viewport(0, 0, RAMP_WIDTH, (int)g_nt_window.fb_height);
    nt_gfx_bind_pipeline(probe->shadow_pip);
    nt_gfx_bind_vertex_input(probe->fullscreen_vi);
    const nt_gfx_texture_binding_t binding = {.name = nt_hash32_str("u_shadow"), .texture = shadow_map->depth, .sampler = probe->comparison};
    nt_gfx_apply_texture_bindings(&binding, 1);
    TEST_ASSERT_TRUE(nt_gfx_test_texture_set_applied());
    const float ref[4] = {0.5F};
    nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, ref, sizeof(ref));
    nt_gfx_draw(0, 3);
    read_after_frame((nt_render_target_t){0}, RAMP_WIDTH, 1, row, RAMP_WIDTH * 4);
}

/* A shadow map needs no color: GL 3.3 core only completes the FBO with draw
   and read buffer NONE. */
static void test_depth_only_shadow_map_renders_and_recreates_at_new_size(void) {
    test_target_t shadow_map = make_test_target(2, 1, NT_TEXTURE_FORMAT_INVALID, NT_TEXTURE_FORMAT_DEPTH24);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_render_target_color(shadow_map.target).id);
    TEST_ASSERT_TRUE_MESSAGE(g_nt_window.fb_width >= RAMP_WIDTH, "drawable narrower than the sampled ramp");
    const shadow_probe_t probe = make_shadow_probe();

    uint8_t row[RAMP_WIDTH * 4] = {0};
    render_shadow_ramp(&probe, &shadow_map, row);
    TEST_ASSERT_EQUAL_UINT8(0, ramp_at(row, RAMP_NEAR));
    TEST_ASSERT_EQUAL_UINT8(255, ramp_at(row, RAMP_FAR));
    TEST_ASSERT_TRUE(ramp_at(row, RAMP_EDGE) > 0 && ramp_at(row, RAMP_EDGE) < 255);

    destroy_test_target(&shadow_map);
    shadow_map = make_test_target(4, 1, NT_TEXTURE_FORMAT_INVALID, NT_TEXTURE_FORMAT_DEPTH24);
    nt_gfx_backend_bind_texture_unit(nt_gfx_test_texture_backend_id(shadow_map.depth), nt_gfx_test_sampler_backend_id(nt_gfx_get_texture_default_sampler(shadow_map.depth)), 0);
    GLint gl_width = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &gl_width);
    TEST_ASSERT_EQUAL_INT(4, gl_width);

    memset(row, 0, sizeof(row));
    render_shadow_ramp(&probe, &shadow_map, row);
    TEST_ASSERT_EQUAL_UINT8(0, ramp_at(row, RAMP_NEAR));
    TEST_ASSERT_EQUAL_UINT8(255, ramp_at(row, RAMP_FAR));
    TEST_ASSERT_TRUE(ramp_at(row, RAMP_EDGE) > 0 && ramp_at(row, RAMP_EDGE) < 255);

    destroy_test_target(&shadow_map);
}

/* Raw depth through the texture's own NEAREST default sampler, one row across the ramp. */
static void read_raw_depth_row(nt_pipeline_t raw_pip, const shadow_probe_t *probe, nt_texture_t depth, uint8_t row[RAMP_WIDTH * 4]) {
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_gfx_set_viewport(0, 0, RAMP_WIDTH, (int)g_nt_window.fb_height);
    nt_gfx_bind_pipeline(raw_pip);
    nt_gfx_bind_vertex_input(probe->fullscreen_vi);
    const nt_gfx_texture_binding_t binding = {.name = nt_hash32_str("u_depth"), .texture = depth, .sampler = NT_SAMPLER_DEFAULT};
    nt_gfx_apply_texture_bindings(&binding, 1);
    TEST_ASSERT_TRUE(nt_gfx_test_texture_set_applied());
    nt_gfx_draw(0, 3);
    read_after_frame((nt_render_target_t){0}, RAMP_WIDTH, 1, row, RAMP_WIDTH * 4);
}

/* Every pass clears its depth, so a depth test cannot carry across targets;
   sharing shows as both targets writing one texture. */
static void test_two_targets_share_one_depth_texture(void) {
    test_target_t writer = make_test_target(2, 1, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);
    nt_texture_t reader_color = make_attachment(NT_TEXTURE_FORMAT_RGBA8, 2, 1);
    nt_render_target_t reader = nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = reader_color, .depth = writer.depth});
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(reader));
    TEST_ASSERT_TRUE_MESSAGE(g_nt_window.fb_width >= RAMP_WIDTH, "drawable narrower than the sampled ramp");
    const shadow_probe_t probe = make_shadow_probe();
    const nt_pipeline_t raw_pip = make_test_pipeline(s_fullscreen_vs, s_raw_fs, false);
    uint8_t row[RAMP_WIDTH * 4] = {0};

    /* Writer: clear 0.2, the right half drawn at 0.8. */
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = writer.target, .clear_depth = 0.2F});
    nt_gfx_bind_pipeline(probe.depth_pip);
    nt_gfx_bind_vertex_input(probe.depth_vi);
    nt_gfx_draw(0, 6);
    nt_gfx_end_pass();
    read_raw_depth_row(raw_pip, &probe, writer.depth, row);
    TEST_ASSERT_UINT8_WITHIN(2, 51, ramp_at(row, RAMP_NEAR));
    TEST_ASSERT_UINT8_WITHIN(2, 204, ramp_at(row, RAMP_FAR));

    /* Reader: its clear lands in the same texture. */
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = reader, .clear_depth = 0.6F});
    nt_gfx_end_pass();
    read_raw_depth_row(raw_pip, &probe, writer.depth, row);
    TEST_ASSERT_UINT8_WITHIN(2, 153, ramp_at(row, RAMP_NEAR));
    TEST_ASSERT_UINT8_WITHIN(2, 153, ramp_at(row, RAMP_FAR));

    destroy_test_target(&writer);
    TEST_ASSERT_FALSE(nt_gfx_render_target_valid(reader));
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(reader_color));
    nt_gfx_destroy_texture(reader_color);
}

/* An over-range clear must survive the round trip — clamping to white would
   mean the target has no headroom and only the format name changed. */
static void test_half_float_target_is_complete_and_keeps_values_above_one(void) {
    TEST_ASSERT_TRUE(g_nt_gfx.gpu_caps.has_float_render_target);

    test_target_t target = make_test_target(4, 4, NT_TEXTURE_FORMAT_RGBA16F, NT_TEXTURE_FORMAT_INVALID);

    nt_gfx_backend_bind_texture_unit(nt_gfx_test_texture_backend_id(target.color), nt_gfx_test_sampler_backend_id(nt_gfx_get_texture_default_sampler(target.color)), 0);
    GLint internal_format = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal_format);
    TEST_ASSERT_EQUAL_INT(GL_RGBA16F, internal_format);

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {3.5F, 0.25F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_frame_execute();
    float pixels[4 * 4 * 4] = {0};
    glReadPixels(0, 0, 4, 4, GL_RGBA, GL_FLOAT, pixels);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    /* Unity float asserts are disabled in this build; compare in millis. */
    TEST_ASSERT_INT_WITHIN(10, 3500, (int)(pixels[0] * 1000.0F));
    TEST_ASSERT_INT_WITHIN(10, 250, (int)(pixels[1] * 1000.0F));

    destroy_test_target(&target);
}

static void test_rgba32f_linear_filtering_and_generated_mips(void) {
    TEST_ASSERT_TRUE(nt_gfx_gpu_caps()->has_float_texture_linear);
    static const float texels[16] = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1};
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 2,
        .height = 2,
        .format = NT_TEXTURE_FORMAT_RGBA32F,
        .data = texels,
        .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .gen_mipmaps = true,
    });
    TEST_ASSERT_TRUE(nt_gfx_texture_ready(texture));
    nt_pipeline_t pipeline = make_test_pipeline("void main() {\n"
                                                "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                                                "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                                                "}\n",
                                                "precision highp float;\n"
                                                "uniform sampler2D u_tex;\n"
                                                "out vec4 frag_color;\n"
                                                "void main() {\n"
                                                "    frag_color = (textureLod(u_tex, vec2(0.5), 0.0) + textureLod(u_tex, vec2(0.5), 1.0)) * 0.5;\n"
                                                "}\n",
                                                false);
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
    const nt_gfx_texture_binding_t binding = {.name = nt_hash32_str("u_tex"), .texture = texture, .sampler = NT_SAMPLER_DEFAULT};
    nt_gfx_apply_texture_bindings(&binding, 1);
    nt_gfx_draw(0, 3);
    uint8_t pixel[4] = {0};
    read_after_frame((nt_render_target_t){0}, 1, 1, pixel, sizeof(pixel));
    /* Both the bilinear base lookup and the reduced mip must average the four colors. */
    assert_rgba(pixel, 1, 128, 128, 128, 255);
    nt_gfx_destroy_texture(texture);
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
    nt_program_t prog = nt_gfx_make_program(vertex_shader, fragment_shader, NULL);
    nt_test_gfx_link_wait(prog);
    nt_pipeline_t no_depth_write_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = prog,
        .depth_write = false,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, no_depth_write_pipeline.id);

    test_target_t target = make_test_target(4, 4, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_depth = 0.25F});
    nt_gfx_bind_pipeline(no_depth_write_pipeline);
    nt_gfx_end_pass();

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_depth = 0.75F});
    nt_gfx_frame_execute();
    float depth = 0.0F;
    glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    uint32_t depth_milli = (uint32_t)((depth * 1000.0F) + 0.5F);
    TEST_ASSERT_UINT32_WITHIN(1, 750, depth_milli);
    nt_gfx_bind_pipeline(no_depth_write_pipeline);
    nt_gfx_frame_execute();
    GLboolean depth_write_enabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write_enabled);
    TEST_ASSERT_EQUAL_INT(GL_FALSE, depth_write_enabled);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    destroy_test_target(&target);
    nt_gfx_destroy_shader(fragment_shader);
    nt_gfx_destroy_shader(vertex_shader);
}

/* Real GL must record the declared block binding when the program links. */
static void test_global_block_from_the_desc_binds_in_the_program(void) {
    static const char *vertex_source = "layout(std140) uniform Frame { vec4 g_offset; };\n"
                                       "void main() { gl_Position = vec4(g_offset.xy, 0.0, 1.0); }\n";
    static const char *fragment_source = "#ifdef GL_ES\n"
                                         "precision mediump float;\n"
                                         "#endif\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(1.0); }\n";

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(prog);

    /* Bind a pipeline so the program becomes current, then read the binding
     * GL actually recorded for the block. */
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_frame_execute();

    GLint current_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    TEST_ASSERT_NOT_EQUAL_INT(0, current_program);
    GLuint block_index = glGetUniformBlockIndex((GLuint)current_program, "Frame");
    TEST_ASSERT_NOT_EQUAL_UINT32(GL_INVALID_INDEX, block_index);
    GLint binding = -1;
    glGetActiveUniformBlockiv((GLuint)current_program, block_index, GL_UNIFORM_BLOCK_BINDING, &binding);
    TEST_ASSERT_EQUAL_INT(NT_GFX_SLOT_GAME, binding);

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_program(prog);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

/* A bound block belongs to the pass, not the pipeline: binding another pipeline
 * does not reset it. */
static void test_block_bound_under_one_pipeline_feeds_the_next(void) {
    /* Attribute-less full-screen triangle: the pipelines carry no layout. */
    static const char *vertex_source = "void main() {\n"
                                       "    float x = float((gl_VertexID << 1) & 2);\n"
                                       "    float y = float(gl_VertexID & 2);\n"
                                       "    gl_Position = vec4((vec2(x, y) * 2.0) - 1.0, 0.0, 1.0);\n"
                                       "}\n";
    static const char *fragment_source = "#ifdef GL_ES\n"
                                         "precision mediump float;\n"
                                         "#endif\n"
                                         "layout(std140) uniform Draw { vec4 u_values[2]; ivec4 u_ints[2]; uvec4 u_uints[2]; mat4 u_matrices[2]; };\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = (u_values[0] + u_values[1] + vec4(u_ints[0] + u_ints[1])\n"
                                         "    + vec4(u_uints[0] + u_uints[1]) + u_matrices[0][1] + u_matrices[1][2]) / 16.0; }\n";

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(prog);

    /* Two pipelines differing only in fixed-function state. */
    nt_pipeline_t pip_a = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog, .depth_test = false});
    nt_pipeline_t pip_b = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog, .depth_test = true, .depth_func = NT_DEPTH_ALWAYS});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip_a.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip_b.id);

    test_target_t target = make_test_target(4, 4, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    /* Block bound while A is bound; the draw happens through B without binding it again. */
    nt_gfx_bind_pipeline(pip_a);
    /* Different array elements and asymmetric matrices expose stride and transpose errors. */
    const struct {
        float values[2][4];
        int32_t ints[2][4];
        uint32_t uints[2][4];
        float matrices[2][16];
    } block = {
        .values = {{0.5F, 1.0F, 1.5F, 2.0F}, {1.5F, 2.0F, 2.5F, 3.0F}},
        .ints = {{-1, 2, -3, 4}, {2, -1, 4, -3}},
        .uints = {{1, 2, 3, 4}, {2, 3, 4, 5}},
        .matrices = {{[4] = -1.0F, [5] = -2.0F, [6] = -3.0F, [7] = -4.0F}, {[8] = -1.0F, [9] = 1.0F, [10] = 3.0F, [11] = 5.0F}},
    };
    nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, &block, sizeof(block));
    nt_gfx_bind_pipeline(pip_b);
    bind_empty_vertex_input();
    nt_gfx_draw(0, 3);

    uint8_t pixels[4 * 4 * 4] = {0};
    read_after_frame(target.target, 4, 4, pixels, sizeof(pixels));
    assert_rgba(pixels, 16, 64, 128, 191, 255);

    destroy_test_target(&target);
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
    nt_program_t red = nt_gfx_make_program(vs, red_fs, NULL);
    nt_test_gfx_link_wait(red);
    nt_program_t blue = nt_gfx_make_program(vs, blue_fs, NULL);
    nt_test_gfx_link_wait(blue);
    nt_pipeline_t pip_red = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = red});
    nt_pipeline_t pip_blue = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = blue});

    test_target_t target = make_test_target(4, 4, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);

    uint8_t pixels[4 * 4 * 4] = {0};

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip_red);
    bind_empty_vertex_input();
    nt_gfx_draw(0, 3);
    read_after_frame(target.target, 4, 4, pixels, sizeof(pixels));
    assert_rgba(pixels, 16, 255, 0, 0, 255);

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip_blue);
    bind_empty_vertex_input();
    nt_gfx_draw(0, 3);
    read_after_frame(target.target, 4, 4, pixels, sizeof(pixels));
    assert_rgba(pixels, 16, 0, 0, 255, 255);

    destroy_test_target(&target);
    nt_gfx_destroy_program(blue);
    nt_gfx_destroy_program(red);
    nt_gfx_destroy_shader(blue_fs);
    nt_gfx_destroy_shader(red_fs);
    nt_gfx_destroy_shader(vs);
}

/* Destroying one program takes its pipelines only: another program's pipeline keeps drawing. */
static void test_destroying_one_program_leaves_another_programs_pipeline_drawing(void) {
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
    nt_program_t doomed = nt_gfx_make_program(vs, fs, NULL);
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(prog);
    nt_pipeline_t pip_a = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = doomed, .depth_test = true});
    nt_pipeline_t pip_b = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog, .depth_test = true});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip_a.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip_b.id);

    test_target_t target = make_test_target(4, 4, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);

    nt_gfx_destroy_program(doomed);
    TEST_ASSERT_FALSE(nt_gfx_pipeline_valid(pip_a));
    TEST_ASSERT_TRUE(nt_gfx_pipeline_valid(pip_b));

    uint8_t pixels[4 * 4 * 4] = {0};
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip_b);
    bind_empty_vertex_input();
    nt_gfx_draw(0, 3);
    read_after_frame(target.target, 4, 4, pixels, sizeof(pixels));
    assert_rgba(pixels, 16, 0, 255, 0, 255);

    destroy_test_target(&target);
    nt_gfx_destroy_program(prog);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
}

static GLuint bind_uniform_test_program(const char *fragment_source) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(program);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_frame_execute();
    GLint current_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    TEST_ASSERT_NOT_EQUAL_INT(0, current_program);
    return (GLuint)current_program;
}

/* A standalone value uniform could never receive data; built-ins are not program inputs. */
static void test_standalone_value_uniform_asserts_at_link(void) {
    static const char *depth_range_source = "out vec4 frag_color;\n"
                                            "void main() { frag_color = vec4(gl_DepthRange.far); }\n";
    (void)bind_uniform_test_program(depth_range_source);
    nt_gfx_end_pass();
    static const char *fragment_source = "uniform float u_value;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = vec4(u_value); }\n";
    NT_TEST_EXPECT_ASSERT((void)bind_uniform_test_program(fragment_source));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "standalone non-sampler uniform"));
}

static void expect_link_assert(const char *fragment_source, const char *message) {
    NT_TEST_EXPECT_ASSERT((void)bind_uniform_test_program(fragment_source));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, message));
}

/* An unknown block would sit on binding 0 and read Globals. */
static void test_unknown_block_name_asserts_at_link(void) {
    expect_link_assert("layout(std140) uniform Lighting { vec4 u_light; };\n"
                       "out vec4 frag_color;\n"
                       "void main() { frag_color = u_light; }\n",
                       "neither reserved nor in");
}

/* A float member packs differently from a C struct of 16-byte members. */
static void test_non_vec4_block_member_asserts_at_link(void) {
    expect_link_assert("layout(std140) uniform Draw { float u_value; };\n"
                       "out vec4 frag_color;\n"
                       "void main() { frag_color = vec4(u_value); }\n",
                       "not vec4");
}

/* A row-major matrix would read a column-major C mat4 transposed. */
static void test_row_major_block_matrix_asserts_at_link(void) {
    expect_link_assert("layout(std140, row_major) uniform Draw { mat4 u_matrix; };\n"
                       "out vec4 frag_color;\n"
                       "void main() { frag_color = u_matrix[1]; }\n",
                       "row_major");
}

/* A truncated block name could match a listed prefix or report the wrong cause. */
static void test_long_block_name_asserts_at_link(void) {
    expect_link_assert("layout(std140) uniform "
                       "Bxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                       "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx { vec4 u_value; };\n"
                       "out vec4 frag_color;\n"
                       "void main() { frag_color = u_value; }\n",
                       "block name too long");
}

// #region program-owned sampler units
static nt_program_t make_sampler_program(const char *vertex_source, const char *fragment_source) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(prog);
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
                                         "layout(std140) uniform Draw { vec4 u_scale; };\n"
                                         "out vec4 frag_color;\n"
                                         "void main() {\n"
                                         "    vec4 c = texture(u_color, vec2(0.5));\n"
                                         "    uvec4 i = texture(u_ids, vec2(0.5));\n"
                                         "    float s = texture(u_shadow, vec3(0.5));\n"
                                         "    frag_color = (c + vec4(i) + vec4(s)) * u_scale.x;\n"
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
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    NT_TEST_EXPECT_ASSERT(nt_test_gfx_link_wait(prog));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "signed integer sampler"));
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
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    NT_TEST_EXPECT_ASSERT(nt_test_gfx_link_wait(prog));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "unsupported sampler type"));
}

/* Desktop GL accepts sampler types GLES never has; they are rejected the same way. */
static void test_program_with_a_desktop_only_sampler_type_asserts(void) {
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fragment_source = "precision highp float;\n"
                                         "uniform sampler2DRect u_rect;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() { frag_color = texture(u_rect, vec2(0.0)); }\n";
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    NT_TEST_EXPECT_ASSERT(nt_test_gfx_link_wait(prog));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "unsupported sampler type"));
}

static void test_cube_array_sampler_types_assert_at_link(void) {
    if (GLAD_GL_ARB_texture_cube_map_array == 0) {
        TEST_IGNORE_MESSAGE("GL_ARB_texture_cube_map_array is unavailable");
    }
    static const char *vertex_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fragment_sources[] = {
        "#extension GL_ARB_texture_cube_map_array : require\n"
        "uniform samplerCubeArray u_env;\n"
        "out vec4 frag_color;\n"
        "void main() { frag_color = texture(u_env, vec4(0.0, 0.0, 1.0, 0.0)); }\n",
        "#extension GL_ARB_texture_cube_map_array : require\n"
        "uniform samplerCubeArrayShadow u_env;\n"
        "out vec4 frag_color;\n"
        "void main() { frag_color = vec4(texture(u_env, vec4(0.0, 0.0, 1.0, 0.0), 0.5)); }\n",
        "#extension GL_ARB_texture_cube_map_array : require\n"
        "uniform isamplerCubeArray u_env;\n"
        "out vec4 frag_color;\n"
        "void main() { frag_color = vec4(texture(u_env, vec4(0.0, 0.0, 1.0, 0.0))); }\n",
        "#extension GL_ARB_texture_cube_map_array : require\n"
        "uniform usamplerCubeArray u_env;\n"
        "out vec4 frag_color;\n"
        "void main() { frag_color = vec4(texture(u_env, vec4(0.0, 0.0, 1.0, 0.0))); }\n",
    };
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source});
    for (size_t i = 0; i < sizeof(fragment_sources) / sizeof(fragment_sources[0]); i++) {
        nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_sources[i]});
        nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
        NT_TEST_EXPECT_ASSERT(nt_test_gfx_link_wait(prog));
        TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "unsupported sampler type"));
        nt_gfx_destroy_program(prog); /* else the next begin_frame finishes it again */
        nt_gfx_destroy_shader(fs);
    }
    nt_gfx_destroy_shader(vs);
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
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    NT_TEST_EXPECT_ASSERT(nt_test_gfx_link_wait(prog));
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

/* Each sampler reads the unit the link wrote. */
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
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 1.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    bind_empty_vertex_input();
    const nt_gfx_texture_binding_t bindings[] = {
        {.name = nt_hash32_str("u_a"), .texture = tex_red, .sampler = NT_SAMPLER_DEFAULT},
        {.name = nt_hash32_str("u_b"), .texture = tex_green, .sampler = NT_SAMPLER_DEFAULT},
    };
    nt_gfx_apply_texture_bindings(bindings, 2);
    TEST_ASSERT_TRUE(nt_gfx_test_texture_set_applied());
    nt_gfx_draw(0, 3);
    read_after_frame((nt_render_target_t){0}, 1, 1, pixel, sizeof(pixel));

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
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 1.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    bind_empty_vertex_input();

    /* First draw: red through u_a, green through u_b. */
    nt_gfx_texture_binding_t bindings[] = {
        {.name = nt_hash32_str("u_a"), .texture = tex_red, .sampler = NT_SAMPLER_DEFAULT},
        {.name = nt_hash32_str("u_b"), .texture = tex_green, .sampler = NT_SAMPLER_DEFAULT},
    };
    nt_gfx_apply_texture_bindings(bindings, 2);
    TEST_ASSERT_TRUE(nt_gfx_test_texture_set_applied());
    nt_gfx_draw(0, 3);
    nt_test_gl_read_in_pass(0, 0, 1, 1, first);

    /* Second draw puts red on u_b's unit only: u_a must keep reading red (red has no green). */
    bindings[1].texture = tex_red;
    nt_gfx_apply_texture_bindings(bindings, 2);
    TEST_ASSERT_TRUE(nt_gfx_test_texture_set_applied());
    nt_gfx_draw(0, 3);
    read_after_frame((nt_render_target_t){0}, 1, 1, second, sizeof(second));

    assert_rgba(first, 1, 255, 255, 0, 255);
    assert_rgba(second, 1, 255, 0, 0, 255);
}

/* Writing the units must leave the bound pipeline's program current. */
static void test_linking_a_program_keeps_the_bound_pipelines_program_current(void) {
    static const char *vertex_source = "void main() {\n"
                                       "    float x = float((gl_VertexID << 1) & 2);\n"
                                       "    float y = float(gl_VertexID & 2);\n"
                                       "    gl_Position = vec4((vec2(x, y) * 2.0) - 1.0, 0.0, 1.0);\n"
                                       "}\n";
    static const char *color_fs = "precision highp float;\n"
                                  "layout(std140) uniform Draw { vec4 u_color; };\n"
                                  "out vec4 frag_color;\n"
                                  "void main() { frag_color = u_color; }\n";
    static const char *sampler_fs = "precision highp float;\n"
                                    "uniform sampler2D u_a;\n"
                                    "out vec4 frag_color;\n"
                                    "void main() { frag_color = texture(u_a, vec2(0.5)); }\n";
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = make_sampler_program(vertex_source, color_fs)});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);

    uint8_t pixel[4] = {0};
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 1.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    bind_empty_vertex_input();
    nt_gfx_frame_execute(); /* the link must finish while the pipeline is bound in GL */
    nt_gfx_end_pass();
    GLint bound_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &bound_program);
    TEST_ASSERT_NOT_EQUAL_INT(0, bound_program);

    nt_program_t linked_under_the_bind = nt_gfx_make_program(nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vertex_source}),
                                                             nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = sampler_fs}), NULL);
    nt_test_gfx_link_wait(linked_under_the_bind);
    TEST_ASSERT_EQUAL_INT(0, nt_gfx_test_program_sampler_unit(linked_under_the_bind, nt_hash32_str("u_a")));
    GLint current_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    TEST_ASSERT_EQUAL_INT(bound_program, current_program);

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 1.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    bind_empty_vertex_input();
    const float green[4] = {0.0F, 1.0F, 0.0F, 1.0F};
    nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, green, sizeof(green));
    nt_gfx_draw(0, 3);
    read_after_frame((nt_render_target_t){0}, 1, 1, pixel, sizeof(pixel));

    assert_rgba(pixel, 1, 0, 255, 0, 255);
}
// #endregion

static PFNGLGETPROGRAMIVPROC s_get_program_iv;
static PFNGLGETACTIVEUNIFORMPROC s_get_active_uniform;
static GLenum s_skipped_program_query;
static GLuint s_failed_reflection_program;
static char s_program_failure_log[512];

static void capture_program_failure(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)domain;
    (void)user;
    if (level == NT_LOG_LEVEL_ERROR) {
        (void)snprintf(s_program_failure_log, sizeof(s_program_failure_log), "%s", msg);
    }
}

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

/* Finishes a link the driver may still run in parallel. */
static nt_gfx_result_t finish_backend_program(uint32_t program) {
    const struct timespec start = nt_test_link_wait_start();
    nt_gfx_result_t result = nt_gfx_backend_finish_program(program, g_nt_gfx.gpu_caps.has_parallel_shader_compile);
    while (result == NT_GFX_RESULT_UNREADY) {
        nt_test_link_wait_check(&start);
        result = nt_gfx_backend_finish_program(program, g_nt_gfx.gpu_caps.has_parallel_shader_compile);
    }
    return result;
}

/* Reflection of a linked program fails only on a lost context: on a live one it asserts. */
static void assert_reflection_query_failure_asserts(GLenum skipped_query) {
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.max_programs = 2;
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();

    uint32_t vs = nt_gfx_backend_create_shader(&(nt_shader_desc_t){
        .type = NT_SHADER_VERTEX,
        .source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n",
    });
    uint32_t fs = nt_gfx_backend_create_shader(&(nt_shader_desc_t){
        .type = NT_SHADER_FRAGMENT,
        .source = "uniform sampler2D u_first, u_second;\n"
                  "out vec4 frag_color;\n"
                  "void main() { frag_color = texture(u_first, vec2(0.5)) + texture(u_second, vec2(0.5)); }\n",
    });
    /* Backend-only programs: the fresh init made no frontend program, so both slots are free. */
    const uint32_t existing = 1;
    const uint32_t failing = 2;
    TEST_ASSERT_TRUE(nt_gfx_backend_create_program(vs, fs, existing, "reflection/existing"));
    TEST_ASSERT_EQUAL(NT_GFX_RESULT_ACCEPTED, finish_backend_program(existing));
    TEST_ASSERT_TRUE(nt_gfx_backend_create_program(vs, fs, failing, ""));

    s_get_program_iv = glad_glGetProgramiv;
    s_get_active_uniform = glad_glGetActiveUniform;
    s_skipped_program_query = skipped_query;
    s_failed_reflection_program = 0;
    glad_glGetProgramiv = skip_program_query;
    glad_glGetActiveUniform = skip_second_uniform_query;
    s_program_failure_log[0] = '\0';
    nt_log_add_sink(capture_program_failure, NULL);
    NT_TEST_EXPECT_ASSERT((void)finish_backend_program(failing));
    nt_log_remove_sink(capture_program_failure, NULL);
    glad_glGetProgramiv = s_get_program_iv;
    glad_glGetActiveUniform = s_get_active_uniform;
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "program uniform reflection failed"));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, s_failed_reflection_program);

    nt_gfx_backend_destroy_program(failing);
    nt_gfx_backend_destroy_program(existing);
    nt_gfx_backend_destroy_shader(fs);
    nt_gfx_backend_destroy_shader(vs);
    if (NT_LOG_MIN_LEVEL <= NT_LOG_LEVEL_ERROR) {
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, " uniforms: "));
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, "\"<unnamed>\""));
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, "no data: gl_error=0x"));
        TEST_ASSERT_NULL(strstr(s_program_failure_log, "reflection/existing"));
    }
}

static void test_missing_active_uniform_count_asserts(void) { assert_reflection_query_failure_asserts(GL_ACTIVE_UNIFORMS); }

static void test_missing_uniform_name_length_asserts(void) { assert_reflection_query_failure_asserts(GL_ACTIVE_UNIFORM_MAX_LENGTH); }

static void test_missing_uniform_details_assert(void) { assert_reflection_query_failure_asserts(0); }

static PFNGLGETACTIVEUNIFORMSIVPROC s_get_active_uniforms_iv;

static void GLAD_API_PTR skip_matrix_order(GLuint program, GLsizei count, const GLuint *indices, GLenum query, GLint *values) {
    if (query != GL_UNIFORM_IS_ROW_MAJOR) {
        s_get_active_uniforms_iv(program, count, indices, query, values);
    }
}

static void test_missing_matrix_order_asserts(void) {
    s_get_active_uniforms_iv = glad_glGetActiveUniformsiv;
    glad_glGetActiveUniformsiv = skip_matrix_order;
    NT_TEST_EXPECT_ASSERT((void)bind_uniform_test_program("layout(std140) uniform Draw { mat4 u_matrix; };\n"
                                                          "out vec4 frag_color;\n"
                                                          "void main() { frag_color = u_matrix[1]; }\n"));
    glad_glGetActiveUniformsiv = s_get_active_uniforms_iv;
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "program uniform reflection failed"));
}

static void GLAD_API_PTR gen_no_names(GLsizei n, GLuint *names) {
    for (GLsizei i = 0; i < n; i++) {
        names[i] = 0;
    }
}

static GLenum GLAD_API_PTR invalid_operation(void) { return GL_INVALID_OPERATION; }

static GLuint GLAD_API_PTR create_no_program(void) { return 0; }

static void begin_device_failure_capture(void) {
    s_program_failure_log[0] = '\0';
    nt_log_add_sink(capture_program_failure, NULL);
}

static void expect_name_failure_record(const char *operation, const char *label) {
    nt_log_remove_sink(capture_program_failure, NULL);
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "could not name a GL object on a live context"));
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    if (NT_LOG_MIN_LEVEL <= NT_LOG_LEVEL_ERROR) {
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, operation));
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, "name:"));
        const char *error = strstr(s_program_failure_log, "gl_error=0x0502");
        const char *name = strstr(s_program_failure_log, label);
        TEST_ASSERT_NOT_NULL(error);
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_TRUE(error < name);
    } else {
        TEST_ASSERT_EQUAL_STRING("", s_program_failure_log);
    }
}

/* Makes one GL naming call fail around expr, with a pending GL error, then checks the record. */
#define EXPECT_NAME_FAILURE(type, ptr, stub, expr, operation, label)                                                                                                                                   \
    do {                                                                                                                                                                                               \
        type saved_call = ptr;                                                                                                                                                                         \
        PFNGLGETERRORPROC saved_error = glad_glGetError;                                                                                                                                               \
        ptr = stub;                                                                                                                                                                                    \
        glad_glGetError = invalid_operation;                                                                                                                                                           \
        begin_device_failure_capture();                                                                                                                                                                \
        NT_TEST_EXPECT_ASSERT((void)(expr));                                                                                                                                                           \
        ptr = saved_call;                                                                                                                                                                              \
        glad_glGetError = saved_error;                                                                                                                                                                 \
        expect_name_failure_record(operation, label);                                                                                                                                                  \
    } while (0)

/* A null name on a live context records the cause before asserting; no loss latches. */
static void test_a_name_failure_on_a_live_context_asserts_without_latching(void) {
    static char long_label[800];
    memset(long_label, 'x', sizeof(long_label) - 1);
    memcpy(long_label, "device-label/", sizeof("device-label/") - 1);
    EXPECT_NAME_FAILURE(PFNGLGENBUFFERSPROC, glad_glGenBuffers, gen_no_names, nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 8, .label = long_label}), "glGenBuffers",
                        "device-label/");
    EXPECT_NAME_FAILURE(PFNGLGENSAMPLERSPROC, glad_glGenSamplers, gen_no_names, nt_gfx_make_sampler(&(nt_sampler_desc_t){.compare_func = NT_COMPARE_GREATER, .label = "failure/sampler"}),
                        "glGenSamplers", "failure/sampler");
    EXPECT_NAME_FAILURE(PFNGLGENVERTEXARRAYSPROC, glad_glGenVertexArrays, gen_no_names, nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.label = "failure/vertex-input"}), "glGenVertexArrays",
                        "failure/vertex-input");
    EXPECT_NAME_FAILURE(PFNGLGENTEXTURESPROC, glad_glGenTextures, gen_no_names,
                        nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 7, .format = NT_TEXTURE_FORMAT_RGBA8, .label = "failure/texture"}), "glGenTextures", "failure/texture");

    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main() { gl_Position = vec4(0.0); }"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "out vec4 color; void main() { color = vec4(1.0); }"});
    EXPECT_NAME_FAILURE(PFNGLCREATEPROGRAMPROC, glad_glCreateProgram, create_no_program, nt_gfx_make_program(vs, fs, "failure/program"), "glCreateProgram", "failure/program");

    nt_texture_t color = make_attachment(NT_TEXTURE_FORMAT_RGBA8, 4, 7);
    EXPECT_NAME_FAILURE(PFNGLGENFRAMEBUFFERSPROC, glad_glGenFramebuffers, gen_no_names, nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = color, .label = "failure/framebuffer"}),
                        "glGenFramebuffers", "failure/framebuffer");
}

static GLuint GLAD_API_PTR create_no_shader(GLenum type) {
    (void)type;
    return 0;
}

static PFNGLREADPIXELSPROC s_read_pixels;
static PFNGLGETERRORPROC s_get_error;
static bool s_fail_next_error;

static void GLAD_API_PTR read_pixels_then_fail(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels) {
    s_read_pixels(x, y, w, h, format, type, pixels);
    s_fail_next_error = true;
}

static GLenum GLAD_API_PTR error_after_read(void) {
    if (s_fail_next_error) {
        s_fail_next_error = false;
        return GL_INVALID_OPERATION;
    }
    return s_get_error();
}

/* A null shader name and a readback GL error on a live context are bugs: they assert, no loss latches. */
static void test_shader_name_and_readback_failures_on_a_live_context_assert(void) {
    EXPECT_NAME_FAILURE(PFNGLCREATESHADERPROC, glad_glCreateShader, create_no_shader,
                        nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main() {}\n", .label = "failure/shader"}), "glCreateShader vertex", "failure/shader");

    nt_gfx_end_frame();
    uint8_t pixel[4] = {0};
    s_read_pixels = glad_glReadPixels;
    s_get_error = glad_glGetError;
    glad_glReadPixels = read_pixels_then_fail;
    glad_glGetError = error_after_read;
    NT_TEST_EXPECT_ASSERT((void)nt_gfx_read_pixels((nt_render_target_t){0}, 0, 0, 1, 1, pixel, sizeof(pixel)));
    glad_glReadPixels = s_read_pixels;
    glad_glGetError = s_get_error;
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "read_pixels: GL error on a live context"));
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    nt_gfx_begin_frame();
}

static GLenum GLAD_API_PTR framebuffer_unsupported(GLenum target) {
    (void)target;
    return GL_FRAMEBUFFER_UNSUPPORTED;
}

/* The engine's attachment formats are required framebuffer formats: an incomplete target on a
 * live context is a driver or engine bug, so it asserts. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- assert traps and Unity checks expand into branches
static void test_an_incomplete_framebuffer_on_a_live_context_asserts(void) {
    static char label[800];
    memset(label, 'x', sizeof(label) - 1);
    memcpy(label, "failure/framebuffer/", sizeof("failure/framebuffer/") - 1);
    nt_texture_t color = make_attachment(NT_TEXTURE_FORMAT_RGBA8, 4, 7);
    nt_texture_t depth = make_attachment(NT_TEXTURE_FORMAT_DEPTH24, 4, 7);
    PFNGLCHECKFRAMEBUFFERSTATUSPROC check_status = glad_glCheckFramebufferStatus;
    PFNGLGETERRORPROC get_error = glad_glGetError;
    glad_glCheckFramebufferStatus = framebuffer_unsupported;
    glad_glGetError = invalid_operation;
    begin_device_failure_capture();
    NT_TEST_EXPECT_ASSERT((void)nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = color, .depth = depth, .label = label}));
    glad_glCheckFramebufferStatus = check_status;
    glad_glGetError = get_error;
    nt_log_remove_sink(capture_program_failure, NULL);
    /* The assert left the half-built framebuffer bound; later tests draw to the window. */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "render target incomplete on a live context"));
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    if (NT_LOG_MIN_LEVEL <= NT_LOG_LEVEL_ERROR) {
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, "status=0x8CDD"));
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, "gl_error=0x0502"));
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, "size=4x7"));
        char formats[32];
        (void)snprintf(formats, sizeof(formats), "color=%u depth=%u", (unsigned)NT_TEXTURE_FORMAT_RGBA8, (unsigned)NT_TEXTURE_FORMAT_DEPTH24);
        const char *format_fields = strstr(s_program_failure_log, formats);
        const char *name = strstr(s_program_failure_log, "failure/framebuffer/");
        TEST_ASSERT_NOT_NULL(format_fields);
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_TRUE(format_fields < name);
    } else {
        TEST_ASSERT_EQUAL_STRING("", s_program_failure_log);
    }
}

static uint32_t s_link_status_queries;

static void GLAD_API_PTR count_link_status_query(GLuint program, GLenum query, GLint *value) {
    s_link_status_queries += query == GL_LINK_STATUS;
    s_get_program_iv(program, query, value);
}

/* make_program only starts the link: reading its status or reflection there would block on the driver. */
static void test_make_program_does_not_query_link_status(void) {
    static const nt_gfx_gl_call_t blocking[] = {NT_GFX_GL_glGetProgramiv, NT_GFX_GL_glGetActiveUniform, NT_GFX_GL_glGetUniformLocation, NT_GFX_GL_glGetActiveUniformBlockName,
                                                NT_GFX_GL_glGetActiveUniformBlockiv};
    enum { BLOCKING_COUNT = sizeof(blocking) / sizeof(blocking[0]) };
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "out vec4 frag_color;\nvoid main() { frag_color = vec4(1.0); }\n"});
    uint32_t before[BLOCKING_COUNT];
    for (uint32_t i = 0; i < BLOCKING_COUNT; i++) {
        before[i] = g_nt_gfx.counters.gl[blocking[i]];
    }
    s_get_program_iv = glad_glGetProgramiv;
    s_link_status_queries = 0;
    glad_glGetProgramiv = count_link_status_query;
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    glad_glGetProgramiv = s_get_program_iv;
    TEST_ASSERT_NOT_EQUAL_UINT32(0, prog.id);
    TEST_ASSERT_EQUAL_UINT32(0, s_link_status_queries);
    for (uint32_t i = 0; i < BLOCKING_COUNT; i++) {
        TEST_ASSERT_EQUAL_UINT32(before[i], g_nt_gfx.counters.gl[blocking[i]]);
    }
    TEST_ASSERT_FALSE(nt_gfx_program_ready(prog));
    nt_test_gfx_link_wait(prog);
}

static uint32_t s_fragment_stage_logs;

static void count_fragment_stage_logs(nt_log_level_t level, const char *domain, const char *msg, void *user) {
    (void)domain;
    (void)user;
    s_fragment_stage_logs += level == NT_LOG_LEVEL_ERROR && strcmp(msg, "fragment shader:") == 0;
}

/* A failed link asserts where it finishes, after logging the stage that broke it. */
static void test_failed_link_logs_its_stage_and_asserts_at_begin_frame(void) {
    /* Mesa reports the compile error through KHR_debug, whose callback asserts at compile; this pins the link path. */
    const GLboolean debug_output = glIsEnabled(GL_DEBUG_OUTPUT);
    glDisable(GL_DEBUG_OUTPUT);
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "out vec4 frag_color;\nvoid main() { frag_color = undeclared_value; }\n"});
    nt_program_t prog = nt_gfx_make_program(vs, fs, NULL);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, prog.id);
    s_fragment_stage_logs = 0;
    s_program_failure_log[0] = '\0';
    nt_log_add_sink(count_fragment_stage_logs, NULL);
    nt_log_add_sink(capture_program_failure, NULL);
    NT_TEST_EXPECT_ASSERT(nt_test_gfx_link_wait(prog));
    nt_log_remove_sink(capture_program_failure, NULL);
    nt_log_remove_sink(count_fragment_stage_logs, NULL);
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "program link failed"));
    TEST_ASSERT_EQUAL_UINT32(NT_LOG_MIN_LEVEL <= NT_LOG_LEVEL_ERROR ? 1U : 0U, s_fragment_stage_logs);
    if (NT_LOG_MIN_LEVEL <= NT_LOG_LEVEL_ERROR) {
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, " link: "));
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, "\"<unnamed>\""));
    }
    nt_gfx_destroy_program(prog);
    if (debug_output) {
        glEnable(GL_DEBUG_OUTPUT);
    }
}

static GLuint s_failed_link_program;

static void GLAD_API_PTR fail_link_without_driver_log(GLuint program, GLenum query, GLint *value) {
    if (query == GL_LINK_STATUS || query == GL_INFO_LOG_LENGTH) {
        s_failed_link_program = program;
        *value = 0;
        return;
    }
    s_get_program_iv(program, query, value);
}

static void test_failed_link_with_empty_driver_logs_records_its_phase(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main() { gl_Position = vec4(0.0); }\n"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "out vec4 color;\nvoid main() { color = vec4(1.0); }\n"});
    nt_program_t program = nt_gfx_make_program(vs, fs, "failure/link");
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
    s_get_program_iv = glad_glGetProgramiv;
    glad_glGetProgramiv = fail_link_without_driver_log;
    s_program_failure_log[0] = '\0';
    nt_log_add_sink(capture_program_failure, NULL);
    NT_TEST_EXPECT_ASSERT(nt_test_gfx_link_wait(program));
    nt_log_remove_sink(capture_program_failure, NULL);
    glad_glGetProgramiv = s_get_program_iv;
    nt_gfx_destroy_program(program);
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "program link failed"));
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    if (NT_LOG_MIN_LEVEL <= NT_LOG_LEVEL_ERROR) {
        char identity[64];
        (void)snprintf(identity, sizeof(identity), "program %u/GL %u ", nt_pool_slot_index(program.id), s_failed_link_program);
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, identity));
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, " link: "));
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, "\"failure/link\""));
    }
}

/* The record carries the label, the phase, and the name and value the driver reflected. */
static void expect_program_failure_record(const char *fragment_source, const char *label, const char *phase, const char *reason, const char *driver_value) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main() { gl_Position = vec4(0.0); }\n"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fragment_source});
    nt_program_t program = nt_gfx_make_program(vs, fs, label);
    s_program_failure_log[0] = '\0';
    nt_log_add_sink(capture_program_failure, NULL);
    NT_TEST_EXPECT_ASSERT(nt_test_gfx_link_wait(program));
    nt_log_remove_sink(capture_program_failure, NULL);
    nt_gfx_destroy_program(program);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, reason));
    if (NT_LOG_MIN_LEVEL <= NT_LOG_LEVEL_ERROR) {
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, phase));
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, label));
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, reason));
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, driver_value));
    }
}

static void test_reflection_invariant_records_its_program_and_phase(void) {
    expect_program_failure_record("uniform float u_value;\nout vec4 color;\nvoid main() { color = vec4(u_value); }\n", "reflection/standalone", " uniforms: ", "standalone non-sampler uniform",
                                  "u_value=0x1406");
}

/* A label longer than the log line cap loses its tail, never the cause. */
static void test_long_label_keeps_the_failure_cause(void) {
    static char label[500];
    memset(label, 'x', sizeof(label) - 1);
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main() { gl_Position = vec4(0.0); }\n"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "uniform float u_value;\nout vec4 color;\nvoid main() { color = vec4(u_value); }\n"});
    nt_program_t program = nt_gfx_make_program(vs, fs, label);
    s_program_failure_log[0] = '\0';
    nt_log_add_sink(capture_program_failure, NULL);
    NT_TEST_EXPECT_ASSERT(nt_test_gfx_link_wait(program));
    nt_log_remove_sink(capture_program_failure, NULL);
    nt_gfx_destroy_program(program);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
    if (NT_LOG_MIN_LEVEL <= NT_LOG_LEVEL_ERROR) {
        TEST_ASSERT_NOT_NULL(strstr(s_program_failure_log, " uniforms: standalone non-sampler uniform: u_value=0x1406 \"xxx"));
    }
}

static void test_block_binding_invariant_records_its_phase(void) {
    expect_program_failure_record("layout(std140) uniform Lighting { vec4 u_light; };\nout vec4 color;\nvoid main() { color = u_light; }\n", "reflection/block", " blocks: ", "neither reserved nor in",
                                  "Lighting=0x10");
}

static void test_pass_load_preserves_color_and_depth_independently(void) {
    test_target_t target = make_test_target(2, 1, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);
    const uint8_t colors[8] = {17, 43, 89, 255, 211, 127, 31, 255};
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_depth = 0.25F});
    nt_gfx_end_pass();
    nt_gfx_frame_execute();
    nt_gfx_update_texture(target.color, 0, 0, 2, 1, colors);

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {1, 0, 0, 1}, .clear_depth = 0.75F, .load_color = true, .load_depth = true});
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    float depth = 0.0F;
    glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    TEST_ASSERT_UINT32_WITHIN(1, 250, (uint32_t)((depth * 1000.0F) + 0.5F));
    uint8_t pixels[8] = {0};
    read_after_frame(target.target, 2, 1, pixels, sizeof(pixels));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(colors, pixels, sizeof(colors));

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_depth = 0.75F, .load_color = true});
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    TEST_ASSERT_UINT32_WITHIN(1, 750, (uint32_t)((depth * 1000.0F) + 0.5F));
    read_after_frame(target.target, 2, 1, pixels, sizeof(pixels));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(colors, pixels, sizeof(colors));

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {1, 0, 0, 1}, .load_depth = true});
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    TEST_ASSERT_UINT32_WITHIN(1, 750, (uint32_t)((depth * 1000.0F) + 0.5F));
    read_after_frame(target.target, 2, 1, pixels, sizeof(pixels));
    assert_rgba(pixels, 2, 255, 0, 0, 255);
    destroy_test_target(&target);
}

static void test_pass_clear_covers_attachment_and_starts_with_scissor_off(void) {
    test_target_t target = make_test_target(4, 3, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_depth = 0.25F});
    nt_gfx_set_scissor(1, 1, 1, 1);
    nt_gfx_set_scissor_enabled(true);
    nt_gfx_end_pass();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {0, 1, 0, 1}, .clear_depth = 0.75F});
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    float depth[12] = {0};
    glReadPixels(0, 0, 4, 3, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    for (uint32_t i = 0; i < 12; i++) {
        TEST_ASSERT_UINT32_WITHIN(1, 750, (uint32_t)((depth[i] * 1000.0F) + 0.5F));
    }
    TEST_ASSERT_FALSE(nt_gfx_test_scissor_enabled());
    TEST_ASSERT_FALSE(glIsEnabled(GL_SCISSOR_TEST));
    uint8_t pixels[48] = {0};
    read_after_frame(target.target, 4, 3, pixels, sizeof(pixels));
    assert_rgba(pixels, 12, 0, 255, 0, 255);
    destroy_test_target(&target);
}

static void test_pass_discard_maps_attachments_and_finishes_before_unbind(void) {
    test_target_t target = make_test_target(2, 2, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);
    glad_glInvalidateFramebuffer = record_invalidate;
    nt_pass_desc_t pass = {.target = target.target, .load_depth = true, .discard_depth = true};
    nt_gfx_begin_pass(&pass);
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_UINT32(0, s_invalidate_count);
    pass.discard_depth = false;
    nt_gfx_end_pass();
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    TEST_ASSERT_EQUAL_UINT32(1, s_invalidate_count);
    TEST_ASSERT_EQUAL_INT(2, s_invalidated_count);
    TEST_ASSERT_EQUAL_HEX32(GL_DEPTH_ATTACHMENT, s_invalidated[0]);
    TEST_ASSERT_EQUAL_HEX32(GL_STENCIL_ATTACHMENT, s_invalidated[1]);
    TEST_ASSERT_NOT_EQUAL(0, s_invalidated_fbo);
    GLint fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    TEST_ASSERT_EQUAL_INT(0, fbo);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .discard_color = true, .discard_depth = true});
    nt_gfx_end_pass();
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_UINT32(2, s_invalidate_count);
    TEST_ASSERT_EQUAL_INT(3, s_invalidated_count);
    TEST_ASSERT_EQUAL_HEX32(GL_COLOR_ATTACHMENT0, s_invalidated[0]);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target});
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(2, s_invalidate_count);
    destroy_test_target(&target);
    nt_gfx_begin_frame();

    nt_gfx_begin_pass(&(nt_pass_desc_t){.discard_depth = true});
    nt_gfx_end_pass();
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    TEST_ASSERT_EQUAL_INT(2, s_invalidated_count);
    TEST_ASSERT_EQUAL_HEX32(GL_DEPTH, s_invalidated[0]);
    TEST_ASSERT_EQUAL_HEX32(GL_STENCIL, s_invalidated[1]);

    glad_glInvalidateFramebuffer = NULL;
    nt_gfx_begin_pass(&(nt_pass_desc_t){.discard_depth = true});
    nt_gfx_end_pass();
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    TEST_ASSERT_EQUAL_UINT32(3, s_invalidate_count);
}

static void test_explicit_clear_preserves_unselected_pixels_and_draw_state(void) {
    test_target_t target = make_test_target(4, 3, NT_TEXTURE_FORMAT_RGBA8, NT_TEXTURE_FORMAT_DEPTH24);
    nt_shader_t vs = nt_gfx_make_shader(
        &(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main() { vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "out vec4 color; void main() { color = vec4(0.0, 1.0, 0.0, 1.0); }"});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(program);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program, .depth_test = true, .depth_func = NT_DEPTH_ALWAYS});
    nt_vertex_input_t input = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {0, 0, 1, 1}, .clear_depth = 0.25F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(input);
    nt_gfx_apply_texture_bindings(NULL, 0);
    nt_gfx_set_scissor(1, 0, 2, 1);
    nt_gfx_set_scissor_enabled(true);
    nt_gfx_clear(&(nt_clear_desc_t){.color = true, .clear_color = {1, 0, 0, 1}, .clear_depth = 0.9F});
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    uint8_t pixels[48] = {0};
    nt_test_gl_read_in_pass(0, 0, 4, 3, pixels);
    uint8_t expected[48] = {0, 0, 255, 255, 255, 0, 0,   255, 255, 0, 0,   255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255,
                            0, 0, 255, 255, 0,   0, 255, 255, 0,   0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, pixels, sizeof(pixels));
    float depths[12] = {0};
    glReadPixels(0, 0, 4, 3, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
    for (uint32_t i = 0; i < 12; i++) {
        TEST_ASSERT_UINT32_WITHIN(1, 250, (uint32_t)((depths[i] * 1000.0F) + 0.5F));
    }
    nt_gfx_clear(&(nt_clear_desc_t){.depth = true, .clear_depth = 0.75F, .clear_color = {1, 1, 1, 1}});
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    uint8_t unchanged[48] = {0};
    nt_test_gl_read_in_pass(0, 0, 4, 3, unchanged);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pixels, unchanged, sizeof(pixels));
    uint32_t clears = g_nt_gfx.counters.gl[NT_GFX_GL_glClear];
    nt_gfx_clear(&(nt_clear_desc_t){.clear_color = {1, 1, 1, 1}, .clear_depth = 1});
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_UINT32(clears, g_nt_gfx.counters.gl[NT_GFX_GL_glClear]);
    TEST_ASSERT_TRUE(nt_gfx_test_scissor_enabled());
    TEST_ASSERT_TRUE(glIsEnabled(GL_SCISSOR_TEST));
    nt_gfx_draw(0, 3);
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    nt_test_gl_read_in_pass(0, 0, 4, 3, pixels);
    const uint8_t green[8] = {0, 255, 0, 255, 0, 255, 0, 255};
    memcpy(&expected[4], green, sizeof(green));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, pixels, sizeof(pixels));
    glReadPixels(0, 0, 4, 3, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
    for (uint32_t i = 0; i < 12; i++) {
        TEST_ASSERT_UINT32_WITHIN(1, i == 1 || i == 2 ? 750 : 250, (uint32_t)((depths[i] * 1000.0F) + 0.5F));
    }
    nt_gfx_clear(&(nt_clear_desc_t){.color = true, .clear_color = {1, 0, 0, 1}});
    nt_test_gl_read_in_pass(0, 0, 4, 3, pixels);
    const uint8_t red[8] = {255, 0, 0, 255, 255, 0, 0, 255};
    memcpy(&expected[4], red, sizeof(red));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, pixels, sizeof(pixels));
    nt_gfx_set_scissor_enabled(false);
    nt_gfx_clear(&(nt_clear_desc_t){.color = true, .clear_color = {1, 0, 0, 1}});
    nt_test_gl_read_in_pass(0, 0, 4, 3, pixels);
    assert_rgba(pixels, 12, 255, 0, 0, 255);
    nt_gfx_clear(&(nt_clear_desc_t){.depth = true, .clear_depth = 0.9F});
    nt_gfx_frame_execute();
    glReadPixels(0, 0, 4, 3, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
    for (uint32_t i = 0; i < 12; i++) {
        TEST_ASSERT_UINT32_WITHIN(1, 900, (uint32_t)((depths[i] * 1000.0F) + 0.5F));
    }
    nt_gfx_end_pass();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target.target, .clear_color = {0, 0, 1, 1}, .clear_depth = 0.25F});
    nt_gfx_frame_execute();
    glReadPixels(0, 0, 4, 3, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
    for (uint32_t i = 0; i < 12; i++) {
        TEST_ASSERT_UINT32_WITHIN(1, 250, (uint32_t)((depths[i] * 1000.0F) + 0.5F));
    }
    read_after_frame(target.target, 4, 3, pixels, sizeof(pixels));
    assert_rgba(pixels, 12, 0, 0, 255, 255);
    nt_gfx_destroy_vertex_input(input);
    nt_gfx_destroy_program(program);
    nt_gfx_destroy_shader(fs);
    nt_gfx_destroy_shader(vs);
    destroy_test_target(&target);
}

int main(void) {
    /* One hidden window and GL context serve every test; setUp/tearDown reset only engine state. */
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){
        .max_dpr = 1.0F,
        .resizable = false,
        .width = 64,
        .height = 64,
    };
    nt_window_init();
    UNITY_BEGIN();
    RUN_TEST(test_device_identity_survives_shutdown);
    RUN_TEST(test_explicit_clear_preserves_unselected_pixels_and_draw_state);
    RUN_TEST(test_pass_discard_maps_attachments_and_finishes_before_unbind);
    RUN_TEST(test_pass_load_preserves_color_and_depth_independently);
    RUN_TEST(test_pass_clear_covers_attachment_and_starts_with_scissor_off);
    RUN_TEST(test_render_target_recreate_at_new_size_without_spare_slots);
    RUN_TEST(test_depth_texture_uses_explicit_format);
    RUN_TEST(test_all_public_depth_functions_reach_gl);
    RUN_TEST(test_all_sampler_compare_functions_reach_gl);
    RUN_TEST(test_gequal_depth_accepts_equal_and_nearer_fragments);
    RUN_TEST(test_custom_blend_state_reaches_gl_unchanged);
    RUN_TEST(test_all_public_blend_enums_reach_gl);
    RUN_TEST(test_multiply_blend_multiplies_rgb_and_preserves_destination_alpha);
    RUN_TEST(test_depth_comparison_sampler_blends_comparison_results);
    RUN_TEST(test_depth_only_shadow_map_renders_and_recreates_at_new_size);
    RUN_TEST(test_two_targets_share_one_depth_texture);
    RUN_TEST(test_half_float_target_is_complete_and_keeps_values_above_one);
    RUN_TEST(test_rgba32f_linear_filtering_and_generated_mips);
    RUN_TEST(test_begin_pass_clears_depth_after_depth_writes_were_disabled);
    RUN_TEST(test_global_block_from_the_desc_binds_in_the_program);
    RUN_TEST(test_block_bound_under_one_pipeline_feeds_the_next);
    RUN_TEST(test_each_pipeline_binds_its_own_program);
    RUN_TEST(test_destroying_one_program_leaves_another_programs_pipeline_drawing);
    RUN_TEST(test_standalone_value_uniform_asserts_at_link);
    RUN_TEST(test_unknown_block_name_asserts_at_link);
    RUN_TEST(test_non_vec4_block_member_asserts_at_link);
    RUN_TEST(test_row_major_block_matrix_asserts_at_link);
    RUN_TEST(test_long_block_name_asserts_at_link);
    RUN_TEST(test_supported_sampler_types_retain_their_classes);
    RUN_TEST(test_signed_sampler_type_asserts_at_link);
    RUN_TEST(test_vertex_stage_and_array_samplers_get_distinct_units);
    RUN_TEST(test_reflection_reports_active_uniforms_with_glsl_declarations);
    RUN_TEST(test_program_with_an_unsupported_sampler_type_asserts);
    RUN_TEST(test_program_with_a_desktop_only_sampler_type_asserts);
    RUN_TEST(test_cube_array_sampler_types_assert_at_link);
    RUN_TEST(test_program_with_more_samplers_than_units_asserts);
    RUN_TEST(test_samplers_read_their_link_time_units_without_uniform_writes);
    RUN_TEST(test_two_draws_on_one_program_bind_at_their_queried_units);
    RUN_TEST(test_linking_a_program_keeps_the_bound_pipelines_program_current);
    RUN_TEST(test_missing_active_uniform_count_asserts);
    RUN_TEST(test_missing_uniform_name_length_asserts);
    RUN_TEST(test_missing_uniform_details_assert);
    RUN_TEST(test_missing_matrix_order_asserts);
    RUN_TEST(test_a_name_failure_on_a_live_context_asserts_without_latching);
    RUN_TEST(test_an_incomplete_framebuffer_on_a_live_context_asserts);
    RUN_TEST(test_shader_name_and_readback_failures_on_a_live_context_assert);
    RUN_TEST(test_make_program_does_not_query_link_status);
    RUN_TEST(test_failed_link_logs_its_stage_and_asserts_at_begin_frame);
    RUN_TEST(test_failed_link_with_empty_driver_logs_records_its_phase);
    RUN_TEST(test_reflection_invariant_records_its_program_and_phase);
    RUN_TEST(test_block_binding_invariant_records_its_phase);
    RUN_TEST(test_long_label_keeps_the_failure_cause);
    int failures = UNITY_END();
    nt_window_shutdown();
    return failures;
}
