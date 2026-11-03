#include "graphics/nt_gfx_capture_text.h"

#include "core/nt_assert.h"
#include "log/nt_log.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region names
#define NT_NAME_(prefix, x) [prefix##x] = #x
static const char *const s_operation_names[NT_GFX_OP_COUNT] = {
    NT_NAME_(NT_GFX_OP_, PASS),        NT_NAME_(NT_GFX_OP_, END_PASS),       NT_NAME_(NT_GFX_OP_, CREATE),         NT_NAME_(NT_GFX_OP_, DESTROY),
    NT_NAME_(NT_GFX_OP_, PIPELINE),    NT_NAME_(NT_GFX_OP_, VERTEX_INPUT),   NT_NAME_(NT_GFX_OP_, TEXTURE_SET),    NT_NAME_(NT_GFX_OP_, TEXTURE),
    NT_NAME_(NT_GFX_OP_, SAMPLER),     NT_NAME_(NT_GFX_OP_, VIEWPORT),       NT_NAME_(NT_GFX_OP_, SCISSOR),        NT_NAME_(NT_GFX_OP_, SCISSOR_ENABLE),
    NT_NAME_(NT_GFX_OP_, UBO),         NT_NAME_(NT_GFX_OP_, BUFFER_UPLOAD),  NT_NAME_(NT_GFX_OP_, TEXTURE_UPLOAD), NT_NAME_(NT_GFX_OP_, ATTRIBUTE),
    NT_NAME_(NT_GFX_OP_, DRAW),        NT_NAME_(NT_GFX_OP_, DRAW_INSTANCED), NT_NAME_(NT_GFX_OP_, DRAW_INDEXED),   NT_NAME_(NT_GFX_OP_, DRAW_INDEXED_INSTANCED),
    NT_NAME_(NT_GFX_OP_, CONTEXT),     NT_NAME_(NT_GFX_OP_, STATE),          NT_NAME_(NT_GFX_OP_, READ_PIXELS),    NT_NAME_(NT_GFX_OP_, SEGMENT_BEGIN),
    NT_NAME_(NT_GFX_OP_, SEGMENT_END), NT_NAME_(NT_GFX_OP_, SEGMENT_POLL),   NT_NAME_(NT_GFX_OP_, GPU_TIMING),     NT_NAME_(NT_GFX_OP_, TIMER_DISJOINT),
    NT_NAME_(NT_GFX_OP_, CLEAR),
#if !defined(NT_PLATFORM_WEB)
    NT_NAME_(NT_GFX_OP_, BUFFER_READ), NT_NAME_(NT_GFX_OP_, BUFFER_WAIT),
#endif
};

static const char *const s_kind_names[] = {
    NT_NAME_(NT_GFX_EVENT_, OP),       NT_NAME_(NT_GFX_EVENT_, BACKEND), NT_NAME_(NT_GFX_EVENT_, SKIP),       NT_NAME_(NT_GFX_EVENT_, INITIAL),     NT_NAME_(NT_GFX_EVENT_, DEFINITION),
    NT_NAME_(NT_GFX_EVENT_, ARGUMENT), NT_NAME_(NT_GFX_EVENT_, NOTE),    NT_NAME_(NT_GFX_EVENT_, NOTE_FIELD), NT_NAME_(NT_GFX_EVENT_, MARKER_PUSH), NT_NAME_(NT_GFX_EVENT_, MARKER_POP),
};

static const char *const s_object_names[] = {
    NT_NAME_(NT_GFX_OBJECT_, NONE),   NT_NAME_(NT_GFX_OBJECT_, SHADER),  NT_NAME_(NT_GFX_OBJECT_, PROGRAM), NT_NAME_(NT_GFX_OBJECT_, PIPELINE),      NT_NAME_(NT_GFX_OBJECT_, VERTEX_INPUT),
    NT_NAME_(NT_GFX_OBJECT_, BUFFER), NT_NAME_(NT_GFX_OBJECT_, TEXTURE), NT_NAME_(NT_GFX_OBJECT_, SAMPLER), NT_NAME_(NT_GFX_OBJECT_, RENDER_TARGET), NT_NAME_(NT_GFX_OBJECT_, INSTANCE_LAYOUT),
};

static const char *const s_result_names[] = {
    NT_NAME_(NT_GFX_RESULT_, NONE),         NT_NAME_(NT_GFX_RESULT_, ACCEPTED),       NT_NAME_(NT_GFX_RESULT_, CACHE),   NT_NAME_(NT_GFX_RESULT_, INACTIVE),
    NT_NAME_(NT_GFX_RESULT_, CONTEXT_LOST), NT_NAME_(NT_GFX_RESULT_, INVALID_HANDLE), NT_NAME_(NT_GFX_RESULT_, UNREADY), NT_NAME_(NT_GFX_RESULT_, UNKNOWN),
};

static const char *const s_note_type_names[] = {
    NT_NAME_(NT_GFX_NOTE_, U32), NT_NAME_(NT_GFX_NOTE_, I32), NT_NAME_(NT_GFX_NOTE_, U64), NT_NAME_(NT_GFX_NOTE_, F32), NT_NAME_(NT_GFX_NOTE_, STR), NT_NAME_(NT_GFX_NOTE_, HANDLE),
};
#undef NT_NAME_

static const char *const s_platform_names[] = {[NT_GFX_CAPTURE_NATIVE] = "native", [NT_GFX_CAPTURE_WEB] = "web"};
static const char *const s_layer_names[] = {[NT_GFX_INITIAL_FRONTEND] = "FRONTEND", [NT_GFX_INITIAL_BACKEND] = "BACKEND"};

_Static_assert(sizeof(s_kind_names) / sizeof(s_kind_names[0]) == NT_GFX_EVENT_MARKER_POP + 1, "every event kind has a name");
_Static_assert(sizeof(s_object_names) / sizeof(s_object_names[0]) == NT_GFX_OBJECT_INSTANCE_LAYOUT + 1, "every object kind has a name");
_Static_assert(sizeof(s_result_names) / sizeof(s_result_names[0]) == NT_GFX_RESULT_UNKNOWN + 1, "every result has a name");
_Static_assert(sizeof(s_note_type_names) / sizeof(s_note_type_names[0]) == NT_GFX_NOTE_HANDLE + 1, "every note type has a name");
_Static_assert(sizeof(s_platform_names) / sizeof(s_platform_names[0]) == NT_GFX_CAPTURE_WEB + 1, "every platform has a name");

#define NT_GFX_GL_CALL_NAME_(name) #name,
static const char *const s_gl_call_names[NT_GFX_GL_COUNT] = {NULL, NT_GFX_GL_CALLS(NT_GFX_GL_CALL_NAME_)};
#undef NT_GFX_GL_CALL_NAME_

const char *nt_gfx_gl_call_name(uint32_t call) { return call < NT_GFX_GL_COUNT ? s_gl_call_names[call] : NULL; }

#define NT_COUNT_(table) ((uint32_t)(sizeof(table) / sizeof((table)[0])))

/* Tables are complete by the asserts above (and a test for operations); a value outside one is a broken record. */
static const char *table_name(const char *const *names, uint32_t count, uint32_t value) {
    NT_ASSERT(value < count && names[value] != NULL && "capture text: value outside its name table");
    return names[value];
}
#define NT_TABLE_NAME_(table, value) table_name((table), NT_COUNT_(table), (value))

const char *nt_gfx_operation_name(uint32_t operation) { return operation < NT_COUNT_(s_operation_names) ? s_operation_names[operation] : NULL; }
const char *nt_gfx_result_name(uint32_t result) { return result < NT_COUNT_(s_result_names) ? s_result_names[result] : NULL; }
const char *nt_gfx_object_kind_name(uint32_t kind) { return kind < NT_COUNT_(s_object_names) ? s_object_names[kind] : NULL; }
const char *nt_gfx_event_kind_name(uint32_t kind) { return kind < NT_COUNT_(s_kind_names) ? s_kind_names[kind] : NULL; }
// #endregion

