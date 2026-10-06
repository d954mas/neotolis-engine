/* Real-GL coverage for frame storage: absolute uint32 indices over mixed strides,
 * uniform blocks allocated after the first pass, and delta uploads mid-frame. */

#include "graphics/nt_gfx.h"
#include "unity.h"
#include "window/nt_window.h"

#include <stdint.h>
#include <string.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

static const char *s_vs_src = "precision mediump float;\n"
                              "layout(location = 0) in vec2 a_position;\n"
                              "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";

static const char *s_fs_red_src = "precision mediump float;\n"
                                  "out vec4 frag_color;\n"
                                  "void main() { frag_color = vec4(1.0, 0.0, 0.0, 1.0); }\n";

static const char *s_fs_green_src = "precision mediump float;\n"
                                    "out vec4 frag_color;\n"
                                    "void main() { frag_color = vec4(0.0, 1.0, 0.0, 1.0); }\n";

static const char *s_vertexid_vs_src = "precision mediump float;\n"
                                       "void main() {\n"
                                       "    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
                                       "    gl_Position = vec4(p, 0.0, 1.0);\n"
                                       "}\n";

static const char *s_fs_block_src = "precision mediump float;\n"
                                    "layout(std140) uniform Color { vec4 color; };\n"
                                    "out vec4 frag_color;\n"
                                    "void main() { frag_color = color; }\n";

/* Left and right triangles: at mid height, 3/16 of the width is inside the left one, 12/16 inside the right. */
static const float s_left[6] = {-1.0F, -1.0F, -0.1F, -1.0F, -1.0F, 3.0F};
static const float s_right[6] = {1.0F, -1.0F, 0.1F, -1.0F, 1.0F, 3.0F};

void setUp(void) {
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.frame_capacity[NT_GFX_FRAME_VERTEX] = 1024U * 1024U;
    desc.frame_capacity[NT_GFX_FRAME_INDEX] = 64U * 1024U;
    desc.frame_capacity[NT_GFX_FRAME_UNIFORM] = 4096;
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

void tearDown(void) { nt_gfx_shutdown(); }

static nt_pipeline_t make_pipeline(const char *vs_src, const char *fs_src) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vs_src});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fs_src});
    return nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_make_program(vs, fs)});
}

/* A position at offset 0 of each stride-sized vertex, over the frame vertex and index storage. */
static nt_vertex_input_t make_frame_input(uint16_t stride) {
    return nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = {.attr_count = 1, .stride = stride, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 2}}},
        .vertex_buffer = nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX),
        .index_buffer = nt_gfx_frame_buffer(NT_GFX_FRAME_INDEX),
    });
}

/* Allocates count zeroed vertices of stride and writes the triangle into the last three;
 * returns the absolute index of the first of them. */
static uint32_t alloc_triangle(uint16_t stride, uint32_t count, const float triangle[6]) {
    uint32_t offset = 0;
    uint8_t *vertices = (uint8_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, count * stride, stride, &offset);
    memset(vertices, 0, (size_t)count * stride);
    for (uint32_t v = 0; v < 3; v++) {
        memcpy(vertices + ((size_t)(count - 3U + v) * stride), triangle + ((size_t)v * 2U), 2U * sizeof(float));
    }
    return (offset / stride) + count - 3U;
}

/* Returns the first index of three absolute indices first_vertex..first_vertex+2. */
static uint32_t alloc_indices(uint32_t first_vertex) {
    uint32_t offset = 0;
    uint32_t *indices = (uint32_t *)nt_gfx_frame_alloc(NT_GFX_FRAME_INDEX, 3U * sizeof(uint32_t), 4, &offset);
    for (uint32_t i = 0; i < 3; i++) {
        indices[i] = first_vertex + i;
    }
    return offset / 4U;
}

static void read_pixel(int x, int y, uint8_t out[4]) { TEST_ASSERT_TRUE(nt_gfx_read_pixels(x, y, 1, 1, out, 4)); }

