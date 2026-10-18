/* Real-GL coverage for VI switching, isolated EBO data operations,
 * and binding preservation across rejected or failed operations. */

#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_frame.h"
#include "graphics/nt_gfx_internal.h"
#include "unity.h"
#include "window/nt_window.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include "test_helpers/nt_gfx_test_frame.h"
#include "test_helpers/nt_gfx_test_gl_read.h"

static const char *s_vs_src = "precision mediump float;\n"
                              "layout(location = 0) in vec2 a_position;\n"
                              "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";

static const char *s_fs_src = "precision mediump float;\n"
                              "out vec4 frag_color;\n"
                              "void main() { frag_color = vec4(1.0, 0.0, 0.0, 1.0); }\n";

/* Clip-space z is baked into the shader so near/far placement needs no uniform. */
static const char *s_vs_near_src = "precision mediump float;\n"
                                   "layout(location = 0) in vec2 a_position;\n"
                                   "void main() { gl_Position = vec4(a_position, -0.5, 1.0); }\n";

static const char *s_vs_far_src = "precision mediump float;\n"
                                  "layout(location = 0) in vec2 a_position;\n"
                                  "void main() { gl_Position = vec4(a_position, 0.5, 1.0); }\n";

static const char *s_fs_green_src = "precision mediump float;\n"
                                    "out vec4 frag_color;\n"
                                    "void main() { frag_color = vec4(0.0, 1.0, 0.0, 1.0); }\n";

/* Covers the whole viewport. */
static const float s_full[6] = {-1.0F, -1.0F, 3.0F, -1.0F, -1.0F, 3.0F};
/* Degenerate: every vertex on one point, so it rasterizes nothing. */
static const float s_empty[6] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};

static const uint16_t s_tri_indices[3] = {0, 1, 2};
static const uint16_t s_degenerate_indices[3] = {0, 0, 0};

void setUp(void) {
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.frame_capacity[NT_GFX_FRAME_UNIFORM] = 0; /* the raw backend recreate would orphan the stream's buffer */
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

static void remove_state_counters(void);
static void init_with_vertex_streams(uint32_t count);
static void disarm_get_error_counter(void);

void tearDown(void) {
    /* A failed assert inside a counting window longjmps past the restore; leaked
     * glad pointers would then outlive this test's GL context. */
    remove_state_counters();
    disarm_get_error_counter();
    nt_gfx_shutdown();
}

static nt_buffer_t make_vbo(const float *data) {
    return nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_IMMUTABLE,
        .size = 6U * sizeof(float),
        .data = data,
    });
}

static nt_vertex_layout_t pos2_layout(void) { return (nt_vertex_layout_t){.attr_count = 1, .stride = 8, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 2}}}; }

static nt_vertex_input_t make_vi(nt_buffer_t vbo, nt_buffer_t ibo) { return nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.layout = pos2_layout(), .vertex_buffer = vbo, .index_buffer = ibo}); }

static nt_pipeline_t make_red_pipeline(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(program);
    return nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
}

/* Between frames: reads the window center through the public readback. */
static uint8_t center_red(void) {
    uint8_t px[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(nt_gfx_read_pixels((nt_render_target_t){0}, 8, 8, 1, 1, px, sizeof(px)));
    return px[0];
}

/* Ends the pass and the frame, reads the window center and opens the next frame. */
static uint8_t end_frame_center_red(void) {
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    const uint8_t red = center_red();
    nt_gfx_begin_frame();
    return red;
}

/* Mid-pass red channel at the window center; the pass stays open for the calls that follow. */
static uint8_t center_red_in_pass(void) {
    uint8_t px[4] = {0, 0, 0, 0};
    nt_test_gl_read_in_pass(8, 8, 1, 1, px);
    return px[0];
}

static nt_pipeline_t make_pipeline_ex(const char *vs_src, const char *fs_src, bool depth_test, bool depth_write, bool blend) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fs_src});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(program);
    return nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = program,
        .depth_test = depth_test,
        .depth_write = depth_write,
        .blend = blend ? nt_blend_alpha() : nt_blend_opaque(),
    });
}

static void begin_black_pass(void) { nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F}); }

/* Counts the state calls a bind emits, straight off the glad pointers, so a
 * dedup claim is measured on real GL traffic rather than on source counters. */
static struct {
    uint32_t use_program;
    uint32_t bind_vao;
    uint32_t depth_mask;
    uint32_t depth_mask_false;
    uint32_t enable;
    uint32_t enable_blend;
    uint32_t disable;
    uint32_t disable_blend;
    uint32_t blend_func_separate;
    uint32_t active_texture;
    uint32_t bind_texture;
    uint32_t viewport;
    uint32_t clear_color;
    uint32_t clear_depth;
} s_gl_calls;

static PFNGLUSEPROGRAMPROC s_saved_use_program;
static PFNGLBINDVERTEXARRAYPROC s_saved_bind_vao;
static PFNGLDEPTHMASKPROC s_saved_depth_mask;
static PFNGLENABLEPROC s_saved_enable;
static PFNGLDISABLEPROC s_saved_disable;
static PFNGLBLENDFUNCSEPARATEPROC s_saved_blend_func_separate;
static PFNGLACTIVETEXTUREPROC s_saved_active_texture;
static PFNGLBINDTEXTUREPROC s_saved_state_bind_texture;
static PFNGLVIEWPORTPROC s_saved_viewport;
static PFNGLCLEARCOLORPROC s_saved_clear_color;
static PFNGLCLEARDEPTHPROC s_saved_clear_depth;

static void GLAD_API_PTR counting_use_program(GLuint program) {
    s_gl_calls.use_program++;
    s_saved_use_program(program);
}

static void GLAD_API_PTR counting_bind_vao(GLuint array) {
    s_gl_calls.bind_vao++;
    s_saved_bind_vao(array);
}

static void GLAD_API_PTR counting_depth_mask(GLboolean flag) {
    s_gl_calls.depth_mask++;
    if (flag == GL_FALSE) {
        s_gl_calls.depth_mask_false++;
    }
    s_saved_depth_mask(flag);
}

static void GLAD_API_PTR counting_enable(GLenum cap) {
    s_gl_calls.enable++;
    if (cap == GL_BLEND) {
        s_gl_calls.enable_blend++;
    }
    s_saved_enable(cap);
}

static void GLAD_API_PTR counting_disable(GLenum cap) {
    s_gl_calls.disable++;
    if (cap == GL_BLEND) {
        s_gl_calls.disable_blend++;
    }
    s_saved_disable(cap);
}

static void GLAD_API_PTR counting_blend_func_separate(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha) {
    s_gl_calls.blend_func_separate++;
    s_saved_blend_func_separate(src_rgb, dst_rgb, src_alpha, dst_alpha);
}

static void GLAD_API_PTR counting_active_texture(GLenum unit) {
    s_gl_calls.active_texture++;
    s_saved_active_texture(unit);
}

static void GLAD_API_PTR counting_state_bind_texture(GLenum target, GLuint texture) {
    s_gl_calls.bind_texture++;
    s_saved_state_bind_texture(target, texture);
}

static void GLAD_API_PTR counting_viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    s_gl_calls.viewport++;
    s_saved_viewport(x, y, width, height);
}

static void GLAD_API_PTR counting_clear_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    s_gl_calls.clear_color++;
    s_saved_clear_color(r, g, b, a);
}

static void GLAD_API_PTR counting_clear_depth(GLdouble depth) {
    s_gl_calls.clear_depth++;
    s_saved_clear_depth(depth);
}

/* nt_gfx_init reloads glad, so this must run after the init under test. */
static void install_state_counters(void) {
    /* Pending commands recorded before counting must not be counted. */
    nt_gfx_frame_execute();
    memset(&s_gl_calls, 0, sizeof(s_gl_calls));
    s_saved_use_program = glad_glUseProgram;
    s_saved_bind_vao = glad_glBindVertexArray;
    s_saved_depth_mask = glad_glDepthMask;
    s_saved_enable = glad_glEnable;
    s_saved_disable = glad_glDisable;
    s_saved_blend_func_separate = glad_glBlendFuncSeparate;
    s_saved_active_texture = glad_glActiveTexture;
    s_saved_state_bind_texture = glad_glBindTexture;
    s_saved_viewport = glad_glViewport;
    s_saved_clear_color = glad_glClearColor;
    s_saved_clear_depth = glad_glClearDepth;
    glad_glUseProgram = counting_use_program;
    glad_glBindVertexArray = counting_bind_vao;
    glad_glDepthMask = counting_depth_mask;
    glad_glEnable = counting_enable;
    glad_glDisable = counting_disable;
    glad_glBlendFuncSeparate = counting_blend_func_separate;
    glad_glActiveTexture = counting_active_texture;
    glad_glBindTexture = counting_state_bind_texture;
    glad_glViewport = counting_viewport;
    glad_glClearColor = counting_clear_color;
    glad_glClearDepth = counting_clear_depth;
}

