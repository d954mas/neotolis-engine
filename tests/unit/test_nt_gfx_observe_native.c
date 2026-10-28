#include "graphics/nt_gfx.h"
#include "pool/nt_pool.h"
#include "test_helpers/nt_gfx_test_frame.h"
#include "unity.h"
#include "window/nt_window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

static PFNGLBUFFERDATAPROC s_buffer_data;
static PFNGLBUFFERSUBDATAPROC s_buffer_sub_data;
static PFNGLUSEPROGRAMPROC s_use_program;
static PFNGLBINDVERTEXARRAYPROC s_bind_vao;
static PFNGLBINDBUFFERRANGEPROC s_bind_buffer_range;
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

static void GLAD_API_PTR count_bind_buffer_range(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    s_ubo_calls++;
    s_bind_buffer_range(target, index, buffer, offset, size);
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
    desc.frame_capacity[NT_GFX_FRAME_VERTEX] = 4096;
    desc.frame_capacity[NT_GFX_FRAME_VERTEX + 1] = 4096;
    desc.frame_capacity[NT_GFX_FRAME_UNIFORM] = 4096;
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    s_buffer_data = glad_glBufferData;
    s_buffer_sub_data = glad_glBufferSubData;
    s_use_program = glad_glUseProgram;
    s_bind_vao = glad_glBindVertexArray;
    s_bind_buffer_range = glad_glBindBufferRange;
    glad_glBufferData = count_buffer_data;
    glad_glBufferSubData = count_buffer_sub_data;
    glad_glUseProgram = count_use_program;
    glad_glBindVertexArray = count_bind_vao;
    glad_glBindBufferRange = count_bind_buffer_range;
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
    s_program_calls = s_vao_calls = s_ubo_calls = 0;
}

void tearDown(void) {
    glad_glBufferData = s_buffer_data;
    glad_glBufferSubData = s_buffer_sub_data;
    glad_glUseProgram = s_use_program;
    glad_glBindVertexArray = s_bind_vao;
    glad_glBindBufferRange = s_bind_buffer_range;
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- inspect both identity layers of a new attachment
static void test_capture_publishes_attachment_mappings_and_skip_reasons(void) {
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_texture_t color = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 13, .height = 7, .format = NT_TEXTURE_FORMAT_RGBA8});
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = color});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_set_scissor_enabled(false);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    uint32_t texture_slot = 0;
    bool cache = false;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_DEFINITION && event->object_kind == NT_GFX_OBJECT_TEXTURE && event->object == color.id) {
            TEST_ASSERT_EQUAL_UINT32(13, event->data.resource.width);
            TEST_ASSERT_EQUAL_UINT32(7, event->data.resource.height);
            texture_slot = event->data.resource.backend;
        }
        if (event->kind == NT_GFX_EVENT_OP && event->operation == NT_GFX_OP_SCISSOR_ENABLE) {
            TEST_ASSERT_EQUAL(NT_GFX_RESULT_CACHE, event->result);
            cache = true;
        }
    }
    TEST_ASSERT_NOT_EQUAL(0, texture_slot);
    bool mapping = false;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_DEFINITION && event->operation == NT_GFX_OP_STATE && event->detail == NT_GFX_OBJECT_TEXTURE && event->data.backend.args[0] == texture_slot) {
            TEST_ASSERT_NOT_EQUAL(0, event->data.backend.args[1]);
            mapping = true;
        }
    }
    TEST_ASSERT_TRUE(mapping && cache);
    nt_gfx_destroy_render_target(target);
}

static void test_new_program_defines_sampler_names(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){gl_Position=vec4(0.0);}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){
        .type = NT_SHADER_FRAGMENT, .source = "precision mediump float; uniform sampler2D a; uniform sampler2D b; out vec4 color; void main(){color=texture(a,vec2(0.0))+texture(b,vec2(0.0));}"});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    /* Record the frame whose begin_frame finishes the link; a parallel-compile driver may take several. */
    const struct timespec link_start = nt_test_link_wait_start();
    do {
        nt_test_link_wait_check(&link_start);
        nt_gfx_capture_request();
        nt_gfx_end_frame();
        nt_gfx_begin_frame();
    } while (!nt_gfx_program_ready(program));
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    uint32_t names = 0;
    uint32_t units = 0;
    uint32_t pipeline_states = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        /* Every operation ended, and every gfx record's operation came before it. */
        TEST_ASSERT_TRUE(event->kind != NT_GFX_EVENT_OP || event->result != NT_GFX_RESULT_NONE);
        TEST_ASSERT_TRUE(event->kind > NT_GFX_EVENT_ARGUMENT || event->link <= i);
        if (event->kind == NT_GFX_EVENT_INITIAL && event->operation == NT_GFX_OP_SAMPLER) {
            if (event->data.backend.args[1] == nt_hash32_str("a").value) {
                names |= 1;
            }
            if (event->data.backend.args[1] == nt_hash32_str("b").value) {
                names |= 2;
            }
            units |= 1U << event->data.backend.args[3];
        }
        /* Pipeline state is defined once, by the backend, in its slots and enums. */
        if (event->kind == NT_GFX_EVENT_DEFINITION && event->operation == NT_GFX_OP_PIPELINE) {
            TEST_ASSERT_EQUAL(NT_GFX_OBJECT_NONE, event->object_kind);
            pipeline_states++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3, names);
    TEST_ASSERT_EQUAL_UINT32(3, units);
    TEST_ASSERT_EQUAL_UINT32(1, pipeline_states);
    TEST_ASSERT_FALSE(capture.overflow);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one walk checks three argument kinds
