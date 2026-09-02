/* Real-GL coverage for VI switching, isolated EBO data operations, orphaning,
 * and binding preservation across rejected or failed operations. */

#include "basisu/nt_basisu_transcoder.h"
#include "graphics/nt_gfx.h"
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

/* Transcoder fake in place of the loud-fail stub: level 0 describes cleanly and
 * the session then refuses, the shape of corrupt basis data and the only way to
 * fail a compressed create AFTER the backend bound its new texture. */
void nt_basisu_transcoder_global_init(void) {}

bool nt_basisu_validate_header(const void *basis_data, uint32_t basis_size) {
    (void)basis_data;
    (void)basis_size;
    return true;
}

uint32_t nt_basisu_get_level_count(const void *basis_data, uint32_t basis_size) {
    (void)basis_data;
    (void)basis_size;
    return 1;
}

bool nt_basisu_get_level_desc(const void *basis_data, uint32_t basis_size, uint32_t level_index, uint32_t *out_width, uint32_t *out_height, uint32_t *out_total_blocks) {
    (void)basis_data;
    (void)basis_size;
    if (level_index != 0) {
        return false;
    }
    *out_width = 4;
    *out_height = 4;
    *out_total_blocks = 1;
    return true;
}

bool nt_basisu_start_transcoding(const void *basis_data, uint32_t basis_size) {
    (void)basis_data;
    (void)basis_size;
    return false;
}

void nt_basisu_stop_transcoding(void) {}

bool nt_basisu_transcode_level(const void *basis_data, uint32_t basis_size, uint32_t level_index, void *output, uint32_t output_blocks, nt_basisu_format_t format) {
    (void)basis_data;
    (void)basis_size;
    (void)level_index;
    (void)output;
    (void)output_blocks;
    (void)format;
    return false;
}

uint32_t nt_basisu_bytes_per_block(nt_basisu_format_t format) {
    (void)format;
    return 16;
}

uint32_t nt_basisu_gl_internal_format(nt_basisu_format_t format) {
    (void)format;
    return 0;
}

void setUp(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){.max_dpr = 1.0F, .resizable = false, .width = 16, .height = 16};
    nt_window_init();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

static void remove_state_counters(void);

void tearDown(void) {
    /* A failed assert inside a counting window longjmps past the restore; leaked
     * glad pointers would then outlive this test's GL context. */
    remove_state_counters();
    nt_gfx_shutdown();
    nt_window_shutdown();
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
    return nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_make_program(vs, fs)});
}

static uint8_t center_red(void) {
    uint8_t px[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(8, 8, 1, 1, px, sizeof(px)));
    return px[0];
}

static nt_pipeline_t make_pipeline_ex(const char *vs_src, const char *fs_src, bool depth_test, bool depth_write, bool blend) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fs_src});
    return nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = nt_gfx_make_program(vs, fs),
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

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());
    nt_gfx_end_pass();

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_empty);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 0, center_red());

    /* Switch geometry WITHOUT touching the pipeline. */
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* Counts REAL glVertexAttribPointer calls via glad-pointer swap, so a
 * reintroduced re-pointing path is caught even if it bypasses the source
 * counters in nt_gfx_gl.c. */
static uint32_t s_real_attrib_pointer_calls;
static PFNGLVERTEXATTRIBPOINTERPROC s_saved_attrib_pointer;
static void GLAD_API_PTR counting_vertex_attrib_pointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
    s_real_attrib_pointer_calls++;
    s_saved_attrib_pointer(index, size, type, normalized, stride, pointer);
}

/* Steady state: the static half of vertex-input setup is baked at creation, so
 * a whole frame of draws issues zero divisor-0 glVertexAttribPointer calls --
 * the only pointer calls left are the per-draw instance re-points, and VAO
 * binds stay one per vertex-input switch (delta-checked). */