// #region layout
/* Field lists shared by format, parse and readers (nt_gfx_capture_fields), so they cannot drift. */
#define NT_AT_(member) (uint16_t)offsetof(nt_gfx_event_t, data.member)
#define NT_ONE_(member, name, type, meaning, sub) {(name), NT_AT_(member), NT_GFX_FIELD_##type, 1, NT_GFX_MEANING_##meaning, (uint8_t)(sub)}
#define NT_LIST_(member, name, type, meaning, sub) {(name), NT_AT_(member), NT_GFX_FIELD_##type, (uint8_t)NT_COUNT_(((nt_gfx_event_t *)NULL)->data.member), NT_GFX_MEANING_##meaning, (uint8_t)(sub)}
/* A scalar named after its member. */
#define NT_F_(arm, member, type, meaning, sub) NT_ONE_(arm.member, #member, type, meaning, sub)
#define NT_LABEL_(arm) NT_F_(arm, label, STR, NUMBER, 0)
#define NT_SLOT_(arm, kind) NT_F_(arm, slot, U32, SLOT, NT_GFX_OBJECT_##kind)
#define NT_HANDLE_(arm, member, kind) NT_F_(arm, member, U32, HANDLE, NT_GFX_OBJECT_##kind)
#define NT_GL_NAME_(arm, member, kind) NT_F_(arm, member, U32, GL_NAME, NT_GFX_GL_OBJECT_##kind)
#define NT_ENUM_(arm, member, domain) NT_F_(arm, member, U32, ENUM, NT_GFX_DOMAIN_##domain)
#define NT_GL_ENUM_(arm, member, domain) NT_F_(arm, member, U32, GL_ENUM, NT_GFX_DOMAIN_##domain)
#define NT_N_(arm, member) NT_F_(arm, member, U32, NUMBER, 0)

typedef nt_gfx_capture_field_t field_t;

static const field_t k_backend[] = {NT_LIST_(backend.args, "args", U32S, GL_ARGS, 0), NT_LIST_(backend.values, "values", F32S, GL_ARGS, 0), NT_F_(backend, bytes, U64, BYTES, 0)};
static const field_t k_key[] = {NT_LIST_(backend.args, "key", U32S, GL_ARGS, 0), NT_LIST_(backend.values, "values", F32S, GL_ARGS, 0), NT_F_(backend, bytes, U64, BYTES, 0)};
static const field_t k_shader[] = {NT_GL_NAME_(shader, gl_name, SHADER), NT_ENUM_(shader, stage, SHADER_TYPE), NT_LABEL_(shader)};
static const field_t k_program[] = {NT_SLOT_(program, PROGRAM), NT_HANDLE_(program, vertex_shader, SHADER), NT_HANDLE_(program, fragment_shader, SHADER), NT_LABEL_(program)};
static const field_t k_pipeline[] = {NT_SLOT_(pipeline, PIPELINE), NT_HANDLE_(pipeline, program, PROGRAM), NT_LABEL_(pipeline)};
static const field_t k_vertex_input[] = {NT_SLOT_(vertex_input, VERTEX_INPUT), NT_HANDLE_(vertex_input, vertex_buffer, BUFFER), NT_HANDLE_(vertex_input, index_buffer, BUFFER),
                                         NT_ENUM_(vertex_input, index_type, INDEX_TYPE), NT_LABEL_(vertex_input)};
static const field_t k_buffer[] = {
    NT_F_(buffer, size, U64, BYTES, 0), NT_SLOT_(buffer, BUFFER), NT_ENUM_(buffer, type, BUFFER_TYPE), NT_ENUM_(buffer, usage, BUFFER_USAGE), NT_ENUM_(buffer, index_type, INDEX_TYPE),
    NT_F_(buffer, data, BOOL, BOOL, 0), NT_LABEL_(buffer)};
static const field_t k_texture[] = {NT_SLOT_(texture, TEXTURE),
                                    NT_N_(texture, width),
                                    NT_N_(texture, height),
                                    NT_ENUM_(texture, format, TEXTURE_FORMAT),
                                    NT_N_(texture, levels),
                                    NT_HANDLE_(texture, sampler, SAMPLER),
                                    NT_F_(texture, gen_mipmaps, BOOL, BOOL, 0),
                                    NT_LABEL_(texture)};
static const field_t k_sampler[] = {NT_GL_NAME_(sampler, gl_name, SAMPLER),  NT_ENUM_(sampler, min_filter, TEXTURE_FILTER), NT_ENUM_(sampler, mag_filter, TEXTURE_FILTER),
                                    NT_ENUM_(sampler, wrap_u, TEXTURE_WRAP), NT_ENUM_(sampler, wrap_v, TEXTURE_WRAP),       NT_ENUM_(sampler, compare, COMPARE_FUNC)};
static const field_t k_render_target[] = {NT_SLOT_(render_target, RENDER_TARGET),
                                          NT_HANDLE_(render_target, color, TEXTURE),
                                          NT_HANDLE_(render_target, depth, TEXTURE),
                                          NT_N_(render_target, width),
                                          NT_N_(render_target, height),
                                          NT_ENUM_(render_target, color_format, TEXTURE_FORMAT),
                                          NT_ENUM_(render_target, depth_format, TEXTURE_FORMAT),
                                          NT_LABEL_(render_target)};
static const field_t k_instance_layout[] = {NT_SLOT_(instance_layout, INSTANCE_LAYOUT), NT_F_(instance_layout, stride, U32, BYTES, 0),
                                            NT_F_(instance_layout, locations, U32, FLAGS, NT_GFX_DOMAIN_LOCATIONS)};
static const field_t k_attribute[] = {NT_HANDLE_(attribute, buffer, BUFFER),      NT_F_(attribute, offset, U32, BYTES, 0),
                                      NT_F_(attribute, stride, U32, BYTES, 0),    NT_N_(attribute, location),
                                      NT_ENUM_(attribute, type, VERTEX_TYPE),     NT_N_(attribute, count),
                                      NT_F_(attribute, normalized, U32, BOOL, 0), NT_N_(attribute, divisor)};
static const field_t k_upload[] = {NT_F_(upload, size, U64, BYTES, 0), NT_F_(upload, offset, U32, BYTES, 0), NT_F_(upload, data, BOOL, BOOL, 0)};
#define NT_RECT_ NT_F_(rect, x, I32, NUMBER, 0), NT_F_(rect, y, I32, NUMBER, 0), NT_F_(rect, width, I32, NUMBER, 0), NT_F_(rect, height, I32, NUMBER, 0)
static const field_t k_rect[] = {NT_RECT_};
static const field_t k_read[] = {NT_RECT_, NT_HANDLE_(rect, source, RENDER_TARGET)};
#undef NT_RECT_
static const field_t k_toggle[] = {NT_F_(toggle, on, BOOL, BOOL, 0)};
static const field_t k_frontend[] = {NT_F_(frontend, context_lost, BOOL, BOOL, 0)};
static const field_t k_texture_set[] = {NT_N_(texture_set, count)};
static const field_t k_texture_binding[] = {NT_F_(texture_binding, name, U32, HASH, 0), NT_HANDLE_(texture_binding, sampler, SAMPLER)};
static const field_t k_instance_binding[] = {NT_HANDLE_(instance_binding, buffer, BUFFER), NT_HANDLE_(instance_binding, layout, INSTANCE_LAYOUT), NT_F_(instance_binding, offset, U32, BYTES, 0)};
static const field_t k_ubo[] = {NT_N_(ubo, slot), NT_F_(ubo, offset, U32, BYTES, 0), NT_F_(ubo, size, U32, BYTES, 0)};
static const field_t k_segment[] = {NT_F_(segment, name, U32, HASH, 0)};
static const field_t k_draw[] = {NT_N_(draw, first), NT_N_(draw, count), NT_N_(draw, vertices), NT_N_(draw, instances), NT_HANDLE_(draw, vertex_input, VERTEX_INPUT)};
static const field_t k_pass[] = {
    NT_HANDLE_(pass, target, RENDER_TARGET), NT_LIST_(pass.color, "color", F32S, NUMBER, 0), NT_F_(pass, depth, F32, NUMBER, 0),        NT_F_(pass, load_color, BOOL, BOOL, 0),
    NT_F_(pass, load_depth, BOOL, BOOL, 0),  NT_F_(pass, discard_color, BOOL, BOOL, 0),      NT_F_(pass, discard_depth, BOOL, BOOL, 0),
};
static const field_t k_clear[] = {NT_LIST_(clear.clear_color, "clear_color", F32S, NUMBER, 0), NT_F_(clear, clear_depth, F32, NUMBER, 0), NT_F_(clear, color, BOOL, BOOL, 0),
                                  NT_F_(clear, depth, BOOL, BOOL, 0)};