static void test_issued_calls_record_floats_names_and_payloads(void) {
    const uint8_t data[16] = {0};
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = sizeof(data), .data = data});
    /* Still bound from the create: the update records a SKIP of glBindBuffer. */
    nt_gfx_update_buffer(buffer, 0, data, sizeof(data));
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.25F, 0.5F, 0.75F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    uint32_t generated = 0;
    bool uploaded = false;
    bool cleared = false;
    bool skipped = false;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_SKIP && event->detail == NT_GFX_GL_glBindBuffer) {
            TEST_ASSERT_EQUAL_UINT8(1, event->arg_count);
            skipped = true;
        }
        if (event->kind != NT_GFX_EVENT_BACKEND) {
            continue;
        }
        if (event->detail == NT_GFX_GL_glGenBuffers) {
            TEST_ASSERT_EQUAL_UINT32(1, event->data.backend.args[0]);
            TEST_ASSERT_EQUAL_UINT8(2, event->arg_count); /* count, then the name */
            generated = event->data.backend.args[1];
        }
        if (event->detail == NT_GFX_GL_glBufferData) {
            TEST_ASSERT_EQUAL_UINT32(sizeof(data), event->data.backend.args[1]);
            TEST_ASSERT_EQUAL_UINT32(1, event->data.backend.args[2]);
            TEST_ASSERT_EQUAL_UINT64(sizeof(data), event->data.backend.bytes);
            TEST_ASSERT_EQUAL_UINT8(4, event->arg_count);
            uploaded = true;
        }
        /* Exactly representable values, so equality is exact. */
        const float *color = event->data.backend.values;
        if (event->detail == NT_GFX_GL_glClearColor && color[0] == 0.25F && color[1] == 0.5F && color[2] == 0.75F && color[3] == 1.0F) {
            TEST_ASSERT_EQUAL_UINT8(4U << 4, event->arg_count);
            cleared = true;
        }
    }
    TEST_ASSERT_NOT_EQUAL(0, generated);
    TEST_ASSERT_TRUE(uploaded && cleared && skipped);
}

/* Frame storage reaches its buffer before the replayed draw that reads it, in issued order. */
static void test_frame_storage_upload_is_issued_before_the_replayed_draw(void) {
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){gl_Position=vec4(0.0);}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "precision mediump float; out vec4 color; void main(){color=vec4(1.0);}"});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(program);
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    uint32_t offset = 0;
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 16, 4, &offset), 0, 16);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    uint32_t upload = UINT32_MAX;
    uint32_t draw = UINT32_MAX;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        if (upload == UINT32_MAX && e->kind == NT_GFX_EVENT_BACKEND && e->detail == NT_GFX_GL_glBufferSubData) {
            upload = i;
        }
        if (draw == UINT32_MAX && e->kind == NT_GFX_EVENT_BACKEND && e->detail == NT_GFX_GL_glDrawArrays) {
            draw = i;
        }
    }
    TEST_ASSERT_NOT_EQUAL_UINT32(UINT32_MAX, draw);
    TEST_ASSERT_LESS_THAN_UINT32(draw, upload);
}

static nt_gfx_operation_t owner_operation(const nt_gfx_capture_view_t *capture, const nt_gfx_event_t *e) {
    TEST_ASSERT_NOT_EQUAL_UINT32(0, e->link);
    const nt_gfx_event_t *owner = &capture->events[e->link - 1U];
    TEST_ASSERT_EQUAL(NT_GFX_EVENT_OP, owner->kind);
    return (nt_gfx_operation_t)owner->operation;
}

