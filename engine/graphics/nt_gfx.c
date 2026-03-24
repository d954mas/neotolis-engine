#include "graphics/nt_gfx_internal.h"

#include <stdlib.h>
#include <string.h>

#include "basisu/nt_basisu_transcoder.h"
#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "nt_mesh_format.h"
#include "nt_shader_format.h"
#include "nt_texture_format.h"

/* Lazy transcoder initialization (one-time, at first v2 texture activation) */
static bool s_transcoder_initialized = false;

/* ---- Buffer metadata (minimal info kept for runtime validation) ---- */

typedef struct {
    uint8_t type;       /* nt_buffer_type_t */
    uint8_t usage;      /* nt_buffer_usage_t */
    uint8_t index_type; /* 0=none, 1=uint16, 2=uint32 */
    uint8_t _pad;
    uint32_t size;
} nt_gfx_buffer_meta_t;

/* ---- Global state ---- */

nt_gfx_t g_nt_gfx;

/* ---- Global UBO block registry ---- */

static nt_global_block_t s_global_blocks[NT_GFX_MAX_GLOBAL_BLOCKS];
static uint32_t s_global_block_count;

/* ---- File-scope internal state ---- */

static struct {
    nt_pool_t shader_pool;
    nt_pool_t pipeline_pool;
    nt_pool_t buffer_pool;
    nt_pool_t texture_pool;

    uint32_t *shader_backends; /* backend handles parallel to pool slots */
    uint32_t *pipeline_backends;
    uint32_t *buffer_backends;
    uint32_t *texture_backends;

    nt_gfx_buffer_meta_t *buffer_metas; /* minimal buffer metadata for runtime validation */

    nt_pool_t mesh_pool;
    nt_gfx_mesh_info_t *mesh_table; /* [capacity+1], index 0 reserved */

    nt_gfx_render_state_t render_state;
    uint32_t bound_pipeline;  /* currently bound pipeline backend handle */
    uint8_t bound_index_type; /* index type of currently bound IBO (1=uint16, 2=uint32) */
} s_gfx;

/* ---- Global UBO block registration ---- */

void nt_gfx_register_global_block(const char *name, uint32_t binding_slot) {
    NT_ASSERT(name != NULL);
    NT_ASSERT(s_global_block_count < NT_GFX_MAX_GLOBAL_BLOCKS);
    s_global_blocks[s_global_block_count].name = name;
    s_global_blocks[s_global_block_count].binding_slot = binding_slot;
    s_global_blocks[s_global_block_count].active = true;
    s_global_block_count++;
}

void nt_gfx_get_global_blocks(const nt_global_block_t **blocks, uint32_t *count) {
    NT_ASSERT(blocks != NULL);
    NT_ASSERT(count != NULL);
    *blocks = s_global_blocks;
    *count = s_global_block_count;
}

/* ---- Lifecycle ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_gfx_init(const nt_gfx_desc_t *desc) {
    NT_ASSERT(desc);
    NT_ASSERT(desc->max_shaders > 0 && "nt_gfx_desc_t.max_shaders is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_pipelines > 0 && "nt_gfx_desc_t.max_pipelines is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_buffers > 0 && "nt_gfx_desc_t.max_buffers is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_textures > 0 && "nt_gfx_desc_t.max_textures is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_meshes > 0 && "nt_gfx_desc_t.max_meshes is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    memset(&s_gfx, 0, sizeof(s_gfx));
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));

    nt_pool_init(&s_gfx.shader_pool, desc->max_shaders);
    nt_pool_init(&s_gfx.pipeline_pool, desc->max_pipelines);
    nt_pool_init(&s_gfx.buffer_pool, desc->max_buffers);
    nt_pool_init(&s_gfx.texture_pool, desc->max_textures);

    s_gfx.shader_backends = (uint32_t *)calloc(desc->max_shaders + 1, sizeof(uint32_t));
    s_gfx.pipeline_backends = (uint32_t *)calloc(desc->max_pipelines + 1, sizeof(uint32_t));
    s_gfx.buffer_backends = (uint32_t *)calloc(desc->max_buffers + 1, sizeof(uint32_t));
    s_gfx.texture_backends = (uint32_t *)calloc(desc->max_textures + 1, sizeof(uint32_t));

    s_gfx.buffer_metas = (nt_gfx_buffer_meta_t *)calloc(desc->max_buffers + 1, sizeof(nt_gfx_buffer_meta_t));

    s_gfx.render_state = NT_GFX_STATE_IDLE;

    /* Mesh pool + data table */
    nt_pool_init(&s_gfx.mesh_pool, desc->max_meshes);
    s_gfx.mesh_table = (nt_gfx_mesh_info_t *)calloc((size_t)desc->max_meshes + 1, sizeof(nt_gfx_mesh_info_t));

    if (!nt_gfx_backend_init(desc)) {
        NT_LOG_ERROR("backend init failed");
        nt_gfx_shutdown();
        return;
    }

    /* Detect GPU compressed texture capabilities */
    g_nt_gfx.gpu_caps = nt_gfx_gl_ctx_detect_gpu_caps();

    g_nt_gfx.initialized = true;
}

