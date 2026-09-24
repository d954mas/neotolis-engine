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
static PFNGLTEXIMAGE2DPROC s_tex_image;
static PFNGLTEXSUBIMAGE2DPROC s_tex_sub_image;
static PFNGLGETERRORPROC s_get_error;
static PFNGLCOMPRESSEDTEXIMAGE2DPROC s_compressed_image;
static PFNGLVERTEXATTRIBPOINTERPROC s_attribute_pointer;
static uint32_t s_attribute_calls;
static uint64_t s_texture_calls;
static uint64_t s_texture_bytes;
static GLenum s_upload_error;
static GLenum s_pending_error;
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

static void GLAD_API_PTR count_tex_image(GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) {
    if (pixels != NULL && format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
        s_texture_calls++;
        s_texture_bytes += (uint64_t)(uint32_t)width * (uint32_t)height * 4;
    }
    s_tex_image(target, level, internal, width, height, border, format, type, pixels);
    s_pending_error = s_upload_error;
    s_upload_error = GL_NO_ERROR;
}

static void GLAD_API_PTR count_tex_sub_image(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) {
    if (pixels != NULL && format == GL_RGBA && type == GL_UNSIGNED_BYTE) {
        s_texture_calls++;
        s_texture_bytes += (uint64_t)(uint32_t)width * (uint32_t)height * 4;
    }
    s_tex_sub_image(target, level, x, y, width, height, format, type, pixels);
}

static GLenum GLAD_API_PTR injected_get_error(void) {
    if (s_pending_error != GL_NO_ERROR) {
        GLenum error = s_pending_error;
        s_pending_error = GL_NO_ERROR;
        return error;
    }
    return s_get_error();
}

static void GLAD_API_PTR count_compressed_image(GLenum target, GLint level, GLenum format, GLsizei width, GLsizei height, GLint border, GLsizei size, const void *data) {
    if (data != NULL) {
        s_texture_calls++;
        s_texture_bytes += (uint32_t)size;
    }
    s_compressed_image(target, level, format, width, height, border, size, data);
}

static void GLAD_API_PTR count_attribute_pointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
    s_attribute_calls++;
    s_attribute_pointer(index, size, type, normalized, stride, pointer);
}

void setUp(void) {
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
    s_tex_image = glad_glTexImage2D;
    s_tex_sub_image = glad_glTexSubImage2D;
    s_get_error = glad_glGetError;
    glad_glTexImage2D = count_tex_image;
    glad_glTexSubImage2D = count_tex_sub_image;
    glad_glGetError = injected_get_error;
    s_compressed_image = glad_glCompressedTexImage2D;
    s_attribute_pointer = glad_glVertexAttribPointer;
    glad_glCompressedTexImage2D = count_compressed_image;
    glad_glVertexAttribPointer = count_attribute_pointer;
    s_attribute_calls = 0;
    s_texture_calls = s_texture_bytes = 0;
    s_upload_error = s_pending_error = GL_NO_ERROR;
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
    glad_glTexImage2D = s_tex_image;
    glad_glTexSubImage2D = s_tex_sub_image;
    glad_glGetError = s_get_error;
    glad_glCompressedTexImage2D = s_compressed_image;
    glad_glVertexAttribPointer = s_attribute_pointer;
    nt_gfx_shutdown();
}

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- inspect the two identity layers before and after resize
static void test_capture_publishes_resize_mappings_and_skip_reasons(void) {
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.width = 8, .height = 4, .color_format = NT_TEXTURE_FORMAT_RGBA8});
    nt_texture_t color = nt_gfx_render_target_color(target);
    nt_gfx_capture_request();
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(nt_gfx_resize_render_target(target, 13, 7));
    nt_gfx_set_scissor_enabled(false);
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    /* Inherited definitions precede the resize; fresh ones follow it. */
    uint32_t resize = 0;
    while (resize < capture.count && !(capture.events[resize].kind == NT_GFX_EVENT_BEGIN && capture.events[resize].operation == NT_GFX_OP_RESIZE)) {
        resize++;
    }
    TEST_ASSERT_LESS_THAN_UINT32(capture.count, resize);
    uint32_t texture_slot = 0;
    uint32_t old_name = 0;
    for (uint32_t i = 0; i < resize; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_DEFINITION && event->object_kind == NT_GFX_OBJECT_TEXTURE && event->object == color.id) {
            texture_slot = event->data.resource.backend;
        }
    }
    TEST_ASSERT_NOT_EQUAL(0, texture_slot);
    for (uint32_t i = 0; i < resize; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_DEFINITION && event->operation == NT_GFX_OP_STATE && event->detail == NT_GFX_OBJECT_TEXTURE && event->data.backend.args[0] == texture_slot) {
            old_name = event->data.backend.args[1];
        }
    }
    TEST_ASSERT_NOT_EQUAL(0, old_name);
    bool dimensions = false;
    bool mapping = false;
    bool cache = false;
    for (uint32_t i = resize; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_DEFINITION && event->object_kind == NT_GFX_OBJECT_TEXTURE && event->object == color.id) {
            TEST_ASSERT_EQUAL_UINT32(13, event->data.resource.width);
            TEST_ASSERT_EQUAL_UINT32(7, event->data.resource.height);
            dimensions = true;
        }
        if (event->kind == NT_GFX_EVENT_DEFINITION && event->operation == NT_GFX_OP_STATE && event->detail == NT_GFX_OBJECT_TEXTURE && event->data.backend.args[0] == texture_slot) {
            TEST_ASSERT_NOT_EQUAL(old_name, event->data.backend.args[1]);
            TEST_ASSERT_NOT_EQUAL(0, event->data.backend.args[1]);
            mapping = true;
        }
        if (event->kind == NT_GFX_EVENT_RESULT && event->operation == NT_GFX_OP_SCISSOR_ENABLE) {
            TEST_ASSERT_EQUAL(NT_GFX_RESULT_CACHE, event->result);
            cache = true;
        }
    }
    TEST_ASSERT_TRUE(dimensions && mapping && cache);
}