#define NT_PIPELINE_STATE_                                                                                                                                                                             \
    NT_F_(pipeline_state, depth_test, U32, BOOL, 0), NT_F_(pipeline_state, depth_write, U32, BOOL, 0), NT_GL_ENUM_(pipeline_state, depth_func, GL_FUNC), NT_ENUM_(pipeline_state, cull, CULL_MODE),    \
        NT_F_(pipeline_state, blend, U32, BOOL, 0), NT_GL_ENUM_(pipeline_state, src_rgb, GL_BLEND_FACTOR), NT_GL_ENUM_(pipeline_state, dst_rgb, GL_BLEND_FACTOR),                                      \
        NT_GL_ENUM_(pipeline_state, src_alpha, GL_BLEND_FACTOR), NT_GL_ENUM_(pipeline_state, dst_alpha, GL_BLEND_FACTOR), NT_GL_ENUM_(pipeline_state, op_rgb, GL_BLEND_EQUATION),                      \
        NT_GL_ENUM_(pipeline_state, op_alpha, GL_BLEND_EQUATION), NT_F_(pipeline_state, polygon_offset, U32, BOOL, 0), NT_LIST_(pipeline_state.blend_color, "blend_color", F32S, NUMBER, 0),           \
        NT_F_(pipeline_state, offset_factor, F32, NUMBER, 0), NT_F_(pipeline_state, offset_units, F32, NUMBER, 0)
static const field_t k_pipeline_definition[] = {NT_F_(pipeline_state, program_slot, U32, SLOT, NT_GFX_OBJECT_PROGRAM), NT_PIPELINE_STATE_};
static const field_t k_pipeline_inherited[] = {NT_GL_NAME_(pipeline_state, program_gl, PROGRAM), NT_PIPELINE_STATE_};
#undef NT_PIPELINE_STATE_
#define NT_BACKEND_RESOURCE_(kind, gl_kind) NT_F_(backend_resource, slot, U32, SLOT, NT_GFX_OBJECT_##kind), NT_GL_NAME_(backend_resource, gl_name, gl_kind)
static const field_t k_backend_program[] = {NT_BACKEND_RESOURCE_(PROGRAM, PROGRAM)};
static const field_t k_backend_buffer[] = {NT_BACKEND_RESOURCE_(BUFFER, BUFFER)};
static const field_t k_backend_texture[] = {NT_BACKEND_RESOURCE_(TEXTURE, TEXTURE)};
static const field_t k_backend_render_target[] = {NT_BACKEND_RESOURCE_(RENDER_TARGET, FRAMEBUFFER)};
static const field_t k_backend_vertex_input[] = {NT_BACKEND_RESOURCE_(VERTEX_INPUT, VERTEX_ARRAY), NT_GL_NAME_(backend_resource, instance_buffer, BUFFER),
                                                 NT_F_(backend_resource, instance_offset, U32, BYTES, 0), NT_F_(backend_resource, instance_layout, U32, SLOT, NT_GFX_OBJECT_INSTANCE_LAYOUT)};
#undef NT_BACKEND_RESOURCE_
static const field_t k_backend_state[] = {NT_GL_NAME_(backend_state, program, PROGRAM),
                                          NT_GL_NAME_(backend_state, vertex_array, VERTEX_ARRAY),
                                          NT_GL_NAME_(backend_state, framebuffer, FRAMEBUFFER),
                                          NT_GL_ENUM_(backend_state, active_texture, GL_TEXTURE_UNIT),
                                          NT_N_(backend_state, width),
                                          NT_N_(backend_state, height),
                                          NT_GL_NAME_(backend_state, upload_vertex_array, VERTEX_ARRAY),
                                          NT_GL_NAME_(backend_state, array_buffer, BUFFER)};
static const field_t k_texture_unit[] = {NT_N_(texture_unit, unit), NT_GL_NAME_(texture_unit, texture, TEXTURE), NT_GL_NAME_(texture_unit, sampler, SAMPLER)};
static const field_t k_program_sampler[] = {NT_F_(program_sampler, program, U32, SLOT, NT_GFX_OBJECT_PROGRAM), NT_F_(program_sampler, name, U32, HASH, 0), NT_N_(program_sampler, unit),
                                            NT_ENUM_(program_sampler, sampler_class, SAMPLER_CLASS), NT_F_(program_sampler, location, I32, NUMBER, 0)};
#undef NT_AT_
#undef NT_ONE_
#undef NT_LIST_
#undef NT_F_
#undef NT_LABEL_
#undef NT_SLOT_
#undef NT_HANDLE_
#undef NT_GL_NAME_
#undef NT_ENUM_
#undef NT_GL_ENUM_
#undef NT_N_

typedef struct {
    const field_t *fields;
    uint32_t count;
} shape_t;
#define NT_SHAPE_(fields) ((shape_t){(fields), NT_COUNT_(fields)})

static bool is_draw(uint32_t operation) {
    return operation == NT_GFX_OP_DRAW || operation == NT_GFX_OP_DRAW_INSTANCED || operation == NT_GFX_OP_DRAW_INDEXED || operation == NT_GFX_OP_DRAW_INDEXED_INSTANCED;
}

/* A CREATE request and its definition share their object's fields; a request leaves slot and label 0. */
static shape_t object_shape(uint32_t object_kind) {
    switch (object_kind) {
    case NT_GFX_OBJECT_SHADER:
        return NT_SHAPE_(k_shader);
    case NT_GFX_OBJECT_PROGRAM:
        return NT_SHAPE_(k_program);
    case NT_GFX_OBJECT_PIPELINE:
        return NT_SHAPE_(k_pipeline);
    case NT_GFX_OBJECT_VERTEX_INPUT:
        return NT_SHAPE_(k_vertex_input);
    case NT_GFX_OBJECT_BUFFER:
        return NT_SHAPE_(k_buffer);
    case NT_GFX_OBJECT_TEXTURE:
        return NT_SHAPE_(k_texture);
    case NT_GFX_OBJECT_SAMPLER:
        return NT_SHAPE_(k_sampler);
    case NT_GFX_OBJECT_RENDER_TARGET:
        return NT_SHAPE_(k_render_target);
    case NT_GFX_OBJECT_INSTANCE_LAYOUT:
        return NT_SHAPE_(k_instance_layout);
    default:
        return (shape_t){0};
    }
}

/* A backend slot's GL name, by the resource kind in detail. */
static shape_t backend_resource_shape(uint32_t resource) {
    switch (resource) {
    case NT_GFX_OBJECT_PROGRAM:
        return NT_SHAPE_(k_backend_program);
    case NT_GFX_OBJECT_BUFFER:
        return NT_SHAPE_(k_backend_buffer);
    case NT_GFX_OBJECT_TEXTURE:
        return NT_SHAPE_(k_backend_texture);
    case NT_GFX_OBJECT_RENDER_TARGET:
        return NT_SHAPE_(k_backend_render_target);
    case NT_GFX_OBJECT_VERTEX_INPUT:
        return NT_SHAPE_(k_backend_vertex_input);
    default:
        return (shape_t){0};
    }
}

static shape_t initial_shape(const nt_gfx_event_t *e) {
    switch (e->operation) {
    case NT_GFX_OP_STATE:
        return e->detail == NT_GFX_INITIAL_BACKEND ? NT_SHAPE_(k_backend_state) : NT_SHAPE_(k_frontend);
    case NT_GFX_OP_SAMPLER:
        return NT_SHAPE_(k_program_sampler);
    case NT_GFX_OP_TEXTURE:
        return NT_SHAPE_(k_texture_unit);
    case NT_GFX_OP_PASS:
        return NT_SHAPE_(k_pass);
    case NT_GFX_OP_PIPELINE:
        return NT_SHAPE_(k_pipeline_inherited);
    case NT_GFX_OP_VIEWPORT:
    case NT_GFX_OP_SCISSOR:
        return NT_SHAPE_(k_rect);
    case NT_GFX_OP_SCISSOR_ENABLE:
        return NT_SHAPE_(k_toggle);
    default:
        return (shape_t){0};
    }
}

