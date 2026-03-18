#include "graphics/nt_gfx_internal.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "nt_mesh_format.h"
#include "nt_shader_format.h"
#include "nt_texture_format.h"

/* ---- Buffer metadata (minimal info kept for runtime validation) ---- */

typedef struct {
    uint8_t type;  /* nt_buffer_type_t */
    uint8_t usage; /* nt_buffer_usage_t */
    uint8_t _pad[2];
    uint32_t size;
} nt_gfx_buffer_meta_t;

/* ---- Global state ---- */

nt_gfx_t g_nt_gfx;

/* ---- File-scope internal state ---- */

static struct {
    nt_gfx_pool_t shader_pool;
    nt_gfx_pool_t pipeline_pool;
    nt_gfx_pool_t buffer_pool;
    nt_gfx_pool_t texture_pool;

    uint32_t *shader_backends; /* backend handles parallel to pool slots */
    uint32_t *pipeline_backends;
    uint32_t *buffer_backends;
    uint32_t *texture_backends;

    nt_gfx_buffer_meta_t *buffer_metas; /* minimal buffer metadata for runtime validation */

    nt_gfx_mesh_info_t mesh_table[NT_GFX_MAX_MESHES + 1]; /* index 0 reserved */

    nt_gfx_render_state_t render_state;
    uint32_t bound_pipeline; /* currently bound pipeline backend handle */
} s_gfx;

/* ---- Pool helpers ---- */

void nt_gfx_pool_init(nt_gfx_pool_t *pool, uint32_t capacity) {
    pool->capacity = capacity;
    pool->slots = (nt_gfx_slot_t *)calloc(capacity + 1, sizeof(nt_gfx_slot_t));
    pool->free_queue = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    pool->queue_top = capacity;

    /* Fill free queue with indices 1..capacity (skip 0, reserved invalid) */
    for (uint32_t i = 0; i < capacity; i++) {
        pool->free_queue[i] = capacity - i; /* stack: top has lowest index */
    }
}

void nt_gfx_pool_shutdown(nt_gfx_pool_t *pool) {
    free(pool->slots);
    free(pool->free_queue);
    pool->slots = NULL;
    pool->free_queue = NULL;
    pool->capacity = 0;
    pool->queue_top = 0;
}

uint32_t nt_gfx_pool_alloc(nt_gfx_pool_t *pool) {
    if (pool->queue_top == 0) {
        return 0; /* pool full */
    }
    pool->queue_top--;
    uint32_t slot_index = pool->free_queue[pool->queue_top];

    /* Increment generation (starts at 1 for first allocation) */
    uint32_t prev_id = pool->slots[slot_index].id;
    uint32_t prev_gen = prev_id >> NT_GFX_SLOT_SHIFT;
    uint32_t new_gen = prev_gen + 1;

    uint32_t id = (new_gen << NT_GFX_SLOT_SHIFT) | slot_index;
    pool->slots[slot_index].id = id;
    return id;
}

void nt_gfx_pool_free(nt_gfx_pool_t *pool, uint32_t id) {
    uint32_t slot_index = id & NT_GFX_SLOT_MASK;
    if (slot_index == 0 || slot_index > pool->capacity) {
        return;
    }
    if (pool->slots[slot_index].id != id) {
        return; /* stale handle */
    }
    /* Increment generation so old handles are invalid. Keep generation
       in slot so the next alloc produces a higher generation. Clear the
       slot index bits so valid() rejects the freed id. */
    uint32_t gen = (id >> NT_GFX_SLOT_SHIFT) + 1;
    pool->slots[slot_index].id = gen << NT_GFX_SLOT_SHIFT;
    pool->free_queue[pool->queue_top] = slot_index;
    pool->queue_top++;
}

bool nt_gfx_pool_valid(const nt_gfx_pool_t *pool, uint32_t id) {
    if (id == 0) {
        return false;
    }
    uint32_t slot_index = id & NT_GFX_SLOT_MASK;
    if (slot_index == 0 || slot_index > pool->capacity) {
        return false;
    }
    return pool->slots[slot_index].id == id;
}

uint32_t nt_gfx_pool_slot_index(uint32_t id) { return id & NT_GFX_SLOT_MASK; }

/* ---- Lifecycle ---- */