static void test_new_program_defines_sampler_names_and_inactive_uniforms(void) {
    nt_gfx_capture_request();
    nt_gfx_begin_frame();
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){gl_Position=vec4(0.0);}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){
        .type = NT_SHADER_FRAGMENT, .source = "precision mediump float; uniform sampler2D a; uniform sampler2D b; out vec4 color; void main(){color=texture(a,vec2(0.0))+texture(b,vec2(0.0));}"});
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_make_program(vs, fs)});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    const float matrix[16] = {0};
    nt_hash32_t inactive = nt_hash32_str("inactive");
    nt_gfx_set_uniform_mat4(inactive, matrix);
    nt_gfx_set_uniform_float(inactive, 1.0F);
    nt_gfx_set_uniform_int(inactive, 1);
    nt_gfx_end_pass();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    uint32_t names = 0;
    uint32_t units = 0;
    uint32_t skips = 0;
    uint32_t pipeline_states = 0;
    nt_gfx_operation_t stack[16] = {0};
    uint32_t depth = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_INITIAL && event->operation == NT_GFX_OP_SAMPLER) {
            if (event->data.backend.args[1] == nt_hash32_str("a").value) {
                names |= 1;
            }
            if (event->data.backend.args[1] == nt_hash32_str("b").value) {
                names |= 2;
            }
            units |= 1U << event->data.backend.args[3];
        }
        if (event->kind == NT_GFX_EVENT_SKIP && event->result == NT_GFX_RESULT_INACTIVE) {
            skips++;
        }
        /* Pipeline state is defined once, by the backend, in its slots and enums. */
        if (event->kind == NT_GFX_EVENT_DEFINITION && event->operation == NT_GFX_OP_PIPELINE) {
            TEST_ASSERT_EQUAL(NT_GFX_OBJECT_NONE, event->object_kind);
            pipeline_states++;
        }
        if (event->kind == NT_GFX_EVENT_BEGIN) {
            TEST_ASSERT_LESS_THAN_UINT32(16, depth);
            stack[depth++] = event->operation;
        } else if (event->kind == NT_GFX_EVENT_RESULT) {
            TEST_ASSERT_GREATER_THAN_UINT32(0, depth);
            TEST_ASSERT_EQUAL(stack[--depth], event->operation);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(0, depth);
    TEST_ASSERT_EQUAL_UINT32(3, names);
    TEST_ASSERT_EQUAL_UINT32(3, units);
    TEST_ASSERT_EQUAL_UINT32(3, skips);
    TEST_ASSERT_EQUAL_UINT32(1, pipeline_states);
    TEST_ASSERT_FALSE(capture.overflow);
}