/* From the pipeline create (the frame's first own operation) every issued, skipped and argument record names its
 * operation; the stream's records lie in the execute range, the readback after it. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one walk checks each owner kind
static void check_links_name_their_operations(const nt_gfx_capture_view_t *capture, uint32_t pipeline, uint32_t updated) {
    const nt_gfx_event_t *events = capture->events;
    uint32_t first = 0;
    while (first < capture->count && !(events[first].kind == NT_GFX_EVENT_OP && events[first].object_kind == NT_GFX_OBJECT_PIPELINE && events[first].object == pipeline)) {
        first++;
    }
    TEST_ASSERT_LESS_THAN_UINT32(capture->execute_begin, first);
    TEST_ASSERT_LESS_THAN_UINT32(capture->execute_end, capture->execute_begin);
    uint32_t pass = 0;
    uint32_t uploads = 0;
    uint32_t draws = 0;
    uint32_t arguments = 0;
    bool inactive = false;
    bool readback = false;
    for (uint32_t i = first; i < capture->count; i++) {
        const nt_gfx_event_t *e = &events[i];
        if (e->kind != NT_GFX_EVENT_BACKEND && e->kind != NT_GFX_EVENT_SKIP && e->kind != NT_GFX_EVENT_ARGUMENT) {
            continue;
        }
        const nt_gfx_operation_t op = owner_operation(capture, e);
        const bool executed = i >= capture->execute_begin && i < capture->execute_end;
        if (e->kind == NT_GFX_EVENT_ARGUMENT) {
            TEST_ASSERT_EQUAL(NT_GFX_OP_TEXTURE_SET, op);
            arguments++;
            continue;
        }
        if (e->kind == NT_GFX_EVENT_SKIP && e->result == NT_GFX_RESULT_INACTIVE) {
            TEST_ASSERT_EQUAL(NT_GFX_OP_TEXTURE_SET, op);
            inactive = true;
        }
        pass += (executed && op == NT_GFX_OP_PASS) ? 1U : 0U;
        if (!executed) {
            /* Recorded commands execute only inside the range. */
            TEST_ASSERT_FALSE(op == NT_GFX_OP_PIPELINE || op == NT_GFX_OP_VERTEX_INPUT || op == NT_GFX_OP_UBO || op == NT_GFX_OP_DRAW);
            if (e->detail == NT_GFX_GL_glBufferSubData) {
                TEST_ASSERT_EQUAL_UINT32(updated, events[e->link - 1U].object);
            }
            if (e->detail == NT_GFX_GL_glReadPixels) {
                TEST_ASSERT_GREATER_OR_EQUAL_UINT32(capture->execute_end, i);
                TEST_ASSERT_EQUAL(NT_GFX_OP_READ_PIXELS, op);
                readback = true;
            }
            continue;
        }
        switch (e->detail) {
        case NT_GFX_GL_glUseProgram:
            TEST_ASSERT_EQUAL(NT_GFX_OP_PIPELINE, op);
            break;
        case NT_GFX_GL_glBindVertexArray:
            TEST_ASSERT_EQUAL(NT_GFX_OP_VERTEX_INPUT, op);
            break;
        case NT_GFX_GL_glBindTexture:
        case NT_GFX_GL_glBindSampler:
            TEST_ASSERT_EQUAL(NT_GFX_OP_TEXTURE_SET, op);
            break;
        case NT_GFX_GL_glBindBufferRange:
            TEST_ASSERT_EQUAL(NT_GFX_OP_UBO, op);
            break;
        case NT_GFX_GL_glBufferSubData:
            TEST_ASSERT_EQUAL(NT_GFX_OP_BUFFER_UPLOAD, op);
            TEST_ASSERT_EQUAL_UINT32(nt_gfx_frame_buffer(NT_GFX_FRAME_UNIFORM).id, events[e->link - 1U].object);
            uploads++;
            break;
        case NT_GFX_GL_glDrawArrays:
            TEST_ASSERT_EQUAL(NT_GFX_OP_DRAW, op);
            draws++;
            break;
        default:
            break;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0, pass);
    TEST_ASSERT_GREATER_THAN_UINT32(0, uploads);
    TEST_ASSERT_EQUAL_UINT32(1, draws);
    TEST_ASSERT_EQUAL_UINT32(2, arguments);
    TEST_ASSERT_TRUE(inactive);
    TEST_ASSERT_TRUE(readback);
}

