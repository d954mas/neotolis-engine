#ifndef NT_GFX_FRAME_H
#define NT_GFX_FRAME_H

/* Draw-phase command stream: the front-end records backend calls in call order,
 * nt_gfx_frame_execute replays them into the backend. Each record function takes
 * the arguments of the backend function it defers. Commands are one op word and
 * a 4-byte aligned argument struct; pointer payloads are copied. */

#include <string.h>

#include "graphics/nt_gfx_internal.h"

typedef enum {
    NT_GFX_CMD_BEGIN_PASS,
    NT_GFX_CMD_END_PASS,
    NT_GFX_CMD_CLEAR,
    NT_GFX_CMD_BIND_PIPELINE,
    NT_GFX_CMD_BIND_VERTEX_INPUT,
    NT_GFX_CMD_BIND_INSTANCE_BUFFER,
    NT_GFX_CMD_SET_VERTEX_ATTRIB_DEFAULT,
    NT_GFX_CMD_BIND_TEXTURE_UNIT,
    NT_GFX_CMD_BIND_UNIFORM_BUFFER,
    NT_GFX_CMD_SET_UNIFORM_MAT4,
    NT_GFX_CMD_SET_UNIFORM_VEC4,
    NT_GFX_CMD_SET_UNIFORM_FLOAT,
    NT_GFX_CMD_SET_UNIFORM_INT,
    NT_GFX_CMD_SET_SCISSOR,
    NT_GFX_CMD_SET_SCISSOR_ENABLED,
    NT_GFX_CMD_SET_VIEWPORT,
    NT_GFX_CMD_DRAW,
    NT_GFX_CMD_DRAW_INDEXED,
    NT_GFX_CMD_DRAW_INSTANCED,
    NT_GFX_CMD_DRAW_INDEXED_INSTANCED,
    NT_GFX_CMD_BEGIN_SEGMENT,
    NT_GFX_CMD_END_SEGMENT,
} nt_gfx_cmd_t;

typedef struct {
    nt_pass_desc_t desc;
    uint32_t render_target;
    uint16_t width, height;
} nt_gfx_cmd_begin_pass_t;
typedef struct {
    uint32_t location;
    float value[4];
} nt_gfx_cmd_attrib_default_t;
typedef struct {
    uint32_t program, name_hash;
    int32_t value;
} nt_gfx_cmd_uniform_int_t;
typedef struct {
    int32_t x, y, w, h;
} nt_gfx_cmd_rect_t;

typedef struct {
    uint32_t *words;
    uint32_t used;     /* words */
    uint32_t capacity; /* words */
} nt_gfx_stream_t;

extern nt_gfx_stream_t g_nt_gfx_stream;

void nt_gfx_frame_init(uint32_t capacity_bytes);
void nt_gfx_frame_shutdown(void);
/* Replays every recorded command into the backend and empties the stream. Runs at
 * nt_gfx_end_frame, and before buffer writes, destroys, read_pixels, global block
 * registration and GPU timing toggles so those keep their order against earlier draws. */
void nt_gfx_frame_execute(void);
/* Cold path: logs needed/free bytes and asserts. */
void nt_gfx_frame_overflow(uint32_t needed_words);

#define NT_GFX_CMD_WORDS(bytes) (((uint32_t)(bytes) + 3U) / 4U)

static inline void *nt_gfx_frame_push(nt_gfx_cmd_t op, uint32_t arg_bytes) {
    nt_gfx_stream_t *s = &g_nt_gfx_stream;
    const uint32_t words = 1U + NT_GFX_CMD_WORDS(arg_bytes);
    if (s->capacity - s->used < words) {
        nt_gfx_frame_overflow(words);
    }
    uint32_t *w = s->words + s->used;
    s->used += words;
    w[0] = (uint32_t)op;
    return w + 1;
}

static inline void nt_gfx_frame_u32x4(nt_gfx_cmd_t op, uint32_t arg_count, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    uint32_t *w = (uint32_t *)nt_gfx_frame_push(op, arg_count * 4U);
    const uint32_t args[4] = {a, b, c, d};
    for (uint32_t i = 0; i < arg_count; i++) {
        w[i] = args[i];
    }
}