/* Must run before any assert -- a failure longjmps past the restore. Idempotent,
 * so tearDown can undo an install a failed assert jumped over. */
static void remove_state_counters(void) {
    if (s_saved_use_program == NULL) {
        return;
    }
    nt_gfx_frame_execute();
    glad_glUseProgram = s_saved_use_program;
    glad_glBindVertexArray = s_saved_bind_vao;
    glad_glDepthMask = s_saved_depth_mask;
    glad_glEnable = s_saved_enable;
    glad_glDisable = s_saved_disable;
    glad_glBlendFuncSeparate = s_saved_blend_func_separate;
    glad_glActiveTexture = s_saved_active_texture;
    glad_glBindTexture = s_saved_state_bind_texture;
    glad_glViewport = s_saved_viewport;
    glad_glClearColor = s_saved_clear_color;
    glad_glClearDepth = s_saved_clear_depth;
    s_saved_use_program = NULL;
    s_saved_bind_vao = NULL;
    s_saved_depth_mask = NULL;
    s_saved_enable = NULL;
    s_saved_disable = NULL;
    s_saved_blend_func_separate = NULL;
    s_saved_active_texture = NULL;
    s_saved_state_bind_texture = NULL;
    s_saved_viewport = NULL;
    s_saved_clear_color = NULL;
    s_saved_clear_depth = NULL;
}

/* One glBindVertexArray selects the whole geometry: two meshes alternate under
 * a single pipeline with no per-switch attribute re-pointing. */
static void test_vertex_inputs_alternate_under_one_pipeline(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_vertex_input_t vi_full = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    nt_vertex_input_t vi_empty = make_vi(make_vbo(s_empty), (nt_buffer_t){0});

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_empty);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 0, center_red_in_pass());

    /* Switch geometry WITHOUT touching the pipeline. */
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());
}

/* Counts REAL glVertexAttribPointer calls via glad-pointer swap, so a raw GL
 * call that skips the NT_GL wrapper (and its counter) is still caught. */
static uint32_t s_real_attrib_pointer_calls;
static PFNGLVERTEXATTRIBPOINTERPROC s_saved_attrib_pointer;
static void GLAD_API_PTR counting_vertex_attrib_pointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
    s_real_attrib_pointer_calls++;
    s_saved_attrib_pointer(index, size, type, normalized, stride, pointer);
}

/* One float2 instance attribute at location 1, stride 8. */
static nt_instance_layout_t offset_layout(void) {
    return nt_gfx_make_instance_layout(&(nt_vertex_layout_t){.attr_count = 1, .stride = 8, .attrs = {{.location = 1, .type = NT_VERTEX_FLOAT, .count = 2}}});
}

/* Two float2 instance attributes at locations 1 and 2, stride 16. */
static nt_instance_layout_t pair_layout(void) {
    return nt_gfx_make_instance_layout(
        &(nt_vertex_layout_t){.attr_count = 2, .stride = 16, .attrs = {{.location = 1, .type = NT_VERTEX_FLOAT, .count = 2}, {.location = 2, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 8}}});
}

/* Steady state: vertex and index buffers are baked at creation and each instanced vertex input
 * keeps its (layout, buffer, offset), so a whole frame of draws issues no glVertexAttribPointer at
 * all, and VAO binds stay one per vertex-input switch (delta-checked). */
static void test_second_frame_issues_no_static_attrib_pointers(void) {
    init_with_vertex_streams(1);
    nt_pipeline_t pip = make_red_pipeline();
    nt_vertex_input_t vi_full = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    nt_vertex_input_t vi_empty = make_vi(make_vbo(s_empty), (nt_buffer_t){0});
    nt_vertex_input_t vi_inst = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    /* The warm-up frame points the instance side; it ends on vi_full, which the next frame starts with. */
    uint32_t inst_offset = 0;
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 8, 4, &inst_offset), 0, 8);
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input_instanced(vi_inst, offset_layout(), nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX), inst_offset);
    nt_gfx_draw_instanced(0, 3, 1);
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();

    const uint32_t attrib_pointers = g_nt_gfx.counters.gl[NT_GFX_GL_glVertexAttribPointer];
    const uint32_t vao_binds = g_nt_gfx.counters.gl[NT_GFX_GL_glBindVertexArray];
    s_real_attrib_pointer_calls = 0;
    s_saved_attrib_pointer = glad_glVertexAttribPointer;
    glad_glVertexAttribPointer = counting_vertex_attrib_pointer;
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    nt_gfx_bind_vertex_input(vi_empty);
    nt_gfx_draw(0, 3);
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 8, 4, &inst_offset), 0, 8);
    nt_gfx_bind_vertex_input_instanced(vi_inst, offset_layout(), nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX), inst_offset);
    nt_gfx_draw_instanced(0, 3, 1);
    nt_gfx_end_pass();
    nt_gfx_frame_execute();
    /* Captured before teardown so its binds cannot pollute it. */
    uint32_t frame_vao_binds = g_nt_gfx.counters.gl[NT_GFX_GL_glBindVertexArray] - vao_binds;
    uint32_t frame_attrib_pointers = g_nt_gfx.counters.gl[NT_GFX_GL_glVertexAttribPointer] - attrib_pointers;
    /* Restore before any assert -- a failure longjmps past this line. */
    glad_glVertexAttribPointer = s_saved_attrib_pointer;
    TEST_ASSERT_EQUAL_UINT32(0, inst_offset);
    /* The carried-over VAO dedups the first bind; one bind per vertex-input switch. */
    TEST_ASSERT_EQUAL_UINT32(2, frame_vao_binds);
    TEST_ASSERT_EQUAL_UINT32(0, frame_attrib_pointers);
    TEST_ASSERT_EQUAL_UINT32(0, s_real_attrib_pointer_calls);
}

/* Re-inits gfx with `count` sized frame vertex streams. */
static void init_with_vertex_streams(uint32_t count) {
    nt_gfx_end_frame();
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    for (uint32_t k = 0; k < count; k++) {
        desc.frame_capacity[NT_GFX_FRAME_VERTEX + k] = 4096;
    }
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
}

typedef struct {
    nt_vertex_input_t vi;
    uint32_t offset;
    nt_buffer_t buffer; /* {0} = the general frame stream */
} instanced_run_t;

/* One frame of instanced draws over the general stream, which gets 64 bytes; returns that frame's
 * glVertexAttribPointer count (counters are per frame) and stores its glBindBuffer count. */
static uint32_t s_frame_buffer_binds;
static uint32_t instanced_frame(nt_pipeline_t pip, const instanced_run_t *runs, uint32_t count) {
    uint32_t offset = 0;
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 64, 4, &offset), 0, 64);
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    for (uint32_t i = 0; i < count; i++) {
        const nt_buffer_t buffer = runs[i].buffer.id != 0 ? runs[i].buffer : nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX);
        nt_gfx_bind_vertex_input_instanced(runs[i].vi, pair_layout(), buffer, runs[i].offset);
        nt_gfx_draw_instanced(0, 3, 1);
    }
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    const uint32_t pointers = g_nt_gfx.counters.gl[NT_GFX_GL_glVertexAttribPointer];
    s_frame_buffer_binds = g_nt_gfx.counters.gl[NT_GFX_GL_glBindBuffer];
    nt_gfx_begin_frame();
    return pointers;
}

/* A VAO keeps its instance pointers across passes and frames: the same tuple re-points nothing;
 * a new offset re-points every attribute and binds the instance buffer only when the
 * ARRAY_BUFFER binding differs. */
static void test_repeated_instance_offset_skips_re_point(void) {
    init_with_vertex_streams(1);
    nt_pipeline_t pip = make_red_pipeline();
    nt_vertex_input_t vi_a = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    nt_vertex_input_t vi_b = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    instanced_frame(pip, &(instanced_run_t){vi_a, 0, {0}}, 1); /* also counts the first bind's pointers */
    TEST_ASSERT_EQUAL_UINT32(0, instanced_frame(pip, &(instanced_run_t){vi_a, 0, {0}}, 1));
    TEST_ASSERT_EQUAL_UINT32(2, instanced_frame(pip, &(instanced_run_t){vi_a, 32, {0}}, 1));
    TEST_ASSERT_EQUAL_UINT32(0, s_frame_buffer_binds);                                       /* the stream's buffer is still bound */
    TEST_ASSERT_EQUAL_UINT32(2, instanced_frame(pip, &(instanced_run_t){vi_b, 32, {0}}, 1)); /* another VAO has its own pointers */
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
}