static void test_initial_uniform_records_cover_only_vec4(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "uniform mat4 m; uniform float f; void main(){gl_Position=m*vec4(f);}"});
    nt_shader_t fs = nt_gfx_make_shader(
        &(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "precision mediump float; uniform vec4 tint; uniform int mode; out vec4 color; void main(){color=tint*float(mode);}"});
    nt_program_t program = nt_gfx_make_program(vs, fs);
    TEST_ASSERT_NOT_EQUAL(0, program.id);
    nt_gfx_capture_request();
    nt_gfx_begin_frame();
    nt_gfx_begin_frame(); /* a recorded frame without gfx work still snapshots inherited state */
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    uint32_t records = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_INITIAL && event->operation == NT_GFX_OP_UNIFORM_VEC4) {
            TEST_ASSERT_EQUAL_UINT32(nt_hash32_str("tint").value, event->data.backend.args[1]);
            records++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(1, records);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one walk checks three argument kinds
static void test_issued_calls_record_floats_names_and_payloads(void) {
    const uint8_t data[16] = {0};
    nt_gfx_capture_request();
    nt_gfx_begin_frame();
    (void)nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = sizeof(data), .data = data});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.25F, 0.5F, 0.75F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    uint32_t generated = 0;
    bool uploaded = false;
    bool cleared = false;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind != NT_GFX_EVENT_BACKEND) {
            continue;
        }
        if (event->detail == NT_GFX_GL_glGenBuffers) {
            TEST_ASSERT_EQUAL_UINT32(1, event->data.backend.args[0]);
            generated = event->data.backend.args[1];
        }
        if (event->detail == NT_GFX_GL_glBufferData) {
            TEST_ASSERT_EQUAL_UINT32(sizeof(data), event->data.backend.args[1]);
            TEST_ASSERT_EQUAL_UINT32(1, event->data.backend.args[2]);
            TEST_ASSERT_EQUAL_UINT64(sizeof(data), event->data.backend.bytes);
            uploaded = true;
        }
        /* Exactly representable values, so equality is exact. */
        const float *color = event->data.backend.values;
        if (event->detail == NT_GFX_GL_glClearColor && color[0] == 0.25F && color[1] == 0.5F && color[2] == 0.75F && color[3] == 1.0F) {
            cleared = true;
        }
    }
    TEST_ASSERT_NOT_EQUAL(0, generated);
    TEST_ASSERT_TRUE(uploaded && cleared);
}

/* Every counted GL call is recorded and every recorded call is counted, per function. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one frame exercises every funnel form
static void test_complete_capture_matches_gl_counters(void) {
    const uint8_t pixels[16] = {0};
    const float tint[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    nt_gfx_capture_request();
    nt_gfx_begin_frame();
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){gl_Position=vec4(0.0);}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){
        .type = NT_SHADER_FRAGMENT, .source = "precision mediump float; uniform sampler2D tex; uniform vec4 tint; out vec4 color; void main(){color=texture(tex,vec2(0.5))*tint;}"});
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = nt_gfx_make_program(vs, fs)});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 2, .height = 2, .format = NT_TEXTURE_FORMAT_RGBA8, .data = pixels});
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = sizeof(pixels), .data = pixels});
    nt_gfx_update_buffer(buffer, 0, pixels, 8);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.1F, 0.2F, 0.3F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
    const nt_gfx_texture_binding_t binding = {.name = nt_hash32_str("tex"), .texture = texture};
    nt_gfx_apply_texture_bindings(&binding, 1);
    nt_gfx_set_uniform_vec4(nt_hash32_str("tint"), tint);
    nt_gfx_draw(0, 3);
    uint8_t pixel[4] = {0};
    (void)nt_gfx_read_pixels(0, 0, 1, 1, pixel, sizeof(pixel));
    nt_gfx_end_pass();
    nt_gfx_destroy_buffer(buffer);
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    uint32_t recorded[NT_GFX_GL_COUNT] = {0};
    uint32_t total = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        if (capture.events[i].kind == NT_GFX_EVENT_BACKEND) {
            TEST_ASSERT_LESS_THAN_UINT32(NT_GFX_GL_COUNT, capture.events[i].detail);
            recorded[capture.events[i].detail]++;
            total++;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(20, total);
    const nt_gfx_counters_t *c = &capture.counters;
    for (uint32_t call = 1; call < NT_GFX_GL_COUNT; call++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(c->gl[call], recorded[call], nt_gfx_gl_call_name(call));
    }
    /* The hooks count what the driver received, independently of the funnel. */
    TEST_ASSERT_EQUAL_UINT32(s_program_calls, c->gl[NT_GFX_GL_glUseProgram]);
    TEST_ASSERT_EQUAL_UINT32(s_vao_calls, c->gl[NT_GFX_GL_glBindVertexArray]);
    TEST_ASSERT_EQUAL_UINT32(s_uniform_calls, c->gl[NT_GFX_GL_glUniform4fv]);
    TEST_ASSERT_EQUAL_UINT32(s_ubo_calls, c->gl[NT_GFX_GL_glBindBufferBase]);
    TEST_ASSERT_EQUAL_UINT32(s_attribute_calls, c->gl[NT_GFX_GL_glVertexAttribPointer]);
    TEST_ASSERT_EQUAL_UINT64(s_buffer_calls, c->buffer_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(s_buffer_bytes, c->buffer_upload_bytes);
    TEST_ASSERT_EQUAL_UINT64(s_texture_calls, c->texture_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(s_texture_bytes, c->texture_upload_bytes);
}

/* Teardown deletes live objects through the GL funnel after the event array is freed.
 * A record written there is a heap-use-after-free that only the ASan build catches:
 * CI's Linux native-debug-test run (Debug + clang => -fsanitize=address,undefined). */
static void test_shutdown_while_recording_writes_no_record(void) {
    nt_gfx_capture_request();
    nt_gfx_begin_frame();
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 16});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, buffer.id);
    TEST_ASSERT_GREATER_THAN_UINT32(0, captured_calls(NT_GFX_GL_glBufferData));
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = 4096;
    nt_gfx_init(&desc);
    nt_gfx_capture_view_t view = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(0, view.count);
    TEST_ASSERT_NULL(view.events);
}