static void test_second_frame_issues_no_static_attrib_pointers(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_vertex_input_t vi_full = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    nt_vertex_input_t vi_empty = make_vi(make_vbo(s_empty), (nt_buffer_t){0});
    nt_vertex_input_t vi_inst = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = pos2_layout(),
        .instance_layout = {.attr_count = 1, .stride = 8, .attrs = {{.location = 1, .type = NT_VERTEX_FLOAT, .count = 2}}},
        .vertex_buffer = make_vbo(s_full),
    });
    nt_buffer_t inst_buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_STREAM, .size = 64});

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_gl_test_reset_counters();
    s_real_attrib_pointer_calls = 0;
    s_saved_attrib_pointer = glad_glVertexAttribPointer;
    glad_glVertexAttribPointer = counting_vertex_attrib_pointer;
    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    nt_gfx_bind_vertex_input(vi_empty);
    nt_gfx_draw(0, 3);
    nt_gfx_bind_vertex_input(vi_inst);
    nt_gfx_bind_instance_buffer(inst_buf, 0);
    nt_gfx_draw_instanced(0, 3, 1);
    nt_gfx_end_pass();
    /* Captured before end_frame so teardown binds cannot pollute it. */
    uint32_t frame_vao_binds = nt_gfx_gl_test_vao_binds();
    nt_gfx_end_frame();
    /* Restore before any assert -- a failure longjmps past this line. */
    glad_glVertexAttribPointer = s_saved_attrib_pointer;
    /* The carried-over VAO dedups the first bind; one bind per vertex-input switch. */
    TEST_ASSERT_EQUAL_UINT32(2, frame_vao_binds);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_gl_test_static_attrib_pointer_calls());
    TEST_ASSERT_EQUAL_UINT32(1, nt_gfx_gl_test_instance_attrib_pointer_calls());
    TEST_ASSERT_EQUAL_UINT32(1, s_real_attrib_pointer_calls); /* just the instance re-point */
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

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_a);
    nt_gfx_draw_indexed(0, 3, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());

    /* All three data-op flavors on other index buffers while A's vertex input
     * stays bound: update, orphan, and creation-with-data. */
    nt_gfx_update_buffer(ibo_b, 0, s_degenerate_indices, sizeof(s_degenerate_indices));
    nt_gfx_orphan_buffer(ibo_b, s_degenerate_indices, sizeof(s_degenerate_indices));
    nt_buffer_t ibo_c = nt_gfx_make_buffer(
        &(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = s_degenerate_indices, .size = sizeof(s_degenerate_indices), .index_type = NT_INDEX_UINT16});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, ibo_c.id);

    nt_gfx_end_pass();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_a);
    nt_gfx_draw_indexed(0, 3, 3); /* still A's triangle indices, not B */
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* orphan_buffer keeps the GL buffer name, so the baked VAO attachment stays
 * live -- the per-flush orphaning pattern sprite/text rely on. */
static void test_orphan_under_live_vertex_input_renders(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .data = s_empty, .size = sizeof(s_empty)});
    nt_vertex_input_t vi = make_vi(vbo, (nt_buffer_t){0});

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_orphan_buffer(vbo, s_full, sizeof(s_full));
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());
    nt_gfx_end_pass();

    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_orphan_buffer(vbo, s_empty, sizeof(s_empty));
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 0, center_red());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* A rejected pipeline bind clears the program selection but must not disturb
 * the orthogonal vertex-input binding. */
static void test_rejected_pipeline_bind_preserves_vertex_input(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_program_t doomed = nt_gfx_make_program(nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vs_src}),
                                              nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src}));
    nt_pipeline_t stale = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = doomed});
    nt_gfx_destroy_program(doomed);
    TEST_ASSERT_FALSE(nt_gfx_pipeline_valid(stale));

    nt_vertex_input_t vi_full = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());

    nt_gfx_bind_pipeline(stale); /* rejected: drops the pipeline only */

    nt_gfx_bind_pipeline(pip);
    nt_gfx_draw(0, 3); /* no vertex-input re-bind needed */
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

/* Creation must restore the previously bound VAO (creation binds its own). */
static void test_creating_vertex_input_preserves_bound_one(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_vertex_input_t vi_full = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    GLint bound_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &bound_vao);
    TEST_ASSERT_NOT_EQUAL_INT(0, bound_vao);

    nt_vertex_input_t other = make_vi(make_vbo(s_empty), (nt_buffer_t){0});
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(other));
    GLint current_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current_vao);
    TEST_ASSERT_EQUAL_INT(bound_vao, current_vao);

    nt_gfx_draw(0, 3);
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
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
    nt_pipeline_t pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_make_program(vs, fs)});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void GLAD_API_PTR fail_gen_vertex_arrays(GLsizei count, GLuint *arrays) {
    for (GLsizei i = 0; i < count; i++) {
        arrays[i] = 0;
    }
}