/* Two vertex inputs over one buffer each keep their own offset: a steady frame re-points
 * nothing, a run that moves re-points only its own VAO, and one vertex input drawn at two
 * offsets in a frame re-points between them every frame. */
static void test_vertex_inputs_over_one_buffer_keep_their_offsets(void) {
    init_with_vertex_streams(1);
    nt_pipeline_t pip = make_red_pipeline();
    nt_vertex_input_t vi_a = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    nt_vertex_input_t vi_b = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    const instanced_run_t steady[2] = {{vi_a, 0, {0}}, {vi_b, 16, {0}}};
    instanced_frame(pip, steady, 2);
    TEST_ASSERT_EQUAL_UINT32(0, instanced_frame(pip, steady, 2));
    const instanced_run_t moved[2] = {{vi_a, 32, {0}}, {vi_b, 16, {0}}};
    TEST_ASSERT_EQUAL_UINT32(2, instanced_frame(pip, moved, 2));
    TEST_ASSERT_EQUAL_UINT32(0, instanced_frame(pip, moved, 2));
    const instanced_run_t shared[2] = {{vi_a, 0, {0}}, {vi_a, 16, {0}}};
    instanced_frame(pip, shared, 2);
    TEST_ASSERT_EQUAL_UINT32(4, instanced_frame(pip, shared, 2));
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
}

static const char *s_vs_offset_src = "precision mediump float;\n"
                                     "layout(location = 0) in vec2 a_position;\n"
                                     "layout(location = 1) in vec2 a_offset;\n"
                                     "void main() { gl_Position = vec4(a_position + a_offset, 0.0, 1.0); }\n";

/* Places two instance offsets at byte `at` of a stream, after off-screen filler. */
static void write_instance_offsets(uint32_t stream, uint32_t at, float x0, float x1) {
    uint32_t offset = 0;
    float *dst = (float *)nt_gfx_frame_alloc(stream, at + 16U, 4, &offset);
    TEST_ASSERT_EQUAL_UINT32(0, offset);
    for (uint32_t i = 0; i < at / 4U; i++) {
        dst[i] = 9.0F;
    }
    const float pairs[4] = {x0, 0.0F, x1, 0.0F};
    memcpy(dst + (at / 4U), pairs, sizeof(pairs));
}

static nt_pipeline_t make_offset_pipeline(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vs_offset_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(program);
    return nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
}

static nt_vertex_input_t make_offset_vi(nt_buffer_t ibo) { return make_vi(make_vbo(s_full), ibo); }

/* Each vertex input reads the buffer it is bound with. One pass draws through the stream 0 vertex input
 * (both instances off-screen), then through the stream 1 one (instance 0 off-screen, instance 1
 * on the center) at equal offsets: the center is red only if the second draw binds the stream 1
 * VAO and its attribute advances per instance, not per vertex. */
static void test_vertex_inputs_draw_their_own_buffer(void) {
    init_with_vertex_streams(2);
    nt_pipeline_t pip = make_offset_pipeline();
    nt_buffer_t ibo =
        nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = s_tri_indices, .size = sizeof(s_tri_indices), .index_type = NT_INDEX_UINT16});
    const uint32_t s0 = NT_GFX_FRAME_VERTEX;
    const uint32_t s1 = NT_GFX_FRAME_VERTEX + 1;
    nt_vertex_input_t vi0 = make_offset_vi(ibo);
    nt_vertex_input_t vi1 = make_offset_vi(ibo);

    for (int indexed = 1; indexed >= 0; indexed--) {
        write_instance_offsets(s0, 8, 5.0F, 5.0F);
        write_instance_offsets(s1, 8, 5.0F, 0.0F);
        begin_black_pass();
        nt_gfx_bind_pipeline(pip);
        nt_gfx_bind_vertex_input_instanced(vi0, offset_layout(), nt_gfx_frame_buffer(s0), 8);
        if (indexed) {
            nt_gfx_draw_indexed_instanced(0, 3, 3, 2);
        } else {
            nt_gfx_draw_instanced(0, 3, 2);
        }
        TEST_ASSERT_EQUAL_UINT8(0, center_red_in_pass());
        nt_gfx_bind_vertex_input_instanced(vi1, offset_layout(), nt_gfx_frame_buffer(s1), 8);
        if (indexed) {
            nt_gfx_draw_indexed_instanced(0, 3, 3, 2);
        } else {
            nt_gfx_draw_instanced(0, 3, 2);
        }
        TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());
    }
    /* New stream contents at the same offset draw fresh data: the upload runs every frame. */
    write_instance_offsets(s1, 8, 5.0F, 5.0F);
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input_instanced(vi1, offset_layout(), nt_gfx_frame_buffer(s1), 8);
    nt_gfx_draw_instanced(0, 3, 2);
    TEST_ASSERT_EQUAL_UINT8(0, end_frame_center_red());
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
}

/* A caller-owned instance buffer, written once: two vertex inputs over it at offsets 0 and 16 draw
 * their own instances (only instance 2 sits on the center; offset 8 would read an off-screen one),
 * a steady frame re-points nothing, and an update between frames draws the new data through the
 * same pointers. */
static void test_caller_buffer_draws_without_re_point(void) {
    init_with_vertex_streams(1);
    nt_pipeline_t pip = make_offset_pipeline();
    const float instances[8] = {5.0F, 0.0F, 0.0F, 9.0F, 0.0F, 0.0F, 6.0F, 0.0F};
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .data = instances, .size = sizeof(instances)});
    nt_vertex_input_t first = make_offset_vi((nt_buffer_t){0});
    nt_vertex_input_t second = make_offset_vi((nt_buffer_t){0});
    uint8_t first_red = 0;
    for (uint32_t frame = 0; frame < 3; frame++) {
        if (frame == 2) {
            const float moved[2] = {9.0F, 0.0F};
            nt_gfx_update_buffer(buffer, 16, moved, sizeof(moved)); /* instance 2 leaves the center */
        }
        begin_black_pass();
        nt_gfx_bind_pipeline(pip);
        nt_gfx_bind_vertex_input_instanced(first, offset_layout(), buffer, 0);
        nt_gfx_draw_instanced(0, 3, 2);
        first_red = center_red_in_pass();
        nt_gfx_bind_vertex_input_instanced(second, offset_layout(), buffer, 16);
        nt_gfx_draw_instanced(0, 3, 1);
        nt_gfx_end_pass();
        nt_gfx_end_frame();
        const uint32_t pointers = g_nt_gfx.counters.gl[NT_GFX_GL_glVertexAttribPointer];
        const uint8_t red = center_red();
        nt_gfx_begin_frame();
        TEST_ASSERT_EQUAL_UINT8(0, first_red);
        if (frame == 0) {
            TEST_ASSERT_UINT8_WITHIN(1, 255, red);
        } else {
            TEST_ASSERT_EQUAL_UINT32(0, pointers);
            TEST_ASSERT_UINT8_WITHIN(1, frame == 1 ? 255 : 0, red);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
}

/* Destroying a bound vertex input grounds the VAO cache, so a recycled GL name still binds. */
static void test_destroying_a_bound_vertex_input_grounds_the_vao_cache(void) {
    init_with_vertex_streams(1);
    nt_pipeline_t pip = make_red_pipeline();
    nt_vertex_input_t vi_a = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    instanced_frame(pip, &(instanced_run_t){vi_a, 0, {0}}, 1);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_gl_test_cached_vao());
    nt_gfx_destroy_vertex_input(vi_a);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_gl_test_cached_vao());

    nt_vertex_input_t vi_b = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_b);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());
    /* A vertex input reusing the freed slot starts with no instance tuple: creation points the
     * static attribute, its first bind both instance attributes. */
    nt_gfx_destroy_vertex_input(vi_b);
    nt_vertex_input_t vi_c = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    TEST_ASSERT_EQUAL_UINT32(1 + 2, instanced_frame(pip, &(instanced_run_t){vi_c, 0, {0}}, 1));
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
}

/* Reads one attribute slot of a VAO straight from GL, then rebinds the cached VAO. */
static void read_attrib(GLuint vao, GLuint location, GLint *out_enabled, GLint *out_buffer) {
    glBindVertexArray(vao);
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_ENABLED, out_enabled);
    glGetVertexAttribiv(location, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, out_buffer);
    glBindVertexArray((GLuint)nt_gfx_gl_test_cached_vao());
}

/* Instances are not baked: destroying the buffer they came from leaves the vertex input alive,
 * its instance slots disabled and detached (WebGL rejects draws through a deleted buffer), and the
 * next bind with another buffer draws again. */
