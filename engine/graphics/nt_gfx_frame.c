#include "graphics/nt_gfx_frame.h"

#include <stdlib.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"

nt_gfx_stream_t g_nt_gfx_stream;
nt_gfx_frame_storage_t g_nt_gfx_frame_storage[NT_GFX_FRAME_STREAM_COUNT];

// #region lifecycle
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
void nt_gfx_frame_init(const nt_gfx_desc_t *desc) {
    NT_ASSERT(desc->stream_capacity >= 4U && "nt_gfx_desc_t.stream_capacity is below one word -- use nt_gfx_desc_defaults() or set explicitly");
    g_nt_gfx_stream.capacity = desc->stream_capacity / 4U;
    g_nt_gfx_stream.words = (uint32_t *)malloc((size_t)g_nt_gfx_stream.capacity * sizeof(uint32_t));
    NT_ASSERT(g_nt_gfx_stream.words != NULL);
    g_nt_gfx_stream.used = 0;
    for (uint32_t s = 0; s < NT_GFX_FRAME_STREAM_COUNT; s++) {
        g_nt_gfx_frame_storage[s] = (nt_gfx_frame_storage_t){0};
        /* Zero capacity disables the stream: its allocations reach the overflow. */
        if (desc->frame_capacity[s] == 0) {
            continue;
        }
        /* Alignment padding is uploaded too: define it once. */
        g_nt_gfx_frame_storage[s] = (nt_gfx_frame_storage_t){.staging = (uint8_t *)calloc(desc->frame_capacity[s], 1), .capacity = desc->frame_capacity[s]};
        NT_ASSERT(g_nt_gfx_frame_storage[s].staging != NULL && "gfx init: out of memory for frame storage");
    }
}

void nt_gfx_frame_shutdown(void) {
    free(g_nt_gfx_stream.words);
    g_nt_gfx_stream = (nt_gfx_stream_t){0};
    for (uint32_t s = 0; s < NT_GFX_FRAME_STREAM_COUNT; s++) {
        free(g_nt_gfx_frame_storage[s].staging);
        g_nt_gfx_frame_storage[s] = (nt_gfx_frame_storage_t){0};
    }
}

void nt_gfx_frame_create_buffers(void) {
    static const nt_buffer_type_t types[NT_GFX_FRAME_STREAM_COUNT] = {NT_BUFFER_VERTEX, NT_BUFFER_INDEX, NT_BUFFER_UNIFORM};
    static const char *const labels[NT_GFX_FRAME_STREAM_COUNT] = {"frame_vertex", "frame_index", "frame_uniform"};
    for (uint32_t s = 0; s < NT_GFX_FRAME_STREAM_COUNT; s++) {
        nt_gfx_frame_storage_t *storage = &g_nt_gfx_frame_storage[s];
        if (storage->capacity == 0) {
            continue;
        }
        /* After a restore the old handle holds a wiped name. */
        if (storage->buffer.id != 0) {
            nt_gfx_destroy_buffer(storage->buffer);
        }
        /* A new context loss is the only expected failure; the next restore makes them again. */
        storage->buffer = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = types[s],
            .usage = NT_USAGE_STREAM,
            .size = storage->capacity,
            .index_type = s == NT_GFX_FRAME_INDEX ? NT_INDEX_UINT32 : NT_INDEX_NONE,
            .label = labels[s],
        });
        NT_ASSERT((storage->buffer.id != 0 || g_nt_gfx.context_lost || nt_gfx_backend_query_context_lost()) && "frame storage buffer creation failed");
    }
}
// #endregion

// #region storage
void nt_gfx_frame_begin(void) {
    for (uint32_t s = 0; s < NT_GFX_FRAME_STREAM_COUNT; s++) {
        /* end_frame uploads everything allocated in the frame; later bytes would never reach the GPU. */
        NT_ASSERT(g_nt_gfx_frame_storage[s].used == g_nt_gfx.counters.frame_bytes[s] && "frame storage allocated outside begin_frame..end_frame");
        g_nt_gfx_frame_storage[s].used = 0;
    }
}

_Noreturn void nt_gfx_frame_alloc_overflow(nt_gfx_frame_stream_t stream, uint32_t size, uint32_t align) {
    NT_LOG_ERROR("gfx frame storage overflow: needed %u bytes aligned to %u, free %u of %u in frame_capacity[%u]", size, align,
                 g_nt_gfx_frame_storage[stream].capacity - g_nt_gfx_frame_storage[stream].used, g_nt_gfx_frame_storage[stream].capacity, (uint32_t)stream);
    NT_ASSERT(false && "gfx frame storage overflow: raise nt_gfx_desc_t.frame_capacity");
    __builtin_trap(); /* the allocation would point past the staging */
}

/* Sends the bytes allocated since the previous upload; the stream's draws read them after this. */
static void upload_storage(void) {
    for (uint32_t s = 0; s < NT_GFX_FRAME_STREAM_COUNT; s++) {
        const nt_gfx_frame_storage_t *storage = &g_nt_gfx_frame_storage[s];
        /* frame_bytes is the part already sent; open_frame zeroes it with the storage. */
        const uint32_t offset = g_nt_gfx.counters.frame_bytes[s];
        if (storage->used == offset) {
            continue;
        }
        g_nt_gfx.counters.frame_bytes[s] = storage->used;
        /* Only a context loss leaves no name: nothing draws until the restore makes new buffers. */
        const uint32_t backend = nt_gfx_buffer_backend(storage->buffer);
        NT_ASSERT((backend != 0 || g_nt_gfx.context_lost || nt_gfx_backend_query_context_lost()) && "frame storage buffer destroyed or never made");
        if (backend == 0) {
            continue;
        }
        const uint32_t size = storage->used - offset;
        NT_GFX_BEGIN_REQUEST(NT_GFX_OP_BUFFER_UPLOAD, NT_GFX_OBJECT_BUFFER, storage->buffer.id, event->data.resource.size = size; event->data.resource.related[0] = offset;
                             event->data.resource.flags = 1);
        nt_gfx_backend_update_buffer(backend, offset, storage->staging + offset, size);
        NT_GFX_END(NT_GFX_RESULT_ACCEPTED);
    }
}
// #endregion

_Noreturn void nt_gfx_frame_overflow(uint32_t needed_words) {
    NT_LOG_ERROR("gfx stream overflow: needed %u bytes, free %u of %u", needed_words * 4U, (g_nt_gfx_stream.capacity - g_nt_gfx_stream.used) * 4U, g_nt_gfx_stream.capacity * 4U);
    NT_ASSERT(false && "gfx stream overflow: raise nt_gfx_desc_t.stream_capacity");
    __builtin_trap(); /* the push would write past the stream */
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one case per recorded backend call
void nt_gfx_frame_execute(void) {
    upload_storage();
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
    g_nt_gfx_stream.merge_end = 0; /* an executed draw is never extended */
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
            const float *value = (const float *)(w + 1);
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
        case NT_GFX_CMD_SET_UNIFORM_FLOAT:
            nt_gfx_backend_set_uniform_float(w[0], w[1], *(const float *)(w + 2));
            w += 2 + 1;
            break;
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
            nt_gfx_backend_draw_indexed(w[0], w[1], w[2], (uint8_t)w[3]);
            w += 4;
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