static void test_readback_is_recorded_as_issued_call(void) {
    nt_gfx_capture_request();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    uint8_t pixel[4] = {0};
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, 1, 1, pixel, sizeof(pixel)));
    nt_gfx_end_pass();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    uint32_t reads = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_BACKEND && event->detail == NT_GFX_GL_glReadPixels) {
            TEST_ASSERT_EQUAL_UINT32(1, event->data.backend.args[2]);
            TEST_ASSERT_EQUAL_UINT32(1, event->data.backend.args[6]);
            reads++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(1, reads);
}
#endif
/* Uploads before any pass land in the open frame. */
static void test_payloads_before_render_land_in_their_frame(void) {
    const uint8_t data[64] = {0};
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = sizeof(data)});
    TEST_ASSERT_EQUAL_UINT64(0, g_nt_gfx.counters.buffer_upload_calls);
    nt_gfx_update_buffer(buffer, 0, data, 16);
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(16, g_nt_gfx.last_frame.buffer_upload_bytes);

    nt_gfx_orphan_buffer(buffer, data, sizeof(data));
    nt_gfx_update_buffer(buffer, 8, data, 12);
    nt_gfx_counters_t live = g_nt_gfx.counters;
    TEST_ASSERT_EQUAL_UINT64(2, live.buffer_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(76, live.buffer_upload_bytes);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_draw_calls(&live));
    nt_gfx_begin_frame();
    const nt_gfx_counters_t *end = &g_nt_gfx.last_frame;
    TEST_ASSERT_EQUAL_UINT64(76, end->buffer_upload_bytes);
    /* Both frames together saw every payload the driver received. */
    TEST_ASSERT_EQUAL_UINT64(3, s_buffer_calls);
    TEST_ASSERT_EQUAL_UINT64(92, s_buffer_bytes);
}

/* Single-byte rows are not padded to the unpack alignment: an odd width counts exact bytes. */
static void test_r8_odd_width_update_counts_exact_bytes(void) {
    const uint8_t pixels[9] = {0};
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 3, .height = 3, .format = NT_TEXTURE_FORMAT_R8, .data = pixels});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, texture.id);
    nt_gfx_begin_frame();
    nt_gfx_update_texture(texture, 0, 1, 3, 1, pixels);
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(1, g_nt_gfx.last_frame.texture_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(3, g_nt_gfx.last_frame.texture_upload_bytes);
}