/* Every counted GL call is recorded and every recorded call is counted, per function. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one frame exercises every funnel form
static void test_complete_capture_matches_gl_counters(void) {
    const uint8_t pixels[16] = {0};
    const float tint[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){gl_Position=vec4(0.0);}"});
    nt_shader_t fs = nt_gfx_make_shader(
        &(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT,
                            .source = "precision mediump float; uniform sampler2D tex; layout(std140) uniform Draw { vec4 tint; }; out vec4 color; void main(){color=texture(tex,vec2(0.5))*tint;}"});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    /* Record the frame whose begin_frame finishes the link; a parallel-compile driver may take several. */
    const struct timespec link_start = nt_test_link_wait_start();
    do {
        nt_test_link_wait_check(&link_start);
        nt_gfx_capture_request();
        nt_gfx_end_frame();
        nt_gfx_begin_frame();
    } while (!nt_gfx_program_ready(program));
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 2, .height = 2, .format = NT_TEXTURE_FORMAT_RGBA8, .data = pixels});
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = sizeof(pixels), .data = pixels});
    nt_gfx_update_buffer(buffer, 0, pixels, 8);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.1F, 0.2F, 0.3F, 1.0F}, .clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(vi);
    /* "absent" is not a program sampler: an INACTIVE skip of the texture set. */
    const nt_gfx_texture_binding_t bindings[2] = {{.name = nt_hash32_str("tex"), .texture = texture}, {.name = nt_hash32_str("absent"), .texture = texture}};
    nt_gfx_apply_texture_bindings(bindings, 2);
    nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, tint, sizeof(tint));
    nt_gfx_draw(0, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    uint8_t pixel[4] = {0};
    (void)nt_gfx_read_pixels((nt_render_target_t){0}, 0, 0, 1, 1, pixel, sizeof(pixel));
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
    TEST_ASSERT_EQUAL_UINT32(s_ubo_calls, c->gl[NT_GFX_GL_glBindBufferRange]);
    TEST_ASSERT_EQUAL_UINT32(s_attribute_calls, c->gl[NT_GFX_GL_glVertexAttribPointer]);
    TEST_ASSERT_EQUAL_UINT64(s_buffer_calls, c->buffer_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(s_buffer_bytes, c->buffer_upload_bytes);
    TEST_ASSERT_EQUAL_UINT64(s_texture_calls, c->texture_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(s_texture_bytes, c->texture_upload_bytes);
    check_links_name_their_operations(&capture, pipeline.id, buffer.id);
}

static uint32_t op_index(const nt_gfx_capture_view_t *capture, uint32_t from, nt_gfx_operation_t operation) {
    for (uint32_t i = from; i < capture->count; i++) {
        if (capture->events[i].kind == NT_GFX_EVENT_OP && capture->events[i].operation == operation) {
            return i;
        }
    }
    return capture->count;
}

/* A merged draw issues one call linked to the first draw; a note does not end a merge, a bind does. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- plain and indexed merges in one frame
static void test_merged_draws_link_to_the_first_draw(void) {
    static const uint16_t indices[6] = {0, 1, 2, 2, 1, 0};
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = "void main(){gl_Position=vec4(0.0);}"});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = "precision mediump float; out vec4 color; void main(){color=vec4(1.0);}"});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(program);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_vertex_input_t plain = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_vertex_input_t other = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    nt_buffer_t ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_INDEX, .usage = NT_USAGE_IMMUTABLE, .data = indices, .size = sizeof(indices), .index_type = NT_INDEX_UINT16});
    nt_vertex_input_t indexed = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.index_buffer = ibo});
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_pipeline(pipeline);
    nt_gfx_bind_vertex_input(plain);
    nt_gfx_draw(0, 3);
    NT_GFX_NOTE("between merged draws");
    nt_gfx_draw(3, 3);
    nt_gfx_draw(6, 3);
    nt_gfx_bind_vertex_input(other);
    nt_gfx_draw(9, 3);
    nt_gfx_bind_vertex_input(indexed);
    nt_gfx_draw_indexed(0, 3, 3);
    nt_gfx_draw_indexed(3, 3, 3);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    uint32_t draw[4];
    draw[0] = op_index(&capture, 0, NT_GFX_OP_DRAW);
    for (uint32_t d = 1; d < 4; d++) {
        draw[d] = op_index(&capture, draw[d - 1] + 1U, NT_GFX_OP_DRAW);
    }
    TEST_ASSERT_LESS_THAN_UINT32(capture.count, draw[3]);
    const nt_gfx_result_t expected[4] = {NT_GFX_RESULT_ACCEPTED, NT_GFX_RESULT_CACHE, NT_GFX_RESULT_CACHE, NT_GFX_RESULT_ACCEPTED};
    const uint32_t joined[4] = {0, draw[0] + 1U, draw[0] + 1U, 0};
    for (uint32_t d = 0; d < 4; d++) {
        TEST_ASSERT_EQUAL(expected[d], capture.events[draw[d]].result);
        TEST_ASSERT_EQUAL_UINT32(joined[d], capture.events[draw[d]].detail);
    }
    const uint32_t indexed_first = op_index(&capture, 0, NT_GFX_OP_DRAW_INDEXED);
    const uint32_t indexed_second = op_index(&capture, indexed_first + 1U, NT_GFX_OP_DRAW_INDEXED);
    TEST_ASSERT_LESS_THAN_UINT32(capture.count, indexed_second);
    TEST_ASSERT_EQUAL(NT_GFX_RESULT_CACHE, capture.events[indexed_second].result);
    TEST_ASSERT_EQUAL_UINT32(indexed_first + 1U, capture.events[indexed_second].detail);
    uint32_t arrays = 0;
    uint32_t elements = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *e = &capture.events[i];
        if (e->kind != NT_GFX_EVENT_BACKEND) {
            continue;
        }
        if (e->detail == NT_GFX_GL_glDrawArrays) {
            /* first merged call: three draws of 3; the next starts after the bind */
            TEST_ASSERT_EQUAL_UINT32(arrays == 0 ? draw[0] + 1U : draw[3] + 1U, e->link);
            TEST_ASSERT_EQUAL_UINT32(arrays == 0 ? 9U : 3U, e->data.backend.args[2]);
            arrays++;
        }
        if (e->detail == NT_GFX_GL_glDrawElements) {
            TEST_ASSERT_EQUAL_UINT32(indexed_first + 1U, e->link);
            TEST_ASSERT_EQUAL_UINT32(6, e->data.backend.args[1]);
            elements++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(2, arrays);
    TEST_ASSERT_EQUAL_UINT32(1, elements);
}

static uint32_t sampler_op(const nt_gfx_capture_view_t *capture) {
    for (uint32_t i = 0; i < capture->count; i++) {
        if (capture->events[i].kind == NT_GFX_EVENT_OP && capture->events[i].operation == NT_GFX_OP_CREATE && capture->events[i].object_kind == NT_GFX_OBJECT_SAMPLER) {
            return i;
        }
    }
    return capture->count;
}

/* Records one frame that makes the first texture after init, which makes its default sampler inside. */
static nt_gfx_capture_view_t record_first_texture(uint32_t capacity, nt_texture_t *texture) {
    static const uint8_t pixels[16] = {0};
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = capacity;
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    *texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 2, .height = 2, .format = NT_TEXTURE_FORMAT_RGBA8, .data = pixels});
    (void)nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 16});
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    return nt_gfx_capture_read();
}