void nt_gfx_init(const nt_gfx_desc_t *desc) {
    NT_ASSERT(desc);
    memset(&s_gfx, 0, sizeof(s_gfx));
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));

    nt_gfx_pool_init(&s_gfx.shader_pool, desc->max_shaders);
    nt_gfx_pool_init(&s_gfx.pipeline_pool, desc->max_pipelines);
    nt_gfx_pool_init(&s_gfx.buffer_pool, desc->max_buffers);
    nt_gfx_pool_init(&s_gfx.texture_pool, desc->max_textures);

    s_gfx.shader_backends = (uint32_t *)calloc(desc->max_shaders + 1, sizeof(uint32_t));
    s_gfx.pipeline_backends = (uint32_t *)calloc(desc->max_pipelines + 1, sizeof(uint32_t));
    s_gfx.buffer_backends = (uint32_t *)calloc(desc->max_buffers + 1, sizeof(uint32_t));
    s_gfx.texture_backends = (uint32_t *)calloc(desc->max_textures + 1, sizeof(uint32_t));

    s_gfx.buffer_metas = (nt_gfx_buffer_meta_t *)calloc(desc->max_buffers + 1, sizeof(nt_gfx_buffer_meta_t));

    s_gfx.render_state = NT_GFX_STATE_IDLE;

    if (!nt_gfx_backend_init(desc)) {
        nt_log_error("gfx: backend init failed");
        nt_gfx_shutdown();
        return;
    }

    g_nt_gfx.initialized = true;
}

void nt_gfx_shutdown(void) {
    nt_gfx_backend_shutdown();

    /* Destroy active mesh table entries */
    for (uint32_t i = 1; i <= NT_GFX_MAX_MESHES; i++) {
        if (s_gfx.mesh_table[i].active) {
            nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[nt_gfx_pool_slot_index(s_gfx.mesh_table[i].vbo.id)]);
            nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[nt_gfx_pool_slot_index(s_gfx.mesh_table[i].ibo.id)]);
            s_gfx.mesh_table[i].active = false;
        }
    }

    nt_gfx_pool_shutdown(&s_gfx.shader_pool);
    nt_gfx_pool_shutdown(&s_gfx.pipeline_pool);
    nt_gfx_pool_shutdown(&s_gfx.buffer_pool);
    nt_gfx_pool_shutdown(&s_gfx.texture_pool);

    free(s_gfx.shader_backends);
    free(s_gfx.pipeline_backends);
    free(s_gfx.buffer_backends);
    free(s_gfx.texture_backends);
    free(s_gfx.buffer_metas);

    memset(&s_gfx, 0, sizeof(s_gfx));
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));
}

/* ---- Frame / Pass ---- */

void nt_gfx_begin_frame(void) {
    if (nt_gfx_backend_is_context_lost()) {
        if (!g_nt_gfx.context_lost) {
            /* First detection: wipe all backend handles */
            for (uint32_t i = 1; i <= s_gfx.shader_pool.capacity; i++) {
                s_gfx.shader_backends[i] = 0;
            }
            for (uint32_t i = 1; i <= s_gfx.pipeline_pool.capacity; i++) {
                s_gfx.pipeline_backends[i] = 0;
            }
            for (uint32_t i = 1; i <= s_gfx.buffer_pool.capacity; i++) {
                s_gfx.buffer_backends[i] = 0;
            }
            for (uint32_t i = 1; i <= s_gfx.texture_pool.capacity; i++) {
                s_gfx.texture_backends[i] = 0;
            }
            /* Clear mesh table (VBO/IBO handles are now invalid) */
            for (uint32_t i = 1; i <= NT_GFX_MAX_MESHES; i++) {
                s_gfx.mesh_table[i].active = false;
            }
            s_gfx.bound_pipeline = 0;
            g_nt_gfx.context_lost = true;
            nt_log_error("gfx: WebGL context lost");
        }
        return; /* Skip frame */
    }

    if (g_nt_gfx.context_lost) {
        /* Context was lost but now available again -- game must re-create resources */
        g_nt_gfx.context_lost = false;
        g_nt_gfx.context_restored = true;
        nt_log_info("gfx: WebGL context restored -- game must re-create resources");
    }

    /* Normal frame begin */
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_IDLE);
    if (s_gfx.render_state != NT_GFX_STATE_IDLE) {
        nt_log_error("gfx: begin_frame called outside IDLE state");
        return;
    }
    s_gfx.render_state = NT_GFX_STATE_FRAME;
    memset(&g_nt_gfx.frame_stats, 0, sizeof(g_nt_gfx.frame_stats));
    nt_gfx_backend_begin_frame();
}