void nt_gfx_shutdown(void) {
    nt_gfx_backend_shutdown();

    /* Destroy active mesh table entries */
    for (uint32_t i = 1; i <= s_gfx.mesh_pool.capacity; i++) {
        if (nt_pool_slot_alive(&s_gfx.mesh_pool, i) && s_gfx.mesh_table[i].vbo.id != 0) {
            nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[nt_pool_slot_index(s_gfx.mesh_table[i].vbo.id)]);
            if (s_gfx.mesh_table[i].ibo.id != 0) {
                nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[nt_pool_slot_index(s_gfx.mesh_table[i].ibo.id)]);
            }
        }
    }

    nt_pool_shutdown(&s_gfx.shader_pool);
    nt_pool_shutdown(&s_gfx.pipeline_pool);
    nt_pool_shutdown(&s_gfx.buffer_pool);
    nt_pool_shutdown(&s_gfx.texture_pool);
    nt_pool_shutdown(&s_gfx.mesh_pool);

    free(s_gfx.shader_backends);
    free(s_gfx.pipeline_backends);
    free(s_gfx.buffer_backends);
    free(s_gfx.texture_backends);
    free(s_gfx.buffer_metas);
    free(s_gfx.mesh_table);

    memset(&s_gfx, 0, sizeof(s_gfx));
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));

    /* Clear global block registry */
    memset(s_global_blocks, 0, sizeof(s_global_blocks));
    s_global_block_count = 0;
}

const nt_gfx_gpu_caps_t *nt_gfx_gpu_caps(void) { return &g_nt_gfx.gpu_caps; }

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
            /* Mesh table: keep entries active. nt_resource_invalidate() will
             * call deactivate_mesh() which returns slots to mesh pool.
             * destroy_buffer on zeroed backend handles is safe (glDeleteBuffers(0) = no-op). */
            s_gfx.bound_pipeline = 0;
            g_nt_gfx.context_lost = true;
            NT_LOG_ERROR("WebGL context lost");
        }
        return; /* Skip frame */
    }

    if (g_nt_gfx.context_lost) {
        /* Context was lost but now available again -- game must re-create resources */
        g_nt_gfx.context_lost = false;
        g_nt_gfx.context_restored = true;
        NT_LOG_INFO("WebGL context restored -- game must re-create resources");
    }

    /* Normal frame begin */
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_IDLE);
    if (s_gfx.render_state != NT_GFX_STATE_IDLE) {
        NT_LOG_ERROR("begin_frame called outside IDLE state");
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
        NT_LOG_ERROR("end_frame called outside FRAME state");
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
        NT_LOG_ERROR("begin_pass called outside FRAME state");
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
        NT_LOG_ERROR("end_pass called outside PASS state");
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

    uint32_t id = nt_pool_alloc(&s_gfx.shader_pool);
    if (id == 0) {
        NT_LOG_ERROR("shader pool full");
        return result;
    }

    uint32_t backend = nt_gfx_backend_create_shader(desc);
    if (backend == 0) {
        NT_LOG_ERROR("backend shader creation failed");
        nt_pool_free(&s_gfx.shader_pool, id);
        return result;
    }

    uint32_t slot = nt_pool_slot_index(id);
    s_gfx.shader_backends[slot] = backend;

    result.id = id;
    return result;
}