/* An operation inside another links to it; the parent's later records and the next operation follow the restored link. */
static void test_nested_operation_links_to_its_parent(void) {
    nt_texture_t texture;
    nt_gfx_capture_view_t capture = record_first_texture(4096, &texture);
    TEST_ASSERT_FALSE(capture.overflow);
    const uint32_t sampler = sampler_op(&capture);
    TEST_ASSERT_LESS_THAN_UINT32(capture.count, sampler);
    const uint32_t parent = capture.events[sampler].link;
    TEST_ASSERT_NOT_EQUAL_UINT32(0, parent);
    const nt_gfx_event_t *create = &capture.events[parent - 1U];
    TEST_ASSERT_EQUAL(NT_GFX_OBJECT_TEXTURE, create->object_kind);
    TEST_ASSERT_EQUAL_UINT32(texture.id, create->object);
    TEST_ASSERT_EQUAL_UINT32(0, create->link);
    bool child_own = false;
    bool parent_after_child = false;
    for (uint32_t i = sampler + 1U; i < capture.count; i++) {
        child_own |= capture.events[i].kind == NT_GFX_EVENT_BACKEND && capture.events[i].link == sampler + 1U;
        parent_after_child |= capture.events[i].kind == NT_GFX_EVENT_DEFINITION && capture.events[i].link == parent;
    }
    TEST_ASSERT_TRUE(child_own);
    TEST_ASSERT_TRUE(parent_after_child);
    const uint32_t buffer = op_index(&capture, sampler + 1U, NT_GFX_OP_CREATE);
    TEST_ASSERT_LESS_THAN_UINT32(capture.count, buffer);
    TEST_ASSERT_EQUAL(NT_GFX_OBJECT_BUFFER, capture.events[buffer].object_kind);
    TEST_ASSERT_EQUAL_UINT32(0, capture.events[buffer].link);
}

/* An overflow inside a nested operation still completes the operations recorded before it. */
static void test_overflow_inside_an_operation_keeps_recorded_results(void) {
    nt_texture_t texture;
    const nt_gfx_capture_view_t measured = record_first_texture(4096, &texture);
    const uint32_t sampler = sampler_op(&measured);
    nt_gfx_capture_view_t capture = record_first_texture(sampler + 1U, &texture);
    TEST_ASSERT_TRUE(capture.overflow);
    TEST_ASSERT_EQUAL_UINT32(sampler + 1U, capture.count);
    const nt_gfx_event_t *child = &capture.events[sampler];
    TEST_ASSERT_EQUAL(NT_GFX_OBJECT_SAMPLER, child->object_kind);
    TEST_ASSERT_EQUAL(NT_GFX_RESULT_ACCEPTED, child->result);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, child->object);
    const nt_gfx_event_t *parent = &capture.events[child->link - 1U];
    TEST_ASSERT_EQUAL(NT_GFX_RESULT_ACCEPTED, parent->result);
    TEST_ASSERT_EQUAL_UINT32(texture.id, parent->object);
}

/* Teardown deletes live objects through the GL funnel after the event array is freed.
 * A record written there is a heap-use-after-free that only the ASan build catches:
 * CI's Linux native-debug-test run (Debug + clang => -fsanitize=address,undefined). */
static void test_shutdown_while_recording_writes_no_record(void) {
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_buffer_t buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .usage = NT_USAGE_DYNAMIC, .size = 16});
    TEST_ASSERT_NOT_EQUAL_UINT32(0, buffer.id);
    TEST_ASSERT_GREATER_THAN_UINT32(0, captured_calls(NT_GFX_GL_glBufferData));
    nt_gfx_shutdown();
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.capture_capacity = 4096;
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t view = nt_gfx_capture_read();
    TEST_ASSERT_EQUAL_UINT32(0, view.count);
    TEST_ASSERT_NULL(view.events);
}

static void GLAD_API_PTR invalidate_noop(GLenum target, GLsizei count, const GLenum *attachments) {
    (void)target;
    (void)count;
    (void)attachments;
}

static void test_pass_actions_capture_values_and_attachment_enums(void) {
    /* Drivers without the optional entry point would skip the recorded call. */
    PFNGLINVALIDATEFRAMEBUFFERPROC driver = glad_glInvalidateFramebuffer;
    if (driver == NULL) {
        glad_glInvalidateFramebuffer = invalidate_noop;
    }
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.load_depth = true, .discard_depth = true});
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    nt_gfx_end_pass();
    TEST_ASSERT_EQUAL_HEX32(GL_NO_ERROR, glGetError());
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    TEST_ASSERT_FALSE(capture.overflow);
    nt_gfx_event_t begin = {0};
    nt_gfx_event_t invalidate = {0};
    uint32_t begins = 0;
    uint32_t invalidates = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_OP && event->operation == NT_GFX_OP_PASS) {
            begin = *event;
            begins++;
        }
        if (event->kind == NT_GFX_EVENT_BACKEND && event->detail == NT_GFX_GL_glInvalidateFramebuffer) {
            invalidate = *event;
            invalidates++;
        }
    }
    glad_glInvalidateFramebuffer = driver;
    TEST_ASSERT_EQUAL_UINT32(1, begins);
    TEST_ASSERT_EQUAL_UINT32(1, invalidates);
    TEST_ASSERT_FALSE(begin.data.pass.load_color);
    TEST_ASSERT_TRUE(begin.data.pass.load_depth);
    TEST_ASSERT_FALSE(begin.data.pass.discard_color);
    TEST_ASSERT_TRUE(begin.data.pass.discard_depth);
    TEST_ASSERT_EQUAL_HEX32(GL_FRAMEBUFFER, invalidate.data.backend.args[0]);
    TEST_ASSERT_EQUAL_UINT32(2, invalidate.data.backend.args[1]);
    TEST_ASSERT_EQUAL_HEX32(GL_DEPTH, invalidate.data.backend.args[2]);
    TEST_ASSERT_EQUAL_HEX32(GL_STENCIL, invalidate.data.backend.args[3]);
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.last_frame.gl[NT_GFX_GL_glInvalidateFramebuffer]);
}