void nt_gfx_end_frame(void) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_FRAME);
    if (s_gfx.render_state != NT_GFX_STATE_FRAME) {
        nt_log_error("gfx: end_frame called outside FRAME state");
        return;
    }

    s_gfx.render_state = NT_GFX_STATE_IDLE;
    nt_gfx_backend_end_frame();
    g_nt_gfx.context_restored = false;
}

void nt_gfx_begin_pass(const nt_pass_desc_t *desc) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_FRAME);
    if (s_gfx.render_state != NT_GFX_STATE_FRAME) {
        nt_log_error("gfx: begin_pass called outside FRAME state");
        return;
    }

    s_gfx.render_state = NT_GFX_STATE_PASS;
    nt_gfx_backend_begin_pass(desc);
}

void nt_gfx_end_pass(void) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        nt_log_error("gfx: end_pass called outside PASS state");
        return;
    }

    s_gfx.render_state = NT_GFX_STATE_FRAME;
    nt_gfx_backend_end_pass();
}

/* ---- Resource creation ---- */

nt_shader_t nt_gfx_make_shader(const nt_shader_desc_t *desc) {
    nt_shader_t result = {0};
    if (!desc) {
        return result;
    }

    uint32_t id = nt_gfx_pool_alloc(&s_gfx.shader_pool);
    if (id == 0) {
        nt_log_error("gfx: shader pool full");
        return result;
    }

    uint32_t backend = nt_gfx_backend_create_shader(desc);
    if (backend == 0) {
        nt_log_error("gfx: backend shader creation failed");
        nt_gfx_pool_free(&s_gfx.shader_pool, id);
        return result;
    }

    uint32_t slot = nt_gfx_pool_slot_index(id);
    s_gfx.shader_backends[slot] = backend;

    result.id = id;
    return result;
}

nt_pipeline_t nt_gfx_make_pipeline(const nt_pipeline_desc_t *desc) {
    nt_pipeline_t result = {0};
    if (!desc) {
        return result;
    }

    if (!nt_gfx_pool_valid(&s_gfx.shader_pool, desc->vertex_shader.id) || !nt_gfx_pool_valid(&s_gfx.shader_pool, desc->fragment_shader.id)) {
        nt_log_error("gfx: pipeline creation failed: invalid shader handle");
        return result;
    }
    if (desc->layout.attr_count > NT_GFX_MAX_VERTEX_ATTRS) {
        nt_log_error("gfx: pipeline creation failed: too many vertex attrs");
        return result;
    }
    if (desc->instance_layout.attr_count > NT_GFX_MAX_VERTEX_ATTRS) {
        nt_log_error("gfx: pipeline creation failed: too many instance attrs");
        return result;
    }

    uint32_t id = nt_gfx_pool_alloc(&s_gfx.pipeline_pool);
    if (id == 0) {
        nt_log_error("gfx: pipeline pool full");
        return result;
    }

    uint32_t vs_slot = nt_gfx_pool_slot_index(desc->vertex_shader.id);
    uint32_t fs_slot = nt_gfx_pool_slot_index(desc->fragment_shader.id);
    uint32_t vs_backend = s_gfx.shader_backends[vs_slot];
    uint32_t fs_backend = s_gfx.shader_backends[fs_slot];

    uint32_t backend = nt_gfx_backend_create_pipeline(desc, vs_backend, fs_backend);
    if (backend == 0) {
        nt_log_error("gfx: backend pipeline creation failed");
        nt_gfx_pool_free(&s_gfx.pipeline_pool, id);
        return result;
    }

    uint32_t slot = nt_gfx_pool_slot_index(id);
    s_gfx.pipeline_backends[slot] = backend;

    result.id = id;
    return result;
}