static void test_destroying_an_instance_buffer_detaches_it(void) {
    init_with_vertex_streams(1);
    nt_pipeline_t pip = make_red_pipeline();
    nt_buffer_t own = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 64});
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    nt_vertex_input_t other = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    instanced_frame(pip, &(instanced_run_t){vi, 0, own}, 1);
    const GLuint vao = (GLuint)nt_gfx_gl_test_cached_vao();
    instanced_frame(pip, &(instanced_run_t){other, 0, {0}}, 1); /* vi's VAO is no longer bound */
    GLint enabled = 0;
    GLint attached = 0;
    read_attrib(vao, 2, &enabled, &attached);
    TEST_ASSERT_EQUAL_INT(1, enabled);
    TEST_ASSERT_NOT_EQUAL_INT(0, attached);
    nt_gfx_destroy_buffer(own);
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi));
    for (GLuint location = 1; location <= 2; location++) {
        read_attrib(vao, location, &enabled, &attached);
        TEST_ASSERT_EQUAL_INT(0, enabled);
        TEST_ASSERT_EQUAL_INT(0, attached);
    }
    /* The mirror forgot the tuple: the next bind enables and points both again. The frame also
     * counts the destroy's two detaching pointers. */
    TEST_ASSERT_EQUAL_UINT32(2 + 2, instanced_frame(pip, &(instanced_run_t){vi, 0, {0}}, 1));
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    uint32_t offset = 0;
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 16, 4, &offset), 0, 16);
    nt_gfx_bind_vertex_input_instanced(vi, pair_layout(), nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX), offset);
    nt_gfx_draw_instanced(0, 3, 1);
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
}

/* Index uploads go through the service VAO and rebind the cached one: the bound VAO keeps its
 * own index binding. */
static void test_index_upload_keeps_the_bound_vao(void) {
    init_with_vertex_streams(2);
    nt_pipeline_t pip = make_offset_pipeline();
    nt_buffer_t ibo_a =
        nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = s_tri_indices, .size = sizeof(s_tri_indices), .index_type = NT_INDEX_UINT16});
    nt_buffer_t ibo_b =
        nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_DYNAMIC, .data = s_tri_indices, .size = sizeof(s_tri_indices), .index_type = NT_INDEX_UINT16});
    nt_vertex_input_t vi = make_offset_vi(ibo_a);
    write_instance_offsets(NT_GFX_FRAME_VERTEX + 1, 0, 0.0F, 0.0F);
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input_instanced(vi, offset_layout(), nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + 1), 0);
    nt_gfx_draw_indexed_instanced(0, 3, 3, 1);
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());

    nt_gfx_update_buffer(ibo_b, 0, s_degenerate_indices, sizeof(s_degenerate_indices)); /* the vertex input stays the cached VAO */
    write_instance_offsets(NT_GFX_FRAME_VERTEX + 1, 0, 0.0F, 0.0F);
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input_instanced(vi, offset_layout(), nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + 1), 0);
    nt_gfx_draw_indexed_instanced(0, 3, 3, 1);
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
}

/* GL_ELEMENT_ARRAY_BUFFER binding is VAO state: data ops on OTHER index
 * buffers must not silently rewire the bound vertex input's index binding. */
static void test_index_data_ops_do_not_rewire_bound_vertex_input(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_buffer_t ibo_a =
        nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = s_tri_indices, .size = sizeof(s_tri_indices), .index_type = NT_INDEX_UINT16});
    nt_vertex_input_t vi_a = make_vi(make_vbo(s_full), ibo_a);
    nt_buffer_t ibo_b =
        nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_DYNAMIC, .data = s_tri_indices, .size = sizeof(s_tri_indices), .index_type = NT_INDEX_UINT16});

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_a);
    nt_gfx_draw_indexed(0, 3, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red_in_pass());

    /* Both data-op flavors on other index buffers while A's vertex input
     * stays bound: update and creation-with-data. */
    nt_gfx_update_buffer(ibo_b, 0, s_degenerate_indices, sizeof(s_degenerate_indices));
    nt_buffer_t ibo_c = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = s_degenerate_indices, .size = sizeof(s_degenerate_indices), .index_type = NT_INDEX_UINT16});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, ibo_c.id);

    nt_gfx_end_pass();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_a);
    nt_gfx_draw_indexed(0, 3, 3); /* still A's triangle indices, not B */
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
}

/* A rejected pipeline bind clears the program selection but must not disturb
 * the orthogonal vertex-input binding. */
static void test_rejected_pipeline_bind_preserves_vertex_input(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_program_t doomed = nt_gfx_make_program(nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vs_src}),
                                              nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src}), NULL);
    nt_pipeline_t stale = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = doomed});
    nt_gfx_destroy_program(doomed);
    TEST_ASSERT_FALSE(nt_gfx_pipeline_valid(stale));

    nt_vertex_input_t vi_full = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red_in_pass());

    nt_gfx_bind_pipeline(stale); /* rejected: drops the pipeline only */

    nt_gfx_bind_pipeline(pip);
    nt_gfx_draw(0, 3); /* no vertex-input re-bind needed */
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());
}

/* Creation must restore the previously bound VAO (creation binds its own). */
static void test_creating_vertex_input_preserves_bound_one(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_vertex_input_t vi_full = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_frame_execute();
    GLint bound_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &bound_vao);
    TEST_ASSERT_NOT_EQUAL_INT(0, bound_vao);

    nt_vertex_input_t other = make_vi(make_vbo(s_empty), (nt_buffer_t){0});
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(other));
    GLint current_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current_vao);
    TEST_ASSERT_EQUAL_INT(bound_vao, current_vao);

    nt_gfx_draw(0, 3);
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());
}

static const char *s_vertexid_vs_src = "precision mediump float;\n"
                                       "void main() {\n"
                                       "    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
                                       "    gl_Position = vec4(p, 0.0, 1.0);\n"
                                       "}\n";

/* Attribute-less draws (gl_VertexID) go through an empty vertex input: a
 * bound VAO with zero enabled arrays must rasterize on real core GL. */
static void test_empty_vertex_input_draws_fullscreen(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vertexid_vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(program);
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());
}

/* Reads a sampling unit without leaving the backend's active-unit mirror stale:
 * the unit that was active is restored before returning. */
static GLint texture_name_on_unit(uint32_t slot) {
    GLint prev_unit = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_unit);
    glActiveTexture(GL_TEXTURE0 + slot);
    GLint name = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &name);
    glActiveTexture((GLenum)prev_unit);
    return name;
}

/* GL-cache probes drive a unit directly: their subject is the backend cache,
 * not the program-driven semantic set. */
static void backend_bind_texture_unit(nt_texture_t tex, nt_sampler_t sampler, uint32_t unit) {
    const nt_sampler_t effective = sampler.id != 0 ? sampler : nt_gfx_get_texture_default_sampler(tex);
    nt_gfx_backend_bind_texture_unit(nt_gfx_test_texture_backend_id(tex), nt_gfx_test_sampler_backend_id(effective), unit);
}

/* Counts glGetError calls; on WebGL each one is a blocking GPU-process round trip. */
static uint32_t s_get_error_calls;
static PFNGLGETERRORPROC s_saved_get_error;

static GLenum GLAD_API_PTR counting_get_error(void) {
    s_get_error_calls++;
    return s_saved_get_error();
}

/* nt_gfx_init reloads glad, so this must run after the init under test. */
static void arm_get_error_counter(void) {
    s_get_error_calls = 0;
    s_saved_get_error = glad_glGetError;
    glad_glGetError = counting_get_error;
}

/* Idempotent: tearDown undoes an arm that a failed assert jumped over. */
static void disarm_get_error_counter(void) {
    if (s_saved_get_error == NULL) {
        return;
    }
    glad_glGetError = s_saved_get_error;
    s_saved_get_error = NULL;
}

/* A compressed create uploads its levels on the scratch unit without reading GL
 * errors, so slot 0 still holds A in GL and in the cache: re-binding A costs
 * nothing and a draw still samples A. */
static void test_compressed_create_keeps_texture_cache_truthful(void) {
    if (!nt_gfx_gpu_caps()->has_bc7) {
        TEST_IGNORE_MESSAGE("BC7 unsupported on this host");
    }
    static const uint8_t pixel[4] = {255, 0, 0, 255};
    nt_texture_t tex_a = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 1,
        .height = 1,
        .data = pixel,
        .format = NT_TEXTURE_FORMAT_RGBA8,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex_a.id);

    backend_bind_texture_unit(tex_a, NT_SAMPLER_DEFAULT, 0);
    GLint name_a = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &name_a);
    TEST_ASSERT_NOT_EQUAL_INT(0, name_a);

    /* 8x8 BC7 = 4 blocks, then a 4x4 level of 1 block. */
    static const uint8_t bc7_chain[(4 * 16) + 16] = {0};
    arm_get_error_counter();
    nt_texture_t created = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 8,
        .height = 8,
        .data = bc7_chain,
        .format = NT_TEXTURE_FORMAT_BC7_RGBA,
        .level_count = 2,
    });
    uint32_t calls = s_get_error_calls;
    disarm_get_error_counter();
    TEST_ASSERT_NOT_EQUAL_UINT32(0, created.id);
    TEST_ASSERT_EQUAL_UINT32(0, calls);

    install_state_counters();
    backend_bind_texture_unit(tex_a, NT_SAMPLER_DEFAULT, 0);
    remove_state_counters();

    GLint bound = texture_name_on_unit(0);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.bind_texture);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.active_texture);
    TEST_ASSERT_EQUAL_INT(name_a, bound);
}