static void test_explicit_clear_records_issued_calls_and_skips_empty_selections(void) {
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    const nt_clear_desc_t expected = {.color = true, .depth = true, .clear_color = {0.25F, 0.5F, 0.75F, 1}, .clear_depth = 0.5F};
    nt_clear_desc_t desc = expected;
    nt_gfx_clear(&desc);
    memset(&desc, 0, sizeof(desc));
    nt_gfx_clear(&expected);
    uint32_t before[NT_GFX_GL_COUNT];
    memcpy(before, g_nt_gfx.counters.gl, sizeof(before));
    nt_gfx_clear(&(nt_clear_desc_t){.clear_color = {1, 0, 0, 1}, .clear_depth = 0.25F});
    TEST_ASSERT_EQUAL_MEMORY(before, g_nt_gfx.counters.gl, sizeof(before));
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(3, g_nt_gfx.last_frame.accepted[NT_GFX_OP_CLEAR]);
    TEST_ASSERT_EQUAL_UINT32(3, g_nt_gfx.last_frame.gl[NT_GFX_GL_glClear]);
    TEST_ASSERT_EQUAL_UINT32(3, captured_calls(NT_GFX_GL_glClear));
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    uint32_t selected = 0;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_BACKEND && event->detail == NT_GFX_GL_glClear) {
            TEST_ASSERT_EQUAL_HEX32(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, event->data.backend.args[0]);
        }
        if (event->kind == NT_GFX_EVENT_OP && event->operation == NT_GFX_OP_CLEAR && event->data.clear.color) {
            TEST_ASSERT_EQUAL_UINT32(NT_GFX_OBJECT_RENDER_TARGET, event->object_kind);
            TEST_ASSERT_EQUAL_UINT32(0, event->object);
            TEST_ASSERT_TRUE(event->data.clear.depth);
            TEST_ASSERT_EQUAL_MEMORY(expected.clear_color, event->data.clear.clear_color, sizeof(expected.clear_color));
            TEST_ASSERT_EQUAL_MEMORY(&expected.clear_depth, &event->data.clear.clear_depth, sizeof(float));
            selected++;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(2, selected);
}

static void test_readback_is_recorded_as_issued_call(void) {
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    uint8_t pixel[4] = {0};
    TEST_ASSERT_TRUE(nt_gfx_read_pixels((nt_render_target_t){0}, 0, 0, 1, 1, pixel, sizeof(pixel)));
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT64(16, g_nt_gfx.last_frame.buffer_upload_bytes);

    nt_gfx_update_buffer(buffer, 0, data, sizeof(data));
    nt_gfx_update_buffer(buffer, 8, data, 12);
    nt_gfx_counters_t live = g_nt_gfx.counters;
    TEST_ASSERT_EQUAL_UINT64(2, live.buffer_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(76, live.buffer_upload_bytes);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_draw_calls(&live));
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_update_texture(texture, 0, 1, 3, 1, pixels);
    nt_gfx_end_frame();
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
}

/* Injected GL_OUT_OF_MEMORY after a real upload verifies no glGetError polling
 * during creation (a blocking round trip on WebGL), not actual VRAM exhaustion. */
static void test_texture_create_reads_no_gl_error(void) {
    const uint8_t pixels[64] = {0};
    nt_gfx_end_frame();
    nt_gfx_begin_frame(); /* init drains errors; measure the create's frame alone */
    s_upload_error = GL_OUT_OF_MEMORY;
    nt_texture_t texture = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 4, .height = 4, .format = NT_TEXTURE_FORMAT_RGBA8, .data = pixels});
    s_upload_error = s_pending_error = GL_NO_ERROR;
    TEST_ASSERT_NOT_EQUAL_UINT32(0, texture.id);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    const nt_gfx_counters_t *snapshot = &g_nt_gfx.last_frame;
    TEST_ASSERT_EQUAL_UINT32(0, snapshot->gl[NT_GFX_GL_glGetError]);
    TEST_ASSERT_EQUAL_UINT64(1, snapshot->texture_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(64, snapshot->texture_upload_bytes);
    nt_gfx_destroy_texture(texture);
}

static void test_repeated_frames_separate_requests_from_issued_calls(void) {
    const char *vs_source = "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }";
    const char *fs_source = "precision mediump float; layout(std140) uniform Draw { vec4 u_color; }; out vec4 color; void main() { color = u_color; }";
    nt_shader_t vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = vs_source});
    nt_shader_t fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = fs_source});
    nt_program_t program = nt_gfx_make_program(vs, fs, NULL);
    nt_test_gfx_link_wait(program);
    nt_pipeline_t pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){0});
    const float block[16] = {0};
    const float color[4] = {1.0F, 0.5F, 0.0F, 1.0F};