static void assert_red_left_green_right(void) {
    uint8_t left[4] = {0};
    uint8_t right[4] = {0};
    read_pixel((int)(g_nt_window.fb_width * 3U / 16U), (int)(g_nt_window.fb_height / 2U), left);
    read_pixel((int)(g_nt_window.fb_width * 12U / 16U), (int)(g_nt_window.fb_height / 2U), right);
    TEST_ASSERT_EQUAL_UINT8(255, left[0]);
    TEST_ASSERT_EQUAL_UINT8(0, left[1]);
    TEST_ASSERT_EQUAL_UINT8(0, right[0]);
    TEST_ASSERT_EQUAL_UINT8(255, right[1]);
}

static void begin_black_pass(void) { nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.0F, 0.0F, 0.0F, 1.0F}, .clear_depth = 1.0F}); }

static void test_absolute_uint32_indices_over_two_strides_in_one_frame(void) {
    const nt_pipeline_t red = make_pipeline(s_vs_src, s_fs_red_src);
    const nt_pipeline_t green = make_pipeline(s_vs_src, s_fs_green_src);
    const nt_vertex_input_t narrow = make_frame_input(12);
    const nt_vertex_input_t wide = make_frame_input(20);
    /* The left triangle sits past vertex 65535, so its indices need 32 bits. */
    const uint32_t left = alloc_triangle(12, 65541, s_left);
    TEST_ASSERT_GREATER_THAN_UINT32(65535, left);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, g_nt_gfx_frame_storage[NT_GFX_FRAME_VERTEX].used % 20U); /* the wide stride starts after padding */
    const uint32_t left_first = alloc_indices(left);
    const uint32_t right_first = alloc_indices(alloc_triangle(20, 3, s_right));

    begin_black_pass();
    nt_gfx_bind_pipeline(red);
    nt_gfx_bind_vertex_input(narrow);
    nt_gfx_draw_indexed(left_first, 3, 65541);
    nt_gfx_bind_pipeline(green);
    nt_gfx_bind_vertex_input(wide);
    nt_gfx_draw_indexed(right_first, 3, 3);
    assert_red_left_green_right();
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    nt_gfx_begin_frame();
}

static uint32_t alloc_color_block(const float color[4]) {
    uint32_t offset = 0;
    memcpy(nt_gfx_frame_alloc(NT_GFX_FRAME_UNIFORM, 4U * sizeof(float), g_nt_gfx.gpu_caps.uniform_buffer_offset_alignment, &offset), color, 4U * sizeof(float));
    return offset;
}

static void assert_center(uint8_t red, uint8_t green) {
    uint8_t pixel[4] = {0};
    read_pixel((int)(g_nt_window.fb_width / 2U), (int)(g_nt_window.fb_height / 2U), pixel);
    TEST_ASSERT_EQUAL_UINT8(red, pixel[0]);
    TEST_ASSERT_EQUAL_UINT8(green, pixel[1]);
}

/* The first pass executes (read_pixels) before the second block exists, so the
 * late block reaches the uniform buffer as a delta of a later execution. */
static void test_a_block_allocated_after_the_first_pass_reaches_a_later_draw(void) {
    static const float red[4] = {1.0F, 0.0F, 0.0F, 1.0F};
    static const float green[4] = {0.0F, 1.0F, 0.0F, 1.0F};
    nt_gfx_register_global_block("Color", 0);
    const nt_pipeline_t pipeline = make_pipeline(s_vertexid_vs_src, s_fs_block_src);
    const nt_vertex_input_t empty = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    const nt_buffer_t ubo = nt_gfx_frame_buffer(NT_GFX_FRAME_UNIFORM);

    const uint32_t first = alloc_color_block(red);
    begin_black_pass();
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(empty);
    nt_gfx_bind_uniform_buffer_range(ubo, 0, first, 4U * sizeof(float));
    nt_gfx_draw(0, 3);
    assert_center(255, 0);
    nt_gfx_end_pass();

    /* Produced while the frame is being drawn, as after UI layout. */
    const uint32_t late = alloc_color_block(green);
    begin_black_pass();
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(empty);
    nt_gfx_bind_uniform_buffer_range(ubo, 0, late, 4U * sizeof(float));
    nt_gfx_draw(0, 3);
    assert_center(0, 255);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    nt_gfx_begin_frame();
}