static inline void nt_gfx_frame_begin_pass(const nt_pass_desc_t *desc, uint32_t render_target_backend, uint16_t width, uint16_t height) {
    nt_gfx_cmd_begin_pass_t *c = (nt_gfx_cmd_begin_pass_t *)nt_gfx_frame_push(NT_GFX_CMD_BEGIN_PASS, sizeof(*c));
    c->desc = *desc;
    c->render_target = render_target_backend;
    c->width = width;
    c->height = height;
}
static inline void nt_gfx_frame_end_pass(void) { (void)nt_gfx_frame_push(NT_GFX_CMD_END_PASS, 0); }
static inline void nt_gfx_frame_clear(const nt_clear_desc_t *desc) { *(nt_clear_desc_t *)nt_gfx_frame_push(NT_GFX_CMD_CLEAR, sizeof(*desc)) = *desc; }
static inline void nt_gfx_frame_bind_pipeline(uint32_t backend_handle) { nt_gfx_frame_u32x4(NT_GFX_CMD_BIND_PIPELINE, 1, backend_handle, 0, 0, 0); }
static inline void nt_gfx_frame_bind_vertex_input(uint32_t backend_handle) { nt_gfx_frame_u32x4(NT_GFX_CMD_BIND_VERTEX_INPUT, 1, backend_handle, 0, 0, 0); }
static inline void nt_gfx_frame_bind_instance_buffer(uint32_t vertex_input_backend, uint32_t buffer_backend, uint32_t byte_offset) {
    nt_gfx_frame_u32x4(NT_GFX_CMD_BIND_INSTANCE_BUFFER, 3, vertex_input_backend, buffer_backend, byte_offset, 0);
}
static inline void nt_gfx_frame_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w) {
    nt_gfx_cmd_attrib_default_t *c = (nt_gfx_cmd_attrib_default_t *)nt_gfx_frame_push(NT_GFX_CMD_SET_VERTEX_ATTRIB_DEFAULT, sizeof(*c));
    c->location = location;
    c->value[0] = x;
    c->value[1] = y;
    c->value[2] = z;
    c->value[3] = w;
}
static inline void nt_gfx_frame_bind_texture_unit(uint32_t texture_backend, uint32_t sampler_backend, uint32_t slot) {
    nt_gfx_frame_u32x4(NT_GFX_CMD_BIND_TEXTURE_UNIT, 3, texture_backend, sampler_backend, slot, 0);
}
static inline void nt_gfx_frame_bind_uniform_buffer(uint32_t backend_handle, uint32_t slot, uint32_t offset, uint32_t size) {
    nt_gfx_frame_u32x4(NT_GFX_CMD_BIND_UNIFORM_BUFFER, 4, backend_handle, slot, offset, size);
}
static inline void nt_gfx_frame_set_uniform_floats(nt_gfx_cmd_t op, uint32_t program_backend, uint32_t name_hash, const float *values, uint32_t count) {
    uint32_t *w = (uint32_t *)nt_gfx_frame_push(op, (2U + count) * 4U);
    w[0] = program_backend;
    w[1] = name_hash;
    memcpy(w + 2, values, count * sizeof(float));
}
static inline void nt_gfx_frame_set_uniform_mat4(uint32_t program_backend, uint32_t name_hash, const float *matrix) {
    nt_gfx_frame_set_uniform_floats(NT_GFX_CMD_SET_UNIFORM_MAT4, program_backend, name_hash, matrix, 16);
}
static inline void nt_gfx_frame_set_uniform_vec4(uint32_t program_backend, uint32_t name_hash, const float *vec) {
    nt_gfx_frame_set_uniform_floats(NT_GFX_CMD_SET_UNIFORM_VEC4, program_backend, name_hash, vec, 4);
}
static inline void nt_gfx_frame_set_uniform_float(uint32_t program_backend, uint32_t name_hash, float val) {
    nt_gfx_frame_set_uniform_floats(NT_GFX_CMD_SET_UNIFORM_FLOAT, program_backend, name_hash, &val, 1);
}
static inline void nt_gfx_frame_set_uniform_int(uint32_t program_backend, uint32_t name_hash, int val) {
    nt_gfx_cmd_uniform_int_t *c = (nt_gfx_cmd_uniform_int_t *)nt_gfx_frame_push(NT_GFX_CMD_SET_UNIFORM_INT, sizeof(*c));
    c->program = program_backend;
    c->name_hash = name_hash;
    c->value = val;
}
static inline void nt_gfx_frame_set_rect(nt_gfx_cmd_t op, int x, int y, int w, int h) { *(nt_gfx_cmd_rect_t *)nt_gfx_frame_push(op, sizeof(nt_gfx_cmd_rect_t)) = (nt_gfx_cmd_rect_t){x, y, w, h}; }
static inline void nt_gfx_frame_set_scissor(int x, int y, int w, int h) { nt_gfx_frame_set_rect(NT_GFX_CMD_SET_SCISSOR, x, y, w, h); }
static inline void nt_gfx_frame_set_viewport(int x, int y, int w, int h) { nt_gfx_frame_set_rect(NT_GFX_CMD_SET_VIEWPORT, x, y, w, h); }
static inline void nt_gfx_frame_set_scissor_enabled(bool enabled) { nt_gfx_frame_u32x4(NT_GFX_CMD_SET_SCISSOR_ENABLED, 1, enabled ? 1U : 0U, 0, 0, 0); }
static inline void nt_gfx_frame_draw(uint32_t first_vertex, uint32_t num_vertices) { nt_gfx_frame_u32x4(NT_GFX_CMD_DRAW, 2, first_vertex, num_vertices, 0, 0); }
static inline void nt_gfx_frame_draw_indexed(uint32_t first_index, uint32_t num_indices, uint8_t index_type) {
    nt_gfx_frame_u32x4(NT_GFX_CMD_DRAW_INDEXED, 3, first_index, num_indices, index_type, 0);
}
static inline void nt_gfx_frame_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    nt_gfx_frame_u32x4(NT_GFX_CMD_DRAW_INSTANCED, 3, first_vertex, num_vertices, instance_count, 0);
}
static inline void nt_gfx_frame_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t instance_count, uint8_t index_type) {
    nt_gfx_frame_u32x4(NT_GFX_CMD_DRAW_INDEXED_INSTANCED, 4, first_index, num_indices, instance_count, index_type);
}
/* The name must have static lifetime; the pointer is stored unaligned, so it goes through memcpy. */
static inline void nt_gfx_frame_begin_segment(const char *name) { memcpy(nt_gfx_frame_push(NT_GFX_CMD_BEGIN_SEGMENT, sizeof(name)), (const void *)&name, sizeof(name)); }
static inline void nt_gfx_frame_end_segment(void) { (void)nt_gfx_frame_push(NT_GFX_CMD_END_SEGMENT, 0); }

#endif /* NT_GFX_FRAME_H */