nt_pipeline_t nt_gfx_make_pipeline(const nt_pipeline_desc_t *desc) {
    nt_pipeline_t result = {0};
    if (!desc) {
        return result;
    }

    if (!nt_pool_valid(&s_gfx.shader_pool, desc->vertex_shader.id) || !nt_pool_valid(&s_gfx.shader_pool, desc->fragment_shader.id)) {
        NT_LOG_ERROR("pipeline creation failed: invalid shader handle");
        return result;
    }
    if (desc->layout.attr_count > NT_GFX_MAX_VERTEX_ATTRS) {
        NT_LOG_ERROR("pipeline creation failed: too many vertex attrs");
        return result;
    }
    if (desc->instance_layout.attr_count > NT_GFX_MAX_VERTEX_ATTRS) {
        NT_LOG_ERROR("pipeline creation failed: too many instance attrs");
        return result;
    }

    uint32_t id = nt_pool_alloc(&s_gfx.pipeline_pool);
    if (id == 0) {
        NT_LOG_ERROR("pipeline pool full");
        return result;
    }

    uint32_t vs_slot = nt_pool_slot_index(desc->vertex_shader.id);
    uint32_t fs_slot = nt_pool_slot_index(desc->fragment_shader.id);
    uint32_t vs_backend = s_gfx.shader_backends[vs_slot];
    uint32_t fs_backend = s_gfx.shader_backends[fs_slot];

    uint32_t backend = nt_gfx_backend_create_pipeline(desc, vs_backend, fs_backend);
    if (backend == 0) {
        NT_LOG_ERROR("backend pipeline creation failed");
        nt_pool_free(&s_gfx.pipeline_pool, id);
        return result;
    }

    uint32_t slot = nt_pool_slot_index(id);
    s_gfx.pipeline_backends[slot] = backend;

    result.id = id;
    return result;
}

nt_buffer_t nt_gfx_make_buffer(const nt_buffer_desc_t *desc) {
    nt_buffer_t result = {0};
    if (!desc) {
        return result;
    }

    uint32_t id = nt_pool_alloc(&s_gfx.buffer_pool);
    if (id == 0) {
        NT_LOG_ERROR("buffer pool full");
        return result;
    }

    uint32_t backend = nt_gfx_backend_create_buffer(desc);
    if (backend == 0) {
        NT_LOG_ERROR("backend buffer creation failed");
        nt_pool_free(&s_gfx.buffer_pool, id);
        return result;
    }

    uint32_t slot = nt_pool_slot_index(id);
    s_gfx.buffer_backends[slot] = backend;
    s_gfx.buffer_metas[slot].type = (uint8_t)desc->type;
    s_gfx.buffer_metas[slot].usage = (uint8_t)desc->usage;
    s_gfx.buffer_metas[slot].index_type = desc->index_type;
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
        NT_LOG_ERROR("make_texture: NULL data");
        return result;
    }
    if (desc->width == 0 || desc->height == 0) {
        NT_LOG_ERROR("make_texture: zero dimension");
        return result;
    }
    nt_texture_desc_t local_desc = *desc;

    /* Clamp mipmap min_filter when no mipmaps — prevents GL incomplete texture */
    if (!local_desc.gen_mipmaps && local_desc.min_filter > NT_FILTER_LINEAR) {
        local_desc.min_filter = (local_desc.min_filter & 1) ? NT_FILTER_LINEAR : NT_FILTER_NEAREST;
        NT_LOG_INFO("make_texture: min_filter clamped (gen_mipmaps=false)");
    }

    /* Validate mag_filter: only NEAREST or LINEAR allowed */
    if (local_desc.mag_filter > NT_FILTER_LINEAR) {
        NT_LOG_INFO("make_texture: mag_filter clamped to LINEAR");
        local_desc.mag_filter = NT_FILTER_LINEAR;
    }

    uint32_t id = nt_pool_alloc(&s_gfx.texture_pool);
    if (id == 0) {
        NT_LOG_ERROR("texture pool full");
        return result;
    }

    uint32_t backend = nt_gfx_backend_create_texture(&local_desc);
    if (backend == 0) {
        NT_LOG_ERROR("backend texture creation failed");
        nt_pool_free(&s_gfx.texture_pool, id);
        return result;
    }

    uint32_t slot = nt_pool_slot_index(id);
    s_gfx.texture_backends[slot] = backend;

    result.id = id;
    return result;
}

/* ---- Resource destruction ---- */

