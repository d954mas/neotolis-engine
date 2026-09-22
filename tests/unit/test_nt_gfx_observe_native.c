#include "graphics/nt_gfx.h"
#include "unity.h"
#include "window/nt_window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

static PFNGLBUFFERDATAPROC s_buffer_data;
static PFNGLBUFFERSUBDATAPROC s_buffer_sub_data;
static PFNGLUSEPROGRAMPROC s_use_program;
static PFNGLBINDVERTEXARRAYPROC s_bind_vao;
static PFNGLUNIFORM4FVPROC s_uniform4fv;
static PFNGLBINDBUFFERBASEPROC s_bind_buffer_base;
static uint64_t s_buffer_calls;
static uint64_t s_buffer_bytes;
static uint32_t s_program_calls;
static uint32_t s_vao_calls;
static uint32_t s_uniform_calls;
static uint32_t s_ubo_calls;

static void GLAD_API_PTR count_buffer_data(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    if (data != NULL) {
        s_buffer_calls++;
        s_buffer_bytes += (uint64_t)size;
    }
    s_buffer_data(target, size, data, usage);
}

static void GLAD_API_PTR count_buffer_sub_data(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    if (data != NULL) {
        s_buffer_calls++;
        s_buffer_bytes += (uint64_t)size;
    }
    s_buffer_sub_data(target, offset, size, data);
}

static void GLAD_API_PTR count_use_program(GLuint program) {
    s_program_calls++;
    s_use_program(program);
}

static void GLAD_API_PTR count_bind_vao(GLuint vao) {
    s_vao_calls++;
    s_bind_vao(vao);
}

static void GLAD_API_PTR count_uniform4fv(GLint location, GLsizei count, const GLfloat *value) {
    s_uniform_calls++;
    s_uniform4fv(location, count, value);
}

static void GLAD_API_PTR count_bind_buffer_base(GLenum target, GLuint index, GLuint buffer) {
    s_ubo_calls++;
    s_bind_buffer_base(target, index, buffer);
}

void setUp(void) {
    TEST_ASSERT_TRUE(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){.max_dpr = 1.0F, .width = 16, .height = 16};
    nt_window_init();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = 4096;
    nt_gfx_init(&desc);
    s_buffer_data = glad_glBufferData;
    s_buffer_sub_data = glad_glBufferSubData;
    s_use_program = glad_glUseProgram;
    s_bind_vao = glad_glBindVertexArray;
    s_uniform4fv = glad_glUniform4fv;
    s_bind_buffer_base = glad_glBindBufferBase;
    glad_glBufferData = count_buffer_data;
    glad_glBufferSubData = count_buffer_sub_data;
    glad_glUseProgram = count_use_program;
    glad_glBindVertexArray = count_bind_vao;
    glad_glUniform4fv = count_uniform4fv;
    glad_glBindBufferBase = count_bind_buffer_base;
    s_buffer_calls = s_buffer_bytes = 0;
    s_program_calls = s_vao_calls = s_uniform_calls = s_ubo_calls = 0;
}

void tearDown(void) {
    glad_glBufferData = s_buffer_data;
    glad_glBufferSubData = s_buffer_sub_data;
    glad_glUseProgram = s_use_program;
    glad_glBindVertexArray = s_bind_vao;
    glad_glUniform4fv = s_uniform4fv;
    glad_glBindBufferBase = s_bind_buffer_base;
    nt_gfx_shutdown();
    nt_window_shutdown();
}

#if NT_GFX_COUNTERS_ENABLED
#if NT_GFX_CAPTURE_ENABLED
static uint32_t captured_calls(nt_gfx_gl_call_t call) {
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    uint32_t count = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        if (capture.events[i].kind == NT_GFX_EVENT_BACKEND && capture.events[i].detail == (uint32_t)call) {
            count++;
        }
    }
    return count;
}
#endif
static void test_payloads_before_render_and_without_frames(void) {
    const uint8_t data[64] = {0};
    nt_gfx_upload_totals_t baseline = nt_gfx_upload_totals_read();
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = sizeof(data)});
    TEST_ASSERT_EQUAL_UINT64(0, nt_gfx_upload_totals_read().buffer_calls - baseline.buffer_calls);
    nt_gfx_update_buffer(buffer, 0, data, 16);
    TEST_ASSERT_EQUAL_UINT64(16, nt_gfx_upload_totals_read().buffer_bytes - baseline.buffer_bytes);

    nt_gfx_observe_begin_frame();
    nt_gfx_orphan_buffer(buffer, data, sizeof(data));
    nt_gfx_update_buffer(buffer, 8, data, 12);
    nt_gfx_counters_t live = nt_gfx_stats_read();
    TEST_ASSERT_EQUAL_UINT64(2, live.buffer_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(76, live.buffer_upload_bytes);
    TEST_ASSERT_EQUAL_UINT32(0, live.draw_calls);
    const nt_gfx_frame_snapshot_t *end = nt_gfx_observe_end_frame();
    TEST_ASSERT_EQUAL_UINT64(76, end->counters.buffer_upload_bytes);
    nt_gfx_upload_totals_t totals = nt_gfx_upload_totals_read();
    TEST_ASSERT_TRUE(totals.available);
    TEST_ASSERT_EQUAL_UINT64(s_buffer_calls, totals.buffer_calls - baseline.buffer_calls);
    TEST_ASSERT_EQUAL_UINT64(s_buffer_bytes, totals.buffer_bytes - baseline.buffer_bytes);
}