static shape_t operation_shape(const nt_gfx_event_t *e) {
    if (is_draw(e->operation)) {
        return NT_SHAPE_(k_draw);
    }
    switch (e->operation) {
    case NT_GFX_OP_CREATE:
        return object_shape(e->object_kind);
    case NT_GFX_OP_BUFFER_UPLOAD:
        return NT_SHAPE_(k_upload);
    case NT_GFX_OP_PASS:
        return NT_SHAPE_(k_pass);
    case NT_GFX_OP_CLEAR:
        return NT_SHAPE_(k_clear);
    case NT_GFX_OP_VERTEX_INPUT:
        return NT_SHAPE_(k_instance_binding);
    case NT_GFX_OP_UBO:
        return NT_SHAPE_(k_ubo);
    case NT_GFX_OP_SEGMENT_BEGIN:
    case NT_GFX_OP_SEGMENT_POLL:
        return NT_SHAPE_(k_segment);
    case NT_GFX_OP_TEXTURE_SET:
        return NT_SHAPE_(k_texture_set);
    case NT_GFX_OP_SCISSOR:
    case NT_GFX_OP_VIEWPORT:
    case NT_GFX_OP_READ_PIXELS:
    case NT_GFX_OP_TEXTURE_UPLOAD:
        return NT_SHAPE_(k_rect);
#if !defined(NT_PLATFORM_WEB)
    case NT_GFX_OP_BUFFER_READ:
        return NT_SHAPE_(k_read);
#endif
    case NT_GFX_OP_SCISSOR_ENABLE:
    case NT_GFX_OP_GPU_TIMING:
        return NT_SHAPE_(k_toggle);
    default:
        return (shape_t){0};
    }
}

/* The fields a gfx record fills; the render architecture spec, "Frame observation", names them. */
static shape_t record_shape(const nt_gfx_event_t *e) {
    switch (e->kind) {
    case NT_GFX_EVENT_BACKEND:
        return NT_SHAPE_(k_backend);
    case NT_GFX_EVENT_SKIP:
        return e->detail != 0 ? NT_SHAPE_(k_key) : NT_SHAPE_(k_texture_binding);
    case NT_GFX_EVENT_ARGUMENT:
        return NT_SHAPE_(k_texture_binding);
    case NT_GFX_EVENT_DEFINITION:
        switch (e->operation) {
        case NT_GFX_OP_ATTRIBUTE:
            return NT_SHAPE_(k_attribute);
        case NT_GFX_OP_PIPELINE:
            return NT_SHAPE_(k_pipeline_definition);
        case NT_GFX_OP_STATE:
            return backend_resource_shape(e->detail);
        default:
            return object_shape(e->object_kind);
        }
    case NT_GFX_EVENT_INITIAL:
        return initial_shape(e);
    case NT_GFX_EVENT_OP:
        return operation_shape(e);
    default:
        return (shape_t){0};
    }
}

uint32_t nt_gfx_capture_fields(const nt_gfx_event_t *e, const nt_gfx_capture_field_t **out) {
    NT_ASSERT(e != NULL && out != NULL);
    const shape_t s = record_shape(e);
    *out = s.fields;
    return s.count;
}

/* How `detail` prints. It decides the shape of SKIP, INITIAL/STATE and DEFINITION/STATE, so a reader takes it before the fields. */
typedef enum { DETAIL_PLAIN, DETAIL_CALL, DETAIL_RESOURCE, DETAIL_LAYER, DETAIL_JOINED } detail_kind_t;

static detail_kind_t detail_kind(const nt_gfx_event_t *e) {
    if (e->kind == NT_GFX_EVENT_BACKEND || e->kind == NT_GFX_EVENT_SKIP) {
        return DETAIL_CALL; /* required on BACKEND; a SKIP without one is an inactive texture-set name */
    }
    if (e->operation == NT_GFX_OP_STATE && e->kind == NT_GFX_EVENT_DEFINITION) {
        return DETAIL_RESOURCE;
    }
    if (e->operation == NT_GFX_OP_STATE && e->kind == NT_GFX_EVENT_INITIAL) {
        return DETAIL_LAYER;
    }
    return e->kind == NT_GFX_EVENT_OP && is_draw(e->operation) ? DETAIL_JOINED : DETAIL_PLAIN; /* a merged draw keeps the link of the draw it joined */
}

/* An issued call and a cache skip's key count their arguments in arg_count. */
static bool issued_call(const nt_gfx_event_t *e) { return e->kind == NT_GFX_EVENT_BACKEND || (e->kind == NT_GFX_EVENT_SKIP && e->detail != 0); }

static size_t field_bytes(const field_t *f) {
    switch ((nt_gfx_field_type_t)f->type) {
    case NT_GFX_FIELD_U64:
        return sizeof(uint64_t);
    case NT_GFX_FIELD_BOOL:
        return sizeof(bool);
    case NT_GFX_FIELD_U32S:
    case NT_GFX_FIELD_F32S:
        return (size_t)f->size * sizeof(uint32_t);
    default:
        return sizeof(uint32_t);
    }
}

/* Counters with a fixed name, in print order; the arrays print by name after them. */
static const struct {
    const char *name;
    uint16_t offset;
    uint8_t bytes;
} k_counters[] = {
#define NT_COUNTER_(member) #member, (uint16_t)offsetof(nt_gfx_counters_t, member), (uint8_t)sizeof(((nt_gfx_counters_t *)NULL)->member)
    {NT_COUNTER_(vertices)},
    {NT_COUNTER_(indices)},
    {NT_COUNTER_(instances)},
    {NT_COUNTER_(buffer_upload_calls)},
    {NT_COUNTER_(buffer_upload_bytes)},
    {NT_COUNTER_(texture_upload_calls)},
    {NT_COUNTER_(texture_upload_bytes)},
    {NT_COUNTER_(stream_bytes)},
#undef NT_COUNTER_
};

/* The 4-byte element i of a list, as bits: a float list trims and counts by bits like an integer one. */
static uint32_t list_slot(const unsigned char *at, uint32_t i) {
    uint32_t v = 0;
    memcpy(&v, at + ((size_t)i * sizeof(v)), sizeof(v));
    return v;
}
// #endregion

// #region output
typedef struct {
    char *out;
    size_t cap;
    size_t len; /* bytes the text needs so far; may exceed cap */
} text_t;

static void put(text_t *t, const char *fmt, ...) NT_PRINTF_ATTR(2, 3);
static void put(text_t *t, const char *fmt, ...) {
    const bool room = t->len < t->cap;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(room ? t->out + t->len : NULL, room ? t->cap - t->len : 0, fmt, ap);
    va_end(ap);
    NT_ASSERT(n >= 0);
    t->len += (size_t)n;
}

static uint32_t float_bits(float f) {
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

/* The shortest %g that reads back to the same float32 (9 digits always do); NaN keeps its payload as bits. */
static void put_float(text_t *t, float f) {
    if (isnan(f)) {
        put(t, "nan:0x%08" PRIx32, float_bits(f));
        return;
    }
    char digits[32];
    for (int precision = 6; precision <= 9; precision++) {
        (void)snprintf(digits, sizeof(digits), "%.*g", precision, (double)f);
        if (float_bits(strtof(digits, NULL)) == float_bits(f)) {
            break;
        }
    }
    put(t, "%s", digits);
}

/* A string of the view, checked: the records name strings by offset. */
static const char *view_string(const nt_gfx_capture_view_t *view, nt_gfx_capture_string_t s) {
    NT_ASSERT(s < view->string_bytes && "capture text: a string offset outside the frame's strings");
    return view->strings + s;
}

static void put_string(text_t *t, const char *s) {
    put(t, "\"");
    for (size_t i = 0; s[i] != '\0'; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20U || c > 0x7eU || c == '"' || c == '\\') {
            put(t, "\\x%02x", c);
        } else {
            put(t, "%c", c);
        }
    }
    put(t, "\"");
}

/* count elements, or the prefix up to the last nonzero one when count is negative. */
static void put_list(text_t *t, const char *key, const unsigned char *at, nt_gfx_field_type_t type, uint32_t size, int count) {
    NT_ASSERT(count <= (int)size && "capture text: an issued call counts more arguments than its record holds");
    uint32_t n = (uint32_t)count;
    if (count < 0) {
        n = size;
        while (n > 0 && list_slot(at, n - 1U) == 0) {
            n--;
        }
    }
    if (n == 0) {
        return;
    }
    put(t, " %s=[", key);
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t bits = list_slot(at, i);
        if (i > 0) {
            put(t, ",");
        }
        if (type == NT_GFX_FIELD_U32S) {
            put(t, "%" PRIu32, bits);
        } else {
            float v = 0.0F;
            memcpy(&v, &bits, sizeof(v));
            put_float(t, v);
        }
    }
    put(t, "]");
}