void nt_gfx_destroy_shader(nt_shader_t shd) {
    if (!nt_pool_valid(&s_gfx.shader_pool, shd.id)) {
        NT_LOG_ERROR("destroy_shader: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(shd.id);
    nt_gfx_backend_destroy_shader(s_gfx.shader_backends[slot]);
    s_gfx.shader_backends[slot] = 0;
    nt_pool_free(&s_gfx.shader_pool, shd.id);
}

void nt_gfx_destroy_pipeline(nt_pipeline_t pip) {
    if (!nt_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        NT_LOG_ERROR("destroy_pipeline: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(pip.id);
    if (s_gfx.bound_pipeline == s_gfx.pipeline_backends[slot]) {
        s_gfx.bound_pipeline = 0;
    }
    nt_gfx_backend_destroy_pipeline(s_gfx.pipeline_backends[slot]);
    s_gfx.pipeline_backends[slot] = 0;
    nt_pool_free(&s_gfx.pipeline_pool, pip.id);
}

void nt_gfx_destroy_buffer(nt_buffer_t buf) {
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        NT_LOG_ERROR("destroy_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(buf.id);
    nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[slot]);
    s_gfx.buffer_backends[slot] = 0;
    memset(&s_gfx.buffer_metas[slot], 0, sizeof(nt_gfx_buffer_meta_t));
    nt_pool_free(&s_gfx.buffer_pool, buf.id);
}

void nt_gfx_destroy_texture(nt_texture_t tex) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        NT_LOG_ERROR("destroy_texture: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(tex.id);
    nt_gfx_backend_destroy_texture(s_gfx.texture_backends[slot]);
    s_gfx.texture_backends[slot] = 0;
    nt_pool_free(&s_gfx.texture_pool, tex.id);
}

/* ---- Draw state ---- */

void nt_gfx_bind_pipeline(nt_pipeline_t pip) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        NT_LOG_ERROR("bind_pipeline: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(pip.id);
    NT_ASSERT(s_gfx.pipeline_backends[slot] != 0); /* stale pipeline after context loss — must recreate */
    s_gfx.bound_pipeline = s_gfx.pipeline_backends[slot];
    nt_gfx_backend_bind_pipeline(s_gfx.bound_pipeline);
}

void nt_gfx_bind_vertex_buffer(nt_buffer_t buf) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        NT_LOG_ERROR("bind_vertex_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[slot].type == NT_BUFFER_VERTEX);
    if (s_gfx.buffer_metas[slot].type != NT_BUFFER_VERTEX) {
        NT_LOG_ERROR("bind_vertex_buffer: buffer is not vertex type");
        return;
    }
    nt_gfx_backend_bind_vertex_buffer(s_gfx.buffer_backends[slot]);
}

void nt_gfx_bind_index_buffer(nt_buffer_t buf) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        NT_LOG_ERROR("bind_index_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[slot].type == NT_BUFFER_INDEX);
    if (s_gfx.buffer_metas[slot].type != NT_BUFFER_INDEX) {
        NT_LOG_ERROR("bind_index_buffer: buffer is not index type");
        return;
    }
    s_gfx.bound_index_type = s_gfx.buffer_metas[slot].index_type;
    nt_gfx_backend_bind_index_buffer(s_gfx.buffer_backends[slot]);
}

void nt_gfx_bind_texture(nt_texture_t tex, uint32_t slot) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        NT_LOG_ERROR("bind_texture: invalid handle");
        return;
    }
    if (slot >= NT_GFX_MAX_TEXTURE_SLOTS) {
        NT_LOG_ERROR("bind_texture: slot exceeds max");
        return;
    }
    uint32_t idx = nt_pool_slot_index(tex.id);
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
        NT_LOG_ERROR("draw called outside PASS state");
        return;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    if (s_gfx.bound_pipeline == 0) {
        NT_LOG_ERROR("draw called without bound pipeline");
        return;
    }

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.vertices += num_vertices;
    nt_gfx_backend_draw(first_vertex, num_vertices);
}

void nt_gfx_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        NT_LOG_ERROR("draw_instanced called outside PASS state");
        return;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    if (s_gfx.bound_pipeline == 0) {
        NT_LOG_ERROR("draw_instanced called without bound pipeline");
        return;
    }

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.draw_calls_instanced++;
    g_nt_gfx.frame_stats.vertices += num_vertices * instance_count;
    g_nt_gfx.frame_stats.instances += instance_count;
    nt_gfx_backend_draw_instanced(first_vertex, num_vertices, instance_count);
}

void nt_gfx_draw_indexed(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        NT_LOG_ERROR("draw_indexed called outside PASS state");
        return;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    if (s_gfx.bound_pipeline == 0) {
        NT_LOG_ERROR("draw_indexed called without bound pipeline");
        return;
    }

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.vertices += num_vertices;
    g_nt_gfx.frame_stats.indices += num_indices;
    nt_gfx_backend_draw_indexed(first_index, num_indices, s_gfx.bound_index_type);
}

void nt_gfx_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices, uint32_t instance_count) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_PASS);
    if (s_gfx.render_state != NT_GFX_STATE_PASS) {
        NT_LOG_ERROR("draw_indexed_instanced called outside PASS state");
        return;
    }
    NT_ASSERT(s_gfx.bound_pipeline != 0);
    if (s_gfx.bound_pipeline == 0) {
        NT_LOG_ERROR("draw_indexed_instanced called without bound pipeline");
        return;
    }

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.draw_calls_instanced++;
    g_nt_gfx.frame_stats.vertices += num_vertices * instance_count;
    g_nt_gfx.frame_stats.indices += num_indices * instance_count;
    g_nt_gfx.frame_stats.instances += instance_count;
    nt_gfx_backend_draw_indexed_instanced(first_index, num_indices, instance_count, s_gfx.bound_index_type);
}