/* A buffer write executes the stream mid-frame; the next execution sends only the
 * new vertices and indices. */
static void test_a_mid_frame_execution_and_a_delta_upload_draw_both_halves(void) {
    const nt_pipeline_t red = make_pipeline(s_vs_src, s_fs_red_src);
    const nt_pipeline_t green = make_pipeline(s_vs_src, s_fs_green_src);
    const nt_vertex_input_t input = make_frame_input(12);
    const nt_buffer_t other = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 16});

    const uint32_t left_first = alloc_indices(alloc_triangle(12, 3, s_left));
    begin_black_pass();
    nt_gfx_bind_pipeline(red);
    nt_gfx_bind_vertex_input(input);
    nt_gfx_draw_indexed(left_first, 3, 3);
    nt_gfx_update_buffer(other, 0, (const uint8_t[16]){0}, 16);

    const uint32_t right_first = alloc_indices(alloc_triangle(12, 3, s_right));
    nt_gfx_bind_pipeline(green);
    nt_gfx_draw_indexed(right_first, 3, 3);
    assert_red_left_green_right();
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    nt_gfx_begin_frame();
}

/* An index upload runs while the last replayed draw's vertex input is bound; that
 * input keeps its own index buffer, so it draws the same triangle afterwards. */
static void test_an_index_upload_leaves_a_bound_input_with_its_own_index_buffer(void) {
    static const uint16_t indices[3] = {0, 1, 2};
    const nt_pipeline_t red = make_pipeline(s_vs_src, s_fs_red_src);
    const nt_pipeline_t green = make_pipeline(s_vs_src, s_fs_green_src);
    const nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_IMMUTABLE, .data = s_left, .size = sizeof(s_left)});
    const nt_buffer_t ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = indices, .size = sizeof(indices), .index_type = NT_INDEX_UINT16});
    const nt_vertex_input_t own = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = {.attr_count = 1, .stride = 8, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 2}}},
        .vertex_buffer = vbo,
        .index_buffer = ibo,
    });
    const nt_vertex_input_t frame = make_frame_input(12);
    const nt_buffer_t other = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 16});

    begin_black_pass();
    nt_gfx_bind_pipeline(red);
    nt_gfx_bind_vertex_input(own);
    nt_gfx_draw_indexed(0, 3, 3);
    nt_gfx_update_buffer(other, 0, (const uint8_t[16]){0}, 16); /* replays: `own` is the bound input */

    const uint32_t right_first = alloc_indices(alloc_triangle(12, 3, s_right));
    nt_gfx_bind_pipeline(green);
    nt_gfx_bind_vertex_input(frame);
    nt_gfx_draw_indexed(right_first, 3, 3);
    nt_gfx_end_pass();
    begin_black_pass();
    nt_gfx_bind_pipeline(red);
    nt_gfx_bind_vertex_input(own);
    nt_gfx_draw_indexed(0, 3, 3);
    uint8_t left[4] = {0};
    read_pixel((int)(g_nt_window.fb_width * 3U / 16U), (int)(g_nt_window.fb_height / 2U), left);
    TEST_ASSERT_EQUAL_UINT8(255, left[0]);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_EQUAL_UINT32(GL_NO_ERROR, glGetError());
    nt_gfx_begin_frame();
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
    RUN_TEST(test_absolute_uint32_indices_over_two_strides_in_one_frame);
    RUN_TEST(test_a_block_allocated_after_the_first_pass_reaches_a_later_draw);
    RUN_TEST(test_a_mid_frame_execution_and_a_delta_upload_draw_both_halves);
    RUN_TEST(test_an_index_upload_leaves_a_bound_input_with_its_own_index_buffer);
    int failures = UNITY_END();
    nt_window_shutdown();
    return failures;
}