/* Zero scalars are omitted; an issued call's lists print exactly their counted elements, so a zero argument stays visible. */
/* A nonzero integer field: unsigned, bool or signed. */
static void put_integer(text_t *t, const field_t *f, const unsigned char *at) {
    bool flag = false;
    int32_t sign = 0;
    memcpy(&flag, at, sizeof(flag));
    memcpy(&sign, at, sizeof(sign));
    if (f->type == NT_GFX_FIELD_I32 && sign != 0) {
        put(t, " %s=%" PRId32, f->name, sign);
        return;
    }
    const uint32_t v = f->type == NT_GFX_FIELD_BOOL ? (uint32_t)flag : list_slot(at, 0);
    if (f->type != NT_GFX_FIELD_I32 && v != 0) {
        put(t, " %s=%" PRIu32, f->name, v);
    }
}

static void put_fields(text_t *t, const nt_gfx_capture_view_t *view, const nt_gfx_event_t *e, shape_t shape) {
    const bool issued = issued_call(e);
    const unsigned char *base = (const unsigned char *)e;
    for (uint32_t i = 0; i < shape.count; i++) {
        const field_t *f = &shape.fields[i];
        const unsigned char *at = base + f->offset;
        uint64_t wide = 0;
        float real = 0.0F;
        switch ((nt_gfx_field_type_t)f->type) {
        case NT_GFX_FIELD_U32:
        case NT_GFX_FIELD_BOOL:
        case NT_GFX_FIELD_I32:
            put_integer(t, f, at);
            break;
        case NT_GFX_FIELD_U64:
            memcpy(&wide, at, sizeof(wide));
            if (wide != 0) {
                put(t, " %s=%" PRIu64, f->name, wide);
            }
            break;
        case NT_GFX_FIELD_F32:
            memcpy(&real, at, sizeof(real));
            if (float_bits(real) != 0) {
                put(t, " %s=", f->name);
                put_float(t, real);
            }
            break;
        case NT_GFX_FIELD_U32S:
            put_list(t, f->name, at, NT_GFX_FIELD_U32S, f->size, issued ? (int)(e->arg_count & 0x0FU) : -1);
            break;
        case NT_GFX_FIELD_F32S:
            put_list(t, f->name, at, NT_GFX_FIELD_F32S, f->size, issued ? e->arg_count >> 4 : -1);
            break;
        case NT_GFX_FIELD_STR: {
            nt_gfx_capture_string_t str = 0;
            memcpy(&str, at, sizeof(str));
            if (str != 0) {
                put(t, " %s=", f->name);
                put_string(t, view_string(view, str));
            }
            break;
        }
        }
    }
}

/* An issued call sets only the arguments it counts; other records count none. */
static void assert_args_counted(const nt_gfx_event_t *e, bool issued) {
    NT_ASSERT((issued || e->arg_count == 0) && "capture text: only issued calls count arguments");
    bool counted = true;
    for (uint32_t i = issued ? e->arg_count & 0x0FU : NT_COUNT_(e->data.backend.args); i < NT_COUNT_(e->data.backend.args); i++) {
        counted = counted && e->data.backend.args[i] == 0;
    }
    for (uint32_t i = issued ? (uint32_t)(e->arg_count >> 4) : NT_COUNT_(e->data.backend.values); i < NT_COUNT_(e->data.backend.values); i++) {
        counted = counted && float_bits(e->data.backend.values[i]) == 0;
    }
    NT_ASSERT(counted && "capture text: an issued call sets an argument it does not count");
}

/* Records are zeroed when reserved and each writer fills only fields of its record's shape, so the text is lossless. */
static void assert_data_in_shape(const nt_gfx_event_t *e, shape_t shape) {
    bool covered[sizeof(e->data)] = {false};
    for (uint32_t i = 0; i < shape.count; i++) {
        const size_t from = shape.fields[i].offset - offsetof(nt_gfx_event_t, data);
        memset(covered + from, 1, field_bytes(&shape.fields[i]));
    }
    const unsigned char *bytes = (const unsigned char *)&e->data;
    for (size_t i = 0; i < sizeof(e->data); i++) {
        NT_ASSERT((covered[i] || bytes[i] == 0) && "capture text: a record writes data outside the fields the text prints");
    }
}

static void put_detail(text_t *t, const nt_gfx_event_t *e) {
    switch (detail_kind(e)) {
    case DETAIL_CALL:
        if (e->kind == NT_GFX_EVENT_BACKEND || e->detail != 0) {
            const char *call = nt_gfx_gl_call_name(e->detail);
            NT_ASSERT(call != NULL && "capture text: issued call outside the GL call table");
            put(t, " call=%s", call);
        }
        break;
    case DETAIL_RESOURCE:
        put(t, " resource=%s", NT_TABLE_NAME_(s_object_names, e->detail));
        break;
    case DETAIL_LAYER:
        put(t, " layer=%s", NT_TABLE_NAME_(s_layer_names, e->detail));
        break;
    case DETAIL_JOINED:
        if (e->detail != 0) {
            put(t, " joined=@%" PRIu32, e->detail - 1U);
        }
        break;
    case DETAIL_PLAIN:
        if (e->detail != 0) {
            put(t, " detail=%" PRIu32, e->detail);
        }
        break;
    }
}

static void put_field(text_t *t, const nt_gfx_capture_view_t *view, const nt_gfx_event_t *e) {
    put(t, " key=");
    put_string(t, view_string(view, e->field.key));
    put(t, " type=%s value=", NT_TABLE_NAME_(s_note_type_names, (uint32_t)e->field.type));
    switch (e->field.type) {
    case NT_GFX_NOTE_U32:
        put(t, "%" PRIu32, e->field.value.u32);
        break;
    case NT_GFX_NOTE_I32:
        put(t, "%" PRId32, e->field.value.i32);
        break;
    case NT_GFX_NOTE_U64:
        put(t, "%" PRIu64, e->field.value.u64);
        break;
    case NT_GFX_NOTE_F32:
        put_float(t, e->field.value.f32);
        break;
    case NT_GFX_NOTE_STR:
        put_string(t, view_string(view, e->field.value.str));
        break;
    case NT_GFX_NOTE_HANDLE:
        put(t, "%s:%" PRIu32, NT_TABLE_NAME_(s_object_names, e->field.value.handle.kind), e->field.value.handle.id);
        break;
    }
}

/* Indentation is cosmetic and capped, so text size stays linear in records. */
enum { MAX_INDENT = 32 };

static uint32_t link_depth(const nt_gfx_capture_view_t *view, uint32_t index) {
    uint32_t depth = 0;
    for (uint32_t link = view->events[index].link; link != 0 && depth < MAX_INDENT; link = view->events[link - 1U].link) {
        depth++;
    }
    return depth;
}

static void put_record(text_t *t, const nt_gfx_capture_view_t *view, uint32_t index, uint32_t markers) {
    const nt_gfx_event_t *e = &view->events[index];
    const bool gfx = e->kind <= NT_GFX_EVENT_ARGUMENT;
    uint32_t indent = markers + (gfx ? link_depth(view, index) : 0);
    indent = indent < MAX_INDENT ? indent : MAX_INDENT;
    put(t, "%" PRIu32 " %*s%s", index, (int)(indent * 2U), "", NT_TABLE_NAME_(s_kind_names, (uint32_t)e->kind));
    if (!gfx) {
        if (e->kind == NT_GFX_EVENT_NOTE || e->kind == NT_GFX_EVENT_MARKER_PUSH) {
            put(t, " label=");
            put_string(t, view_string(view, e->note.label));
        } else if (e->kind == NT_GFX_EVENT_NOTE_FIELD) {
            put_field(t, view, e);
        }
        put(t, "\n");
        return;
    }
    const shape_t shape = record_shape(e);
    assert_data_in_shape(e, shape);
    assert_args_counted(e, issued_call(e));
    put(t, " op=%s", NT_TABLE_NAME_(s_operation_names, e->operation));
    if (e->object_kind != NT_GFX_OBJECT_NONE || e->object != 0) {
        put(t, " obj=%s:%" PRIu32, NT_TABLE_NAME_(s_object_names, e->object_kind), e->object);
    }
    if (e->result != NT_GFX_RESULT_NONE) {
        put(t, " result=%s", NT_TABLE_NAME_(s_result_names, e->result));
    }
    put_detail(t, e);
    if (e->link != 0) {
        put(t, " link=@%" PRIu32, e->link - 1U);
    }
    put_fields(t, view, e, shape);
    put(t, "\n");
}