/* ---- Instance buffer ---- */

void nt_gfx_bind_instance_buffer(nt_buffer_t buf) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        NT_LOG_ERROR("bind_instance_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[slot].type == NT_BUFFER_VERTEX);
    if (s_gfx.buffer_metas[slot].type != NT_BUFFER_VERTEX) {
        NT_LOG_ERROR("bind_instance_buffer: buffer is not vertex type");
        return;
    }
    nt_gfx_backend_bind_instance_buffer(s_gfx.buffer_backends[slot]);
}

void nt_gfx_set_instance_offset(uint32_t byte_offset) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    nt_gfx_backend_set_instance_offset(byte_offset);
}

void nt_gfx_set_vertex_attrib_default(uint8_t location, float x, float y, float z, float w) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    nt_gfx_backend_set_vertex_attrib_default(location, x, y, z, w);
}

/* ---- Uniform buffer ---- */

void nt_gfx_bind_uniform_buffer(nt_buffer_t buf, uint32_t slot) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        NT_LOG_ERROR("bind_uniform_buffer: invalid handle");
        return;
    }
    uint32_t idx = nt_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[idx].type == NT_BUFFER_UNIFORM);
    if (s_gfx.buffer_metas[idx].type != NT_BUFFER_UNIFORM) {
        NT_LOG_ERROR("bind_uniform_buffer: buffer is not uniform type");
        return;
    }
    nt_gfx_backend_bind_uniform_buffer(s_gfx.buffer_backends[idx], slot);
}

void nt_gfx_set_uniform_block(nt_pipeline_t pip, const char *block_name, uint32_t slot) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        NT_LOG_ERROR("set_uniform_block: invalid pipeline handle");
        return;
    }
    uint32_t idx = nt_pool_slot_index(pip.id);
    nt_gfx_backend_set_uniform_block(s_gfx.pipeline_backends[idx], block_name, slot);
}

/* ---- Buffer update ---- */

void nt_gfx_update_buffer(nt_buffer_t buf, const void *data, uint32_t size) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        NT_LOG_ERROR("update_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[slot].usage != NT_USAGE_IMMUTABLE);
    if (s_gfx.buffer_metas[slot].usage == NT_USAGE_IMMUTABLE) {
        NT_LOG_ERROR("update_buffer: cannot update immutable buffer");
        return;
    }
    NT_ASSERT(size <= s_gfx.buffer_metas[slot].size);
    if (size > s_gfx.buffer_metas[slot].size) {
        NT_LOG_ERROR("update_buffer: size exceeds buffer capacity");
        return;
    }
    nt_gfx_backend_update_buffer(s_gfx.buffer_backends[slot], data, size);
}

/* ---- Mesh side table helpers ---- */

/* mesh_table_alloc and mesh_handle_make replaced by nt_pool_alloc(&s_gfx.mesh_pool) */

/* ---- Asset activators ---- */