nt_buffer_t nt_gfx_make_buffer(const nt_buffer_desc_t *desc) {
    nt_buffer_t result = {0};
    if (!desc) {
        return result;
    }

    uint32_t id = nt_gfx_pool_alloc(&s_gfx.buffer_pool);
    if (id == 0) {
        nt_log_error("gfx: buffer pool full");
        return result;
    }

    uint32_t backend = nt_gfx_backend_create_buffer(desc);
    if (backend == 0) {
        nt_log_error("gfx: backend buffer creation failed");
        nt_gfx_pool_free(&s_gfx.buffer_pool, id);
        return result;
    }

    uint32_t slot = nt_gfx_pool_slot_index(id);
    s_gfx.buffer_backends[slot] = backend;
    s_gfx.buffer_metas[slot].type = (uint8_t)desc->type;
    s_gfx.buffer_metas[slot].usage = (uint8_t)desc->usage;
    s_gfx.buffer_metas[slot].size = desc->size;

    result.id = id;
    return result;
}

nt_texture_t nt_gfx_make_texture(const nt_texture_desc_t *desc) {
    nt_texture_t result = {0};
    if (!desc) {
        return result;
    }
    if (!desc->data) {
        nt_log_error("gfx: make_texture: NULL data");
        return result;
    }
    if (desc->width == 0 || desc->height == 0) {
        nt_log_error("gfx: make_texture: zero dimension");
        return result;
    }
    nt_texture_desc_t local_desc = *desc;

    /* Clamp mipmap min_filter when no mipmaps — prevents GL incomplete texture */
    if (!local_desc.gen_mipmaps && local_desc.min_filter > NT_FILTER_LINEAR) {
        local_desc.min_filter = (local_desc.min_filter & 1) ? NT_FILTER_LINEAR : NT_FILTER_NEAREST;
        nt_log_info("gfx: make_texture: min_filter clamped (gen_mipmaps=false)");
    }

    /* Validate mag_filter: only NEAREST or LINEAR allowed */
    if (local_desc.mag_filter > NT_FILTER_LINEAR) {
        nt_log_info("gfx: make_texture: mag_filter clamped to LINEAR");
        local_desc.mag_filter = NT_FILTER_LINEAR;
    }

    uint32_t id = nt_gfx_pool_alloc(&s_gfx.texture_pool);
    if (id == 0) {
        nt_log_error("gfx: texture pool full");
        return result;
    }

    uint32_t backend = nt_gfx_backend_create_texture(&local_desc);
    if (backend == 0) {
        nt_log_error("gfx: backend texture creation failed");
        nt_gfx_pool_free(&s_gfx.texture_pool, id);
        return result;
    }

    uint32_t slot = nt_gfx_pool_slot_index(id);
    s_gfx.texture_backends[slot] = backend;

    result.id = id;
    return result;
}

/* ---- Resource destruction ---- */