static void put_counters(text_t *t, const nt_gfx_counters_t *c) {
    for (uint32_t i = 0; i < NT_COUNT_(k_counters); i++) {
        uint64_t value = 0;
        memcpy(&value, (const unsigned char *)c + k_counters[i].offset, k_counters[i].bytes); /* little-endian: a u32 fills the low half */
        if (value != 0) {
            put(t, "counter %s=%" PRIu64 "\n", k_counters[i].name, value);
        }
    }
    for (uint32_t i = 0; i < NT_GFX_OP_COUNT; i++) {
        if (c->accepted[i] != 0) {
            put(t, "counter accepted.%s=%" PRIu32 "\n", NT_TABLE_NAME_(s_operation_names, i), c->accepted[i]);
        }
    }
    for (uint32_t i = 0; i < NT_GFX_FRAME_STREAM_COUNT; i++) {
        if (c->frame_bytes[i] != 0) {
            put(t, "counter frame_bytes.%" PRIu32 "=%" PRIu32 "\n", i, c->frame_bytes[i]);
        }
    }
    for (uint32_t i = 1; i < NT_GFX_GL_COUNT; i++) {
        if (c->gl[i] != 0) {
            put(t, "counter gl.%s=%" PRIu32 "\n", nt_gfx_gl_call_name(i), c->gl[i]);
        }
    }
}
// #endregion

size_t nt_gfx_capture_format(const nt_gfx_capture_view_t *view, char *out, size_t cap) {
    NT_ASSERT(view != NULL && (out != NULL || cap == 0));
    text_t t = {.out = out, .cap = cap, .len = 0};
    if (cap > 0) {
        out[0] = '\0';
    }
    put(&t, "ntcapture 4\n");
    put(&t, "frame sequence=%" PRIu64 " platform=%s records=%" PRIu32 " overflow=%d execute=%" PRIu32 "..%" PRIu32 "\n", view->counters.frame_sequence,
        NT_TABLE_NAME_(s_platform_names, (uint32_t)view->platform), view->count, view->overflow ? 1 : 0, view->execute_begin, view->execute_end);
    put_counters(&t, &view->counters);
    uint32_t markers = 0;
    for (uint32_t i = 0; i < view->count; i++) {
        if (view->events[i].kind == NT_GFX_EVENT_MARKER_POP && markers > 0) {
            markers--;
        }
        put_record(&t, view, i, markers);
        if (view->events[i].kind == NT_GFX_EVENT_MARKER_PUSH) {
            markers++;
        }
    }
    return t.len;
}

// #region parse
/* One line of untrusted text: [p, end), end at its '\n'. The first failure wins. */
typedef struct {
    const char *p;
    const char *end;
    const char *error;
    const char *error_at;
} line_t;

static bool fail(line_t *l, const char *error) {
    if (l->error == NULL) {
        l->error = error;
        l->error_at = l->p;
    }
    return false;
}

static bool take(line_t *l, const char *literal) {
    const size_t n = strlen(literal);
    if ((size_t)(l->end - l->p) < n || memcmp(l->p, literal, n) != 0) {
        return false;
    }
    l->p += n;
    return true;
}

static bool expect(line_t *l, const char *literal) { return take(l, literal) || fail(l, "unexpected text"); }

static bool at_end(line_t *l) { return l->p == l->end || fail(l, "unexpected text"); }

/* `name=` with nothing consumed unless both match, so a longer name with the same prefix stays readable. */
static bool take_assign(line_t *l, const char *name) {
    const size_t n = strlen(name);
    if ((size_t)(l->end - l->p) <= n || memcmp(l->p, name, n) != 0 || l->p[n] != '=') {
        return false;
    }
    l->p += n + 1;
    return true;
}

/* Decimal digits only: no sign, no spaces, no wrap. */
static bool read_uint(line_t *l, uint64_t max, uint64_t *out) {
    const char *start = l->p;
    uint64_t v = 0;
    while (l->p < l->end && *l->p >= '0' && *l->p <= '9') {
        const uint64_t d = (uint64_t)(*l->p - '0');
        if (d > max || v > (max - d) / 10U) {
            l->p = start;
            return fail(l, "number out of range");
        }
        v = (v * 10U) + d;
        l->p++;
    }
    if (l->p == start) {
        return fail(l, "expected a number");
    }
    *out = v;
    return true;
}

static bool read_u32(line_t *l, uint32_t *out) {
    uint64_t v = 0;
    const bool ok = read_uint(l, UINT32_MAX, &v);
    *out = (uint32_t)v;
    return ok;
}

static size_t ident_length(const line_t *l) {
    size_t n = 0;
    while (l->p + n < l->end) {
        const char c = l->p[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            break;
        }
        n++;
    }
    return n;
}

static bool name_is(const char *name, const char *s, size_t n) { return name != NULL && strlen(name) == n && memcmp(name, s, n) == 0; }

/* A name in this build's table (a NULL slot never matches); a name only another build knows is unknown here. */
static bool read_name(line_t *l, const char *const *names, uint32_t count, uint32_t *out) {
    const size_t n = ident_length(l);
    for (uint32_t i = 0; i < count; i++) {
        if (name_is(names[i], l->p, n)) {
            *out = i;
            l->p += n;
            return true;
        }
    }
    return fail(l, "unknown name");
}
#define NT_READ_NAME_(l, table, out) read_name((l), (table), NT_COUNT_(table), (out))

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* The 8 hex digits after "nan:0x"; false unless they are NaN bits. */
static bool nan_bits(const char *hex, float *out) {
    uint32_t bits = 0;
    for (size_t i = 0; i < 8; i++) {
        const int d = hex_digit(hex[i]);
        if (d < 0) {
            return false;
        }
        bits = (bits << 4) | (uint32_t)d;
    }
    memcpy(out, &bits, sizeof(*out));
    return isnan(*out);
}