static void test_texture_mips_storage_and_subrect_payloads(void) {
    const uint8_t pixels[84] = {0};
    nt_texture_t storage = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .format = NT_TEXTURE_FORMAT_RGBA8});
    TEST_ASSERT_TRUE(storage.id != 0);
    TEST_ASSERT_EQUAL_UINT64(0, g_nt_gfx.counters.texture_upload_calls);
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .format = NT_TEXTURE_FORMAT_RGBA8, .level_count = 3, .data = pixels});
    TEST_ASSERT_TRUE(texture.id != 0);
    nt_gfx_update_texture(storage, 1, 1, 2, 3, pixels);
    nt_gfx_counters_t live = g_nt_gfx.counters;
    TEST_ASSERT_EQUAL_UINT64(4, live.texture_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(108, live.texture_upload_bytes);
    TEST_ASSERT_EQUAL_UINT64(s_texture_calls, live.texture_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(s_texture_bytes, live.texture_upload_bytes);
    nt_gfx_begin_frame();
}

/* The error code alone never reports a loss: only the browser's isContextLost does, and native has none. */
static void test_failed_upload_keeps_issued_bytes(void) {
    const uint8_t pixels[64] = {0};
    const GLenum errors[] = {GL_OUT_OF_MEMORY, 0x9242U /* CONTEXT_LOST_WEBGL */};
    for (uint32_t i = 0; i < 2; i++) {
        s_upload_error = errors[i];
        nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .format = NT_TEXTURE_FORMAT_RGBA8, .data = pixels});
        TEST_ASSERT_EQUAL_UINT32(0, texture.id);
        nt_gfx_begin_frame();
        const nt_gfx_counters_t *snapshot = &g_nt_gfx.last_frame;
        TEST_ASSERT_EQUAL_UINT64(1, snapshot->texture_upload_calls);
        TEST_ASSERT_EQUAL_UINT64(64, snapshot->texture_upload_bytes);
    }
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
#if NT_GFX_CAPTURE_ENABLED
    nt_gfx_capture_request();
#endif
    nt_gfx_begin_frame();
    for (uint32_t frame = 0; frame < 2; frame++) {
        s_program_calls = s_vao_calls = s_uniform_calls = s_ubo_calls = 0;
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
        nt_gfx_begin_frame();
#if NT_GFX_CAPTURE_ENABLED
        if (frame == 1) {
            nt_gfx_capture_view_t capture = nt_gfx_capture_read();
            bool color_known = false;
            bool viewport_known = false;
            for (uint32_t i = 0; i < capture.count; i++) {
                const nt_gfx_event_t *e = &capture.events[i];
                if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_UNIFORM_VEC4 && e->data.backend.args[1] == nt_hash32_str("u_color").value) {
                    TEST_ASSERT_EQUAL(NT_GFX_RESULT_NONE, e->result);
                    TEST_ASSERT_EQUAL_MEMORY(color, e->data.backend.values, sizeof(color));
                    color_known = true;
                }
                if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_VIEWPORT) {
                    TEST_ASSERT_EQUAL_UINT32(g_nt_window.fb_width, e->data.state.integers[2]);
                    TEST_ASSERT_EQUAL_UINT32(g_nt_window.fb_height, e->data.state.integers[3]);
                    viewport_known = true;
                }
            }
            TEST_ASSERT_TRUE(color_known);
            TEST_ASSERT_TRUE(viewport_known);
        }
#endif
        nt_gfx_counters_t c = g_nt_gfx.last_frame;
        TEST_ASSERT_EQUAL_UINT32(2, c.accepted[NT_GFX_OP_PIPELINE]);
        TEST_ASSERT_EQUAL_UINT32(2, c.accepted[NT_GFX_OP_VERTEX_INPUT]);
        TEST_ASSERT_EQUAL_UINT32(3, c.accepted[NT_GFX_OP_UNIFORM_VEC4]);
        TEST_ASSERT_EQUAL_UINT32(2, c.accepted[NT_GFX_OP_UBO]);
        TEST_ASSERT_EQUAL_UINT32(s_program_calls, c.gl[NT_GFX_GL_glUseProgram]);
        TEST_ASSERT_EQUAL_UINT32(s_vao_calls, c.gl[NT_GFX_GL_glBindVertexArray]);
        TEST_ASSERT_EQUAL_UINT32(s_uniform_calls, c.gl[NT_GFX_GL_glUniform4fv]);
        TEST_ASSERT_EQUAL_UINT32(s_ubo_calls, c.gl[NT_GFX_GL_glBindBufferBase]);
        TEST_ASSERT_EQUAL_UINT32(frame == 0 ? 1 : 0, c.gl[NT_GFX_GL_glUseProgram]);
        TEST_ASSERT_EQUAL_UINT32(frame == 0 ? 1 : 0, c.gl[NT_GFX_GL_glUniform4fv]);
        TEST_ASSERT_EQUAL_UINT32(2, c.gl[NT_GFX_GL_glBindBufferBase]);