void nt_gfx_destroy_shader(nt_shader_t shd) {
    if (!nt_gfx_pool_valid(&s_gfx.shader_pool, shd.id)) {
        nt_log_error("gfx: destroy_shader: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(shd.id);
    nt_gfx_backend_destroy_shader(s_gfx.shader_backends[slot]);
    s_gfx.shader_backends[slot] = 0;
    nt_gfx_pool_free(&s_gfx.shader_pool, shd.id);
}

void nt_gfx_destroy_pipeline(nt_pipeline_t pip) {
    if (!nt_gfx_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        nt_log_error("gfx: destroy_pipeline: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(pip.id);
    if (s_gfx.bound_pipeline == s_gfx.pipeline_backends[slot]) {
        s_gfx.bound_pipeline = 0;
    }
    nt_gfx_backend_destroy_pipeline(s_gfx.pipeline_backends[slot]);
    s_gfx.pipeline_backends[slot] = 0;
    nt_gfx_pool_free(&s_gfx.pipeline_pool, pip.id);
}

void nt_gfx_destroy_buffer(nt_buffer_t buf) {
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        nt_log_error("gfx: destroy_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[slot]);
    s_gfx.buffer_backends[slot] = 0;
    memset(&s_gfx.buffer_metas[slot], 0, sizeof(nt_gfx_buffer_meta_t));
    nt_gfx_pool_free(&s_gfx.buffer_pool, buf.id);
}

void nt_gfx_destroy_texture(nt_texture_t tex) {
    if (!nt_gfx_pool_valid(&s_gfx.texture_pool, tex.id)) {
        nt_log_error("gfx: destroy_texture: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(tex.id);
    nt_gfx_backend_destroy_texture(s_gfx.texture_backends[slot]);
    s_gfx.texture_backends[slot] = 0;
    nt_gfx_pool_free(&s_gfx.texture_pool, tex.id);
}

/* ---- Draw state ---- */

void nt_gfx_bind_pipeline(nt_pipeline_t pip) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        nt_log_error("gfx: bind_pipeline: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(pip.id);
    s_gfx.bound_pipeline = s_gfx.pipeline_backends[slot];
    nt_gfx_backend_bind_pipeline(s_gfx.bound_pipeline);
}

void nt_gfx_bind_vertex_buffer(nt_buffer_t buf) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        nt_log_error("gfx: bind_vertex_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[slot].type == NT_BUFFER_VERTEX);
    if (s_gfx.buffer_metas[slot].type != NT_BUFFER_VERTEX) {
        nt_log_error("gfx: bind_vertex_buffer: buffer is not vertex type");
        return;
    }
    nt_gfx_backend_bind_vertex_buffer(s_gfx.buffer_backends[slot]);
}

void nt_gfx_bind_index_buffer(nt_buffer_t buf) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        nt_log_error("gfx: bind_index_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[slot].type == NT_BUFFER_INDEX);
    if (s_gfx.buffer_metas[slot].type != NT_BUFFER_INDEX) {
        nt_log_error("gfx: bind_index_buffer: buffer is not index type");
        return;
    }
    nt_gfx_backend_bind_index_buffer(s_gfx.buffer_backends[slot]);
}

void nt_gfx_bind_texture(nt_texture_t tex, uint32_t slot) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.texture_pool, tex.id)) {
        nt_log_error("gfx: bind_texture: invalid handle");
        return;
    }
    if (slot >= NT_GFX_MAX_TEXTURE_SLOTS) {
        nt_log_error("gfx: bind_texture: slot exceeds max");
        return;
    }
    uint32_t idx = nt_gfx_pool_slot_index(tex.id);
    nt_gfx_backend_bind_texture(s_gfx.texture_backends[idx], slot);
}

/* ---- Uniforms ---- */

void nt_gfx_set_uniform_mat4(const char *name, const float *matrix) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    nt_gfx_backend_set_uniform_mat4(name, matrix);
}

void nt_gfx_set_uniform_vec4(const char *name, const float *vec) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    nt_gfx_backend_set_uniform_vec4(name, vec);
}

void nt_gfx_set_uniform_float(const char *name, float val) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    nt_gfx_backend_set_uniform_float(name, val);
}

void nt_gfx_set_uniform_int(const char *name, int val) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    nt_gfx_backend_set_uniform_int(name, val);
}

/* ---- Draw calls ---- */

void nt_gfx_draw(uint32_t first_vertex, uint32_t num_vertices) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        nt_log_error("gfx: draw called outside PASS state");
        return;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    if (s_gfx.bound_pipeline == 0) {
        nt_log_error("gfx: draw called without bound pipeline");
        return;
    }

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.vertices += num_vertices;
    nt_gfx_backend_draw(first_vertex, num_vertices);
}

void nt_gfx_draw_indexed(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        nt_log_error("gfx: draw_indexed called outside PASS state");
        return;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    if (s_gfx.bound_pipeline == 0) {
        nt_log_error("gfx: draw_indexed called without bound pipeline");
        return;
    }

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.vertices += num_vertices;
    g_nt_gfx.frame_stats.indices += num_indices;
    nt_gfx_backend_draw_indexed(first_index, num_indices);
}

void nt_gfx_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices, uint32_t instance_count) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        nt_log_error("gfx: draw_indexed_instanced called outside PASS state");
        return;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    if (s_gfx.bound_pipeline == 0) {
        nt_log_error("gfx: draw_indexed_instanced called without bound pipeline");
        return;
    }

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.draw_calls_instanced++;
    g_nt_gfx.frame_stats.vertices += num_vertices * instance_count;
    g_nt_gfx.frame_stats.indices += num_indices * instance_count;
    g_nt_gfx.frame_stats.instances += instance_count;
    nt_gfx_backend_draw_indexed_instanced(first_index, num_indices, instance_count);
}

