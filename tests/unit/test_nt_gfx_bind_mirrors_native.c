/* A stale pipeline mirror lets the next buffer bind corrupt the live VAO.
 * Real-GL pixels verify that a rejected bind preserves its vertex input. */

#include "graphics/nt_gfx.h"
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

void setUp(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){.max_dpr = 1.0F, .resizable = false, .width = 16, .height = 16};
    nt_window_init();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.max_pipelines = 2;
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

static uint8_t center_red(void) {
    uint8_t px[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(8, 8, 1, 1, px, sizeof(px)));
    return px[0];
}

static void test_rejected_bind_does_not_rewire_the_previous_vao(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src});
    nt_program_t prog = nt_gfx_make_program(vs, fs);
    nt_pipeline_desc_t desc = {
        .program = prog,
        .layout = {.attr_count = 1, .stride = 8, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 2}}},
    };
    nt_pipeline_t pip = nt_gfx_make_pipeline(&desc);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, pip.id);

    /* A second pipeline whose program is then destroyed: the cascade takes the
     * pipeline with it, so this handle is stale and its bind gets rejected. */
    nt_program_t doomed = nt_gfx_make_program(nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vs_src}),
                                              nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src}));
    nt_pipeline_desc_t doomed_desc = desc;
    doomed_desc.program = doomed;
    nt_pipeline_t stale = nt_gfx_make_pipeline(&doomed_desc);
    nt_gfx_destroy_program(doomed);
    TEST_ASSERT_FALSE(nt_gfx_pipeline_valid(stale));

    nt_buffer_t full = make_vbo(s_full);
    nt_buffer_t empty = make_vbo(s_empty);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});

    /* Baseline: the pipeline's VAO reads the full-screen triangle. */
    nt_gfx_bind_pipeline(pip);
    nt_gfx_bind_vertex_buffer(full);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());

    /* Rejected bind, then a different buffer. A stale slot mirror would rewire
     * the live VAO's attributes onto the degenerate triangle here. */
    nt_gfx_bind_pipeline(stale);
    nt_gfx_bind_vertex_buffer(empty);

    /* Re-bind and draw WITHOUT re-binding the full buffer: the VAO must still
     * hold the attributes it had, so the full-screen triangle covers again. */
    nt_gfx_end_pass();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pip);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());

    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

static void test_creating_pipeline_preserves_bound_pipeline(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src});
    nt_pipeline_desc_t desc = {
        .program = nt_gfx_make_program(vs, fs),
        .layout = {.attr_count = 1, .stride = 16, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 2}}},
    };
    nt_pipeline_t first = nt_gfx_make_pipeline(&desc);
    static const float vertices[12] = {-1.0F, -1.0F, 0.0F, 0.0F, 3.0F, -1.0F, 0.0F, 0.0F, -1.0F, 3.0F, 0.0F, 0.0F};
    nt_buffer_t full = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_IMMUTABLE,
        .size = sizeof(vertices),
        .data = vertices,
    });

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(first);
    nt_gfx_bind_vertex_buffer(full);
    GLint first_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &first_vao);
    TEST_ASSERT_NOT_EQUAL_INT(0, first_vao);

    desc.layout.attrs[0].offset = 8;
    nt_pipeline_t second = nt_gfx_make_pipeline(&desc);
    TEST_ASSERT_TRUE(nt_gfx_pipeline_valid(second));
    GLint current_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current_vao);
    TEST_ASSERT_EQUAL_INT(first_vao, current_vao);
    nt_gfx_bind_vertex_buffer(full);
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

static void test_failed_vao_creation_preserves_bound_pipeline_and_pool_slot(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_fs_src});
    nt_pipeline_desc_t desc = {
        .program = nt_gfx_make_program(vs, fs),
        .layout = {.attr_count = 1, .stride = 8, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 2}}},
    };
    nt_pipeline_t first = nt_gfx_make_pipeline(&desc);
    nt_buffer_t full = make_vbo(s_full);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(first);
    nt_gfx_bind_vertex_buffer(full);
    GLint first_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &first_vao);

    nt_pipeline_desc_t failed_desc = {.program = desc.program};
    PFNGLGENVERTEXARRAYSPROC saved_gen = glad_glGenVertexArrays;
    glad_glGenVertexArrays = fail_gen_vertex_arrays;
    nt_pipeline_t failed = nt_gfx_make_pipeline(&failed_desc);
    glad_glGenVertexArrays = saved_gen;
    TEST_ASSERT_EQUAL_UINT32(0, failed.id);
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    nt_gfx_draw(0, 3);
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());
    GLint current_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current_vao);
    TEST_ASSERT_EQUAL_INT(first_vao, current_vao);

    nt_pipeline_t retry = nt_gfx_make_pipeline(&desc);
    TEST_ASSERT_TRUE(nt_gfx_pipeline_valid(retry));
    nt_gfx_end_pass();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(retry);
    nt_gfx_bind_vertex_buffer(full);
    nt_gfx_draw(0, 3);
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    TEST_ASSERT_UINT8_WITHIN(1, 255, center_red());
    nt_gfx_end_pass();
    nt_gfx_end_frame();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rejected_bind_does_not_rewire_the_previous_vao);
    RUN_TEST(test_creating_pipeline_preserves_bound_pipeline);
    RUN_TEST(test_failed_vao_creation_preserves_bound_pipeline_and_pool_slot);
    return UNITY_END();
}
