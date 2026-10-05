#ifndef NT_GFX_FRAME_H
#define NT_GFX_FRAME_H

/* Draw-phase command stream: one op word plus 4-byte aligned argument words per
 * command; descriptor and uniform payloads are copied, not referenced. */

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
    NT_GFX_CMD_BEGIN_SEGMENT,
    NT_GFX_CMD_END_SEGMENT,
} nt_gfx_cmd_t;

typedef struct {
    nt_pass_desc_t desc;
    uint32_t render_target;
    uint16_t width, height;
} nt_gfx_cmd_begin_pass_t;
/* Descriptors are read in place from 4-byte aligned words. */
_Static_assert(_Alignof(nt_gfx_cmd_begin_pass_t) <= 4 && _Alignof(nt_clear_desc_t) <= 4, "gfx stream arguments must be 4-byte aligned");

typedef struct {
    uint32_t *words;
    uint32_t used;      /* words */
    uint32_t capacity;  /* words */
    uint32_t merge_end; /* words up to the end of the last mergeable indexed draw; 0 = none */
} nt_gfx_stream_t;

extern nt_gfx_stream_t g_nt_gfx_stream;

void nt_gfx_frame_init(uint32_t capacity_bytes);
void nt_gfx_frame_shutdown(void);
/* Replays the recorded commands in call order and empties the stream. */
void nt_gfx_frame_execute(void);
/* Cold path: logs needed/free bytes and stops; it never returns, also with asserts OFF. */
_Noreturn void nt_gfx_frame_overflow(uint32_t needed_words);

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
    uint32_t *c = (uint32_t *)nt_gfx_frame_push(NT_GFX_CMD_SET_VERTEX_ATTRIB_DEFAULT, 5U * 4U);
    const float value[4] = {x, y, z, w};
    c[0] = location;
    memcpy(c + 1, value, sizeof(value));
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
    nt_gfx_frame_u32x4(NT_GFX_CMD_SET_UNIFORM_INT, 3, program_backend, name_hash, (uint32_t)val, 0);
}
static inline void nt_gfx_frame_set_scissor(int x, int y, int w, int h) { nt_gfx_frame_u32x4(NT_GFX_CMD_SET_SCISSOR, 4, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h); }
static inline void nt_gfx_frame_set_viewport(int x, int y, int w, int h) { nt_gfx_frame_u32x4(NT_GFX_CMD_SET_VIEWPORT, 4, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h); }
static inline void nt_gfx_frame_set_scissor_enabled(bool enabled) { nt_gfx_frame_u32x4(NT_GFX_CMD_SET_SCISSOR_ENABLED, 1, enabled ? 1U : 0U, 0, 0, 0); }
static inline void nt_gfx_frame_draw(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) { nt_gfx_frame_u32x4(NT_GFX_CMD_DRAW, 3, first_vertex, num_vertices, instance_count, 0); }
static inline void nt_gfx_frame_draw_indexed(uint32_t first_index, uint32_t num_indices, uint32_t instance_count, uint8_t index_type) {
    nt_gfx_frame_u32x4(NT_GFX_CMD_DRAW_INDEXED, 4, first_index, num_indices, instance_count, index_type);
}
/* Extends the previous nt_gfx_draw_indexed when it is still the last command (any other
 * command moves `used` past `merge_end`) and the range continues; callers keep index
 * data restart-free, so the joined draw is identical. True when merged. */
static inline bool nt_gfx_frame_draw_indexed_merging(uint32_t first_index, uint32_t num_indices, uint8_t index_type) {
    nt_gfx_stream_t *s = &g_nt_gfx_stream;
    if (s->merge_end != 0 && s->merge_end == s->used) {
        uint32_t *prev = s->words + s->merge_end - 4U; /* first, count, instances, index type */
        /* The backend passes the count as GLsizei. */
        if ((uint64_t)prev[0] + prev[1] == first_index && (uint64_t)prev[1] + num_indices <= INT32_MAX) {
            prev[1] += num_indices;
            return true;
        }
    }
    nt_gfx_frame_draw_indexed(first_index, num_indices, 1, index_type);
    s->merge_end = s->used;
    return false;
}
/* The name must have static lifetime; the pointer is stored unaligned, so it goes through memcpy. */
static inline void nt_gfx_frame_begin_segment(const char *name) { memcpy(nt_gfx_frame_push(NT_GFX_CMD_BEGIN_SEGMENT, sizeof(name)), (const void *)&name, sizeof(name)); }
static inline void nt_gfx_frame_end_segment(void) { (void)nt_gfx_frame_push(NT_GFX_CMD_END_SEGMENT, 0); }

#endif /* NT_GFX_FRAME_H */