/* The formatter's forms only: decimal %g, inf, -inf and nan:0x<8 hex digits>. */
static bool read_float(line_t *l, float *out) {
    const char *start = l->p;
    size_t n = 0;
    while (start + n < l->end && start[n] != ' ' && start[n] != ',' && start[n] != ']') {
        n++;
    }
    if (n == 14 && memcmp(start, "nan:0x", 6) == 0) {
        if (!nan_bits(start + 6, out)) {
            return fail(l, "bad float");
        }
        l->p += n;
        return true;
    }
    const bool word = (n == 3 && memcmp(start, "inf", 3) == 0) || (n == 4 && memcmp(start, "-inf", 4) == 0);
    char buf[33]; /* the token terminated for strtof */
    if (n == 0 || n >= sizeof(buf)) {
        return fail(l, "bad float");
    }
    for (size_t i = 0; i < n; i++) {
        const char c = start[i];
        if (!word && !((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e')) {
            return fail(l, "bad float");
        }
        buf[i] = c;
    }
    buf[n] = '\0';
    char *stop = NULL;
    *out = strtof(buf, &stop);
    if (stop != buf + n || (!word && isinf(*out))) { /* a decimal past FLT_MAX is not formatter output */
        return fail(l, "bad float");
    }
    l->p += n;
    return true;
}

/* The caller's string bytes the document fills; used counts bytes[0], the "" of offset 0. */
typedef struct {
    char *bytes;
    uint32_t cap;
    uint32_t used;
} strings_t;

/* The string byte at l->p and the text it takes (itself or \xNN, never NUL); -1 when it is none. */
static int string_byte(const line_t *l, size_t *step) {
    const unsigned char c = (unsigned char)*l->p;
    *step = 1;
    if (c != '\\') {
        return c >= 0x20U && c <= 0x7eU ? c : -1;
    }
    const int hi = l->end - l->p >= 4 && l->p[1] == 'x' ? hex_digit(l->p[2]) : -1;
    const int lo = hi >= 0 ? hex_digit(l->p[3]) : -1;
    *step = 4;
    return lo < 0 || (hi == 0 && lo == 0) ? -1 : (hi << 4) | lo;
}

/* A quoted string: printable bytes and \xNN for the rest, no NUL; appended to strings, "" as offset 0. */
static bool read_string(line_t *l, strings_t *strings, nt_gfx_capture_string_t *out) {
    if (!take(l, "\"")) {
        return fail(l, "expected a string");
    }
    const uint32_t start = strings->used;
    uint32_t n = 0;
    while (l->p < l->end && *l->p != '"') {
        size_t step = 0;
        const int c = string_byte(l, &step);
        if (c < 0) {
            return fail(l, *l->p == '\\' ? "bad escape" : "bad string byte");
        }
        if (n + 1U >= strings->cap - start) {
            return fail(l, "capacity: more string bytes than the array holds");
        }
        strings->bytes[start + n++] = (char)c;
        l->p += step;
    }
    if (l->p == l->end) {
        return fail(l, "unterminated string");
    }
    l->p++;
    if (n > 0) {
        strings->bytes[start + n] = '\0';
        strings->used = start + n + 1U;
    }
    *out = n > 0 ? start : 0U;
    return true;
}

/* `[a,b,...]` into consecutive 4-byte slots. */
static bool read_list(line_t *l, unsigned char *at, nt_gfx_field_type_t type, uint32_t size, uint32_t *count) {
    if (!expect(l, "[")) {
        return false;
    }
    uint32_t n = 0;
    do {
        uint32_t bits = 0;
        float real = 0.0F;
        if (n == size) {
            return fail(l, "list too long");
        }
        if (type == NT_GFX_FIELD_U32S ? !read_u32(l, &bits) : !read_float(l, &real)) {
            return false;
        }
        if (type == NT_GFX_FIELD_F32S) {
            memcpy(&bits, &real, sizeof(bits));
        }
        memcpy(at + ((size_t)n * sizeof(bits)), &bits, sizeof(bits));
        n++;
    } while (take(l, ","));
    *count = n;
    return expect(l, "]");
}

/* ` name=`, consuming nothing unless it matches. */
static bool take_field(line_t *l, const char *name) {
    if (l->p == l->end || l->p[0] != ' ') {
        return false;
    }
    l->p++;
    if (!take_assign(l, name)) {
        l->p--;
        return false;
    }
    return true;
}

/* An optional '-' then decimal digits within int32. */
static bool read_i32(line_t *l, int32_t *out) {
    uint64_t v = 0;
    const bool negative = take(l, "-");
    if (!read_uint(l, negative ? (uint64_t)INT32_MAX + 1U : (uint64_t)INT32_MAX, &v)) {
        return false;
    }
    *out = negative ? (int32_t)(-(int64_t)v) : (int32_t)v;
    return true;
}

/* Fields in table order, each optional; an issued call's lists set its arg_count. */
static bool read_fields(line_t *l, strings_t *strings, nt_gfx_event_t *e, shape_t shape) {
    const bool issued = issued_call(e);
    unsigned char *base = (unsigned char *)e;
    for (uint32_t i = 0; i < shape.count; i++) {
        const field_t *f = &shape.fields[i];
        unsigned char *at = base + f->offset;
        uint64_t v = 0;
        uint32_t n = 0;
        int32_t sign = 0;
        bool ok = true;
        if (!take_field(l, f->name)) {
            continue;
        }
        switch ((nt_gfx_field_type_t)f->type) {
        case NT_GFX_FIELD_U32:
            ok = read_uint(l, UINT32_MAX, &v);
            memcpy(at, &(uint32_t){(uint32_t)v}, sizeof(uint32_t));
            break;
        case NT_GFX_FIELD_I32:
            ok = read_i32(l, &sign);
            memcpy(at, &sign, sizeof(sign));
            break;
        case NT_GFX_FIELD_U64:
            ok = read_uint(l, UINT64_MAX, &v);
            memcpy(at, &v, sizeof(v));
            break;
        case NT_GFX_FIELD_BOOL:
            ok = read_uint(l, 1, &v);
            *(bool *)at = v != 0;
            break;
        case NT_GFX_FIELD_F32: {
            float real = 0.0F;
            ok = read_float(l, &real);
            memcpy(at, &real, sizeof(real));
            break;
        }
        case NT_GFX_FIELD_U32S:
        case NT_GFX_FIELD_F32S:
            ok = read_list(l, at, (nt_gfx_field_type_t)f->type, f->size, &n);
            if (ok && issued) {
                e->arg_count = (uint8_t)(f->type == NT_GFX_FIELD_U32S ? (e->arg_count & 0xF0U) | n : (e->arg_count & 0x0FU) | (n << 4));
            }
            break;
        case NT_GFX_FIELD_STR: {
            nt_gfx_capture_string_t str = 0;
            ok = read_string(l, strings, &str);
            memcpy(at, &str, sizeof(str));
            break;
        }
        }
        if (!ok) {
            return false;
        }
    }
    return at_end(l);
}

/* `@i`: an earlier OP record (a draw for joined), stored as i + 1. */
static bool read_ref(line_t *l, const nt_gfx_event_t *events, uint32_t index, bool draw, uint32_t *out) {
    const char *start = l->p;
    uint32_t target = 0;
    if (!read_u32(l, &target)) {
        return false;
    }
    if (target >= index || events[target].kind != NT_GFX_EVENT_OP || (draw && !is_draw(events[target].operation))) {
        l->p = start;
        return fail(l, draw ? "joined must name an earlier draw" : "link must name an earlier operation");
    }
    *out = target + 1U;
    return true;
}

static bool read_detail(line_t *l, nt_gfx_event_t *e, const nt_gfx_event_t *events, uint32_t index) {
    uint32_t v = 0;
    switch (detail_kind(e)) {
    case DETAIL_CALL:
        if (take(l, " call=")) {
            return NT_READ_NAME_(l, s_gl_call_names, &e->detail);
        }
        return e->kind != NT_GFX_EVENT_BACKEND || fail(l, "an issued call needs call=");
    case DETAIL_RESOURCE:
        if (!expect(l, " resource=") || !NT_READ_NAME_(l, s_object_names, &v)) {
            return false;
        }
        e->detail = v;
        return true;
    case DETAIL_LAYER:
        if (!expect(l, " layer=") || !NT_READ_NAME_(l, s_layer_names, &v)) {
            return false;
        }
        e->detail = v;
        return true;
    case DETAIL_JOINED:
        return !take(l, " joined=@") || read_ref(l, events, index, true, &e->detail);
    case DETAIL_PLAIN:
        return !take(l, " detail=") || read_u32(l, &e->detail);
    }
    return false;
}

static bool read_gfx(line_t *l, strings_t *strings, nt_gfx_event_t *events, uint32_t index) {
    nt_gfx_event_t *e = &events[index];
    uint32_t v = 0;
    if (!expect(l, " op=") || !NT_READ_NAME_(l, s_operation_names, &v)) {
        return false;
    }
    e->operation = (uint8_t)v;
    if (take(l, " obj=")) {
        if (!NT_READ_NAME_(l, s_object_names, &v) || !expect(l, ":") || !read_u32(l, &e->object)) {
            return false;
        }
        e->object_kind = (uint8_t)v;
    }
    if (take(l, " result=")) {
        if (!NT_READ_NAME_(l, s_result_names, &v)) {
            return false;
        }
        e->result = (uint8_t)v;
    }
    if (!read_detail(l, e, events, index) || (take(l, " link=@") && !read_ref(l, events, index, false, &e->link))) {
        return false;
    }
    return read_fields(l, strings, e, record_shape(e));
}

static bool read_note_field(line_t *l, strings_t *strings, nt_gfx_event_t *e) {
    uint32_t type = 0;
    if (!expect(l, " key=") || !read_string(l, strings, &e->field.key) || !expect(l, " type=") || !NT_READ_NAME_(l, s_note_type_names, &type) || !expect(l, " value=")) {
        return false;
    }
    e->field.type = (nt_gfx_note_type_t)type;
    switch (e->field.type) {
    case NT_GFX_NOTE_U32:
        return read_u32(l, &e->field.value.u32);
    case NT_GFX_NOTE_I32:
        return read_i32(l, &e->field.value.i32);
    case NT_GFX_NOTE_U64:
        return read_uint(l, UINT64_MAX, &e->field.value.u64);
    case NT_GFX_NOTE_F32:
        return read_float(l, &e->field.value.f32);
    case NT_GFX_NOTE_STR:
        return read_string(l, strings, &e->field.value.str);
    case NT_GFX_NOTE_HANDLE:
        return NT_READ_NAME_(l, s_object_names, &e->field.value.handle.kind) && expect(l, ":") && read_u32(l, &e->field.value.handle.id);
    }
    return false;
}

static bool read_record(line_t *l, strings_t *strings, nt_gfx_event_t *events, uint32_t index) {
    nt_gfx_event_t *e = &events[index];
    memset(e, 0, sizeof(*e));
    const char *start = l->p;
    uint32_t number = 0;
    uint32_t kind = 0;
    if (!read_u32(l, &number)) {
        return false;
    }
    if (number != index) {
        l->p = start;
        return fail(l, "record index out of order");
    }
    if (!expect(l, " ")) {
        return false;
    }
    while (take(l, " ")) {
    }
    if (!NT_READ_NAME_(l, s_kind_names, &kind)) {
        return false;
    }
    e->kind = (nt_gfx_event_kind_t)kind;
    switch (e->kind) {
    case NT_GFX_EVENT_NOTE:
    case NT_GFX_EVENT_MARKER_PUSH:
        return expect(l, " label=") && read_string(l, strings, &e->note.label) && at_end(l);
    case NT_GFX_EVENT_NOTE_FIELD:
        if (index == 0 || (events[index - 1U].kind != NT_GFX_EVENT_NOTE && events[index - 1U].kind != NT_GFX_EVENT_NOTE_FIELD)) {
            return fail(l, "a note field must follow its note");
        }
        return read_note_field(l, strings, e) && at_end(l);
    case NT_GFX_EVENT_MARKER_POP:
        return at_end(l);
    default:
        return read_gfx(l, strings, events, index);
    }
}

static bool read_counter(line_t *l, nt_gfx_counters_t *c) {
    uint64_t v = 0;
    for (uint32_t i = 0; i < NT_COUNT_(k_counters); i++) {
        if (take_assign(l, k_counters[i].name)) {
            if (!read_uint(l, k_counters[i].bytes == sizeof(uint64_t) ? UINT64_MAX : UINT32_MAX, &v)) {
                return false;
            }
            memcpy((unsigned char *)c + k_counters[i].offset, &v, k_counters[i].bytes); /* little-endian: a u32 takes the low half */
            return at_end(l);
        }
    }
    uint32_t *slot = NULL;
    uint32_t i = 0;
    if (take(l, "accepted.")) {
        if (!NT_READ_NAME_(l, s_operation_names, &i)) {
            return false;
        }
        slot = &c->accepted[i];
    } else if (take(l, "frame_bytes.")) {
        if (!read_u32(l, &i)) {
            return false;
        }
        if (i >= NT_GFX_FRAME_STREAM_COUNT) {
            return fail(l, "frame stream beyond this build's NT_GFX_MAX_VERTEX_STREAMS");
        }
        slot = &c->frame_bytes[i];
    } else if (take(l, "gl.")) {
        if (!NT_READ_NAME_(l, s_gl_call_names, &i)) {
            return false;
        }
        slot = &c->gl[i];
    } else {
        return fail(l, "unknown counter");
    }
    return expect(l, "=") && read_u32(l, slot) && at_end(l);
}

/* Opens the line at `at`; a line without '\n' is a truncated file. */
static bool open_line(line_t *l, const char *at, const char *end) {
    const char *nl = (const char *)memchr(at, '\n', (size_t)(end - at));
    l->p = at;
    l->end = nl != NULL ? nl : end;
    return nl != NULL || fail(l, "truncated: the last line has no newline");
}

static bool is_document_start(const char *at, const char *end) { return (size_t)(end - at) >= 10 && memcmp(at, "ntcapture ", 10) == 0; }

/* The first line from `at` (a line start) that opens a document. Strings escape '\n', so every line break is a record boundary. */
static const char *next_document(const char *at, const char *end) {
    while (at < end && !is_document_start(at, end)) {
        const char *nl = (const char *)memchr(at, '\n', (size_t)(end - at));
        at = nl != NULL ? nl + 1 : end;
    }
    return at;
}

static bool read_header(line_t *l, const char **at, const char *end, nt_gfx_capture_view_t *view, uint32_t *records) {
    uint32_t platform = 0;
    uint64_t overflow = 0;
    if (!open_line(l, *at, end) || !expect(l, "ntcapture ")) {
        return false;
    }
    if (!take(l, "4") || l->p != l->end) {
        return fail(l, "unsupported ntcapture version");
    }
    *at = l->end + 1;
    if (!open_line(l, *at, end) || !expect(l, "frame sequence=") || !read_uint(l, UINT64_MAX, &view->counters.frame_sequence) || !expect(l, " platform=") ||
        !NT_READ_NAME_(l, s_platform_names, &platform) || !expect(l, " records=") || !read_u32(l, records) || !expect(l, " overflow=") || !read_uint(l, 1, &overflow) || !expect(l, " execute=") ||
        !read_u32(l, &view->execute_begin) || !expect(l, "..") || !read_u32(l, &view->execute_end) || !at_end(l)) {
        return false;
    }
    if (view->execute_begin > view->execute_end || view->execute_end > *records) {
        return fail(l, "execute range outside the records");
    }
    view->platform = (nt_gfx_capture_platform_t)platform;
    view->overflow = overflow != 0;
    *at = l->end + 1;
    while ((size_t)(end - *at) >= 8 && memcmp(*at, "counter ", 8) == 0) {
        if (!open_line(l, *at, end) || !expect(l, "counter ") || !read_counter(l, &view->counters)) {
            return false;
        }
        *at = l->end + 1;
    }
    return true;
}

/* The records= lines from *at into events[0..cap); returns how many parsed, the first failure in l. A failed
 * line's strings are dropped with it. */
static uint32_t read_records(line_t *l, const char **at, const char *end, strings_t *strings, nt_gfx_event_t *events, uint32_t cap, uint32_t records) {
    uint32_t count = 0;
    for (; count < records; count++) {
        const uint32_t used = strings->used;
        l->p = *at;
        if (*at == end || is_document_start(*at, end)) {
            (void)fail(l, "truncated: fewer records than records=");
        } else if (count == cap) {
            (void)fail(l, "capacity: more records than the array holds");
        } else if (open_line(l, *at, end) && read_record(l, strings, events, count)) {
            *at = l->end + 1;
            continue;
        }
        strings->used = used;
        return count;
    }
    if (*at < end && !is_document_start(*at, end)) {
        l->p = *at;
        (void)fail(l, "a record beyond records=");
    }
    return count;
}

nt_gfx_capture_parsed_t nt_gfx_capture_parse(const char *text, size_t len, nt_gfx_event_t *events, uint32_t cap, char *strings, uint32_t string_cap) {
    NT_ASSERT((text != NULL || len == 0) && events != NULL && strings != NULL && string_cap > 0);
    nt_gfx_capture_parsed_t r = {0};
    if (len == 0) {
        r.error = "empty document";
        return r;
    }
    const char *end = text + len;
    const char *at = text;
    line_t l = {.p = text, .end = text};
    nt_gfx_capture_view_t view = {0};
    uint32_t records = 0;
    if (!read_header(&l, &at, end, &view, &records)) {
        /* Rejected; the search for the next document starts after the version line, which may itself be bad. */
        const char *first = (const char *)memchr(text, '\n', len);
        r.next = (size_t)(next_document(first != NULL ? first + 1 : end, end) - text);
        r.error = l.error;
        r.error_at = (size_t)(l.error_at - text);
        return r;
    }
    strings[0] = '\0';
    strings_t pool = {.bytes = strings, .cap = string_cap, .used = 1};
    const uint32_t count = read_records(&l, &at, end, &pool, events, cap, records);
    /* A valid prefix stays a usable view: the execution range is clipped to it. */
    view.events = events;
    view.count = count;
    view.strings = strings;
    view.string_bytes = pool.used;
    view.execute_begin = view.execute_begin < count ? view.execute_begin : count;
    view.execute_end = view.execute_end < count ? view.execute_end : count;
    r.view = view;
    if (l.error != NULL) {
        r.error = l.error;
        r.error_at = (size_t)(l.error_at - text);
        at = next_document(at, end); /* at is the start of the failed line */
    }
    r.next = (size_t)(at - text);
    return r;
}
// #endregion