static void test_failed_vao_creation_returns_invalid_and_preserves_binding(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_buffer_t full = make_vbo(s_full);
    nt_vertex_input_t vi_full = make_vi(full, (nt_buffer_t){0});

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    GLint bound_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &bound_vao);

    PFNGLGENVERTEXARRAYSPROC saved_gen = glad_glGenVertexArrays;
    glad_glGenVertexArrays = fail_gen_vertex_arrays;
    nt_vertex_input_t failed = make_vi(full, (nt_buffer_t){0});
    glad_glGenVertexArrays = saved_gen;
    TEST_ASSERT_EQUAL_UINT32(0, failed.id);
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());

    GLint current_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current_vao);
    TEST_ASSERT_EQUAL_INT(bound_vao, current_vao);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());

    /* The pool slot went back, so the retry succeeds. */
    nt_vertex_input_t retry = make_vi(full, (nt_buffer_t){0});
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(retry));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
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

/* A compressed create that fails after binding its own texture uploads on the
 * scratch unit, so slot 0 still holds A in GL and in the cache: re-binding A
 * costs nothing and a draw still samples A. */
static void test_failed_compressed_upload_keeps_texture_cache_truthful(void) {
    static const uint8_t pixel[4] = {255, 0, 0, 255};
    nt_texture_t tex_a = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 1,
        .height = 1,
        .data = pixel,
        .format = NT_TEXTURE_FORMAT_RGBA8,
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex_a.id);

    nt_gfx_bind_texture(tex_a, 0);
    GLint name_a = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &name_a);
    TEST_ASSERT_NOT_EQUAL_INT(0, name_a);

    static const uint8_t fake_basis[8] = {0};
    uint32_t failed = nt_gfx_backend_create_texture_compressed(fake_basis, sizeof(fake_basis), 4, 4, 1, NT_FILTER_NEAREST, NT_FILTER_NEAREST, NT_WRAP_CLAMP_TO_EDGE, NT_WRAP_CLAMP_TO_EDGE,
                                                               (uint32_t)NT_BASISU_FORMAT_RGBA32);
    TEST_ASSERT_EQUAL_UINT32(0, failed);

    install_state_counters();
    nt_gfx_bind_texture(tex_a, 0);
    remove_state_counters();

    GLint bound = texture_name_on_unit(0);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.bind_texture);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.active_texture);
    TEST_ASSERT_EQUAL_INT(name_a, bound);
}

/* Ground state is real GL calls, so scissor left enabled by a previous gfx
 * lifetime cannot survive into the next one on the same native context. */
static void test_ground_state_disables_scissor(void) {
    nt_gfx_set_scissor_enabled(true);
    TEST_ASSERT_EQUAL_INT(GL_TRUE, (int)glIsEnabled(GL_SCISSOR_TEST));

    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);

    TEST_ASSERT_EQUAL_INT(GL_FALSE, (int)glIsEnabled(GL_SCISSOR_TEST));
    TEST_ASSERT_FALSE(nt_gfx_scissor_enabled());
}

/* The cache survives end_frame, so a frame that repeats the previous one reaches
 * GL with no state calls at all. */