/* Activate a v2 compressed texture (Basis Universal transcode + compressed upload) */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t activate_texture_v2(const uint8_t *data, uint32_t size) {
    const NtTextureAssetHeaderV2 *hdr2 = (const NtTextureAssetHeaderV2 *)data;

    /* Validate data size (subtraction safe — caller verified size >= sizeof header) */
    if (hdr2->data_size > size - sizeof(NtTextureAssetHeaderV2)) {
        NT_LOG_ERROR("activate_texture: v2 blob truncated");
        return 0;
    }

    /* RAW compression: handle same as v1 (raw pixels after v2 header) */
    if (hdr2->compression == NT_TEXTURE_COMPRESSION_RAW) {
        nt_pixel_format_t pixel_fmt;
        switch (hdr2->format) {
        case NT_TEXTURE_FORMAT_RGBA8:
            pixel_fmt = NT_PIXEL_RGBA8;
            break;
        case NT_TEXTURE_FORMAT_RGB8:
            pixel_fmt = NT_PIXEL_RGB8;
            break;
        case NT_TEXTURE_FORMAT_RG8:
            pixel_fmt = NT_PIXEL_RG8;
            break;
        case NT_TEXTURE_FORMAT_R8:
            pixel_fmt = NT_PIXEL_R8;
            break;
        default:
            NT_LOG_ERROR("activate_texture: v2 unsupported format");
            return 0;
        }
        const uint8_t *pixels = data + sizeof(NtTextureAssetHeaderV2);
        nt_texture_desc_t desc = {
            .width = hdr2->width,
            .height = hdr2->height,
            .data = pixels,
            .format = pixel_fmt,
            .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
            .mag_filter = NT_FILTER_LINEAR,
            .wrap_u = NT_WRAP_REPEAT,
            .wrap_v = NT_WRAP_REPEAT,
            .gen_mipmaps = (hdr2->mip_count == 1),
            .label = NULL,
        };
        nt_texture_t tex = nt_gfx_make_texture(&desc);
        return tex.id;
    }

    /* BASIS compression: transcode to best available GPU format */
    if (hdr2->compression != NT_TEXTURE_COMPRESSION_BASIS) {
        NT_LOG_ERROR("activate_texture: v2 unknown compression %u", hdr2->compression);
        return 0;
    }

    /* Lazy transcoder init */
    if (!s_transcoder_initialized) {
        nt_basisu_transcoder_global_init();
        s_transcoder_initialized = true;
    }

    const uint8_t *basis_data = data + sizeof(NtTextureAssetHeaderV2);
    uint32_t basis_size = hdr2->data_size;

    if (!nt_basisu_validate_header(basis_data, basis_size)) {
        NT_LOG_ERROR("activate_texture: invalid Basis data");
        return 0;
    }

    /* Select transcode target: BC7 > ASTC > ETC2 > RGBA8 (D-14 cascade) */
    bool has_alpha = (hdr2->format == NT_TEXTURE_FORMAT_RGBA8);
    const nt_gfx_gpu_caps_t *caps = nt_gfx_gpu_caps();
    nt_basisu_format_t target;
    if (caps->has_bc7) {
        target = NT_BASISU_FORMAT_BC7_RGBA;
    } else if (caps->has_astc) {
        target = NT_BASISU_FORMAT_ASTC_4x4_RGBA;
    } else if (caps->has_etc2) {
        target = has_alpha ? NT_BASISU_FORMAT_ETC2_RGBA : NT_BASISU_FORMAT_ETC1_RGB;
    } else {
        target = NT_BASISU_FORMAT_RGBA32;
    }

    uint32_t levels = nt_basisu_get_level_count(basis_data, basis_size);
    if (levels == 0) {
        NT_LOG_ERROR("activate_texture: Basis data has 0 levels");
        return 0;
    }

    /* Allocate pool slot for the texture */
    uint32_t id = nt_pool_alloc(&s_gfx.texture_pool);
    if (id == 0) {
        NT_LOG_ERROR("texture pool full");
        return 0;
    }

    /* Call backend for per-mip transcode + compressed upload */
    uint32_t backend = nt_gfx_backend_create_texture_compressed(basis_data, basis_size, hdr2->width, hdr2->height, levels, NT_FILTER_LINEAR_MIPMAP_LINEAR, NT_FILTER_LINEAR, NT_WRAP_REPEAT,
                                                                NT_WRAP_REPEAT, (uint32_t)target);
    if (backend == 0) {
        NT_LOG_ERROR("activate_texture: transcode failed");
        nt_pool_free(&s_gfx.texture_pool, id);
        return 0;
    }

    uint32_t slot = nt_pool_slot_index(id);
    s_gfx.texture_backends[slot] = backend;
    return id;
}

/* Texture activation cost: log2 scaling by dimension.
 * 64→1, 128→1, 256→2, 512→4, 1024→8, 2048→16.
 * Compressed textures +50% for transcode overhead. */
int32_t nt_gfx_texture_activate_cost(const uint8_t *data, uint32_t size) {
    if (size < sizeof(NtTextureAssetHeader)) {
        return 1;
    }
    const NtTextureAssetHeader *hdr = (const NtTextureAssetHeader *)data;
    uint32_t max_dim = hdr->width > hdr->height ? hdr->width : hdr->height;

    /* 1 << max(0, log2(max_dim) - 6): each doubling above 64 doubles cost */
    int32_t cost = 1;
    uint32_t dim = max_dim >> 7; /* divide by 128: 64→0, 128→1, 256→2, 512→4 */
    while (dim > 0) {
        cost <<= 1;
        dim >>= 1;
    }

    /* Compressed texture: +50% for transcode */
    if (hdr->version == NT_TEXTURE_VERSION_V2 && size >= sizeof(NtTextureAssetHeaderV2)) {
        const NtTextureAssetHeaderV2 *v2 = (const NtTextureAssetHeaderV2 *)data;
        if (v2->compression == NT_TEXTURE_COMPRESSION_BASIS) {
            cost += cost / 2;
        }
    }

    return cost;
}

