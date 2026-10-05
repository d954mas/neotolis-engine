#include "graphics/nt_gfx_frame.h"

#include <stdlib.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"

nt_gfx_stream_t g_nt_gfx_stream;

void nt_gfx_frame_init(uint32_t capacity_bytes) {
    NT_ASSERT(capacity_bytes >= 4U && "nt_gfx_desc_t.stream_capacity is below one word -- use nt_gfx_desc_defaults() or set explicitly");
    g_nt_gfx_stream.capacity = capacity_bytes / 4U;
    g_nt_gfx_stream.words = (uint32_t *)malloc((size_t)g_nt_gfx_stream.capacity * sizeof(uint32_t));
    NT_ASSERT(g_nt_gfx_stream.words != NULL);
    g_nt_gfx_stream.used = 0;
}

void nt_gfx_frame_shutdown(void) {
    free(g_nt_gfx_stream.words);
    g_nt_gfx_stream = (nt_gfx_stream_t){0};
}

void nt_gfx_frame_overflow(uint32_t needed_words) {
    NT_LOG_ERROR("gfx stream overflow: needed %u bytes, free %u of %u", needed_words * 4U, (g_nt_gfx_stream.capacity - g_nt_gfx_stream.used) * 4U, g_nt_gfx_stream.capacity * 4U);
    NT_ASSERT(false && "gfx stream overflow: raise nt_gfx_desc_t.stream_capacity");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one case per recorded backend call
void nt_gfx_frame_execute(void) {
    if (g_nt_gfx_stream.used == 0) {
        return;
    }
    const uint32_t *w = g_nt_gfx_stream.words;
    const uint32_t *end = w + g_nt_gfx_stream.used;
    const uint32_t bytes = g_nt_gfx_stream.used * 4U;
    if (bytes > g_nt_gfx.counters.stream_bytes) {
        g_nt_gfx.counters.stream_bytes = bytes;
    }
    g_nt_gfx_stream.used = 0;
    while (w < end) {
        const nt_gfx_cmd_t op = (nt_gfx_cmd_t)*w++;
        switch (op) {
        case NT_GFX_CMD_BEGIN_PASS: {
            const nt_gfx_cmd_begin_pass_t *c = (const nt_gfx_cmd_begin_pass_t *)w;
            nt_gfx_backend_begin_pass(&c->desc, c->render_target, c->width, c->height);
            w += NT_GFX_CMD_WORDS(sizeof(*c));
            break;
        }
        case NT_GFX_CMD_END_PASS:
            nt_gfx_backend_end_pass();
            break;
        case NT_GFX_CMD_CLEAR:
            nt_gfx_backend_clear((const nt_clear_desc_t *)w);
            w += NT_GFX_CMD_WORDS(sizeof(nt_clear_desc_t));
            break;
        case NT_GFX_CMD_BIND_PIPELINE:
            nt_gfx_backend_bind_pipeline(w[0]);
            w += 1;
            break;
        case NT_GFX_CMD_BIND_VERTEX_INPUT:
            nt_gfx_backend_bind_vertex_input(w[0]);
            w += 1;
            break;
        case NT_GFX_CMD_BIND_INSTANCE_BUFFER:
            nt_gfx_backend_bind_instance_buffer(w[0], w[1], w[2]);
            w += 3;
            break;
        case NT_GFX_CMD_SET_VERTEX_ATTRIB_DEFAULT: {
            float value[4];
            memcpy(value, w + 1, sizeof(value));
            nt_gfx_backend_set_vertex_attrib_default((uint8_t)w[0], value[0], value[1], value[2], value[3]);
            w += 5;
            break;
        }
        case NT_GFX_CMD_BIND_TEXTURE_UNIT:
            nt_gfx_backend_bind_texture_unit(w[0], w[1], w[2]);
            w += 3;
            break;
        case NT_GFX_CMD_BIND_UNIFORM_BUFFER:
            nt_gfx_backend_bind_uniform_buffer(w[0], w[1], w[2], w[3]);
            w += 4;
            break;
        case NT_GFX_CMD_SET_UNIFORM_MAT4:
            nt_gfx_backend_set_uniform_mat4(w[0], w[1], (const float *)(w + 2));
            w += 2 + 16;
            break;
        case NT_GFX_CMD_SET_UNIFORM_VEC4:
            nt_gfx_backend_set_uniform_vec4(w[0], w[1], (const float *)(w + 2));
            w += 2 + 4;
            break;
        case NT_GFX_CMD_SET_UNIFORM_FLOAT: {
            float val;
            memcpy(&val, w + 2, sizeof(val));
            nt_gfx_backend_set_uniform_float(w[0], w[1], val);
            w += 2 + 1;
            break;
        }
        case NT_GFX_CMD_SET_UNIFORM_INT:
            nt_gfx_backend_set_uniform_int(w[0], w[1], (int)w[2]);
            w += 3;
            break;
        case NT_GFX_CMD_SET_SCISSOR:
            nt_gfx_backend_set_scissor((int)w[0], (int)w[1], (int)w[2], (int)w[3]);
            w += 4;
            break;
        case NT_GFX_CMD_SET_SCISSOR_ENABLED:
            nt_gfx_backend_set_scissor_enabled(w[0] != 0);
            w += 1;
            break;
        case NT_GFX_CMD_SET_VIEWPORT:
            nt_gfx_backend_set_viewport((int)w[0], (int)w[1], (int)w[2], (int)w[3]);
            w += 4;
            break;
        case NT_GFX_CMD_DRAW:
            nt_gfx_backend_draw(w[0], w[1], w[2]);
            w += 3;
            break;
        case NT_GFX_CMD_DRAW_INDEXED:
            nt_gfx_backend_draw_indexed(w[0], w[1], w[2]);
            w += 3;
            break;
        case NT_GFX_CMD_BEGIN_SEGMENT: {
            const char *name;
            memcpy((void *)&name, w, sizeof(name));
            nt_gfx_backend_begin_segment(name);
            w += NT_GFX_CMD_WORDS(sizeof(name));
            break;
        }
        case NT_GFX_CMD_END_SEGMENT:
            nt_gfx_backend_end_segment();
            break;
        }
    }
}