#if NT_GFX_CAPTURE_ENABLED
    nt_gfx_capture_request();
#endif
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    for (uint32_t frame = 0; frame < 2; frame++) {
        s_program_calls = s_vao_calls = s_ubo_calls = 0;
        nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
        for (uint32_t repeat = 0; repeat < 2; repeat++) {
            nt_gfx_bind_pipeline(pipeline);
            nt_gfx_bind_vertex_input(vi);
            nt_gfx_bind_uniform_block(NT_GFX_SLOT_DRAW, color, sizeof(color));
        }
        nt_gfx_bind_uniform_block(NT_GFX_SLOT_GLOBALS, block, sizeof(block));
        nt_gfx_draw(0, 3);
        nt_gfx_end_pass();
        nt_gfx_end_frame();
        nt_gfx_begin_frame();
#if NT_GFX_CAPTURE_ENABLED
        if (frame == 1) {
            nt_gfx_capture_view_t capture = nt_gfx_capture_read();
            bool viewport_known = false;
            for (uint32_t i = 0; i < capture.count; i++) {
                const nt_gfx_event_t *e = &capture.events[i];
                if (e->kind == NT_GFX_EVENT_INITIAL && e->operation == NT_GFX_OP_VIEWPORT) {
                    TEST_ASSERT_EQUAL_UINT32(g_nt_window.fb_width, e->data.state.integers[2]);
                    TEST_ASSERT_EQUAL_UINT32(g_nt_window.fb_height, e->data.state.integers[3]);
                    viewport_known = true;
                }
            }
            TEST_ASSERT_TRUE(viewport_known);
        }
#endif
        nt_gfx_counters_t c = g_nt_gfx.last_frame;
        /* Equal binds end CACHE: pipeline, vertex input and the equal Draw block once per pass.
         * A uniform block is frame data and is requested every frame; the backend skips a range
         * GL already holds, so the second frame's equal offsets issue nothing. */
        TEST_ASSERT_EQUAL_UINT32(1, c.accepted[NT_GFX_OP_PIPELINE]);
        TEST_ASSERT_EQUAL_UINT32(1, c.accepted[NT_GFX_OP_VERTEX_INPUT]);
        TEST_ASSERT_EQUAL_UINT32(2, c.accepted[NT_GFX_OP_UBO]);
        TEST_ASSERT_EQUAL_UINT32(s_program_calls, c.gl[NT_GFX_GL_glUseProgram]);
        TEST_ASSERT_EQUAL_UINT32(s_vao_calls, c.gl[NT_GFX_GL_glBindVertexArray]);
        TEST_ASSERT_EQUAL_UINT32(s_ubo_calls, c.gl[NT_GFX_GL_glBindBufferRange]);
        TEST_ASSERT_EQUAL_UINT32(frame == 0 ? 1 : 0, c.gl[NT_GFX_GL_glUseProgram]);
        TEST_ASSERT_EQUAL_UINT32(frame == 0 ? 2 : 0, c.gl[NT_GFX_GL_glBindBufferRange]);
#if NT_GFX_CAPTURE_ENABLED
        TEST_ASSERT_EQUAL_UINT32(c.gl[NT_GFX_GL_glUseProgram], captured_calls(NT_GFX_GL_glUseProgram));
        TEST_ASSERT_EQUAL_UINT32(c.gl[NT_GFX_GL_glBindVertexArray], captured_calls(NT_GFX_GL_glBindVertexArray));
        /* An empty frame starts the next recording only after this one was read. */
        nt_gfx_capture_request();
        nt_gfx_end_frame();
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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_counters_t counters = g_nt_gfx.last_frame;
    TEST_ASSERT_EQUAL_UINT64(4, counters.texture_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(80, counters.texture_upload_bytes);
    TEST_ASSERT_EQUAL_UINT64(s_texture_calls, counters.texture_upload_calls);
    TEST_ASSERT_EQUAL_UINT64(s_texture_bytes, counters.texture_upload_bytes);
}

#if NT_GFX_CAPTURE_ENABLED
/* A capture that starts after an instanced bind names the VAO's instance tuple: the layout is
 * defined with its stride, the backend VAO record carries the buffer's GL name, the offset its
 * pointers hold and the layout slot, and the bind request carries layout, buffer and offset. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one walk checks three record kinds
static void test_a_captured_vertex_input_names_its_instance_tuple(void) {
    nt_buffer_t vertices = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 64});
    const nt_buffer_t instances = nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX + 1);
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .vertex_buffer = vertices,
        .layout = {.attr_count = 1, .stride = 8, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 2}}},
    });
    const nt_instance_layout_t layout = nt_gfx_make_instance_layout(&(nt_vertex_layout_t){.attr_count = 1, .stride = 16, .attrs = {{.location = 1, .type = NT_VERTEX_FLOAT, .count = 4}}});
    uint32_t offset = 0;
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX + 1, 32, 4, &offset), 0, 32);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_vertex_input_instanced(vi, layout, instances, offset);
    nt_gfx_end_pass();
    nt_gfx_capture_request();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX + 1, 32, 4, &offset), 0, 32);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    nt_gfx_bind_vertex_input_instanced(vi, layout, instances, offset + 16U);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_capture_view_t capture = nt_gfx_capture_read();
    bool layout_defined = false;
    bool vao_defined = false;
    bool requested = false;
    for (uint32_t i = 0; i < capture.count; i++) {
        const nt_gfx_event_t *event = &capture.events[i];
        if (event->kind == NT_GFX_EVENT_DEFINITION && event->operation == NT_GFX_OP_CREATE && event->object_kind == NT_GFX_OBJECT_INSTANCE_LAYOUT && event->object == layout.id) {
            TEST_ASSERT_EQUAL_UINT64(16, event->data.resource.size);
            layout_defined = true;
        }
        if (event->kind == NT_GFX_EVENT_DEFINITION && event->operation == NT_GFX_OP_STATE && event->detail == NT_GFX_OBJECT_VERTEX_INPUT && event->data.backend.args[0] == nt_pool_slot_index(vi.id)) {
            TEST_ASSERT_NOT_EQUAL_UINT32(0, event->data.backend.args[1]);
            TEST_ASSERT_NOT_EQUAL_UINT32(0, event->data.backend.args[2]);
            TEST_ASSERT_EQUAL_UINT32(0, event->data.backend.args[3]);
            TEST_ASSERT_EQUAL_UINT32(nt_pool_slot_index(layout.id), event->data.backend.args[4]);
            vao_defined = true;
        }
        if (event->kind == NT_GFX_EVENT_OP && event->operation == NT_GFX_OP_VERTEX_INPUT && event->object == vi.id) {
            TEST_ASSERT_EQUAL_UINT32(layout.id, event->data.binding.slot);
            TEST_ASSERT_EQUAL_UINT32(instances.id, event->data.binding.secondary);
            TEST_ASSERT_EQUAL_UINT32(offset + 16U, event->data.binding.offset);
            requested = true;
        }
    }
    TEST_ASSERT_TRUE(layout_defined);
    TEST_ASSERT_TRUE(vao_defined);
    TEST_ASSERT_TRUE(requested);
}
#endif

static void test_attribute_pointer_calls_are_counted_per_issue(void) {
    nt_buffer_t vertices = nt_gfx_make_buffer(&(nt_buffer_desc_t){.type = NT_BUFFER_VERTEX, .size = 64});
    nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .vertex_buffer = vertices,
        .layout = {.attr_count = 1, .stride = 8, .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 2}}},
    });
    const nt_instance_layout_t layout = nt_gfx_make_instance_layout(&(nt_vertex_layout_t){.attr_count = 1, .stride = 16, .attrs = {{.location = 1, .type = NT_VERTEX_FLOAT, .count = 4}}});
    /* Creation points the static attribute; the instance side waits for the first bind. */
    TEST_ASSERT_EQUAL_UINT32(1, g_nt_gfx.counters.gl[NT_GFX_GL_glVertexAttribPointer]);
    TEST_ASSERT_EQUAL_UINT32(1, s_attribute_calls);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    uint32_t offset = 0;
    memset(nt_gfx_frame_alloc(NT_GFX_FRAME_VERTEX, 32, 4, &offset), 0, 32);
    nt_gfx_bind_vertex_input_instanced(vi, layout, nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX), offset);
    nt_gfx_bind_vertex_input_instanced(vi, layout, nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX), offset + 16U);
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_counters_t counters = g_nt_gfx.last_frame;
    /* One pointer at creation, one for the first instanced bind, one for the move to 16. */
    TEST_ASSERT_EQUAL_UINT32(0, offset);
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
    RUN_TEST(test_capture_publishes_attachment_mappings_and_skip_reasons);
    RUN_TEST(test_new_program_defines_sampler_names);
    RUN_TEST(test_issued_calls_record_floats_names_and_payloads);
    RUN_TEST(test_frame_storage_upload_is_issued_before_the_replayed_draw);
    RUN_TEST(test_complete_capture_matches_gl_counters);
    RUN_TEST(test_merged_draws_link_to_the_first_draw);
    RUN_TEST(test_nested_operation_links_to_its_parent);
    RUN_TEST(test_overflow_inside_an_operation_keeps_recorded_results);
    RUN_TEST(test_readback_is_recorded_as_issued_call);
    RUN_TEST(test_explicit_clear_records_issued_calls_and_skips_empty_selections);
    RUN_TEST(test_pass_actions_capture_values_and_attachment_enums);
    RUN_TEST(test_shutdown_while_recording_writes_no_record);
    RUN_TEST(test_a_captured_vertex_input_names_its_instance_tuple);
#endif
    RUN_TEST(test_payloads_before_render_land_in_their_frame);
    RUN_TEST(test_texture_mips_storage_and_subrect_payloads);
    RUN_TEST(test_r8_odd_width_update_counts_exact_bytes);
    RUN_TEST(test_texture_create_reads_no_gl_error);
    RUN_TEST(test_repeated_frames_separate_requests_from_issued_calls);
    RUN_TEST(test_compressed_mips_use_issued_block_sizes);
    RUN_TEST(test_attribute_pointer_calls_are_counted_per_issue);
    int failures = UNITY_END();
    nt_window_shutdown();
    return failures;
}