uint32_t nt_gfx_activate_texture(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(NtTextureAssetHeader)) {
        NT_LOG_ERROR("activate_texture: blob too small");
        return 0;
    }
    const NtTextureAssetHeader *hdr = (const NtTextureAssetHeader *)data;
    if (hdr->magic != NT_TEXTURE_MAGIC) {
        NT_LOG_ERROR("activate_texture: bad magic");
        return 0;
    }
    if (hdr->version != NT_TEXTURE_VERSION && hdr->version != NT_TEXTURE_VERSION_V2) {
        NT_LOG_ERROR("activate_texture: unsupported version %u", hdr->version);
        return 0;
    }

    /* v2 compressed texture path */
    if (hdr->version == NT_TEXTURE_VERSION_V2) {
        if (size < sizeof(NtTextureAssetHeaderV2)) {
            NT_LOG_ERROR("activate_texture: v2 blob too small");
            return 0;
        }
        return activate_texture_v2(data, size);
    }

    /* v1 path (unchanged) */
    nt_pixel_format_t pixel_fmt;
    switch (hdr->format) {
    case NT_TEXTURE_FORMAT_RGBA8:
        pixel_fmt = NT_PIXEL_RGBA8;
        break;
    case NT_TEXTURE_FORMAT_RGB8:
        pixel_fmt = NT_PIXEL_RGB8;
        break;
    case NT_TEXTURE_FORMAT_RG8:
        pixel_fmt = NT_PIXEL_RG8;
        break;
    case NT_TEXTURE_FORMAT_R8:
        pixel_fmt = NT_PIXEL_R8;
        break;
    default:
        NT_LOG_ERROR("activate_texture: unsupported format");
        return 0;
    }
    uint32_t bpp = nt_texture_bpp((nt_texture_pixel_format_t)hdr->format);
    uint32_t pixel_size = hdr->width * hdr->height * bpp;
    if (sizeof(NtTextureAssetHeader) + pixel_size > size) {
        NT_LOG_ERROR("activate_texture: blob truncated");
        return 0;
    }
    const uint8_t *pixels = data + sizeof(NtTextureAssetHeader);
    nt_texture_desc_t desc = {
        .width = hdr->width,
        .height = hdr->height,
        .data = pixels,
        .format = pixel_fmt,
        .min_filter = NT_FILTER_LINEAR_MIPMAP_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_REPEAT,
        .wrap_v = NT_WRAP_REPEAT,
        .gen_mipmaps = (hdr->mip_count == 1),
        .label = NULL,
    };
    nt_texture_t tex = nt_gfx_make_texture(&desc);
    return tex.id;
}

uint32_t nt_gfx_activate_mesh(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(NtMeshAssetHeader)) {
        NT_LOG_ERROR("activate_mesh: blob too small");
        return 0;
    }
    const NtMeshAssetHeader *hdr = (const NtMeshAssetHeader *)data;
    if (hdr->magic != NT_MESH_MAGIC) {
        NT_LOG_ERROR("activate_mesh: bad magic");
        return 0;
    }
    if (hdr->version > NT_MESH_VERSION) {
        NT_LOG_ERROR("activate_mesh: unsupported version");
        return 0;
    }
    if (hdr->index_type > 2) {
        NT_LOG_ERROR("activate_mesh: invalid index_type");
        return 0;
    }
    if (hdr->stream_count == 0 || hdr->stream_count > NT_MESH_MAX_STREAMS) {
        NT_LOG_ERROR("activate_mesh: invalid stream_count");
        return 0;
    }
    uint32_t streams_size = (uint32_t)hdr->stream_count * (uint32_t)sizeof(NtStreamDesc);
    uint32_t required = (uint32_t)sizeof(NtMeshAssetHeader) + streams_size + hdr->vertex_data_size + hdr->index_data_size;
    if (required > size) {
        NT_LOG_ERROR("activate_mesh: blob truncated");
        return 0;
    }
    const uint8_t *vertex_data = data + sizeof(NtMeshAssetHeader) + streams_size;
    const uint8_t *index_data = vertex_data + hdr->vertex_data_size;

    nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = vertex_data,
        .size = hdr->vertex_data_size,
        .label = NULL,
    });
    if (vbo.id == 0) {
        NT_LOG_ERROR("activate_mesh: VBO creation failed");
        return 0;
    }

    nt_buffer_t ibo = {0};
    if (hdr->index_type != 0 && hdr->index_data_size > 0) {
        ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = NT_BUFFER_INDEX,
            .usage = NT_USAGE_IMMUTABLE,
            .data = index_data,
            .size = hdr->index_data_size,
            .index_type = hdr->index_type,
            .label = NULL,
        });
        if (ibo.id == 0) {
            NT_LOG_ERROR("activate_mesh: IBO creation failed");
            nt_gfx_destroy_buffer(vbo);
            return 0;
        }
    }

    uint32_t mesh_id = nt_pool_alloc(&s_gfx.mesh_pool);
    if (mesh_id == 0) {
        NT_LOG_ERROR("activate_mesh: mesh pool full");
        nt_gfx_destroy_buffer(ibo);
        nt_gfx_destroy_buffer(vbo);
        return 0;
    }

    uint32_t slot = nt_pool_slot_index(mesh_id);
    memset(&s_gfx.mesh_table[slot], 0, sizeof(nt_gfx_mesh_info_t));

    s_gfx.mesh_table[slot].vbo = vbo;
    s_gfx.mesh_table[slot].ibo = ibo;
    s_gfx.mesh_table[slot].vertex_count = hdr->vertex_count;
    s_gfx.mesh_table[slot].index_count = hdr->index_count;
    s_gfx.mesh_table[slot].stream_count = hdr->stream_count;
    s_gfx.mesh_table[slot].index_type = hdr->index_type;

    /* Copy stream descriptors from pack data for render module vertex layout building */
    const NtStreamDesc *src_streams = (const NtStreamDesc *)(data + sizeof(NtMeshAssetHeader));
    memcpy(s_gfx.mesh_table[slot].streams, src_streams, (size_t)hdr->stream_count * sizeof(NtStreamDesc));

    /* Compute stride: sum of all stream sizes (type_size * count) */
    uint16_t stride = 0;
    for (uint8_t i = 0; i < hdr->stream_count; i++) {
        stride += (uint16_t)(nt_stream_type_size(src_streams[i].type) * src_streams[i].count);
    }
    s_gfx.mesh_table[slot].stride = stride;

    /* Compute layout_hash from stream descriptors for pipeline cache keying */
    s_gfx.mesh_table[slot].layout_hash = nt_hash32(src_streams, (uint32_t)hdr->stream_count * (uint32_t)sizeof(NtStreamDesc)).value;

    return mesh_id;
}