static void test_identical_second_frame_issues_no_bind_calls(void) {
    static const uint8_t pixel[4] = {255, 255, 255, 255};
    nt_pipeline_t pip = make_pipeline_ex(s_vs_src, s_fs_src, false, true, false);
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    nt_texture_t tex = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = pixel, .format = NT_TEXTURE_FORMAT_RGBA8});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex.id);

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_bind_texture(tex, 0);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    install_state_counters();
    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_bind_texture(tex, 0);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    remove_state_counters();
    /* Zero calls only counts if the frame still rendered what the first one did. */
    uint8_t red = center_red();
    nt_gfx_end_frame();

    TEST_ASSERT_UINT8_WITHIN(1, 255, red);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.use_program);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.bind_vao);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.depth_mask);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.enable);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.disable);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.blend_func_separate);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.active_texture);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.bind_texture);
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

    nt_gfx_begin_frame();
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
    nt_gfx_end_frame();

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

    nt_gfx_begin_frame();
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
    uint32_t depth_mask_through_far_bind = s_gl_calls.depth_mask;
    uint8_t px[4] = {0, 0, 0, 0};
    bool read_ok = nt_gfx_read_pixels(8, 8, 1, 1, px, sizeof(px));

    nt_gfx_bind_pipeline(no_write_pip);
    remove_state_counters();
    GLboolean depth_write_enabled = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write_enabled);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

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

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_begin_frame();
    begin_black_pass();
    install_state_counters();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    remove_state_counters();
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

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

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(blend_pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_INT(GL_TRUE, (int)glIsEnabled(GL_BLEND));

    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
    TEST_ASSERT_EQUAL_INT(GL_FALSE, (int)glIsEnabled(GL_BLEND));

    nt_pipeline_t opaque_pip = make_pipeline_ex(s_vs_src, s_fs_src, false, true, false);
    nt_pipeline_t alpha_pip = make_pipeline_ex(s_vs_src, s_fs_src, false, true, true);
    nt_vertex_input_t fresh_vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    install_state_counters();
    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(opaque_pip);
    uint32_t disable_blend_after_opaque = s_gl_calls.disable_blend;
    nt_gfx_bind_pipeline(alpha_pip);
    nt_gfx_bind_vertex_input(fresh_vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    remove_state_counters();
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(0, disable_blend_after_opaque);
    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.enable_blend);
}

/* The viewport is cached like any other GL state: a repeated rect costs nothing,
 * a real change and a framebuffer resize both reach GL. */
static void test_viewport_dedup_and_resize(void) {
    nt_gfx_begin_frame();
    begin_black_pass();
    install_state_counters();
    nt_gfx_set_viewport(0, 0, 8, 8);
    nt_gfx_set_viewport(0, 0, 8, 8);
    uint32_t after_same_rect = s_gl_calls.viewport;
    nt_gfx_set_viewport(0, 0, 4, 4);
    uint32_t after_new_rect = s_gl_calls.viewport;
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    const uint32_t saved_fb_width = g_nt_window.fb_width;
    g_nt_window.fb_width = saved_fb_width + 1;
    nt_gfx_begin_frame();
    begin_black_pass();
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
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
    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_end_pass();

    install_state_counters();
    begin_black_pass();
    nt_gfx_end_pass();
    uint32_t color_after_identical_pass = s_gl_calls.clear_color;
    uint32_t depth_after_identical_pass = s_gl_calls.clear_depth;

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {1.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_end_pass();
    remove_state_counters();
    nt_gfx_end_frame();

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

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);

    install_state_counters();
    nt_gfx_begin_frame();
    begin_black_pass();
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    remove_state_counters();

    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.viewport);
    TEST_ASSERT_EQUAL_INT((int)g_nt_window.fb_width, viewport[2]);
    TEST_ASSERT_EQUAL_INT((int)g_nt_window.fb_height, viewport[3]);
}

/* Uniform values are program state: the front-end names the bound pipeline's
 * program on every write, so two programs keep their own value. */