/* Ground state is real GL calls, so scissor left enabled by a previous gfx
 * lifetime cannot survive into the next one on the same native context. */
static void test_ground_state_disables_scissor(void) {
    begin_black_pass();
    nt_gfx_set_scissor_enabled(true);
    nt_gfx_end_pass();
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_INT(GL_TRUE, (int)glIsEnabled(GL_SCISSOR_TEST));

    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);

    TEST_ASSERT_EQUAL_INT(GL_FALSE, (int)glIsEnabled(GL_SCISSOR_TEST));
    TEST_ASSERT_FALSE(nt_gfx_test_scissor_enabled());
}

/* The cache survives begin_frame, so a frame that repeats the previous one reaches
 * GL with no state calls at all. */
static void test_identical_second_frame_issues_no_bind_calls(void) {
    static const char *textured_fs = "precision mediump float;\n"
                                     "uniform sampler2D u_texture;\n"
                                     "out vec4 frag_color;\n"
                                     "void main() { frag_color = texture(u_texture, vec2(0.5)); }\n";
    static const uint8_t pixel[4] = {255, 255, 255, 255};
    nt_pipeline_t pip = make_pipeline_ex(s_vs_src, textured_fs, false, true, false);
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = pixel, .format = NT_TEXTURE_FORMAT_RGBA8});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    const nt_gfx_texture_binding_t binding = {
        .name = nt_hash32_str("u_texture"),
        .texture = tex,
        .sampler = NT_SAMPLER_DEFAULT,
    };

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_apply_texture_bindings(&binding, 1);
    TEST_ASSERT_TRUE(nt_gfx_test_texture_set_applied());
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();

    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    const uint32_t sampler_binds = g_nt_gfx.counters.gl[NT_GFX_GL_glBindSampler];
    install_state_counters();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_apply_texture_bindings(&binding, 1);
    TEST_ASSERT_TRUE(nt_gfx_test_texture_set_applied());
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    remove_state_counters();
    nt_gfx_end_frame();
    /* Zero calls only counts if the frame still rendered what the first one did. */
    uint8_t red = center_red();
    nt_gfx_begin_frame();

    TEST_ASSERT_UINT8_WITHIN(1, 255, red);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.use_program);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.bind_vao);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.depth_mask);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.enable);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.disable);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.blend_func_separate);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.active_texture);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.bind_texture);
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx.counters.gl[NT_GFX_GL_glBindSampler] - sampler_binds);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.viewport);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.clear_color);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.clear_depth);
}

/* Deduplication is a diff, not a mute: a pipeline that really changes state
 * still emits, and GL ends up holding the second pipeline's state. */
static void test_state_change_mid_frame_still_emits(void) {
    nt_pipeline_t pip_a = make_pipeline_ex(s_vs_src, s_fs_src, true, true, false);
    nt_pipeline_t pip_b = make_pipeline_ex(s_vs_src, s_fs_src, true, false, true);
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    begin_black_pass();
    install_state_counters();
    nt_gfx_bind_pipeline(pip_a);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_bind_pipeline(pip_b);
    nt_gfx_draw(0, 3);
    remove_state_counters();
    GLboolean depth_write_enabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write_enabled);
    GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    nt_gfx_end_pass();

    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.depth_mask);
    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.depth_mask_false);
    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.enable_blend);
    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.blend_func_separate);
    TEST_ASSERT_EQUAL_INT(GL_FALSE, (int)depth_write_enabled);
    TEST_ASSERT_EQUAL_INT(GL_TRUE, (int)blend_enabled);
}

/* The pass clear forces the depth mask on and leaves it on: the depth really
 * clears after a depth_write=false pipeline, and the pipeline that wants the
 * mask off has to say so again inside the new pass. */
static void test_pass_clear_after_depth_write_off(void) {
    nt_pipeline_t near_pip = make_pipeline_ex(s_vs_near_src, s_fs_src, true, true, false);
    nt_pipeline_t far_pip = make_pipeline_ex(s_vs_far_src, s_fs_green_src, true, true, false);
    nt_pipeline_t no_write_pip = make_pipeline_ex(s_vs_near_src, s_fs_src, true, false, false);
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    begin_black_pass();
    nt_gfx_bind_pipeline(near_pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_bind_pipeline(no_write_pip);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();

    install_state_counters();
    begin_black_pass();
    nt_gfx_bind_pipeline(far_pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    /* One glDepthMask(GL_TRUE) for the clear, and none for the bind: a mask put
     * back to GL_FALSE after the clear would cost two more. */
    nt_gfx_frame_execute();
    uint32_t depth_mask_through_far_bind = s_gl_calls.depth_mask;

    nt_gfx_bind_pipeline(no_write_pip);
    remove_state_counters();
    GLboolean depth_write_enabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write_enabled);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    uint8_t px[4] = {0, 0, 0, 0};
    bool read_ok = nt_gfx_read_pixels((nt_render_target_t){0}, 8, 8, 1, 1, px, sizeof(px));
    nt_gfx_begin_frame();

    /* The far quad only survives the LESS test if the clear reset depth to 1.0. */
    TEST_ASSERT_TRUE(read_ok);
    TEST_ASSERT_UINT8_WITHIN(1, 0, px[0]);
    TEST_ASSERT_UINT8_WITHIN(1, 255, px[1]);
    TEST_ASSERT_EQUAL_UINT32(1, depth_mask_through_far_bind);
    TEST_ASSERT_EQUAL_UINT32(2, s_gl_calls.depth_mask);
    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.depth_mask_false);
    TEST_ASSERT_EQUAL_INT(GL_FALSE, (int)depth_write_enabled);
}

/* Bound state is pass-scoped but the GL cache is not: re-binding the same
 * pipeline and vertex input in the next pass -- and the next frame -- costs
 * nothing. */
static void test_same_pipeline_across_passes_rebinds_for_free(void) {
    nt_pipeline_t pip = make_pipeline_ex(s_vs_src, s_fs_src, false, true, false);
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();

    begin_black_pass();
    install_state_counters();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    remove_state_counters();
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();

    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.use_program);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.bind_vao);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.depth_mask);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.enable);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.disable);
}

/* Without a per-frame reset, init is the only grounding: state a previous gfx
 * lifetime left on the shared native context must be off again, and the fresh
 * cache must agree with GL rather than re-issue what is already right. */
static void test_ground_state_after_reinit(void) {
    nt_pipeline_t blend_pip = make_pipeline_ex(s_vs_src, s_fs_src, false, true, true);
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {1.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(blend_pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_INT(GL_TRUE, (int)glIsEnabled(GL_BLEND));

    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
    TEST_ASSERT_EQUAL_INT(GL_FALSE, (int)glIsEnabled(GL_BLEND));

    /* The fresh cache says the clear color is already {0,0,0,0}, so this pass
     * dedups it: only a real glClearColor in ground state clears away the red
     * the previous lifetime left in GL. */
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 0.0F}, .clear_depth = 1.0F});
    uint8_t cleared_red = end_frame_center_red();
    TEST_ASSERT_UINT8_WITHIN(1, 0, cleared_red);

    nt_pipeline_t opaque_pip = make_pipeline_ex(s_vs_src, s_fs_src, false, true, false);
    nt_pipeline_t alpha_pip = make_pipeline_ex(s_vs_src, s_fs_src, false, true, true);
    nt_vertex_input_t fresh_vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    install_state_counters();
    begin_black_pass();
    nt_gfx_bind_pipeline(opaque_pip);
    nt_gfx_frame_execute();
    uint32_t disable_blend_after_opaque = s_gl_calls.disable_blend;
    nt_gfx_bind_pipeline(alpha_pip);
    nt_gfx_bind_vertex_input(fresh_vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    remove_state_counters();

    TEST_ASSERT_EQUAL_UINT32(0, disable_blend_after_opaque);
    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.enable_blend);
}

/* The viewport is cached like any other GL state: a repeated rect costs nothing,
 * a real change and a framebuffer resize both reach GL. */