static void test_texture_mips_storage_and_subrect_payloads(void) {
    const uint8_t pixels[84] = {0};
    nt_gfx_observe_begin_frame();
    nt_texture_t storage = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .format = NT_TEXTURE_FORMAT_RGBA8});
    TEST_ASSERT_TRUE(storage.id != 0);
    TEST_ASSERT_EQUAL_UINT64(0, nt_gfx_stats_read().texture_upload_calls);
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .format = NT_TEXTURE_FORMAT_RGBA8, .level_count = 3, .data = pixels});
    TEST_ASSERT_TRUE(texture.id != 0);
    nt_gfx_update_texture(storage, 1, 1, 2, 3, pixels);
    nt_gfx_counters_t live = nt_gfx_stats_read();
    TEST_ASSERT_EQUAL_UINT64(4, live.texture_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(108, live.texture_upload_bytes);
    (void)nt_gfx_observe_end_frame();
}

static void test_repeated_frames_separate_requests_from_issued_calls(void) {
    const char *vs_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }";
    const char *fs_source = "precision mediump float; uniform vec4 u_color; out vec4 color; void main() { color = u_color; }";
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vs_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fs_source});
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_make_program(vs, fs)});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_buffer_t ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_UNIFORM, .usage = NT_USAGE_DYNAMIC, .size = 64});
    const float color[4] = {1.0F, 0.5F, 0.0F, 1.0F};
    nt_gfx_capture_set_enabled(true);
    for (uint32_t frame = 0; frame < 2; frame++) {
        s_program_calls = s_vao_calls = s_uniform_calls = s_ubo_calls = 0;
        nt_gfx_observe_begin_frame();
        nt_gfx_begin_frame();
        nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
        for (uint32_t repeat = 0; repeat < 2; repeat++) {
            nt_gfx_bind_pipeline(pipeline);
            nt_gfx_bind_vertex_input(vi);
            nt_gfx_set_uniform_vec4(nt_hash32_str("u_color"), color);
            nt_gfx_bind_uniform_buffer(ubo, 0);
        }
        nt_gfx_set_uniform_vec4(nt_hash32_str("inactive"), color);
        nt_gfx_draw(0, 3);
        nt_gfx_end_pass();
        nt_gfx_end_frame();
        nt_gfx_counters_t c = nt_gfx_observe_end_frame()->counters;
        TEST_ASSERT_EQUAL_UINT32(2, c.pipeline_requests);
        TEST_ASSERT_EQUAL_UINT32(2, c.vertex_input_requests);
        TEST_ASSERT_EQUAL_UINT32(3, c.uniform_requests);
        TEST_ASSERT_EQUAL_UINT32(2, c.ubo_requests);
        TEST_ASSERT_EQUAL_UINT32(s_program_calls, c.program_calls);
        TEST_ASSERT_EQUAL_UINT32(s_vao_calls, c.vao_calls);
        TEST_ASSERT_EQUAL_UINT32(s_uniform_calls, c.uniform_calls);
        TEST_ASSERT_EQUAL_UINT32(s_ubo_calls, c.ubo_calls);
        TEST_ASSERT_EQUAL_UINT32(frame == 0 ? 1 : 0, c.program_calls);
        TEST_ASSERT_EQUAL_UINT32(frame == 0 ? 1 : 0, c.uniform_calls);
        TEST_ASSERT_EQUAL_UINT32(2, c.ubo_calls);
#if NT_GFX_CAPTURE_ENABLED
        TEST_ASSERT_EQUAL_UINT32(c.program_calls, captured_calls(NT_GFX_GL_USEPROGRAM));
        TEST_ASSERT_EQUAL_UINT32(c.vao_calls, captured_calls(NT_GFX_GL_BINDVERTEXARRAY));
        TEST_ASSERT_EQUAL_UINT32(c.uniform_calls, captured_calls(NT_GFX_GL_UNIFORM4FV));
        TEST_ASSERT_EQUAL_UINT32(c.ubo_calls, captured_calls(NT_GFX_GL_BINDBUFFERBASE));
#endif
    }
}

static void test_runtime_policy_changes_at_next_boundary(void) {
    const uint8_t data[16] = {0};
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = sizeof(data)});
    uint64_t epoch = nt_gfx_upload_totals_read().epoch;
    nt_gfx_observe_begin_frame();
    nt_gfx_stats_set_enabled(false);
    nt_gfx_update_buffer(buffer, 0, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT64(16, nt_gfx_observe_end_frame()->counters.buffer_upload_bytes);
    nt_gfx_observe_begin_frame();
    nt_gfx_update_buffer(buffer, 0, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_stats_read().availability);
    nt_gfx_stats_set_enabled(true);
    (void)nt_gfx_observe_end_frame();
    nt_gfx_observe_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(epoch + 1, nt_gfx_upload_totals_read().epoch);
    TEST_ASSERT_EQUAL_UINT64(0, nt_gfx_upload_totals_read().buffer_bytes);
    nt_gfx_update_buffer(buffer, 0, data, sizeof(data));
    TEST_ASSERT_EQUAL_UINT64(16, nt_gfx_observe_end_frame()->counters.buffer_upload_bytes);
}
#else
static void test_compiled_off_is_unavailable(void) { TEST_ASSERT_FALSE(nt_gfx_upload_totals_read().available); }
#endif

int main(void) {
    UNITY_BEGIN();
#if NT_GFX_COUNTERS_ENABLED
    RUN_TEST(test_payloads_before_render_and_without_frames);
    RUN_TEST(test_texture_mips_storage_and_subrect_payloads);
    RUN_TEST(test_repeated_frames_separate_requests_from_issued_calls);
    RUN_TEST(test_runtime_policy_changes_at_next_boundary);
#else
    RUN_TEST(test_compiled_off_is_unavailable);
#endif
    return UNITY_END();
}