/* ---- Instance buffer ---- */

void nt_gfx_bind_instance_buffer(nt_buffer_t buf) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        nt_log_error("gfx: bind_instance_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[slot].type == NT_BUFFER_VERTEX);
    if (s_gfx.buffer_metas[slot].type != NT_BUFFER_VERTEX) {
        nt_log_error("gfx: bind_instance_buffer: buffer is not vertex type");
        return;
    }
    nt_gfx_backend_bind_instance_buffer(s_gfx.buffer_backends[slot]);
}

/* ---- Buffer update ---- */

void nt_gfx_update_buffer(nt_buffer_t buf, const void *data, uint32_t size) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_gfx_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        nt_log_error("gfx: update_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_gfx_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[slot].usage != NT_USAGE_IMMUTABLE);
    if (s_gfx.buffer_metas[slot].usage == NT_USAGE_IMMUTABLE) {
        nt_log_error("gfx: update_buffer: cannot update immutable buffer");
        return;
    }
    NT_ASSERT(size <= s_gfx.buffer_metas[slot].size);
    if (size > s_gfx.buffer_metas[slot].size) {
        nt_log_error("gfx: update_buffer: size exceeds buffer capacity");
        return;
    }
    nt_gfx_backend_update_buffer(s_gfx.buffer_backends[slot], data, size);
}

/* ---- Mesh side table helpers ---- */

static uint32_t mesh_table_alloc(void) {
    for (uint32_t i = 1; i <= NT_GFX_MAX_MESHES; i++) {
        if (!s_gfx.mesh_table[i].active) {
            return i;
        }
    }
    return 0;
}

static uint32_t mesh_handle_make(uint32_t index, uint16_t gen) { return ((uint32_t)gen << 16) | (index & 0xFFFF); }

/* ---- Asset activators ---- */

uint32_t nt_gfx_activate_texture(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(NtTextureAssetHeader)) {
        nt_log_error("gfx: activate_texture: blob too small");
        return 0;
    }
    const NtTextureAssetHeader *hdr = (const NtTextureAssetHeader *)data;
    if (hdr->magic != NT_TEXTURE_MAGIC) {
        nt_log_error("gfx: activate_texture: bad magic");
        return 0;
    }
    uint32_t pixel_size = hdr->width * hdr->height * 4; /* RGBA8 */
    if (sizeof(NtTextureAssetHeader) + pixel_size > size) {
        nt_log_error("gfx: activate_texture: blob truncated");
        return 0;
    }
    const uint8_t *pixels = data + sizeof(NtTextureAssetHeader);
    nt_texture_desc_t desc = {
        .width = hdr->width,
        .height = hdr->height,
        .data = pixels,
        .format = NT_PIXEL_RGBA8,
        .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_REPEAT,
        .wrap_v = NT_WRAP_REPEAT,
        .gen_mipmaps = (hdr->mip_count == 1),
        .label = "pack_texture",
    };
    nt_texture_t tex = nt_gfx_make_texture(&desc);
    return tex.id;
}