static void test_viewport_dedup_and_resize(void) {
    begin_black_pass();
    install_state_counters();
    nt_gfx_frame_execute();
    const uint32_t after_begin_pass = s_gl_calls.viewport;
    /* The front-end records this (begin_pass leaves its mirror unknown); the backend drops it. */
    nt_gfx_set_viewport(0, 0, (int)g_nt_window.fb_width, (int)g_nt_window.fb_height);
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_UINT32(after_begin_pass, s_gl_calls.viewport);
    nt_gfx_set_viewport(0, 0, 8, 8);
    nt_gfx_set_viewport(0, 0, 8, 8);
    nt_gfx_frame_execute();
    uint32_t after_same_rect = s_gl_calls.viewport;
    nt_gfx_set_viewport(0, 0, 4, 4);
    nt_gfx_frame_execute();
    uint32_t after_new_rect = s_gl_calls.viewport;
    nt_gfx_end_pass();

    const uint32_t saved_fb_width = g_nt_window.fb_width;
    g_nt_window.fb_width = saved_fb_width + 1;
    begin_black_pass();
    nt_gfx_frame_execute();
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    nt_gfx_end_pass();
    remove_state_counters();
    uint32_t after_resize = s_gl_calls.viewport;
    g_nt_window.fb_width = saved_fb_width;

    TEST_ASSERT_EQUAL_UINT32(1, after_same_rect);
    TEST_ASSERT_EQUAL_UINT32(2, after_new_rect);
    TEST_ASSERT_EQUAL_UINT32(3, after_resize);
    TEST_ASSERT_EQUAL_INT((int)saved_fb_width + 1, viewport[2]);
    TEST_ASSERT_EQUAL_INT((int)g_nt_window.fb_height, viewport[3]);
}

/* Clear color and clear depth are cached per value, so repeating a pass desc is
 * free and changing one of them re-issues only that call. */
static void test_clear_values_dedup(void) {
    begin_black_pass();
    nt_gfx_end_pass();

    install_state_counters();
    begin_black_pass();
    nt_gfx_end_pass();
    nt_gfx_frame_execute();
    uint32_t color_after_identical_pass = s_gl_calls.clear_color;
    uint32_t depth_after_identical_pass = s_gl_calls.clear_depth;

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {1.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_end_pass();
    remove_state_counters();

    TEST_ASSERT_EQUAL_UINT32(0, color_after_identical_pass);
    TEST_ASSERT_EQUAL_UINT32(0, depth_after_identical_pass);
    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.clear_color);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.clear_depth);
}

/* Ground state parks the viewport at zero size, which no real pass can match:
 * the first pass of a fresh gfx lifetime always re-issues it. */
static void test_ground_state_viewport_reissued_after_reinit(void) {
    nt_pipeline_t pip = make_pipeline_ex(s_vs_src, s_fs_src, false, true, false);
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();

    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);

    install_state_counters();
    begin_black_pass();
    nt_gfx_frame_execute();
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    nt_gfx_end_pass();
    remove_state_counters();

    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.viewport);
    TEST_ASSERT_EQUAL_INT((int)g_nt_window.fb_width, viewport[2]);
    TEST_ASSERT_EQUAL_INT((int)g_nt_window.fb_height, viewport[3]);
}

/* A raw backend recreate grounds the cache against the fresh context: state the
 * old lifetime left on is off, and the first bind that wants it says so again. */
static void test_recreate_all_resources_grounds_cache(void) {
    nt_pipeline_t blend_pip = make_pipeline_ex(s_vs_src, s_fs_src, false, true, true);
    nt_buffer_t vbo = make_vbo(s_full);
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});

    begin_black_pass();
    nt_gfx_bind_pipeline(blend_pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_INT(GL_TRUE, (int)glIsEnabled(GL_BLEND));

    /* A real restore follows the loss wipe, which leaves no frontend object on a backend record. */
    nt_gfx_begin_frame();
    nt_gfx_destroy_program(nt_gfx_pipeline_program(blend_pip));
    nt_gfx_destroy_buffer(vbo);
    TEST_ASSERT_TRUE(nt_gfx_backend_recreate_all_resources());
    TEST_ASSERT_EQUAL_INT(GL_FALSE, (int)glIsEnabled(GL_BLEND));

    nt_pipeline_t fresh_blend = make_pipeline_ex(s_vs_src, s_fs_src, false, true, true);

    /* After the recreate: it reloads glad and would drop the swapped pointers. */
    install_state_counters();
    begin_black_pass();
    nt_gfx_bind_pipeline(fresh_blend);
    nt_gfx_end_pass();
    remove_state_counters();

    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.enable_blend);
}

/* Deterministic half: destroying the bound vertex input clears the cached VAO
 * name. The redraw half only bites on drivers that recycle the deleted name. */
static void test_gl_name_reuse_after_destroying_bound_vertex_input(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_buffer_t vbo = make_vbo(s_full);
    nt_vertex_input_t vi_a = make_vi(vbo, (nt_buffer_t){0});

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_a);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_gl_test_cached_vao());
    nt_gfx_destroy_vertex_input(vi_a); /* destroyed while bound */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_gl_test_cached_vao());

    nt_vertex_input_t vi_b = make_vi(vbo, (nt_buffer_t){0});
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi_b));

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_b);
    nt_gfx_draw(0, 3);
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    TEST_ASSERT_UINT8_WITHIN(1, 255, end_frame_center_red());
}

static nt_texture_t make_pixel_texture(const uint8_t pixel[4]) { return nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = pixel, .format = NT_TEXTURE_FORMAT_RGBA8}); }

/* Creation uploads on the scratch unit and leaves it active, so this reads the
 * new texture's GL name. */
static GLint current_texture_name(void) {
    GLint name = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &name);
    return name;
}

/* Deterministic half: destroy clears the slot the texture was bound in, leaving
 * the other slots alone. The re-bind half only bites on drivers that recycle the
 * deleted GL name. */
static void test_gl_name_reuse_after_destroying_bound_texture(void) {
    static const uint8_t white[4] = {255, 255, 255, 255};
    static const uint8_t grey[4] = {128, 128, 128, 255};
    nt_texture_t tex_a = make_pixel_texture(white);
    nt_texture_t keep = make_pixel_texture(grey);
    GLint name_keep = current_texture_name();
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex_a.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, keep.id);
    TEST_ASSERT_NOT_EQUAL_INT(0, name_keep);

    backend_bind_texture_unit(tex_a, NT_SAMPLER_DEFAULT, 0);
    backend_bind_texture_unit(keep, NT_SAMPLER_DEFAULT, 1);

    nt_gfx_destroy_texture(tex_a);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_gl_test_cached_texture(0));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)name_keep, nt_gfx_gl_test_cached_texture(1));

    nt_texture_t tex_b = make_pixel_texture(white);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex_b.id);
    GLint name_b = current_texture_name();
    TEST_ASSERT_NOT_EQUAL_INT(0, name_b);

    backend_bind_texture_unit(tex_b, NT_SAMPLER_DEFAULT, 0);
    TEST_ASSERT_EQUAL_INT(name_b, texture_name_on_unit(0));
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
}

/* update_texture and plain creation both upload on the scratch unit, so the
 * texture sampling slot 0 holds stays bound there and re-binding it mid-pass
 * costs no GL call. */
static void test_update_texture_mid_pass_leaves_sampling_slot_bound(void) {
    static const uint8_t white[4] = {255, 255, 255, 255};
    static const uint8_t grey[4] = {128, 128, 128, 255};
    nt_texture_t tex_1 = make_pixel_texture(white);
    GLint name_1 = current_texture_name();
    nt_texture_t tex_2 = make_pixel_texture(grey);
    GLint name_2 = current_texture_name();
    TEST_ASSERT_NOT_EQUAL_INT(0, name_1);
    TEST_ASSERT_NOT_EQUAL_INT(name_1, name_2);

    begin_black_pass();
    backend_bind_texture_unit(tex_1, NT_SAMPLER_DEFAULT, 0);
    /* A failed create would leave slot 0 untouched and false-pass the next line. */
    nt_texture_t created_mid_pass = make_pixel_texture(grey);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, created_mid_pass.id);
    TEST_ASSERT_EQUAL_INT(name_1, texture_name_on_unit(0));
    nt_gfx_update_texture(tex_2, 0, 0, 1, 1, white);

    GLint upload_unit = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &upload_unit);
    GLint upload_bound = current_texture_name();
    GLint slot0 = texture_name_on_unit(0);

    install_state_counters();
    backend_bind_texture_unit(tex_1, NT_SAMPLER_DEFAULT, 0);
    remove_state_counters();
    nt_gfx_end_pass();

    TEST_ASSERT_EQUAL_INT(GL_TEXTURE0 + NT_GFX_MAX_TEXTURE_SLOTS, upload_unit);
    TEST_ASSERT_EQUAL_INT(name_2, upload_bound);
    TEST_ASSERT_EQUAL_INT(name_1, slot0);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.bind_texture);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.active_texture);
}