#if NT_GFX_CAPTURE_ENABLED
        TEST_ASSERT_EQUAL_UINT32(c.gl[NT_GFX_GL_glUseProgram], captured_calls(NT_GFX_GL_glUseProgram));
        TEST_ASSERT_EQUAL_UINT32(c.gl[NT_GFX_GL_glBindVertexArray], captured_calls(NT_GFX_GL_glBindVertexArray));
        /* An empty frame starts the next recording only after this one was read. */
        nt_gfx_capture_request();
        nt_gfx_begin_frame();
#endif
    }
}

static void test_compressed_mips_use_issued_block_sizes(void) {
    const nt_gfx_gpu_caps_t *caps = nt_gfx_gpu_caps();
    nt_texture_format_t format = NT_TEXTURE_FORMAT_ASTC_4x4_RGBA;
    if (caps->has_bc7) {
        format = NT_TEXTURE_FORMAT_BC7_RGBA;
    } else if (caps->has_etc2) {
        format = NT_TEXTURE_FORMAT_ETC2_RGBA8;
    }
    if (!caps->has_bc7 && !caps->has_etc2 && !caps->has_astc) {
        TEST_IGNORE_MESSAGE("Compressed payload unverified: no supported block format");
    }
    const uint8_t blocks[80] = {0};
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 8, .height = 4, .format = format, .level_count = 4, .data = blocks});
    TEST_ASSERT_NOT_EQUAL(0, texture.id);
    nt_gfx_begin_frame();
    nt_gfx_counters_t counters = g_nt_gfx.last_frame;
    TEST_ASSERT_EQUAL_UINT64(4, counters.texture_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(80, counters.texture_upload_bytes);
    TEST_ASSERT_EQUAL_UINT64(s_texture_calls, counters.texture_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(s_texture_bytes, counters.texture_upload_bytes);
}

static void test_attribute_pointer_calls_are_counted_per_issue(void) {
    nt_buffer_t vertices = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 64});
    nt_buffer_t instances = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 64});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .vertex_buffer = vertices,
        .layout = {.attr_count = 1, .stride = 8, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 2}}},
        .instance_layout = {.attr_count = 1, .stride = 16, .attrs = {{.location = 1, .type = NT_VERTEX_FLOAT, .count = 4}}},
    });
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.counters.gl[NT_GFX_GL_glVertexAttribPointer]);
    TEST_ASSERT_EQUAL_UINT32(1, s_attribute_calls);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_bind_instance_buffer(instances, 0);
    nt_gfx_bind_instance_buffer(instances, 16);
    nt_gfx_end_pass();
    nt_gfx_begin_frame();
    nt_gfx_counters_t counters = g_nt_gfx.last_frame;
    /* One static pointer at creation plus one instance pointer per instance-buffer bind. */
    TEST_ASSERT_EQUAL_UINT32(3, counters.gl[NT_GFX_GL_glVertexAttribPointer]);
    TEST_ASSERT_EQUAL_UINT32(s_attribute_calls, counters.gl[NT_GFX_GL_glVertexAttribPointer]);
}

int main(void) {
    /* One hidden window and GL context serve every test; setUp/tearDown reset only engine state. */
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){.max_dpr = 1.0F, .width = 16, .height = 16};
    nt_window_init();
    UNITY_BEGIN();
#if NT_GFX_CAPTURE_ENABLED
    RUN_TEST(test_capture_publishes_resize_mappings_and_skip_reasons);
    RUN_TEST(test_new_program_defines_sampler_names_and_inactive_uniforms);
    RUN_TEST(test_initial_uniform_records_cover_only_vec4);
    RUN_TEST(test_issued_calls_record_floats_names_and_payloads);
    RUN_TEST(test_complete_capture_matches_gl_counters);
    RUN_TEST(test_readback_is_recorded_as_issued_call);
    RUN_TEST(test_shutdown_while_recording_writes_no_record);
#endif
    RUN_TEST(test_payloads_before_render_land_in_their_frame);
    RUN_TEST(test_texture_mips_storage_and_subrect_payloads);
    RUN_TEST(test_r8_odd_width_update_counts_exact_bytes);
    RUN_TEST(test_failed_upload_keeps_issued_bytes);
    RUN_TEST(test_repeated_frames_separate_requests_from_issued_calls);
    RUN_TEST(test_compressed_mips_use_issued_block_sizes);
    RUN_TEST(test_attribute_pointer_calls_are_counted_per_issue);
    int failures = UNITY_END();
    nt_window_shutdown();
    return failures;
}