uint32_t nt_gfx_activate_shader(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(NtShaderCodeHeader)) {
        NT_LOG_ERROR("activate_shader: blob too small");
        return 0;
    }
    const NtShaderCodeHeader *hdr = (const NtShaderCodeHeader *)data;
    if (hdr->magic != NT_SHADER_CODE_MAGIC) {
        NT_LOG_ERROR("activate_shader: bad magic");
        return 0;
    }
    if (hdr->version > NT_SHADER_CODE_VERSION) {
        NT_LOG_ERROR("activate_shader: unsupported version");
        return 0;
    }
    if (hdr->stage > NT_SHADER_STAGE_FRAGMENT) {
        NT_LOG_ERROR("activate_shader: invalid stage");
        return 0;
    }
    if ((uint32_t)sizeof(NtShaderCodeHeader) + hdr->code_size > size) {
        NT_LOG_ERROR("activate_shader: blob truncated");
        return 0;
    }
    const char *source = (const char *)(data + sizeof(NtShaderCodeHeader));
    nt_shader_type_t type = (hdr->stage == NT_SHADER_STAGE_VERTEX) ? NT_SHADER_VERTEX : NT_SHADER_FRAGMENT;
    nt_shader_t shd = nt_gfx_make_shader(&(nt_shader_desc_t){
        .type = type,
        .source = source,
        .label = NULL,
    });
    return shd.id;
}

/* ---- Asset deactivators ---- */

void nt_gfx_deactivate_texture(uint32_t handle) {
    nt_texture_t tex = {.id = handle};
    nt_gfx_destroy_texture(tex);
}

void nt_gfx_deactivate_mesh(uint32_t handle) {
    if (!nt_pool_valid(&s_gfx.mesh_pool, handle)) {
        NT_LOG_ERROR("deactivate_mesh: invalid or stale handle");
        return;
    }
    uint32_t index = nt_pool_slot_index(handle);
    nt_gfx_destroy_buffer(s_gfx.mesh_table[index].vbo);
    if (s_gfx.mesh_table[index].ibo.id != 0) {
        nt_gfx_destroy_buffer(s_gfx.mesh_table[index].ibo);
    }
    memset(&s_gfx.mesh_table[index], 0, sizeof(nt_gfx_mesh_info_t));
    nt_pool_free(&s_gfx.mesh_pool, handle);
}

void nt_gfx_deactivate_shader(uint32_t handle) {
    nt_shader_t shd = {.id = handle};
    nt_gfx_destroy_shader(shd);
}

/* ---- Mesh info query ---- */

const nt_gfx_mesh_info_t *nt_gfx_get_mesh_info(nt_mesh_t mesh) {
    if (!nt_pool_valid(&s_gfx.mesh_pool, mesh.id)) {
        return NULL;
    }
    uint32_t index = nt_pool_slot_index(mesh.id);
    return &s_gfx.mesh_table[index];
}