/* A burst of uploads switches to the scratch unit once and stays there; the
 * sampling slot it left behind still needs no GL call to re-bind. */
static void test_upload_burst_costs_one_active_texture_switch(void) {
    static const uint8_t white[4] = {255, 255, 255, 255};
    static const uint8_t grey[4] = {128, 128, 128, 255};
    static const uint8_t black[4] = {0, 0, 0, 255};
    nt_texture_t tex_1 = make_pixel_texture(white);
    GLint name_1 = current_texture_name();
    nt_texture_t tex_2 = make_pixel_texture(grey);
    nt_texture_t tex_3 = make_pixel_texture(black);
    TEST_ASSERT_NOT_EQUAL_INT(0, name_1);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex_2.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex_3.id);

    begin_black_pass();
    backend_bind_texture_unit(tex_1, NT_SAMPLER_DEFAULT, 0);

    install_state_counters();
    nt_gfx_update_texture(tex_2, 0, 0, 1, 1, white);
    nt_gfx_update_texture(tex_3, 0, 0, 1, 1, white);
    nt_gfx_update_texture(tex_2, 0, 0, 1, 1, grey);
    remove_state_counters();
    uint32_t burst_active = s_gl_calls.active_texture;
    uint32_t burst_bind = s_gl_calls.bind_texture;

    install_state_counters();
    backend_bind_texture_unit(tex_1, NT_SAMPLER_DEFAULT, 0);
    remove_state_counters();
    nt_gfx_end_pass();

    TEST_ASSERT_EQUAL_UINT32(1, burst_active);
    TEST_ASSERT_EQUAL_UINT32(3, burst_bind);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.bind_texture);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.active_texture);
}

/* Destruction must release the current GL program even without another bind. */
static void test_destroy_current_program_then_relink_reissues_use_program(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src});
    nt_program_t prog_p = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(prog_p);
    nt_pipeline_t pip_p = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog_p});
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    begin_black_pass();
    nt_gfx_bind_pipeline(pip_p);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();

    GLint old_program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    TEST_ASSERT_NOT_EQUAL_INT(0, old_program);
    nt_gfx_destroy_program(prog_p); /* cascades into pip_p */
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_gl_test_cached_program());

    begin_black_pass();
    nt_gfx_end_pass();
    nt_gfx_frame_execute();
    TEST_ASSERT_FALSE(glIsProgram((GLuint)old_program));
    GLint current_program = -1;
    glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
    TEST_ASSERT_EQUAL_INT(0, current_program);

    nt_program_t prog_q = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(prog_q);
    nt_pipeline_t pip_q = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog_q});
    TEST_ASSERT_TRUE(nt_gfx_pipeline_valid(pip_q));

    begin_black_pass();
    install_state_counters();
    nt_gfx_bind_pipeline(pip_q);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    remove_state_counters();
    uint8_t red = end_frame_center_red();

    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.use_program);
    TEST_ASSERT_UINT8_WITHIN(1, 255, red);
}

// #region sampler binds
/* Reads a sampling unit's sampler without leaving the backend's active-unit
 * mirror stale: the unit that was active is restored before returning. */
static GLint sampler_name_on_unit(uint32_t slot) {
    GLint prev_unit = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_unit);
    glActiveTexture(GL_TEXTURE0 + slot);
    GLint name = 0;
    glGetIntegerv(GL_SAMPLER_BINDING, &name);
    glActiveTexture((GLenum)prev_unit);
    return name;
}

/* The sampler is cached like the texture: a unit that already holds it costs
 * nothing, across passes and across frames. */
static void test_same_sampler_on_a_slot_binds_once(void) {
    static const uint8_t white[4] = {255, 255, 255, 255};
    nt_texture_t tex = make_pixel_texture(white);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    uint32_t sampler_backend = nt_gfx_test_sampler_backend_id(nt_gfx_get_texture_default_sampler(tex));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, sampler_backend);

    const uint32_t sampler_binds = g_nt_gfx.counters.gl[NT_GFX_GL_glBindSampler];
    begin_black_pass();
    backend_bind_texture_unit(tex, NT_SAMPLER_DEFAULT, 0);
    backend_bind_texture_unit(tex, NT_SAMPLER_DEFAULT, 0);
    nt_gfx_end_pass();
    begin_black_pass();
    backend_bind_texture_unit(tex, NT_SAMPLER_DEFAULT, 0);
    nt_gfx_end_pass();

    begin_black_pass();
    backend_bind_texture_unit(tex, NT_SAMPLER_DEFAULT, 0);
    nt_gfx_end_pass();

    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.counters.gl[NT_GFX_GL_glBindSampler] - sampler_binds);
    TEST_ASSERT_EQUAL_UINT32(sampler_backend, nt_gfx_gl_test_cached_sampler(0));
    TEST_ASSERT_EQUAL_INT((GLint)sampler_backend, sampler_name_on_unit(0));
}

/* One bind installs the override itself: the texture default never reaches the
 * unit first, so an overriding material costs one glBindSampler, not two. The
 * cache is per unit, so a default on one unit and an override on another coexist. */
static void test_override_binds_one_sampler(void) {
    static const uint8_t white[4] = {255, 255, 255, 255};
    nt_texture_t tex = make_pixel_texture(white);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_sampler_t override = nt_gfx_make_sampler(&(nt_sampler_desc_t){
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_REPEAT,
        .wrap_v = NT_WRAP_REPEAT,
    });
    uint32_t override_backend = nt_gfx_test_sampler_backend_id(override);
    uint32_t default_backend = nt_gfx_test_sampler_backend_id(nt_gfx_get_texture_default_sampler(tex));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, override_backend);
    TEST_ASSERT_NOT_EQUAL_UINT32(default_backend, override_backend);

    const uint32_t sampler_binds = g_nt_gfx.counters.gl[NT_GFX_GL_glBindSampler];
    begin_black_pass();
    backend_bind_texture_unit(tex, NT_SAMPLER_DEFAULT, 0);
    backend_bind_texture_unit(tex, override, 1);
    nt_gfx_end_pass();

    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.counters.gl[NT_GFX_GL_glBindSampler] - sampler_binds);
    TEST_ASSERT_EQUAL_UINT32(default_backend, nt_gfx_gl_test_cached_sampler(0));
    TEST_ASSERT_EQUAL_UINT32(override_backend, nt_gfx_gl_test_cached_sampler(1));
    TEST_ASSERT_NOT_EQUAL_UINT32(nt_gfx_gl_test_cached_sampler(0), nt_gfx_gl_test_cached_sampler(1));
    TEST_ASSERT_EQUAL_INT((GLint)default_backend, sampler_name_on_unit(0));
    TEST_ASSERT_EQUAL_INT((GLint)override_backend, sampler_name_on_unit(1));
}

/* The texture cache hit on the unit must not skip the sampler half of the bind. */
static void test_cached_texture_still_binds_a_new_sampler(void) {
    static const uint8_t white[4] = {255, 255, 255, 255};
    nt_texture_t tex = make_pixel_texture(white);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);
    nt_sampler_t override = nt_gfx_make_sampler(&(nt_sampler_desc_t){.min_filter = NT_FILTER_LINEAR, .mag_filter = NT_FILTER_LINEAR});
    uint32_t override_backend = nt_gfx_test_sampler_backend_id(override);
    TEST_ASSERT_NOT_EQUAL_UINT32(nt_gfx_test_sampler_backend_id(nt_gfx_get_texture_default_sampler(tex)), override_backend);

    backend_bind_texture_unit(tex, NT_SAMPLER_DEFAULT, 0);
    backend_bind_texture_unit(tex, override, 0);

    TEST_ASSERT_EQUAL_UINT32(override_backend, nt_gfx_gl_test_cached_sampler(0));
    TEST_ASSERT_EQUAL_INT((GLint)override_backend, sampler_name_on_unit(0));
}

/* The native context outlives a recreate and its sampler objects with it, so
 * only ground state's own unbind makes the zeroed cache truthful. */
static void test_ground_state_reissues_sampler_bind(void) {
    static const uint8_t white[4] = {255, 255, 255, 255};
    nt_texture_t tex = make_pixel_texture(white);
    backend_bind_texture_unit(tex, NT_SAMPLER_DEFAULT, 0);
    GLint bound_before = sampler_name_on_unit(0);
    TEST_ASSERT_NOT_EQUAL_INT(0, bound_before);

    /* A real restore follows the loss wipe, which leaves no frontend object on a backend record. */
    nt_gfx_destroy_texture(tex);
    TEST_ASSERT_TRUE(nt_gfx_backend_recreate_all_resources());
    TEST_ASSERT_EQUAL_INT(GL_TRUE, (int)glIsSampler((GLuint)bound_before));
    TEST_ASSERT_EQUAL_INT(0, sampler_name_on_unit(0));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_gl_test_cached_sampler(0));

    nt_texture_t fresh = make_pixel_texture(white);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, fresh.id);

    const uint32_t sampler_binds = g_nt_gfx.counters.gl[NT_GFX_GL_glBindSampler];
    backend_bind_texture_unit(fresh, NT_SAMPLER_DEFAULT, 0);
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.counters.gl[NT_GFX_GL_glBindSampler] - sampler_binds);
    TEST_ASSERT_NOT_EQUAL_INT(0, sampler_name_on_unit(0));
}
// #endregion