static void test_uniform_write_targets_bound_pipelines_program(void) {
    static const char *vs_src = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    static const char *fs_src = "precision mediump float;\n"
                                "uniform vec4 u_x;\n"
                                "out vec4 frag_color;\n"
                                "void main() { frag_color = u_x; }\n";
    const float value_a[4] = {1.0F, 2.0F, 3.0F, 4.0F};
    const float value_b[4] = {5.0F, 6.0F, 7.0F, 8.0F};

    nt_pipeline_t pip_a = make_pipeline_ex(vs_src, fs_src, false, true, false);
    nt_pipeline_t pip_b = make_pipeline_ex(vs_src, fs_src, false, true, false);

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip_a);
    GLint program_a = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program_a);
    nt_gfx_set_uniform_vec4(nt_hash32_str("u_x"), value_a);

    nt_gfx_bind_pipeline(pip_b);
    GLint program_b = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program_b);
    nt_gfx_set_uniform_vec4(nt_hash32_str("u_x"), value_b);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_NOT_EQUAL_INT(0, program_a);
    TEST_ASSERT_NOT_EQUAL_INT(program_a, program_b);

    GLint location_a = glGetUniformLocation((GLuint)program_a, "u_x");
    GLint location_b = glGetUniformLocation((GLuint)program_b, "u_x");
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, location_a);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, location_b);

    GLfloat read_a[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    GLfloat read_b[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    glGetUniformfv((GLuint)program_a, location_a, read_a);
    glGetUniformfv((GLuint)program_b, location_b, read_b);
    for (uint32_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT32((int32_t)value_a[i], (int32_t)read_a[i]);
        TEST_ASSERT_EQUAL_INT32((int32_t)value_b[i], (int32_t)read_b[i]);
    }
}

/* A raw backend recreate grounds the cache against the fresh context: state the
 * old lifetime left on is off, and the first bind that wants it says so again. */
static void test_recreate_all_resources_grounds_cache(void) {
    nt_pipeline_t blend_pip = make_pipeline_ex(s_vs_src, s_fs_src, false, true, true);
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(blend_pip);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_INT(GL_TRUE, (int)glIsEnabled(GL_BLEND));

    TEST_ASSERT_TRUE(nt_gfx_backend_recreate_all_resources());
    TEST_ASSERT_EQUAL_INT(GL_FALSE, (int)glIsEnabled(GL_BLEND));

    /* The zeroed backend tables orphan every old handle, so the frame that
     * follows a recreate has to build its own. */
    nt_pipeline_t fresh_blend = make_pipeline_ex(s_vs_src, s_fs_src, false, true, true);

    /* After the recreate: it reloads glad and would drop the swapped pointers. */
    install_state_counters();
    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(fresh_blend);
    nt_gfx_end_pass();
    remove_state_counters();
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.enable_blend);
}

/* Destroying the bound vertex input clears the cached VAO name: a new VAO that
 * reuses the deleted GL name must still reach glBindVertexArray. */
static void test_gl_name_reuse_after_destroying_bound_vertex_input(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_buffer_t vbo = make_vbo(s_full);
    nt_vertex_input_t vi_a = make_vi(vbo, (nt_buffer_t){0});

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_a);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());
    nt_gfx_destroy_vertex_input(vi_a); /* destroyed while bound */
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_vertex_input_t vi_b = make_vi(vbo, (nt_buffer_t){0});
    TEST_ASSERT_TRUE(nt_gfx_vertex_input_valid(vi_b));

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_b);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static nt_texture_t make_pixel_texture(const uint8_t pixel[4]) { return nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = pixel, .format = NT_TEXTURE_FORMAT_RGBA8}); }

/* Creation leaves its own texture bound on the active unit. */
static GLint current_texture_name(void) {
    GLint name = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &name);
    return name;
}

/* Same for textures: destroy clears the cache slot, so a new texture reusing the
 * deleted GL name is not mistaken for the one still recorded there. */
static void test_gl_name_reuse_after_destroying_bound_texture(void) {
    static const uint8_t white[4] = {255, 255, 255, 255};
    static const uint8_t grey[4] = {128, 128, 128, 255};
    nt_texture_t tex_a = make_pixel_texture(white);
    nt_texture_t keep = make_pixel_texture(grey);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex_a.id);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, keep.id);

    nt_gfx_bind_texture(tex_a, 0);
    nt_gfx_bind_texture(keep, 1);

    nt_gfx_destroy_texture(tex_a);
    nt_texture_t tex_b = make_pixel_texture(white);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, tex_b.id);
    GLint name_b = current_texture_name();
    TEST_ASSERT_NOT_EQUAL_INT(0, name_b);

    nt_gfx_bind_texture(tex_b, 0);
    TEST_ASSERT_EQUAL_INT(name_b, current_texture_name());
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
}

/* update_texture uploads on the scratch unit, so the texture sampling slot 0
 * holds stays bound there and re-binding it mid-pass costs no GL call. */