uint32_t nt_gfx_activate_mesh(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(NtMeshAssetHeader)) {
        nt_log_error("gfx: activate_mesh: blob too small");
        return 0;
    }
    const NtMeshAssetHeader *hdr = (const NtMeshAssetHeader *)data;
    if (hdr->magic != NT_MESH_MAGIC) {
        nt_log_error("gfx: activate_mesh: bad magic");
        return 0;
    }
    if (hdr->stream_count == 0 || hdr->stream_count > NT_MESH_MAX_STREAMS) {
        nt_log_error("gfx: activate_mesh: invalid stream_count");
        return 0;
    }
    uint32_t streams_size = (uint32_t)hdr->stream_count * (uint32_t)sizeof(NtStreamDesc);
    uint32_t required = (uint32_t)sizeof(NtMeshAssetHeader) + streams_size + hdr->vertex_data_size + hdr->index_data_size;
    if (required > size) {
        nt_log_error("gfx: activate_mesh: blob truncated");
        return 0;
    }
    const uint8_t *vertex_data = data + sizeof(NtMeshAssetHeader) + streams_size;
    const uint8_t *index_data = vertex_data + hdr->vertex_data_size;

    nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = vertex_data,
        .size = hdr->vertex_data_size,
        .label = "mesh_vbo",
    });
    if (vbo.id == 0) {
        nt_log_error("gfx: activate_mesh: VBO creation failed");
        return 0;
    }

    nt_buffer_t ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = index_data,
        .size = hdr->index_data_size,
        .label = "mesh_ibo",
    });
    if (ibo.id == 0) {
        nt_log_error("gfx: activate_mesh: IBO creation failed");
        nt_gfx_destroy_buffer(vbo);
        return 0;
    }

    uint32_t slot = mesh_table_alloc();
    if (slot == 0) {
        nt_log_error("gfx: activate_mesh: mesh table full");
        nt_gfx_destroy_buffer(ibo);
        nt_gfx_destroy_buffer(vbo);
        return 0;
    }

    s_gfx.mesh_table[slot].vbo = vbo;
    s_gfx.mesh_table[slot].ibo = ibo;
    s_gfx.mesh_table[slot].vertex_count = hdr->vertex_count;
    s_gfx.mesh_table[slot].index_count = hdr->index_count;
    s_gfx.mesh_table[slot].stream_count = hdr->stream_count;
    s_gfx.mesh_table[slot].index_type = hdr->index_type;
    s_gfx.mesh_table[slot].generation++;
    s_gfx.mesh_table[slot].active = true;

    return mesh_handle_make(slot, s_gfx.mesh_table[slot].generation);
}

uint32_t nt_gfx_activate_shader(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(NtShaderCodeHeader)) {
        nt_log_error("gfx: activate_shader: blob too small");
        return 0;
    }
    const NtShaderCodeHeader *hdr = (const NtShaderCodeHeader *)data;
    if (hdr->magic != NT_SHADER_CODE_MAGIC) {
        nt_log_error("gfx: activate_shader: bad magic");
        return 0;
    }
    if ((uint32_t)sizeof(NtShaderCodeHeader) + hdr->code_size > size) {
        nt_log_error("gfx: activate_shader: blob truncated");
        return 0;
    }
    const char *source = (const char *)(data + sizeof(NtShaderCodeHeader));
    nt_shader_type_t type = (hdr->stage == NT_SHADER_STAGE_VERTEX) ? NT_SHADER_VERTEX : NT_SHADER_FRAGMENT;
    nt_shader_t shd = nt_gfx_make_shader(&(nt_shader_desc_t){
        .type = type,
        .source = source,
        .label = "pack_shader",
    });
    return shd.id;
}

/* ---- Asset deactivators ---- */

void nt_gfx_deactivate_texture(uint32_t handle) {
    nt_texture_t tex = {.id = handle};
    nt_gfx_destroy_texture(tex);
}

void nt_gfx_deactivate_mesh(uint32_t handle) {
    uint32_t index = handle & 0xFFFF;
    uint16_t gen = (uint16_t)(handle >> 16);
    if (index == 0 || index > NT_GFX_MAX_MESHES) {
        nt_log_error("gfx: deactivate_mesh: invalid handle index");
        return;
    }
    if (!s_gfx.mesh_table[index].active || s_gfx.mesh_table[index].generation != gen) {
        nt_log_error("gfx: deactivate_mesh: stale or inactive handle");
        return;
    }
    nt_gfx_destroy_buffer(s_gfx.mesh_table[index].vbo);
    nt_gfx_destroy_buffer(s_gfx.mesh_table[index].ibo);
    s_gfx.mesh_table[index].active = false;
}

void nt_gfx_deactivate_shader(uint32_t handle) {
    nt_shader_t shd = {.id = handle};
    nt_gfx_destroy_shader(shd);
}

/* ---- Mesh info query ---- */

const nt_gfx_mesh_info_t *nt_gfx_get_mesh_info(uint32_t mesh_handle) {
    uint32_t index = mesh_handle & 0xFFFF;
    uint16_t gen = (uint16_t)(mesh_handle >> 16);
    if (index == 0 || index > NT_GFX_MAX_MESHES) {
        return NULL;
    }
    if (!s_gfx.mesh_table[index].active || s_gfx.mesh_table[index].generation != gen) {
        return NULL;
    }
    return &s_gfx.mesh_table[index];
}