/* A block's array members are not standalone uniforms: linking must not look up a location per element. */
static void test_block_members_need_no_location_lookup(void) {
    TEST_ASSERT_NOT_EQUAL_UINT32(0, make_red_pipeline().id);
    /* Completion (when supported), link status, block count and uniform count suffice without blocks. */
    TEST_ASSERT_EQUAL_UINT32(g_nt_gfx.gpu_caps.has_parallel_shader_compile ? 4U : 3U, g_nt_gfx.counters.gl[NT_GFX_GL_glGetProgramiv]);
    const char *vs = "precision highp float;\n"
                     "layout(std140) uniform Draw { vec4 big_data[1024]; };\n"
                     "void main() { gl_Position = big_data[gl_VertexID]; }\n";
    nt_pipeline_t pip = make_pipeline_ex(vs, s_fs_src, false, false, false);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);
    /* Reflection runs in the begin_frame that finished the link, the frame these counters cover. */
    TEST_ASSERT_EQUAL_UINT32(0, g_nt_gfx.counters.gl[NT_GFX_GL_glGetUniformLocation]);
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.counters.gl[NT_GFX_GL_glGetActiveUniformsiv]);

    const char *matrix_vs = "precision highp float;\n"
                            "layout(std140) uniform Draw { mat4 matrices[2]; };\n"
                            "void main() { gl_Position = matrices[gl_VertexID & 1] * vec4(1.0); }\n";
    pip = make_pipeline_ex(matrix_vs, s_fs_src, false, false, false);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);
    TEST_ASSERT_EQUAL_UINT32(2, g_nt_gfx.counters.gl[NT_GFX_GL_glGetActiveUniformsiv]);
}

/* An equal block bind reuses the bound range; a changed value binds a new one, and the draw sees the last. */
static void test_equal_block_bind_skips_the_range_bind(void) {
    const char *fs = "precision mediump float;\n"
                     "layout(std140) uniform Draw { vec4 u_color; };\n"
                     "out vec4 frag_color;\n"
                     "void main() { frag_color = u_color; }\n";
    nt_gfx_end_frame();
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.frame_capacity[NT_GFX_FRAME_UNIFORM] = 4096;
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    nt_pipeline_t pip = make_pipeline_ex(s_vs_src, fs, false, false, false);
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    float value[4] = {0};
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_frame_execute();
    const uint32_t before = g_nt_gfx.counters.gl[NT_GFX_GL_glBindBufferRange];
    nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, value, sizeof(value));
    nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, value, sizeof(value));
    nt_gfx_frame_execute();
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.counters.gl[NT_GFX_GL_glBindBufferRange] - before);
    for (uint32_t i = 0; i < 4; i++) {
        value[i] = (float)(i + 1U) * 0.25F;
        nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, value, sizeof(value));
        nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, value, sizeof(value));
        nt_gfx_frame_execute();
        TEST_ASSERT_EQUAL_UINT32(i + 2U, g_nt_gfx.counters.gl[NT_GFX_GL_glBindBufferRange] - before);
    }
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    uint8_t px[4] = {0};
    TEST_ASSERT_TRUE(nt_gfx_read_pixels((nt_render_target_t){0}, 8, 8, 1, 1, px, sizeof(px)));
    nt_gfx_begin_frame();
    const uint8_t expected[4] = {64, 128, 191, 255};
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT_UINT8_WITHIN(1, expected[i], px[i]);
    }
}

/* The backend table is indexed by the frontend slot: the pool hands out the last freed slot first,
 * and each reused slot draws its own buffer's vertices. */
static void test_buffer_slot_reuse_draws_each_buffers_own_data(void) {
    nt_buffer_t a = make_vbo(s_full);
    nt_buffer_t b = make_vbo(s_empty);
    nt_buffer_t c = make_vbo(s_full);
    const uint32_t slot_a = nt_pool_slot_index(a.id);
    const uint32_t slot_c = nt_pool_slot_index(c.id);
    nt_gfx_destroy_buffer(a);
    nt_gfx_destroy_buffer(c);
    nt_buffer_t d = make_vbo(s_empty);
    nt_buffer_t e = make_vbo(s_full);
    TEST_ASSERT_EQUAL_UINT32(slot_c, nt_pool_slot_index(d.id));
    TEST_ASSERT_EQUAL_UINT32(slot_a, nt_pool_slot_index(e.id));
    TEST_ASSERT_EQUAL_UINT32(slot_c, nt_gfx_test_buffer_backend_id(d));
    TEST_ASSERT_EQUAL_UINT32(slot_a, nt_gfx_test_buffer_backend_id(e));

    nt_pipeline_t pip = make_red_pipeline();
    const nt_buffer_t buffers[3] = {b, d, e};
    const uint8_t expected_red[3] = {0, 0, 255};
    for (uint32_t i = 0; i < 3; i++) {
        nt_vertex_input_t vi = make_vi(buffers[i], (nt_buffer_t){0});
        begin_black_pass();
        nt_gfx_bind_pipeline(pip);
        nt_gfx_bind_vertex_input(vi);
        nt_gfx_draw(0, 3);
        TEST_ASSERT_EQUAL_UINT8(expected_red[i], end_frame_center_red());
    }
}

int main(void) {
    /* One hidden window and GL context serve every test; setUp/tearDown reset only engine state. */
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){.max_dpr = 1.0F, .resizable = false, .width = 16, .height = 16};
    nt_window_init();
    UNITY_BEGIN();
    RUN_TEST(test_block_members_need_no_location_lookup);
    RUN_TEST(test_equal_block_bind_skips_the_range_bind);
    RUN_TEST(test_buffer_slot_reuse_draws_each_buffers_own_data);
    RUN_TEST(test_vertex_inputs_alternate_under_one_pipeline);
    RUN_TEST(test_second_frame_issues_no_static_attrib_pointers);
    RUN_TEST(test_repeated_instance_offset_skips_re_point);
    RUN_TEST(test_vertex_inputs_over_one_buffer_keep_their_offsets);
    RUN_TEST(test_vertex_inputs_draw_their_own_buffer);
    RUN_TEST(test_caller_buffer_draws_without_re_point);
    RUN_TEST(test_destroying_a_bound_vertex_input_grounds_the_vao_cache);
    RUN_TEST(test_destroying_an_instance_buffer_detaches_it);
    RUN_TEST(test_index_upload_keeps_the_bound_vao);
    RUN_TEST(test_index_data_ops_do_not_rewire_bound_vertex_input);
    RUN_TEST(test_empty_vertex_input_draws_fullscreen);
    RUN_TEST(test_rejected_pipeline_bind_preserves_vertex_input);
    RUN_TEST(test_creating_vertex_input_preserves_bound_one);
    RUN_TEST(test_compressed_create_keeps_texture_cache_truthful);
    RUN_TEST(test_ground_state_disables_scissor);
    RUN_TEST(test_identical_second_frame_issues_no_bind_calls);
    RUN_TEST(test_state_change_mid_frame_still_emits);
    RUN_TEST(test_pass_clear_after_depth_write_off);
    RUN_TEST(test_same_pipeline_across_passes_rebinds_for_free);
    RUN_TEST(test_ground_state_after_reinit);
    RUN_TEST(test_viewport_dedup_and_resize);
    RUN_TEST(test_clear_values_dedup);
    RUN_TEST(test_ground_state_viewport_reissued_after_reinit);
    RUN_TEST(test_recreate_all_resources_grounds_cache);
    RUN_TEST(test_gl_name_reuse_after_destroying_bound_vertex_input);
    RUN_TEST(test_gl_name_reuse_after_destroying_bound_texture);
    RUN_TEST(test_update_texture_mid_pass_leaves_sampling_slot_bound);
    RUN_TEST(test_upload_burst_costs_one_active_texture_switch);
    RUN_TEST(test_destroy_current_program_then_relink_reissues_use_program);
    RUN_TEST(test_same_sampler_on_a_slot_binds_once);
    RUN_TEST(test_override_binds_one_sampler);
    RUN_TEST(test_cached_texture_still_binds_a_new_sampler);
    RUN_TEST(test_ground_state_reissues_sampler_bind);
    int failures = UNITY_END();
    nt_window_shutdown();
    return failures;
}