static void test_update_texture_mid_pass_leaves_sampling_slot_bound(void) {
    static const uint8_t white[4] = {255, 255, 255, 255};
    static const uint8_t grey[4] = {128, 128, 128, 255};
    nt_texture_t tex_1 = make_pixel_texture(white);
    GLint name_1 = current_texture_name();
    nt_texture_t tex_2 = make_pixel_texture(grey);
    GLint name_2 = current_texture_name();
    TEST_ASSERT_NOT_EQUAL_INT(0, name_1);
    TEST_ASSERT_NOT_EQUAL_INT(name_1, name_2);

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_texture(tex_1, 0);
    nt_gfx_update_texture(tex_2, 0, 0, 1, 1, white);

    GLint upload_unit = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &upload_unit);
    GLint upload_bound = current_texture_name();
    GLint slot0 = texture_name_on_unit(0);

    install_state_counters();
    nt_gfx_bind_texture(tex_1, 0);
    remove_state_counters();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

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

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_texture(tex_1, 0);

    install_state_counters();
    nt_gfx_update_texture(tex_2, 0, 0, 1, 1, white);
    nt_gfx_update_texture(tex_3, 0, 0, 1, 1, white);
    nt_gfx_update_texture(tex_2, 0, 0, 1, 1, grey);
    remove_state_counters();
    uint32_t burst_active = s_gl_calls.active_texture;
    uint32_t burst_bind = s_gl_calls.bind_texture;

    install_state_counters();
    nt_gfx_bind_texture(tex_1, 0);
    remove_state_counters();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(1, burst_active);
    TEST_ASSERT_EQUAL_UINT32(3, burst_bind);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.bind_texture);
    TEST_ASSERT_EQUAL_UINT32(0, s_gl_calls.active_texture);
}

/* Destroying the current program clears the cached name: a relink that reuses it
 * must still reach glUseProgram, now that the cache outlives the frame. */
static void test_destroy_current_program_then_relink_reissues_use_program(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src});
    nt_program_t prog_p = nt_gfx_make_program(vs, fs);
    nt_pipeline_t pip_p = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog_p});
    nt_vertex_input_t vi = make_vi(make_vbo(s_full), (nt_buffer_t){0});

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip_p);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_destroy_program(prog_p); /* cascades into pip_p */
    nt_program_t prog_q = nt_gfx_make_program(vs, fs);
    nt_pipeline_t pip_q = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = prog_q});
    TEST_ASSERT_TRUE(nt_gfx_pipeline_valid(pip_q));

    nt_gfx_begin_frame();
    begin_black_pass();
    install_state_counters();
    nt_gfx_bind_pipeline(pip_q);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    remove_state_counters();
    uint8_t red = center_red();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    TEST_ASSERT_EQUAL_UINT32(1, s_gl_calls.use_program);
    TEST_ASSERT_UINT8_WITHIN(1, 255, red);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_vertex_inputs_alternate_under_one_pipeline);
    RUN_TEST(test_second_frame_issues_no_static_attrib_pointers);
    RUN_TEST(test_index_data_ops_do_not_rewire_bound_vertex_input);
    RUN_TEST(test_orphan_under_live_vertex_input_renders);
    RUN_TEST(test_empty_vertex_input_draws_fullscreen);
    RUN_TEST(test_rejected_pipeline_bind_preserves_vertex_input);
    RUN_TEST(test_creating_vertex_input_preserves_bound_one);
    RUN_TEST(test_failed_vao_creation_returns_invalid_and_preserves_binding);
    RUN_TEST(test_failed_compressed_upload_keeps_texture_cache_truthful);
    RUN_TEST(test_ground_state_disables_scissor);
    RUN_TEST(test_identical_second_frame_issues_no_bind_calls);
    RUN_TEST(test_state_change_mid_frame_still_emits);
    RUN_TEST(test_pass_clear_after_depth_write_off);
    RUN_TEST(test_same_pipeline_across_passes_rebinds_for_free);
    RUN_TEST(test_ground_state_after_reinit);
    RUN_TEST(test_viewport_dedup_and_resize);
    RUN_TEST(test_clear_values_dedup);
    RUN_TEST(test_ground_state_viewport_reissued_after_reinit);
    RUN_TEST(test_uniform_write_targets_bound_pipelines_program);
    RUN_TEST(test_recreate_all_resources_grounds_cache);
    RUN_TEST(test_gl_name_reuse_after_destroying_bound_vertex_input);
    RUN_TEST(test_gl_name_reuse_after_destroying_bound_texture);
    RUN_TEST(test_update_texture_mid_pass_leaves_sampling_slot_bound);
    RUN_TEST(test_upload_burst_costs_one_active_texture_switch);
    RUN_TEST(test_destroy_current_program_then_relink_reissues_use_program);
    return UNITY_END();
}
