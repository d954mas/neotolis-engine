/* Real-GL pixel tests for owned vertex inputs: alternating binds under one
 * pipeline, index-data ops that must not rewire the bound VAO, orphaning
 * under a live vertex input, and bind-preservation across rejected or
 * failed operations. */

#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"
#include "unity.h"
#include "window/nt_window.h"

#include <stddef.h>
#include <stdint.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

static const char *s_vs_src = "precision mediump float;\n"
                              "layout(location = 0) in vec2 a_position;\n"
                              "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";

static const char *s_fs_src = "precision mediump float;\n"
                              "out vec4 frag_color;\n"
                              "void main() { frag_color = vec4(1.0, 0.0, 0.0, 1.0); }\n";

/* Covers the whole viewport. */
static const float s_full[6] = {-1.0F, -1.0F, 3.0F, -1.0F, -1.0F, 3.0F};
/* Degenerate: every vertex on one point, so it rasterizes nothing. */
static const float s_empty[6] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};

static const uint16_t s_tri_indices[3] = {0, 1, 2};
static const uint16_t s_degenerate_indices[3] = {0, 0, 0};

void setUp(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){.max_dpr = 1.0F, .resizable = false, .width = 16, .height = 16};
    nt_window_init();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

void tearDown(void) {
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

static void begin_black_pass(void) { nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F}); }

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

/* Steady state: the static half of vertex-input setup is baked at creation, so
 * a whole frame of draws issues zero divisor-0 glVertexAttribPointer calls. */
static void test_second_frame_issues_no_static_attrib_pointers(void) {
    nt_pipeline_t pip = make_red_pipeline();
    nt_vertex_input_t vi_full = make_vi(make_vbo(s_full), (nt_buffer_t){0});
    nt_vertex_input_t vi_empty = make_vi(make_vbo(s_empty), (nt_buffer_t){0});

    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_gfx_gl_test_reset_counters();
    nt_gfx_begin_frame();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_input(vi_full);
    nt_gfx_draw(0, 3);
    nt_gfx_bind_vertex_input(vi_empty);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_gl_test_static_attrib_pointer_calls());
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

    /* Both data-op flavors on buffer B while A's vertex input stays bound. */
    nt_gfx_update_buffer(ibo_b, 0, s_degenerate_indices, sizeof(s_degenerate_indices));
    nt_gfx_orphan_buffer(ibo_b, s_degenerate_indices, sizeof(s_degenerate_indices));

    nt_gfx_end_pass();
    begin_black_pass();
    nt_gfx_bind_pipeline(pip);
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

    nt_gfx_end_pass();
    begin_black_pass();
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_vertex_inputs_alternate_under_one_pipeline);
    RUN_TEST(test_second_frame_issues_no_static_attrib_pointers);
    RUN_TEST(test_index_data_ops_do_not_rewire_bound_vertex_input);
    RUN_TEST(test_orphan_under_live_vertex_input_renders);
    RUN_TEST(test_rejected_pipeline_bind_preserves_vertex_input);
    RUN_TEST(test_creating_vertex_input_preserves_bound_one);
    RUN_TEST(test_failed_vao_creation_returns_invalid_and_preserves_binding);
    return UNITY_END();
}
