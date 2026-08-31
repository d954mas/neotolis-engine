#include "graphics/nt_gfx_internal.h"

#include <stdlib.h>
#include <string.h>

#include "basisu/nt_basisu_transcoder.h"
#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "meshwire/nt_meshwire.h"
#include "nt_mesh_format.h"
#include "nt_shader_format.h"
#include "nt_texture_format.h"

/* Shared mesh-decode staging, mirroring the backend's transcode buffer:
 * grow-on-demand, reused across activations (VBO then IBO sequentially),
 * freed after NT_MESH_STAGE_IDLE_FRAMES without use -- no per-activation
 * heap traffic while a pack streams in. */
#define NT_MESH_STAGE_IDLE_FRAMES 60 /* ~1s at 60fps */
static uint8_t *s_mesh_stage_buf = NULL;
static uint32_t s_mesh_stage_size = 0;
static uint32_t s_mesh_stage_idle = 0;

static uint8_t *mesh_stage_acquire(uint32_t size) {
    if (size > s_mesh_stage_size) {
        free(s_mesh_stage_buf);
        s_mesh_stage_buf = (uint8_t *)malloc(size);
        if (!s_mesh_stage_buf) {
            s_mesh_stage_size = 0;
            return NULL;
        }
        s_mesh_stage_size = size;
    }
    s_mesh_stage_idle = 0;
    return s_mesh_stage_buf;
}

static void mesh_stage_frame_tick(void) {
    if (s_mesh_stage_buf != NULL) {
        s_mesh_stage_idle++;
        if (s_mesh_stage_idle > NT_MESH_STAGE_IDLE_FRAMES) {
            free(s_mesh_stage_buf);
            s_mesh_stage_buf = NULL;
            s_mesh_stage_size = 0;
        }
    }
}

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

/* ---- Vertex input metadata (destroy cascade + draw-invariant checks) ---- */

typedef struct {
    uint32_t vbo_id; /* full buffer handles: destroy_buffer cascades on exact match */
    uint32_t ibo_id;
    uint8_t index_type; /* captured from the IBO; NT_INDEX_NONE for non-indexed */
    uint8_t instance_attr_count;
    /* Enabled-but-unpointed instance attribs are invalid GL that fails
     * silently -- draws assert this went through bind_instance_buffer once. */
    bool instance_pointed;
} nt_gfx_vertex_input_meta_t;

/* ---- Texture metadata (format + dimensions for update_texture validation) ---- */

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t format;    /* nt_texture_format_t */
    uint8_t mip_count; /* 1 = base only, >1 = has mip chain */
    bool compressed;   /* true for Basis/GPU-compressed textures */
    bool render_target_owned;
    /* Sampler bound automatically by bind_texture. Always non-zero in
     * normal runtime (make_texture / activator assert this). Reset to
     * NT_SAMPLER_INVALID transiently during context-loss recovery. */
    nt_sampler_t default_sampler;
} nt_gfx_texture_meta_t;

typedef struct {
    nt_render_target_desc_t desc;
    nt_texture_t color;
    nt_texture_t depth;
    bool complete;
} nt_gfx_render_target_meta_t;

/* ---- Global state ---- */

nt_gfx_t g_nt_gfx;

/* ---- Global UBO block registry ---- */

static nt_global_block_t s_global_blocks[NT_GFX_MAX_GLOBAL_BLOCKS];
static uint32_t s_global_block_count;

/* ---- File-scope internal state ---- */

/* Sampler cache entry — packed key, original desc, and current backend handle.
 * Lifetime = engine. desc is preserved across context loss so backend can be
 * lazily recreated without invalidating material-side sampler.id handles. */
typedef struct {
    uint32_t key;           /* hash of (min, mag, wrap_u, wrap_v, compare) */
    uint32_t backend;       /* GL sampler object handle; 0 after context loss */
    nt_sampler_desc_t desc; /* recorded for context-loss recreate */
} nt_gfx_sampler_entry_t;

static struct {
    nt_pool_t shader_pool;
    nt_pool_t program_pool;
    nt_pool_t pipeline_pool;
    nt_pool_t vertex_input_pool;
    nt_pool_t buffer_pool;
    nt_pool_t texture_pool;
    nt_pool_t render_target_pool;

    uint32_t *shader_backends; /* backend handles parallel to pool slots */
    uint32_t *program_backends;
    uint32_t *pipeline_backends;
    uint32_t *pipeline_programs; /* full program handle each pipeline borrows */
    uint32_t *vertex_input_backends;
    uint32_t *buffer_backends;
    uint32_t *texture_backends;
    uint32_t *render_target_backends;

    nt_gfx_buffer_meta_t *buffer_metas;   /* minimal buffer metadata for runtime validation */
    nt_gfx_texture_meta_t *texture_metas; /* format + dimensions for update_texture validation */
    nt_gfx_vertex_input_meta_t *vertex_input_metas;
    nt_gfx_render_target_meta_t *render_target_metas;

    /* Sampler cache (dedup by descriptor; samplers are immutable after create
     * and shared across materials/textures). nt_sampler_t.id is 1+slot. */
    nt_gfx_sampler_entry_t sampler_cache[NT_GFX_MAX_SAMPLERS];
    uint32_t sampler_count;

    nt_pool_t mesh_pool;
    nt_gfx_mesh_info_t *mesh_table; /* [capacity+1], index 0 reserved */

    nt_gfx_render_state_t render_state;
    bool context_restore_retry;
    uint32_t bound_pipeline;     /* currently bound pipeline backend handle */
    uint32_t bound_vertex_input; /* full handle of the bound vertex input, 0 = none */
    uint32_t bound_texture_ids[NT_GFX_MAX_TEXTURE_SLOTS];
    uint8_t bound_index_type; /* index type of currently bound IBO (1=uint16, 2=uint32) */
    bool scissor_enabled;     /* mirrors GL_SCISSOR_TEST */

    /* Mirrors of last set_scissor / set_viewport — only NT_TEST_ACCESS
     * probes read them; production never does. */
    int scissor_rect[4];  /* GL bottom-left x,y,w,h */
    int viewport_rect[4]; /* GL bottom-left x,y,w,h */
} s_gfx;

#ifdef NT_TEST_ACCESS
static nt_gfx_test_draw_t s_test_draws[128];
static nt_pipeline_t s_test_bound_pipeline;
static uint32_t s_test_draw_count;
static bool s_test_draw_enabled;
static bool s_test_draw_overflow;

void nt_gfx_test_draw_trace_reset(bool enabled) {
    s_test_draw_count = 0;
    s_test_draw_overflow = false;
    s_test_draw_enabled = enabled;
}

uint32_t nt_gfx_test_draw_trace_count(void) { return s_test_draw_count; }
bool nt_gfx_test_draw_trace_overflowed(void) { return s_test_draw_overflow; }

nt_gfx_test_draw_t nt_gfx_test_draw_trace_at(uint32_t index) {
    NT_ASSERT(index < s_test_draw_count);
    return s_test_draws[index];
}

static void test_record_draw(uint32_t first_vertex, uint32_t num_vertices, uint32_t first_index, uint32_t num_indices, uint32_t instance_count) {
    if (!s_test_draw_enabled) {
        return;
    }
    if (s_test_draw_count == sizeof(s_test_draws) / sizeof(s_test_draws[0])) {
        s_test_draw_overflow = true;
        return;
    }
    s_test_draws[s_test_draw_count++] = (nt_gfx_test_draw_t){
        .pipeline = s_test_bound_pipeline,
        .program = {s_gfx.pipeline_programs[nt_pool_slot_index(s_test_bound_pipeline.id)]},
        .first_vertex = first_vertex,
        .num_vertices = num_vertices,
        .first_index = first_index,
        .num_indices = num_indices,
        .instance_count = instance_count,
    };
}
#endif

/* ---- Global UBO block registration ---- */

void nt_gfx_register_global_block(const char *name, uint32_t binding_slot) {
    NT_ASSERT(name != NULL);
    NT_ASSERT(s_global_block_count < NT_GFX_MAX_GLOBAL_BLOCKS);
    /* Borrowed until nt_gfx_shutdown; use a string literal or equally long-lived immutable storage. */
    s_global_blocks[s_global_block_count].name = name;
    s_global_blocks[s_global_block_count].binding_slot = binding_slot;
    s_global_blocks[s_global_block_count].active = true;
    s_global_block_count++;

    /* Late registration must also bind blocks in programs already linked. */
    for (uint32_t i = 1; i <= s_gfx.program_pool.capacity; i++) {
        if (s_gfx.program_backends[i] != 0) {
            nt_gfx_backend_set_uniform_block(s_gfx.program_backends[i], name, binding_slot);
        }
    }
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
#ifdef NT_TEST_ACCESS
    nt_gfx_test_draw_trace_reset(false);
#endif
    NT_ASSERT(desc);
    NT_ASSERT(desc->max_shaders > 0 && "nt_gfx_desc_t.max_shaders is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_programs > 0 && "nt_gfx_desc_t.max_programs is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_pipelines > 0 && "nt_gfx_desc_t.max_pipelines is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_buffers > 0 && "nt_gfx_desc_t.max_buffers is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_textures > 0 && "nt_gfx_desc_t.max_textures is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_meshes > 0 && "nt_gfx_desc_t.max_meshes is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_vertex_inputs > 0 && "nt_gfx_desc_t.max_vertex_inputs is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    NT_ASSERT(desc->max_render_targets > 0 && "nt_gfx_desc_t.max_render_targets is 0 -- use nt_gfx_desc_defaults() or set explicitly");
    uint16_t max_render_targets = desc->max_render_targets;
    memset(&s_gfx, 0, sizeof(s_gfx));
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));

    nt_pool_init(&s_gfx.shader_pool, desc->max_shaders);
    nt_pool_init(&s_gfx.program_pool, desc->max_programs);
    nt_pool_init(&s_gfx.pipeline_pool, desc->max_pipelines);
    nt_pool_init(&s_gfx.vertex_input_pool, desc->max_vertex_inputs);
    nt_pool_init(&s_gfx.buffer_pool, desc->max_buffers);
    nt_pool_init(&s_gfx.texture_pool, desc->max_textures);
    nt_pool_init(&s_gfx.render_target_pool, max_render_targets);

    s_gfx.shader_backends = (uint32_t *)calloc(desc->max_shaders + 1, sizeof(uint32_t));
    s_gfx.program_backends = (uint32_t *)calloc(desc->max_programs + 1, sizeof(uint32_t));
    s_gfx.pipeline_backends = (uint32_t *)calloc(desc->max_pipelines + 1, sizeof(uint32_t));
    s_gfx.pipeline_programs = (uint32_t *)calloc(desc->max_pipelines + 1, sizeof(uint32_t));
    s_gfx.vertex_input_backends = (uint32_t *)calloc(desc->max_vertex_inputs + 1, sizeof(uint32_t));
    s_gfx.buffer_backends = (uint32_t *)calloc(desc->max_buffers + 1, sizeof(uint32_t));
    s_gfx.texture_backends = (uint32_t *)calloc(desc->max_textures + 1, sizeof(uint32_t));
    s_gfx.render_target_backends = (uint32_t *)calloc(max_render_targets + 1, sizeof(uint32_t));

    s_gfx.buffer_metas = (nt_gfx_buffer_meta_t *)calloc(desc->max_buffers + 1, sizeof(nt_gfx_buffer_meta_t));
    s_gfx.vertex_input_metas = (nt_gfx_vertex_input_meta_t *)calloc(desc->max_vertex_inputs + 1, sizeof(nt_gfx_vertex_input_meta_t));
    s_gfx.texture_metas = (nt_gfx_texture_meta_t *)calloc(desc->max_textures + 1, sizeof(nt_gfx_texture_meta_t));
    s_gfx.render_target_metas = (nt_gfx_render_target_meta_t *)calloc(max_render_targets + 1, sizeof(nt_gfx_render_target_meta_t));

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
    /* Delete GL objects before backend teardown, which destroys the WebGL context. */

    /* Render targets own attachment texture handles. */
    for (uint32_t i = 1; i <= s_gfx.render_target_pool.capacity; i++) {
        if (nt_pool_slot_alive(&s_gfx.render_target_pool, i)) {
            nt_gfx_backend_destroy_render_target(s_gfx.render_target_backends[i]);
            uint32_t color_slot = nt_pool_slot_index(s_gfx.render_target_metas[i].color.id);
            if (color_slot != 0) {
                nt_gfx_backend_destroy_texture(s_gfx.texture_backends[color_slot]);
            }
            uint32_t depth_slot = nt_pool_slot_index(s_gfx.render_target_metas[i].depth.id);
            if (depth_slot != 0) {
                nt_gfx_backend_destroy_texture(s_gfx.texture_backends[depth_slot]);
            }
        }
    }

    /* Destroy active mesh-table buffers */
    for (uint32_t i = 1; i <= s_gfx.mesh_pool.capacity; i++) {
        if (nt_pool_slot_alive(&s_gfx.mesh_pool, i) && s_gfx.mesh_table[i].vbo.id != 0) {
            nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[nt_pool_slot_index(s_gfx.mesh_table[i].vbo.id)]);
            if (s_gfx.mesh_table[i].ibo.id != 0) {
                nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[nt_pool_slot_index(s_gfx.mesh_table[i].ibo.id)]);
            }
        }
    }

    /* Release cached sampler backends */
    for (uint32_t i = 0; i < s_gfx.sampler_count; i++) {
        if (s_gfx.sampler_cache[i].backend != 0) {
            nt_gfx_backend_destroy_sampler(s_gfx.sampler_cache[i].backend);
        }
    }

    /* The native context survives backend teardown, so release remaining GL objects.
     * Backend destroys clear entries; owned attachments and mesh buffers are safe
     * to revisit. Pipelines
     * own their VAOs, not their programs. */
    for (uint32_t i = 1; i <= s_gfx.vertex_input_pool.capacity; i++) {
        nt_gfx_backend_destroy_vertex_input(s_gfx.vertex_input_backends[i]);
    }
    for (uint32_t i = 1; i <= s_gfx.pipeline_pool.capacity; i++) {
        nt_gfx_backend_destroy_pipeline(s_gfx.pipeline_backends[i]);
    }
    for (uint32_t i = 1; i <= s_gfx.program_pool.capacity; i++) {
        nt_gfx_backend_destroy_program(s_gfx.program_backends[i]);
    }
    for (uint32_t i = 1; i <= s_gfx.shader_pool.capacity; i++) {
        nt_gfx_backend_destroy_shader(s_gfx.shader_backends[i]);
    }
    for (uint32_t i = 1; i <= s_gfx.buffer_pool.capacity; i++) {
        nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[i]);
    }
    for (uint32_t i = 1; i <= s_gfx.texture_pool.capacity; i++) {
        nt_gfx_backend_destroy_texture(s_gfx.texture_backends[i]);
    }

    nt_gfx_backend_shutdown();

    nt_pool_shutdown(&s_gfx.shader_pool);
    nt_pool_shutdown(&s_gfx.program_pool);
    nt_pool_shutdown(&s_gfx.pipeline_pool);
    nt_pool_shutdown(&s_gfx.vertex_input_pool);
    nt_pool_shutdown(&s_gfx.buffer_pool);
    nt_pool_shutdown(&s_gfx.texture_pool);
    nt_pool_shutdown(&s_gfx.render_target_pool);
    nt_pool_shutdown(&s_gfx.mesh_pool);

    free(s_gfx.shader_backends);
    free(s_gfx.program_backends);
    free(s_gfx.pipeline_backends);
    free(s_gfx.pipeline_programs);
    free(s_gfx.vertex_input_backends);
    free(s_gfx.buffer_backends);
    free(s_gfx.texture_backends);
    free(s_gfx.render_target_backends);
    free(s_gfx.buffer_metas);
    free(s_gfx.vertex_input_metas);
    free(s_gfx.texture_metas);
    free(s_gfx.render_target_metas);
    free(s_gfx.mesh_table);
    free(s_mesh_stage_buf);
    s_mesh_stage_buf = NULL;
    s_mesh_stage_size = 0;

    memset(&s_gfx, 0, sizeof(s_gfx));
    memset(&g_nt_gfx, 0, sizeof(g_nt_gfx));

    /* Clear global block registry */
    memset(s_global_blocks, 0, sizeof(s_global_blocks));
    s_global_block_count = 0;
}

const nt_gfx_gpu_caps_t *nt_gfx_gpu_caps(void) { return &g_nt_gfx.gpu_caps; }

/* ---- Render target helpers ---- */

static nt_texture_desc_t render_target_color_texture_desc(const nt_render_target_desc_t *desc) {
    return (nt_texture_desc_t){
        .width = desc->width,
        .height = desc->height,
        .data = NULL,
        .format = desc->color_format,
        .min_filter = desc->color_min_filter,
        .mag_filter = desc->color_mag_filter,
        .wrap_u = desc->color_wrap_u,
        .wrap_v = desc->color_wrap_v,
        .gen_mipmaps = false,
        .label = desc->label,
    };
}

static nt_texture_desc_t render_target_depth_texture_desc(const nt_render_target_desc_t *desc) {
    return (nt_texture_desc_t){
        .width = desc->width,
        .height = desc->height,
        .data = NULL,
        .format = desc->depth_format,
        .min_filter = desc->depth_texture_min_filter,
        .mag_filter = desc->depth_texture_mag_filter,
        .wrap_u = desc->depth_texture_wrap_u,
        .wrap_v = desc->depth_texture_wrap_v,
        .gen_mipmaps = false,
        .label = desc->label,
    };
}

static nt_texture_t render_target_make_attachment(const nt_texture_desc_t *desc) {
    nt_texture_t tex = nt_gfx_make_texture(desc);
    if (tex.id != 0) {
        s_gfx.texture_metas[nt_pool_slot_index(tex.id)].render_target_owned = true;
    }
    return tex;
}

static bool render_target_color_sampler_valid(const nt_render_target_desc_t *desc) {
    return desc->color_min_filter >= NT_FILTER_NEAREST && desc->color_min_filter <= NT_FILTER_LINEAR && desc->color_mag_filter >= NT_FILTER_NEAREST && desc->color_mag_filter <= NT_FILTER_LINEAR &&
           desc->color_wrap_u >= NT_WRAP_CLAMP_TO_EDGE && desc->color_wrap_u <= NT_WRAP_MIRRORED_REPEAT && desc->color_wrap_v >= NT_WRAP_CLAMP_TO_EDGE && desc->color_wrap_v <= NT_WRAP_MIRRORED_REPEAT;
}

static bool render_target_depth_sampler_valid(const nt_render_target_desc_t *desc) {
    if (desc->depth_storage != NT_RT_DEPTH_TEXTURE) {
        return true;
    }
    return desc->depth_texture_min_filter == NT_FILTER_NEAREST && desc->depth_texture_mag_filter == NT_FILTER_NEAREST && desc->depth_texture_wrap_u >= NT_WRAP_CLAMP_TO_EDGE &&
           desc->depth_texture_wrap_u <= NT_WRAP_MIRRORED_REPEAT && desc->depth_texture_wrap_v >= NT_WRAP_CLAMP_TO_EDGE && desc->depth_texture_wrap_v <= NT_WRAP_MIRRORED_REPEAT;
}

static bool texture_format_is_depth(nt_texture_format_t format) { return format >= NT_TEXTURE_FORMAT_DEPTH16 && format <= NT_TEXTURE_FORMAT_DEPTH32F; }

static bool render_target_depth_format_valid(const nt_render_target_desc_t *desc) {
    if (desc->depth_storage == NT_RT_DEPTH_NONE) {
        return desc->depth_format == NT_TEXTURE_FORMAT_INVALID;
    }
    return texture_format_is_depth(desc->depth_format);
}

static void destroy_texture_slot(nt_texture_t tex, bool allow_render_target_owned) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        NT_LOG_ERROR("destroy_texture: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(tex.id);
    NT_ASSERT((!s_gfx.texture_metas[slot].render_target_owned || allow_render_target_owned) && "destroy_texture: texture is owned by a render target");
    if (s_gfx.texture_metas[slot].render_target_owned && !allow_render_target_owned) {
        NT_LOG_ERROR("destroy_texture: texture is owned by a render target");
        return;
    }
    nt_gfx_backend_destroy_texture(s_gfx.texture_backends[slot]);
    s_gfx.texture_backends[slot] = 0;
    memset(&s_gfx.texture_metas[slot], 0, sizeof(nt_gfx_texture_meta_t));
    nt_pool_free(&s_gfx.texture_pool, tex.id);
}

static void render_target_commit_attachment_backend(nt_texture_t tex, uint32_t backend, const nt_texture_desc_t *desc) {
    uint32_t slot = nt_pool_slot_index(tex.id);
    s_gfx.texture_backends[slot] = backend;
    s_gfx.texture_metas[slot].width = desc->width;
    s_gfx.texture_metas[slot].height = desc->height;
    s_gfx.texture_metas[slot].format = (uint8_t)desc->format;
    s_gfx.texture_metas[slot].mip_count = 1;
    s_gfx.texture_metas[slot].compressed = false;
    s_gfx.texture_metas[slot].render_target_owned = true;
    nt_sampler_desc_t sampler_desc = {
        .min_filter = desc->min_filter,
        .mag_filter = desc->mag_filter,
        .wrap_u = desc->wrap_u,
        .wrap_v = desc->wrap_v,
        .label = NULL,
    };
    s_gfx.texture_metas[slot].default_sampler = nt_gfx_make_sampler(&sampler_desc);
}

static uint32_t render_target_create_attachment_backend(nt_texture_t tex, const nt_texture_desc_t *desc) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return 0;
    }
    return nt_gfx_backend_create_texture(desc);
}

static bool render_target_recreate_attachment(nt_texture_t tex, const nt_texture_desc_t *desc) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return false;
    }
    uint32_t slot = nt_pool_slot_index(tex.id);
    uint32_t replacement = render_target_create_attachment_backend(tex, desc);
    if (replacement == 0) {
        return false;
    }
    uint32_t old_backend = s_gfx.texture_backends[slot];
    render_target_commit_attachment_backend(tex, replacement, desc);
    nt_gfx_backend_destroy_texture(old_backend);
    return true;
}

static bool render_target_recreate_backend(uint32_t slot) {
    nt_gfx_render_target_meta_t *meta = &s_gfx.render_target_metas[slot];
    nt_texture_desc_t color_desc = render_target_color_texture_desc(&meta->desc);
    if (!render_target_recreate_attachment(meta->color, &color_desc)) {
        meta->complete = false;
        return false;
    }
    uint32_t depth_backend = 0;
    if (meta->desc.depth_storage == NT_RT_DEPTH_TEXTURE) {
        nt_texture_desc_t depth_desc = render_target_depth_texture_desc(&meta->desc);
        if (!render_target_recreate_attachment(meta->depth, &depth_desc)) {
            meta->complete = false;
            return false;
        }
        depth_backend = s_gfx.texture_backends[nt_pool_slot_index(meta->depth.id)];
    }
    uint32_t color_backend = s_gfx.texture_backends[nt_pool_slot_index(meta->color.id)];
    s_gfx.render_target_backends[slot] = nt_gfx_backend_create_render_target(&meta->desc, color_backend, depth_backend);
    meta->complete = s_gfx.render_target_backends[slot] != 0;
    return meta->complete;
}

static bool render_target_resize_backend(uint32_t slot, uint16_t width, uint16_t height) {
    nt_gfx_render_target_meta_t *meta = &s_gfx.render_target_metas[slot];
    nt_render_target_desc_t next_desc = meta->desc;
    next_desc.width = width;
    next_desc.height = height;

    nt_texture_desc_t color_desc = render_target_color_texture_desc(&next_desc);
    uint32_t color_backend = s_gfx.texture_backends[nt_pool_slot_index(meta->color.id)];
    uint32_t depth_backend = 0;
    nt_texture_desc_t depth_desc = {0};
    if (next_desc.depth_storage == NT_RT_DEPTH_TEXTURE) {
        depth_desc = render_target_depth_texture_desc(&next_desc);
        depth_backend = s_gfx.texture_backends[nt_pool_slot_index(meta->depth.id)];
    }

    if (!nt_gfx_backend_resize_render_target(s_gfx.render_target_backends[slot], &next_desc, color_backend, depth_backend)) {
        return false;
    }

    render_target_commit_attachment_backend(meta->color, color_backend, &color_desc);

    if (next_desc.depth_storage == NT_RT_DEPTH_TEXTURE) {
        render_target_commit_attachment_backend(meta->depth, depth_backend, &depth_desc);
    }

    meta->desc = next_desc;
    meta->complete = true;
    return true;
}

/* ---- Frame / Pass ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — context-loss recovery branches push it just over 25
void nt_gfx_begin_frame(void) {
    bool backend_context_lost = nt_gfx_backend_is_context_lost();
    if (backend_context_lost && !g_nt_gfx.context_lost) {
        /* First detection: wipe all backend handles */
        for (uint32_t i = 1; i <= s_gfx.shader_pool.capacity; i++) {
            s_gfx.shader_backends[i] = 0;
        }
        for (uint32_t i = 1; i <= s_gfx.program_pool.capacity; i++) {
            s_gfx.program_backends[i] = 0;
        }
        for (uint32_t i = 1; i <= s_gfx.pipeline_pool.capacity; i++) {
            s_gfx.pipeline_backends[i] = 0;
        }
        /* Vertex inputs die with the context and are not auto-restored --
         * the zeroed backend makes a stale bind after recovery trap. */
        for (uint32_t i = 1; i <= s_gfx.vertex_input_pool.capacity; i++) {
            s_gfx.vertex_input_backends[i] = 0;
        }
        for (uint32_t i = 1; i <= s_gfx.buffer_pool.capacity; i++) {
            s_gfx.buffer_backends[i] = 0;
        }
        for (uint32_t i = 1; i <= s_gfx.texture_pool.capacity; i++) {
            s_gfx.texture_backends[i] = 0;
        }
        for (uint32_t i = 1; i <= s_gfx.render_target_pool.capacity; i++) {
            s_gfx.render_target_backends[i] = 0;
            s_gfx.render_target_metas[i].complete = false;
        }
        /* Mesh table: keep entries active. nt_resource_invalidate() will
         * call deactivate_mesh() which returns slots to mesh pool.
         * destroy_buffer on zeroed backend handles is safe (glDeleteBuffers(0) = no-op). */
        s_gfx.bound_pipeline = 0;
        s_gfx.bound_vertex_input = 0;
        s_gfx.bound_index_type = NT_INDEX_NONE;
        memset(s_gfx.bound_texture_ids, 0, sizeof(s_gfx.bound_texture_ids));
        /* Sampler cache: zero only the GL backend ids — keys, descs
         * and sampler_count stay so material-stored sampler.id values
         * remain valid slot references. Backend is lazy-recreated on
         * the next nt_gfx_make_sampler hit or on bind_sampler. */
        for (uint32_t i = 0; i < s_gfx.sampler_count; i++) {
            s_gfx.sampler_cache[i].backend = 0;
        }
        for (uint32_t i = 1; i <= s_gfx.texture_pool.capacity; i++) {
            s_gfx.texture_metas[i].default_sampler = NT_SAMPLER_INVALID;
        }
        /* Timer-segment GL queries are dead too — let backend re-allocate
         * on next begin_segment via the lazy-find-or-alloc path. */
        nt_gfx_backend_drop_timer_segments();
        g_nt_gfx.context_lost = true;
        s_gfx.context_restore_retry = false;
        NT_LOG_ERROR("WebGL context lost");
        return; /* Skip frame */
    }

    if (backend_context_lost && !s_gfx.context_restore_retry) {
        return;
    }

    if (g_nt_gfx.context_lost) {
        if (!nt_gfx_backend_recreate_all_resources()) {
            s_gfx.context_restore_retry = true;
            NT_LOG_ERROR("WebGL context restore failed");
            return;
        }
        g_nt_gfx.gpu_caps = nt_gfx_gl_ctx_detect_gpu_caps();
        s_gfx.context_restore_retry = false;
        g_nt_gfx.context_lost = false;
        s_gfx.scissor_enabled = false;
        g_nt_gfx.context_restored = true;
        bool render_targets_restored = true;
        for (uint32_t i = 1; i <= s_gfx.render_target_pool.capacity; i++) {
            if (nt_pool_slot_alive(&s_gfx.render_target_pool, i)) {
                if (!render_target_recreate_backend(i)) {
                    render_targets_restored = false;
                }
            }
        }
        if (render_targets_restored) {
            NT_LOG_INFO("WebGL context restored -- render targets restored, game must re-create other resources");
        } else {
            NT_LOG_ERROR("WebGL context restored -- one or more render targets failed to restore");
        }
    }

    /* Normal frame begin */
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_IDLE);
    if (s_gfx.render_state != NT_GFX_STATE_IDLE) {
        NT_LOG_ERROR("begin_frame called outside IDLE state");
        return;
    }
    s_gfx.render_state = NT_GFX_STATE_FRAME;
    memset(&g_nt_gfx.frame_stats, 0, sizeof(g_nt_gfx.frame_stats));
    /* Backend resets its pipeline cache per frame; mirror it so the
     * bound-pipeline asserts stay truthful across frame boundaries. */
    s_gfx.bound_pipeline = 0;
    s_gfx.bound_vertex_input = 0;
    s_gfx.bound_index_type = NT_INDEX_NONE;
    nt_gfx_backend_begin_frame();
}

void nt_gfx_end_frame(void) {
    mesh_stage_frame_tick();
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

uint32_t nt_gfx_get_frame_draw_calls(void) { return g_nt_gfx.frame_stats.draw_calls; }

/* Cap-checked rgba8 readback + single Y-flip to top-left. L1 contract,
 * so bad size returns false (bot-param validation is the L2 concern). */
bool nt_gfx_read_pixels(int x, int y, int w, int h, uint8_t *out, uint32_t out_cap) {
    if (w <= 0 || h <= 0) {
        return false;
    }
    NT_ASSERT(out != NULL); /* L1 writes the readback (and row-swaps) through out — NULL is a caller bug. */
    if (out == NULL) {
        return false;
    }
    /* A lost context returns uninitialized garbage as a "successful" read — every other GL wrapper
       early-returns on this. The capture producer treats false as failure -> NULL -> capture_failed. */
    if (g_nt_gfx.context_lost) {
        return false;
    }
    /* Compute in uint64_t so w*h*4 cannot overflow before the cap check. */
    uint64_t need = (uint64_t)(uint32_t)w * (uint64_t)(uint32_t)h * 4U;
    if (need > (uint64_t)out_cap) {
        return false;
    }
    if (!nt_gfx_backend_read_pixels(x, y, w, h, out)) {
        return false; /* GL read error -> capture_failed, not an encode of uninitialized memory. */
    }

    /* Single in-place row swap: GL bottom-left -> top-left. Row stride = w*4. */
    size_t stride = (size_t)(uint32_t)w * 4U;
    uint8_t *top = out;
    uint8_t *bot = out + (stride * (size_t)((uint32_t)h - 1U));
    while (top < bot) {
        for (size_t i = 0; i < stride; i++) {
            uint8_t tmp = top[i];
            top[i] = bot[i];
            bot[i] = tmp;
        }
        top += stride;
        bot -= stride;
    }
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool render_target_backend_for_pass(nt_render_target_t target, uint32_t *out_backend) {
    NT_ASSERT(out_backend != NULL);
    if (out_backend == NULL) {
        return false;
    }
    *out_backend = 0;
    if (target.id == 0) {
        return true;
    }
    bool valid = nt_pool_valid(&s_gfx.render_target_pool, target.id);
    NT_ASSERT(valid && "begin_pass: invalid render target");
    if (!valid) {
        NT_LOG_ERROR("begin_pass: invalid render target");
        return false;
    }
    uint32_t slot = nt_pool_slot_index(target.id);
    NT_ASSERT(s_gfx.render_target_metas[slot].complete && "begin_pass: render target is not ready");
    if (!s_gfx.render_target_metas[slot].complete) {
        NT_LOG_ERROR("begin_pass: incomplete render target");
        return false;
    }
    *out_backend = s_gfx.render_target_backends[slot];
    NT_ASSERT(*out_backend != 0);
    return *out_backend != 0;
}

void nt_gfx_begin_pass(const nt_pass_desc_t *desc) {
    if (g_nt_gfx.context_lost) {
        return;
    }

    NT_ASSERT(desc != NULL);
    if (desc == NULL) {
        NT_LOG_ERROR("begin_pass: NULL desc");
        return;
    }
    NT_ASSERT(s_gfx.render_state == NT_GFX_STATE_FRAME);
    if (s_gfx.render_state != NT_GFX_STATE_FRAME) {
        NT_LOG_ERROR("begin_pass called outside FRAME state");
        return;
    }

    uint32_t render_target_backend;
    if (!render_target_backend_for_pass(desc->target, &render_target_backend)) {
        return;
    }

    s_gfx.render_state = NT_GFX_STATE_PASS;
    nt_gfx_backend_begin_pass(desc, render_target_backend);
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
nt_program_t nt_gfx_make_program(nt_shader_t vs, nt_shader_t fs) {
    NT_ASSERT(nt_pool_valid(&s_gfx.shader_pool, vs.id) && "make_program: invalid vertex shader handle");
    NT_ASSERT(nt_pool_valid(&s_gfx.shader_pool, fs.id) && "make_program: invalid fragment shader handle");

    /* The browser can recover before begin_frame resets the backend tables. */
    if (g_nt_gfx.context_lost || nt_gfx_backend_is_context_lost()) {
        return NT_PROGRAM_INVALID;
    }

    uint32_t vs_backend = s_gfx.shader_backends[nt_pool_slot_index(vs.id)];
    uint32_t fs_backend = s_gfx.shader_backends[nt_pool_slot_index(fs.id)];
    /* Rejected, not trapped: a context loss leaves stage handles live but
     * permanently unready, so this is recoverable state and not a caller error.
     * The owner recreates the stages and links again. */
    if (vs_backend == 0 || fs_backend == 0) {
        return NT_PROGRAM_INVALID;
    }

    /* Before the link, not after: the GL backend's program table has the same
     * capacity, so linking first makes exhaustion surface as a link failure. */
    uint32_t id = nt_pool_alloc(&s_gfx.program_pool);
    NT_ASSERT(id != 0 && "program pool full -- raise nt_gfx_desc_t.max_programs");

    uint32_t backend = nt_gfx_backend_create_program(vs_backend, fs_backend);
    if (backend == 0 && nt_gfx_backend_is_context_lost()) {
        nt_pool_free(&s_gfx.program_pool, id);
        return NT_PROGRAM_INVALID;
    }
    NT_ASSERT(backend != 0 && "program link failed");

    s_gfx.program_backends[nt_pool_slot_index(id)] = backend;

    nt_program_t result = {id};
    return result;
}

static bool blend_factor_valid(nt_blend_factor_t factor) { return factor <= NT_BLEND_SRC_ALPHA_SATURATE; }

static bool blend_factor_uses_constant_color(nt_blend_factor_t factor) { return factor == NT_BLEND_CONSTANT_COLOR || factor == NT_BLEND_ONE_MINUS_CONSTANT_COLOR; }

static bool blend_factor_uses_constant_alpha(nt_blend_factor_t factor) { return factor == NT_BLEND_CONSTANT_ALPHA || factor == NT_BLEND_ONE_MINUS_CONSTANT_ALPHA; }

static bool blend_constant_color_valid(const float color[4]) {
    for (uint8_t i = 0; i < 4; i++) {
        if (!(color[i] >= 0.0F && color[i] <= 1.0F)) {
            return false;
        }
    }
    return true;
}

static bool blend_state_valid(const nt_blend_state_t *blend) {
    if (!blend->enabled) {
        return true;
    }

    bool uses_constant_color = blend_factor_uses_constant_color(blend->src_rgb) || blend_factor_uses_constant_color(blend->dst_rgb);
    bool uses_constant_alpha = blend_factor_uses_constant_alpha(blend->src_rgb) || blend_factor_uses_constant_alpha(blend->dst_rgb);
    return blend_constant_color_valid(blend->constant_color) && blend_factor_valid(blend->src_rgb) && blend_factor_valid(blend->dst_rgb) && blend_factor_valid(blend->src_alpha) &&
           blend_factor_valid(blend->dst_alpha) && blend->op_rgb <= NT_BLEND_OP_MAX && blend->op_alpha <= NT_BLEND_OP_MAX && blend->dst_rgb != NT_BLEND_SRC_ALPHA_SATURATE &&
           blend->dst_alpha != NT_BLEND_SRC_ALPHA_SATURATE && !(uses_constant_color && uses_constant_alpha);
}

/* A location used twice (within a layout or across vertex/instance layouts) means
 * glVertexAttribPointer runs twice on one slot -- last bind wins, silently wrong data. */
static bool layout_locations_unique(const nt_vertex_layout_t *a, const nt_vertex_layout_t *b) {
    uint32_t seen[8] = {0}; /* 256 bits, one per possible location */
    const nt_vertex_layout_t *layouts[2] = {a, b};
    for (int l = 0; l < 2; l++) {
        for (uint8_t i = 0; i < layouts[l]->attr_count; i++) {
            uint8_t loc = layouts[l]->attrs[i].location;
            if (seen[loc >> 5U] & (1U << (loc & 31U))) {
                return false;
            }
            seen[loc >> 5U] |= 1U << (loc & 31U);
        }
    }
    return true;
}

/* WebGL2 raises INVALID_OPERATION when an attribute offset or the stride is not a
 * multiple of the attribute's type size; desktop GL tolerates it, so without this
 * a bad layout only fails in the browser. Off hot path (pipelines are cached). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static void assert_layout_webgl2_rules(const nt_vertex_layout_t *layout) {
    for (uint8_t i = 0; i < layout->attr_count; i++) {
        const nt_vertex_attr_t *attr = &layout->attrs[i];
        NT_ASSERT(attr->location < NT_GFX_MAX_VERTEX_ATTRS && "WebGL2 guarantees only 16 vertex attribute locations");
        NT_ASSERT(attr->count >= 1 && attr->count <= 4);
        NT_ASSERT(nt_vertex_type_size(attr->type) != 0);
        NT_ASSERT(!(attr->normalized && (attr->type == NT_VERTEX_FLOAT || attr->type == NT_VERTEX_HALF)) && "normalized is for integer types only");
        NT_ASSERT((attr->offset % nt_vertex_type_size(attr->type)) == 0 && "WebGL2: attribute offset must be a multiple of its type size");
        NT_ASSERT((layout->stride % nt_vertex_type_size(attr->type)) == 0 && "WebGL2: stride must be a multiple of each attribute's type size");
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
nt_pipeline_t nt_gfx_make_pipeline(const nt_pipeline_desc_t *desc) {
    /* Everything a caller controls is a developer error and traps. What is left
     * -- a lost context, a failed backend allocation -- returns an invalid handle
     * the caller retries on a later frame. */
    nt_pipeline_t result = {0};
    NT_ASSERT(desc != NULL);
    /* Same predicate as make_program: context loss is what zeroes the program
     * backend, so without this every renderer would trap on the readiness assert
     * below -- and the browser can recover before begin_frame resets the tables. */
    if (g_nt_gfx.context_lost || nt_gfx_backend_is_context_lost()) {
        return result;
    }
    NT_ASSERT(nt_gfx_program_ready(desc->program) && "make_pipeline: program is not linked");
    NT_ASSERT(desc->layout.attr_count <= NT_GFX_MAX_VERTEX_ATTRS && "too many vertex attrs");
    NT_ASSERT(desc->instance_layout.attr_count <= NT_GFX_MAX_VERTEX_ATTRS && "too many instance attrs");
    /* WebGL2 caps vertexAttribPointer stride at 255 bytes (INVALID_VALUE beyond).
     * Reachable only from game-declared layouts -- mesh-pack strides max out at 128. */
    NT_ASSERT(desc->layout.stride <= 255 && desc->instance_layout.stride <= 255 && "WebGL2 caps vertex stride at 255 bytes");
    /* After the attr_count asserts -- the loops read attr_count entries. */
    assert_layout_webgl2_rules(&desc->layout);
    assert_layout_webgl2_rules(&desc->instance_layout);
    NT_ASSERT(layout_locations_unique(&desc->layout, &desc->instance_layout) && "attribute location used twice across vertex/instance layouts");
    NT_ASSERT(blend_state_valid(&desc->blend));

    uint32_t id = nt_pool_alloc(&s_gfx.pipeline_pool);
    NT_ASSERT(id != 0 && "pipeline pool full -- raise nt_gfx_desc_t.max_pipelines");

    uint32_t program_backend = s_gfx.program_backends[nt_pool_slot_index(desc->program.id)];
    uint32_t backend = nt_gfx_backend_create_pipeline(desc, program_backend);
    if (backend == 0) {
        NT_LOG_ERROR("backend pipeline creation failed");
        nt_pool_free(&s_gfx.pipeline_pool, id);
        return result;
    }

    uint32_t slot = nt_pool_slot_index(id);
    s_gfx.pipeline_backends[slot] = backend;
    s_gfx.pipeline_programs[slot] = desc->program.id;

    result.id = id;
    return result;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
nt_vertex_input_t nt_gfx_make_vertex_input(const nt_vertex_input_desc_t *desc) {
    /* Same contract as make_pipeline: caller errors trap, only a lost context
     * or a failed backend allocation returns an invalid handle. */
    nt_vertex_input_t result = {0};
    NT_ASSERT(desc != NULL);
    if (g_nt_gfx.context_lost || nt_gfx_backend_is_context_lost()) {
        return result;
    }
    NT_ASSERT(desc->layout.attr_count <= NT_GFX_MAX_VERTEX_ATTRS && "too many vertex attrs");
    NT_ASSERT(desc->instance_layout.attr_count <= NT_GFX_MAX_VERTEX_ATTRS && "too many instance attrs");
    NT_ASSERT(desc->layout.stride <= 255 && desc->instance_layout.stride <= 255 && "WebGL2 caps vertex stride at 255 bytes");
    /* After the attr_count asserts -- the loops read attr_count entries. */
    assert_layout_webgl2_rules(&desc->layout);
    assert_layout_webgl2_rules(&desc->instance_layout);
    NT_ASSERT(layout_locations_unique(&desc->layout, &desc->instance_layout) && "attribute location used twice across vertex/instance layouts");
    /* attr_count 0 with no buffers is the attribute-less gl_VertexID path. */
    NT_ASSERT((desc->layout.attr_count > 0) == (desc->vertex_buffer.id != 0) && "make_vertex_input: vertex_buffer is required iff layout has attrs");

    uint32_t vbo_backend = 0;
    if (desc->vertex_buffer.id != 0) {
        NT_ASSERT(nt_pool_valid(&s_gfx.buffer_pool, desc->vertex_buffer.id) && "make_vertex_input: invalid vertex_buffer handle");
        uint32_t vbo_slot = nt_pool_slot_index(desc->vertex_buffer.id);
        NT_ASSERT(s_gfx.buffer_metas[vbo_slot].type == NT_BUFFER_VERTEX && "make_vertex_input: vertex_buffer is not vertex type");
        vbo_backend = s_gfx.buffer_backends[vbo_slot];
    }
    uint32_t ibo_backend = 0;
    uint8_t index_type = NT_INDEX_NONE;
    if (desc->index_buffer.id != 0) {
        NT_ASSERT(nt_pool_valid(&s_gfx.buffer_pool, desc->index_buffer.id) && "make_vertex_input: invalid index_buffer handle");
        uint32_t ibo_slot = nt_pool_slot_index(desc->index_buffer.id);
        NT_ASSERT(s_gfx.buffer_metas[ibo_slot].type == NT_BUFFER_INDEX && "make_vertex_input: index_buffer is not index type");
        index_type = s_gfx.buffer_metas[ibo_slot].index_type;
        /* Without the declared type the backend would silently draw UINT16. */
        NT_ASSERT(index_type != NT_INDEX_NONE && "make_vertex_input: index_buffer was created without an index_type");
        ibo_backend = s_gfx.buffer_backends[ibo_slot];
    }

    uint32_t id = nt_pool_alloc(&s_gfx.vertex_input_pool);
    NT_ASSERT(id != 0 && "vertex input pool full -- raise nt_gfx_desc_t.max_vertex_inputs");

    uint32_t backend = nt_gfx_backend_create_vertex_input(desc, vbo_backend, ibo_backend);
    if (backend == 0) {
        NT_LOG_ERROR("backend vertex input creation failed");
        nt_pool_free(&s_gfx.vertex_input_pool, id);
        return result;
    }

    uint32_t slot = nt_pool_slot_index(id);
    s_gfx.vertex_input_backends[slot] = backend;
    s_gfx.vertex_input_metas[slot] = (nt_gfx_vertex_input_meta_t){
        .vbo_id = desc->vertex_buffer.id,
        .ibo_id = desc->index_buffer.id,
        .index_type = index_type,
        .instance_attr_count = desc->instance_layout.attr_count,
        .instance_pointed = false,
    };

    result.id = id;
    return result;
}

nt_buffer_t nt_gfx_make_buffer(const nt_buffer_desc_t *desc) {
    nt_buffer_t result = {0};
    if (!desc) {
        return result;
    }

    /* Pool exhaustion is a configuration error, not a backend allocation failure. */
    uint32_t id = nt_pool_alloc(&s_gfx.buffer_pool);
    NT_ASSERT(id != 0 && "buffer pool full -- raise nt_gfx_desc_t.max_buffers");

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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_texture_t nt_gfx_make_texture(const nt_texture_desc_t *desc) {
    nt_texture_t result = {0};

    // #region validate
    if (!desc) {
        return result;
    }
    if (desc->width == 0 || desc->height == 0) {
        NT_LOG_ERROR("make_texture: zero dimension");
        return result;
    }
    if (desc->width > g_nt_gfx.gpu_caps.max_texture_size || desc->height > g_nt_gfx.gpu_caps.max_texture_size) {
#ifdef NT_DEBUG
        NT_ASSERT(0 && "make_texture: dimensions exceed GPU max_texture_size");
#endif
        NT_LOG_ERROR("make_texture: %ux%u exceeds GPU max_texture_size %u", desc->width, desc->height, g_nt_gfx.gpu_caps.max_texture_size);
        return result;
    }
    nt_texture_desc_t local_desc = *desc;

    bool format_valid = local_desc.format > NT_TEXTURE_FORMAT_INVALID && local_desc.format <= NT_TEXTURE_FORMAT_DEPTH32F;
    NT_ASSERT(format_valid && "make_texture: format is required");
    if (!format_valid) {
        return result;
    }

    /* Mipmaps require initial data — GL cannot generate from empty storage */
    NT_ASSERT((!local_desc.gen_mipmaps || local_desc.data) && "make_texture: gen_mipmaps requires data");

    /* Integer textures: NEAREST only, no mipmaps */
    if (local_desc.format == NT_TEXTURE_FORMAT_RG16UI) {
        NT_ASSERT(local_desc.min_filter == NT_FILTER_NEAREST && "integer texture requires NEAREST min_filter");
        NT_ASSERT(local_desc.mag_filter == NT_FILTER_NEAREST && "integer texture requires NEAREST mag_filter");
        NT_ASSERT(!local_desc.gen_mipmaps && "integer texture does not support mipmaps");
    }
    if (texture_format_is_depth(local_desc.format)) {
        NT_ASSERT(local_desc.data == NULL && "depth texture upload is not supported");
        NT_ASSERT(local_desc.min_filter == NT_FILTER_NEAREST && "depth texture requires NEAREST min_filter without compare mode");
        NT_ASSERT(local_desc.mag_filter == NT_FILTER_NEAREST && "depth texture requires NEAREST mag_filter without compare mode");
        NT_ASSERT(!local_desc.gen_mipmaps && "depth texture mipmaps are not supported");
    }

    /* Mipmap min_filter requires gen_mipmaps */
    NT_ASSERT((local_desc.gen_mipmaps || local_desc.min_filter <= NT_FILTER_LINEAR) && "make_texture: mipmap filter requires gen_mipmaps");
    /* mag_filter: only NEAREST or LINEAR allowed */
    NT_ASSERT(local_desc.mag_filter <= NT_FILTER_LINEAR && "make_texture: mag_filter must be NEAREST or LINEAR");
    // #endregion

    // #region allocate
    /* Pool exhaustion is a configuration error, not a backend allocation failure. */
    uint32_t id = nt_pool_alloc(&s_gfx.texture_pool);
    NT_ASSERT(id != 0 && "texture pool full -- raise nt_gfx_desc_t.max_textures");

    uint32_t backend = nt_gfx_backend_create_texture(&local_desc);
    if (backend == 0) {
        NT_LOG_ERROR("backend texture creation failed");
        nt_pool_free(&s_gfx.texture_pool, id);
        return result;
    }
    // #endregion

    // #region store meta
    uint32_t slot = nt_pool_slot_index(id);
    s_gfx.texture_backends[slot] = backend;
    s_gfx.texture_metas[slot].width = local_desc.width;
    s_gfx.texture_metas[slot].height = local_desc.height;
    s_gfx.texture_metas[slot].format = (uint8_t)local_desc.format;
    s_gfx.texture_metas[slot].mip_count = 1;
    if (local_desc.gen_mipmaps && local_desc.data) {
        /* floor(log2(max(w,h))) + 1 */
        uint16_t max_dim = local_desc.width > local_desc.height ? local_desc.width : local_desc.height;
        uint8_t levels = 1;
        while (max_dim > 1) {
            max_dim >>= 1;
            levels++;
        }
        s_gfx.texture_metas[slot].mip_count = levels;
    }

    nt_sampler_desc_t sd = {
        .min_filter = local_desc.min_filter,
        .mag_filter = local_desc.mag_filter,
        .wrap_u = local_desc.wrap_u,
        .wrap_v = local_desc.wrap_v,
        .label = NULL,
    };
    nt_sampler_t default_sampler = nt_gfx_make_sampler(&sd);
    NT_ASSERT(default_sampler.id != 0 && "nt_gfx_make_texture: default sampler creation failed");
    s_gfx.texture_metas[slot].default_sampler = default_sampler;
    // #endregion

    result.id = id;
    return result;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_render_target_t nt_gfx_make_render_target(const nt_render_target_desc_t *desc) {
    nt_render_target_t result = NT_RENDER_TARGET_INVALID;
    NT_ASSERT(desc != NULL);
    if (!desc) {
        return result;
    }
    NT_ASSERT(s_gfx.render_state != NT_GFX_STATE_PASS);
    if (s_gfx.render_state == NT_GFX_STATE_PASS) {
        NT_LOG_ERROR("make_render_target called inside a pass");
        return result;
    }
    NT_ASSERT(desc->width > 0 && desc->height > 0 && "make_render_target: zero dimension");
    if (desc->width == 0 || desc->height == 0) {
        NT_LOG_ERROR("make_render_target: zero dimension");
        return result;
    }
    /* Deliberately not gated on gpu_caps.has_float_render_target: the backend
       completeness check is the real gate, and it already returns invalid. */
    bool color_format_valid = desc->color_format == NT_TEXTURE_FORMAT_RGBA8 || desc->color_format == NT_TEXTURE_FORMAT_RGBA16F;
    NT_ASSERT(color_format_valid && "make_render_target: unsupported color format");
    if (!color_format_valid) {
        NT_LOG_ERROR("make_render_target: unsupported color format");
        return result;
    }
    bool depth_valid = desc->depth_storage >= NT_RT_DEPTH_NONE && desc->depth_storage <= NT_RT_DEPTH_TEXTURE;
    NT_ASSERT(depth_valid && "make_render_target: invalid depth mode");
    if (!depth_valid) {
        NT_LOG_ERROR("make_render_target: invalid depth mode");
        return result;
    }
    NT_ASSERT(render_target_depth_format_valid(desc) && "make_render_target: depth format does not match depth storage");
    if (!render_target_depth_format_valid(desc)) {
        NT_LOG_ERROR("make_render_target: depth format does not match depth storage");
        return result;
    }
    NT_ASSERT(render_target_color_sampler_valid(desc) && "make_render_target: invalid color sampler");
    if (!render_target_color_sampler_valid(desc)) {
        NT_LOG_ERROR("make_render_target: invalid color sampler");
        return result;
    }
    NT_ASSERT(render_target_depth_sampler_valid(desc) && "make_render_target: invalid depth texture sampler");
    if (!render_target_depth_sampler_valid(desc)) {
        NT_LOG_ERROR("make_render_target: invalid depth texture sampler");
        return result;
    }

    uint32_t id = nt_pool_alloc(&s_gfx.render_target_pool);
    NT_ASSERT(id != 0 && "render target pool full; raise nt_gfx_desc_t.max_render_targets");
    if (id == 0) {
        NT_LOG_ERROR("render target pool full");
        return result;
    }
    uint32_t slot = nt_pool_slot_index(id);

    nt_texture_desc_t color_desc = render_target_color_texture_desc(desc);
    nt_texture_t color = render_target_make_attachment(&color_desc);
    if (color.id == 0) {
        nt_pool_free(&s_gfx.render_target_pool, id);
        return result;
    }

    nt_texture_t depth = {0};
    if (desc->depth_storage == NT_RT_DEPTH_TEXTURE) {
        nt_texture_desc_t depth_desc = render_target_depth_texture_desc(desc);
        depth = render_target_make_attachment(&depth_desc);
        if (depth.id == 0) {
            destroy_texture_slot(color, true);
            nt_pool_free(&s_gfx.render_target_pool, id);
            return result;
        }
    }

    s_gfx.render_target_metas[slot] = (nt_gfx_render_target_meta_t){
        .desc = *desc,
        .color = color,
        .depth = depth,
        .complete = false,
    };

    uint32_t color_backend = s_gfx.texture_backends[nt_pool_slot_index(color.id)];
    uint32_t depth_backend = depth.id != 0 ? s_gfx.texture_backends[nt_pool_slot_index(depth.id)] : 0;
    s_gfx.render_target_backends[slot] = nt_gfx_backend_create_render_target(desc, color_backend, depth_backend);
    if (s_gfx.render_target_backends[slot] == 0) {
        if (depth.id != 0) {
            destroy_texture_slot(depth, true);
        }
        destroy_texture_slot(color, true);
        memset(&s_gfx.render_target_metas[slot], 0, sizeof(nt_gfx_render_target_meta_t));
        nt_pool_free(&s_gfx.render_target_pool, id);
        return result;
    }

    s_gfx.render_target_metas[slot].complete = true;
    result.id = id;
    return result;
}

/* ---- Resource destruction ---- */

void nt_gfx_destroy_shader(nt_shader_t shd) {
    if (shd.id == 0) {
        return; /* invalid-zero is a first-class value, as for programs */
    }
    if (!nt_pool_valid(&s_gfx.shader_pool, shd.id)) {
        NT_LOG_ERROR("destroy_shader: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(shd.id);
    nt_gfx_backend_destroy_shader(s_gfx.shader_backends[slot]);
    s_gfx.shader_backends[slot] = 0;
    nt_pool_free(&s_gfx.shader_pool, shd.id);
}

void nt_gfx_destroy_program(nt_program_t prog) {
    /* NT_PROGRAM_INVALID is a first-class value -- games clear their handles on
     * context loss and destroy them again at shutdown. Not an error. */
    if (prog.id == 0) {
        return;
    }
    /* A stale non-zero handle means the owner lost track of which programs it
     * still holds -- the one mistake this ownership model cannot absorb. */
    NT_ASSERT(nt_pool_valid(&s_gfx.program_pool, prog.id) && "destroy_program: stale handle -- clear the handle to NT_PROGRAM_INVALID when you destroy it");
    /* Dependent pipelines cannot draw again; reclaim their slots and VAOs now. */
    for (uint32_t i = 1; i <= s_gfx.pipeline_pool.capacity; i++) {
        if (s_gfx.pipeline_programs[i] != prog.id) {
            continue;
        }
        nt_gfx_destroy_pipeline((nt_pipeline_t){s_gfx.pipeline_pool.slots[i].id});
    }
    uint32_t slot = nt_pool_slot_index(prog.id);
    nt_gfx_backend_destroy_program(s_gfx.program_backends[slot]);
    s_gfx.program_backends[slot] = 0;
    nt_pool_free(&s_gfx.program_pool, prog.id);
}

void nt_gfx_destroy_pipeline(nt_pipeline_t pip) {
    /* Program destruction may already have reclaimed this cached pipeline. */
    if (!nt_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        return;
    }
    uint32_t slot = nt_pool_slot_index(pip.id);
    if (s_gfx.bound_pipeline == s_gfx.pipeline_backends[slot]) {
        s_gfx.bound_pipeline = 0;
    }
    nt_gfx_backend_destroy_pipeline(s_gfx.pipeline_backends[slot]);
    s_gfx.pipeline_backends[slot] = 0;
    s_gfx.pipeline_programs[slot] = 0;
    nt_pool_free(&s_gfx.pipeline_pool, pip.id);
}

void nt_gfx_destroy_vertex_input(nt_vertex_input_t vi) {
    /* The destroy_buffer cascade makes stale handles routine here, so both
     * INVALID and stale are tolerated no-ops (same contract as pipelines). */
    if (!nt_pool_valid(&s_gfx.vertex_input_pool, vi.id)) {
        return;
    }
    uint32_t slot = nt_pool_slot_index(vi.id);
    if (s_gfx.bound_vertex_input == vi.id) {
        s_gfx.bound_vertex_input = 0;
        s_gfx.bound_index_type = NT_INDEX_NONE;
    }
    nt_gfx_backend_destroy_vertex_input(s_gfx.vertex_input_backends[slot]);
    s_gfx.vertex_input_backends[slot] = 0;
    memset(&s_gfx.vertex_input_metas[slot], 0, sizeof(nt_gfx_vertex_input_meta_t));
    nt_pool_free(&s_gfx.vertex_input_pool, vi.id);
}

void nt_gfx_destroy_buffer(nt_buffer_t buf) {
    if (buf.id == 0) {
        return; /* invalid-zero is a first-class value, as for programs */
    }
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        NT_LOG_ERROR("destroy_buffer: invalid handle");
        return;
    }
    /* Dependent vertex inputs baked this buffer into their VAO state and can
     * never draw correctly again -- reclaim them now. Mesh deactivation
     * reaches no renderer, so renderer caches rely on this cascade. */
    for (uint32_t i = 1; i <= s_gfx.vertex_input_pool.capacity; i++) {
        if (s_gfx.vertex_input_metas[i].vbo_id == buf.id || s_gfx.vertex_input_metas[i].ibo_id == buf.id) {
            nt_gfx_destroy_vertex_input((nt_vertex_input_t){s_gfx.vertex_input_pool.slots[i].id});
        }
    }
    uint32_t slot = nt_pool_slot_index(buf.id);
    nt_gfx_backend_destroy_buffer(s_gfx.buffer_backends[slot]);
    s_gfx.buffer_backends[slot] = 0;
    memset(&s_gfx.buffer_metas[slot], 0, sizeof(nt_gfx_buffer_meta_t));
    nt_pool_free(&s_gfx.buffer_pool, buf.id);
}

void nt_gfx_destroy_texture(nt_texture_t tex) { destroy_texture_slot(tex, false); }

void nt_gfx_destroy_render_target(nt_render_target_t rt) {
    bool valid = nt_pool_valid(&s_gfx.render_target_pool, rt.id);
    NT_ASSERT(valid && "destroy_render_target: invalid handle");
    if (!valid) {
        NT_LOG_ERROR("destroy_render_target: invalid handle");
        return;
    }
    NT_ASSERT(s_gfx.render_state != NT_GFX_STATE_PASS);
    if (s_gfx.render_state == NT_GFX_STATE_PASS) {
        NT_LOG_ERROR("destroy_render_target called inside a pass");
        return;
    }
    uint32_t slot = nt_pool_slot_index(rt.id);
    nt_gfx_backend_destroy_render_target(s_gfx.render_target_backends[slot]);
    s_gfx.render_target_backends[slot] = 0;
    if (s_gfx.render_target_metas[slot].depth.id != 0) {
        destroy_texture_slot(s_gfx.render_target_metas[slot].depth, true);
    }
    destroy_texture_slot(s_gfx.render_target_metas[slot].color, true);
    memset(&s_gfx.render_target_metas[slot], 0, sizeof(nt_gfx_render_target_meta_t));
    nt_pool_free(&s_gfx.render_target_pool, rt.id);
}

bool nt_gfx_resize_render_target(nt_render_target_t rt, uint16_t width, uint16_t height) {
    bool valid = nt_pool_valid(&s_gfx.render_target_pool, rt.id);
    NT_ASSERT(valid && "resize_render_target: invalid handle");
    if (!valid) {
        NT_LOG_ERROR("resize_render_target: invalid handle");
        return false;
    }
    NT_ASSERT(s_gfx.render_state != NT_GFX_STATE_PASS);
    if (s_gfx.render_state == NT_GFX_STATE_PASS) {
        NT_LOG_ERROR("resize_render_target called inside a pass");
        return false;
    }
    NT_ASSERT(width > 0 && height > 0 && "resize_render_target: zero dimension");
    if (width == 0 || height == 0) {
        NT_LOG_ERROR("resize_render_target: zero dimension");
        return false;
    }
    uint32_t slot = nt_pool_slot_index(rt.id);
    return render_target_resize_backend(slot, width, height);
}

nt_texture_t nt_gfx_render_target_color(nt_render_target_t rt) {
    if (!nt_pool_valid(&s_gfx.render_target_pool, rt.id)) {
        return (nt_texture_t){0};
    }
    return s_gfx.render_target_metas[nt_pool_slot_index(rt.id)].color;
}

nt_texture_t nt_gfx_render_target_depth(nt_render_target_t rt) {
    if (!nt_pool_valid(&s_gfx.render_target_pool, rt.id)) {
        return (nt_texture_t){0};
    }
    uint32_t slot = nt_pool_slot_index(rt.id);
    if (s_gfx.render_target_metas[slot].desc.depth_storage != NT_RT_DEPTH_TEXTURE) {
        return (nt_texture_t){0};
    }
    return s_gfx.render_target_metas[slot].depth;
}

bool nt_gfx_render_target_ready(nt_render_target_t rt) {
    if (!nt_pool_valid(&s_gfx.render_target_pool, rt.id)) {
        return false;
    }
    return s_gfx.render_target_metas[nt_pool_slot_index(rt.id)].complete;
}

bool nt_gfx_shader_ready(nt_shader_t shd) {
    if (!nt_pool_valid(&s_gfx.shader_pool, shd.id)) {
        return false;
    }
    return s_gfx.shader_backends[nt_pool_slot_index(shd.id)] != 0;
}

bool nt_gfx_program_valid(nt_program_t prog) { return nt_pool_valid(&s_gfx.program_pool, prog.id); }

bool nt_gfx_pipeline_valid(nt_pipeline_t pip) { return nt_pool_valid(&s_gfx.pipeline_pool, pip.id); }

bool nt_gfx_vertex_input_valid(nt_vertex_input_t vi) { return nt_pool_valid(&s_gfx.vertex_input_pool, vi.id); }

bool nt_gfx_program_ready(nt_program_t prog) {
    if (!nt_pool_valid(&s_gfx.program_pool, prog.id)) {
        return false;
    }
    return s_gfx.program_backends[nt_pool_slot_index(prog.id)] != 0;
}

bool nt_gfx_texture_ready(nt_texture_t tex) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return false;
    }
    return s_gfx.texture_backends[nt_pool_slot_index(tex.id)] != 0;
}

bool nt_gfx_texture_size(nt_texture_t tex, uint16_t *out_width, uint16_t *out_height) {
    NT_ASSERT(out_width != NULL && out_height != NULL);
    if (out_width == NULL || out_height == NULL) {
        return false;
    }
    *out_width = 0;
    *out_height = 0;
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return false;
    }
    uint32_t slot = nt_pool_slot_index(tex.id);
    *out_width = s_gfx.texture_metas[slot].width;
    *out_height = s_gfx.texture_metas[slot].height;
    return true;
}

nt_texture_format_t nt_gfx_texture_format(nt_texture_t tex) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return NT_TEXTURE_FORMAT_INVALID;
    }
    return (nt_texture_format_t)s_gfx.texture_metas[nt_pool_slot_index(tex.id)].format;
}
/* ---- Draw state ---- */

void nt_gfx_bind_pipeline(nt_pipeline_t pip) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    /* Transitional while pipelines still own VAOs: binding one clobbers the
     * bound vertex input, so the mirror must not survive (bind_instance_buffer
     * would otherwise write foreign pointers into the pipeline's VAO). */
    s_gfx.bound_vertex_input = 0;
    if (!nt_pool_valid(&s_gfx.pipeline_pool, pip.id)) {
        /* Clear both bind mirrors so later draws or buffer binds cannot reuse
         * the previous pipeline's program and VAO. */
        s_gfx.bound_pipeline = 0;
        nt_gfx_backend_bind_pipeline(0);
        NT_LOG_ERROR("bind_pipeline: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(pip.id);
    NT_ASSERT(s_gfx.pipeline_backends[slot] != 0 &&
              "bind_pipeline: this pipeline outlived a context loss -- call the owning renderer's restore entry point (nt_*_renderer_restore_gpu) in the restored frame");
    s_gfx.bound_pipeline = s_gfx.pipeline_backends[slot];
#ifdef NT_TEST_ACCESS
    s_test_bound_pipeline = pip;
#endif
    nt_gfx_backend_bind_pipeline(s_gfx.bound_pipeline);
}

void nt_gfx_bind_vertex_input(nt_vertex_input_t vi) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.vertex_input_pool, vi.id)) {
        /* Parallel to bind_pipeline's invalid path: clear the mirrors and
         * bind VAO 0 so later draws cannot use stale vertex-input state. */
        s_gfx.bound_vertex_input = 0;
        s_gfx.bound_index_type = NT_INDEX_NONE;
        nt_gfx_backend_bind_vertex_input(0);
        NT_LOG_ERROR("bind_vertex_input: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(vi.id);
    NT_ASSERT(s_gfx.vertex_input_backends[slot] != 0 && "bind_vertex_input: this vertex input outlived a context loss -- recreate it from the restored frame's resources");
    s_gfx.bound_vertex_input = vi.id;
    /* NT_INDEX_NONE for a non-indexed vertex input: cleared, not stale. */
    s_gfx.bound_index_type = s_gfx.vertex_input_metas[slot].index_type;
    nt_gfx_backend_bind_vertex_input(s_gfx.vertex_input_backends[slot]);
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
    s_gfx.bound_texture_ids[slot] = tex.id;
    nt_gfx_bind_sampler(s_gfx.texture_metas[idx].default_sampler, slot);
}

nt_sampler_t nt_gfx_get_texture_default_sampler(nt_texture_t tex) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return NT_SAMPLER_INVALID;
    }
    return s_gfx.texture_metas[nt_pool_slot_index(tex.id)].default_sampler;
}

#ifdef NT_TEST_ACCESS
uint32_t nt_gfx_test_sampler_backend_id(nt_sampler_t s) {
    if (s.id == 0 || s.id > s_gfx.sampler_count) {
        return 0;
    }
    return s_gfx.sampler_cache[s.id - 1].backend;
}

uint32_t nt_gfx_test_texture_backend_id(nt_texture_t tex) {
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        return 0;
    }
    return s_gfx.texture_backends[nt_pool_slot_index(tex.id)];
}

uint32_t nt_gfx_test_render_target_backend_id(nt_render_target_t rt) {
    if (!nt_pool_valid(&s_gfx.render_target_pool, rt.id)) {
        return 0;
    }
    return s_gfx.render_target_backends[nt_pool_slot_index(rt.id)];
}

void nt_gfx_test_scissor_rect(int out[4]) {
    NT_ASSERT(out != NULL);
    out[0] = s_gfx.scissor_rect[0];
    out[1] = s_gfx.scissor_rect[1];
    out[2] = s_gfx.scissor_rect[2];
    out[3] = s_gfx.scissor_rect[3];
}

void nt_gfx_test_viewport_rect(int out[4]) {
    NT_ASSERT(out != NULL);
    out[0] = s_gfx.viewport_rect[0];
    out[1] = s_gfx.viewport_rect[1];
    out[2] = s_gfx.viewport_rect[2];
    out[3] = s_gfx.viewport_rect[3];
}
#endif

/* ---- Sampler (deduplicated cache) ---- */

/* One clamp for the whole sampler path: key, GL object and recorded desc derive
 * from the result, so they cannot disagree. Pack headers cast raw bytes into
 * these enums, so out of range takes the zero value the backend maps it to. */
static inline nt_sampler_desc_t sampler_normalize(const nt_sampler_desc_t *desc) {
    nt_sampler_desc_t out = *desc;
    if ((uint32_t)out.min_filter > (uint32_t)NT_FILTER_LINEAR_MIPMAP_LINEAR) {
        out.min_filter = NT_FILTER_NEAREST;
    }
    if ((uint32_t)out.mag_filter > (uint32_t)NT_FILTER_LINEAR) {
        out.mag_filter = NT_FILTER_NEAREST;
    }
    if ((uint32_t)out.wrap_u > (uint32_t)NT_WRAP_MIRRORED_REPEAT) {
        out.wrap_u = NT_WRAP_CLAMP_TO_EDGE;
    }
    if ((uint32_t)out.wrap_v > (uint32_t)NT_WRAP_MIRRORED_REPEAT) {
        out.wrap_v = NT_WRAP_CLAMP_TO_EDGE;
    }
    if ((uint32_t)out.compare_func > (uint32_t)NT_COMPARE_LESS) {
        out.compare_func = NT_COMPARE_NONE;
    }
    return out;
}

/* Normalized descs only: equal key must mean an identical GL sampler. */
static inline uint32_t sampler_pack_key(const nt_sampler_desc_t *desc) {
    return (uint32_t)desc->min_filter | ((uint32_t)desc->mag_filter << 3) | ((uint32_t)desc->wrap_u << 4) | ((uint32_t)desc->wrap_v << 6) | ((uint32_t)desc->compare_func << 8);
}
_Static_assert(NT_FILTER_LINEAR_MIPMAP_LINEAR < 8 && NT_FILTER_LINEAR < 2 && NT_WRAP_MIRRORED_REPEAT < 4 && NT_COMPARE_LESS < 4,
               "sampler_pack_key field widths — a new enum value would overlap the next field");

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — cache hit / miss / lazy-recreate paths
nt_sampler_t nt_gfx_make_sampler(const nt_sampler_desc_t *desc) {
    NT_ASSERT(desc != NULL);
    /* Unsigned: nt_compare_func_t and nt_texture_filter_t are signed under the
     * MSVC ABI, where a negative cast would pass an upper-bound-only check. */
    NT_ASSERT((uint32_t)desc->mag_filter <= NT_FILTER_LINEAR && "sampler mag_filter must be NEAREST or LINEAR");
    NT_ASSERT((uint32_t)desc->compare_func <= NT_COMPARE_LESS && "sampler compare_func out of range");

    nt_sampler_desc_t normalized = sampler_normalize(desc);
    uint32_t key = sampler_pack_key(&normalized);

    for (uint32_t i = 0; i < s_gfx.sampler_count; i++) {
        if (s_gfx.sampler_cache[i].key == key) {
            /* Hit; lazy-recreate backend if context loss zeroed it. */
            if (s_gfx.sampler_cache[i].backend == 0 && !g_nt_gfx.context_lost) {
                s_gfx.sampler_cache[i].backend = nt_gfx_backend_create_sampler(&s_gfx.sampler_cache[i].desc);
            }
            return (nt_sampler_t){.id = i + 1};
        }
    }

    NT_ASSERT(s_gfx.sampler_count < NT_GFX_MAX_SAMPLERS && "sampler cache full; raise NT_GFX_MAX_SAMPLERS");
    uint32_t backend = nt_gfx_backend_create_sampler(&normalized);
    if (backend == 0) {
        NT_LOG_ERROR("make_sampler: backend failed");
        return NT_SAMPLER_INVALID;
    }
    uint32_t slot = s_gfx.sampler_count++;
    s_gfx.sampler_cache[slot].key = key;
    s_gfx.sampler_cache[slot].backend = backend;
    s_gfx.sampler_cache[slot].desc = normalized;
    return (nt_sampler_t){.id = slot + 1};
}

static bool texture_has_complete_mip_chain(const nt_gfx_texture_meta_t *meta) {
    uint16_t max_dim = meta->width > meta->height ? meta->width : meta->height;
    uint8_t required_levels = 1;
    while (max_dim > 1) {
        max_dim >>= 1;
        required_levels++;
    }
    return meta->mip_count >= required_levels;
}

static bool bound_texture_sampler_compatible(uint32_t slot, const nt_sampler_desc_t *desc) {
    bool compares = desc->compare_func != NT_COMPARE_NONE;
    uint32_t texture_id = s_gfx.bound_texture_ids[slot];
    if (!nt_pool_valid(&s_gfx.texture_pool, texture_id)) {
        /* Comparison needs depth storage to check against, and nt_gfx_bind_texture
         * reinstalls the texture's own sampler — so a comparison sampler on an
         * empty slot can only be a bind-order mistake. */
        return !compares;
    }
    uint32_t texture_slot = nt_pool_slot_index(texture_id);
    const nt_gfx_texture_meta_t *meta = &s_gfx.texture_metas[texture_slot];
    uint8_t format = meta->format;
    bool depth = format >= (uint8_t)NT_TEXTURE_FORMAT_DEPTH16;
    /* Comparison against a non-depth texture makes every lookup undefined in
     * GL, whichever sampler type the shader declares. */
    if (compares && !depth) {
        return false;
    }
    /* Filtering raw depth averages texels into a depth that exists in none of
     * them; with comparison on, LINEAR blends the 0/1 comparison results
     * instead. Integer storage has no such escape and stays NEAREST. */
    bool nearest_only = format == (uint8_t)NT_TEXTURE_FORMAT_RG16UI || (depth && !compares);
    if (nearest_only && (desc->min_filter != NT_FILTER_NEAREST || desc->mag_filter != NT_FILTER_NEAREST)) {
        return false;
    }
    bool mipmap_filter = desc->min_filter > NT_FILTER_LINEAR;
    return !mipmap_filter || texture_has_complete_mip_chain(meta);
}

void nt_gfx_bind_sampler(nt_sampler_t s, uint32_t slot) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (slot >= NT_GFX_MAX_TEXTURE_SLOTS) {
        NT_LOG_ERROR("bind_sampler: slot exceeds max");
        return;
    }
    if (s.id == 0) {
        /* Unbind: revert texture unit to texture's own filter/wrap. */
        nt_gfx_backend_bind_sampler(0, slot);
        return;
    }
    NT_ASSERT(s.id <= s_gfx.sampler_count && "bind_sampler: invalid handle");
    nt_gfx_sampler_entry_t *e = &s_gfx.sampler_cache[s.id - 1];
    bool sampler_compatible = bound_texture_sampler_compatible(slot, &e->desc);
    NT_ASSERT(sampler_compatible && "bind_sampler: sampler is incompatible with texture storage");
    if (!sampler_compatible) {
        return;
    }
    if (e->backend == 0) {
        /* Lazy recreate after context-loss recovery — desc was preserved. */
        e->backend = nt_gfx_backend_create_sampler(&e->desc);
    }
    nt_gfx_backend_bind_sampler(e->backend, slot);
}

/* ---- Scissor and viewport ----
 *
 * All three wrappers early-return on context loss — backend is dead, cached
 * state must not drift. Callers re-issue from a clean frame after restore. */

void nt_gfx_set_scissor(int x, int y, int w, int h) {
    /* Negative width/height is undefined in GL — assert early per AGENTS.md "fail early". */
    NT_ASSERT(w >= 0);
    NT_ASSERT(h >= 0);
    if (g_nt_gfx.context_lost) {
        return;
    }
    s_gfx.scissor_rect[0] = x;
    s_gfx.scissor_rect[1] = y;
    s_gfx.scissor_rect[2] = w;
    s_gfx.scissor_rect[3] = h;
    nt_gfx_backend_set_scissor(x, y, w, h);
}

void nt_gfx_set_scissor_enabled(bool enabled) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    s_gfx.scissor_enabled = enabled;
    nt_gfx_backend_set_scissor_enabled(enabled);
}

bool nt_gfx_scissor_enabled(void) { return s_gfx.scissor_enabled; }

void nt_gfx_set_viewport(int x, int y, int w, int h) {
    NT_ASSERT(w >= 0);
    NT_ASSERT(h >= 0);
    if (g_nt_gfx.context_lost) {
        return;
    }
    s_gfx.viewport_rect[0] = x;
    s_gfx.viewport_rect[1] = y;
    s_gfx.viewport_rect[2] = w;
    s_gfx.viewport_rect[3] = h;
    nt_gfx_backend_set_viewport(x, y, w, h);
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

/* Pre-frame readiness decisions may describe the lost context: rebuild on the
 * restored frame and submit on the next. Pass clears remain legal. */
static void assert_draws_allowed_this_frame(void) { NT_ASSERT(!g_nt_gfx.context_restored && "no draws on the restored frame; see docs/spec/assets/resource.md"); }

/* Enabled-but-unpointed instance attribs are invalid GL that fails silently.
 * Transitional: enforced only when a vertex input is bound -- the legacy
 * pipeline-VAO path keeps its old checks until it is removed. */
static void assert_instance_attribs_pointed(void) {
    if (s_gfx.bound_vertex_input == 0) {
        return;
    }
    NT_ASSERT(
        (s_gfx.vertex_input_metas[nt_pool_slot_index(s_gfx.bound_vertex_input)].instance_attr_count == 0 || s_gfx.vertex_input_metas[nt_pool_slot_index(s_gfx.bound_vertex_input)].instance_pointed) &&
        "instanced draw: bind_instance_buffer has not pointed the bound vertex input's instance attribs");
}

/* Transitional like above: a bound vertex input carries its own index type;
 * NT_INDEX_NONE here means the caller draws indexed on a non-indexed input. */
static void assert_indexed_draw_has_index_type(void) { NT_ASSERT((s_gfx.bound_vertex_input == 0 || s_gfx.bound_index_type != NT_INDEX_NONE) && "draw_indexed: bound vertex input is non-indexed"); }

void nt_gfx_draw(uint32_t first_vertex, uint32_t num_vertices) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    assert_draws_allowed_this_frame();

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
#ifdef NT_TEST_ACCESS
    test_record_draw(first_vertex, num_vertices, 0, 0, 1);
#endif
    nt_gfx_backend_draw(first_vertex, num_vertices);
}

void nt_gfx_draw_instanced(uint32_t first_vertex, uint32_t num_vertices, uint32_t instance_count) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    assert_draws_allowed_this_frame();

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
    assert_instance_attribs_pointed();

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.draw_calls_instanced++;
    g_nt_gfx.frame_stats.vertices += num_vertices * instance_count;
    g_nt_gfx.frame_stats.instances += instance_count;
#ifdef NT_TEST_ACCESS
    test_record_draw(first_vertex, num_vertices, 0, 0, instance_count);
#endif
    nt_gfx_backend_draw_instanced(first_vertex, num_vertices, instance_count);
}

void nt_gfx_draw_indexed(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    assert_draws_allowed_this_frame();

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
    assert_indexed_draw_has_index_type();

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.vertices += num_vertices;
    g_nt_gfx.frame_stats.indices += num_indices;
#ifdef NT_TEST_ACCESS
    test_record_draw(0, num_vertices, first_index, num_indices, 1);
#endif
    nt_gfx_backend_draw_indexed(first_index, num_indices, s_gfx.bound_index_type);
}

void nt_gfx_draw_indexed_instanced(uint32_t first_index, uint32_t num_indices, uint32_t num_vertices, uint32_t instance_count) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    assert_draws_allowed_this_frame();

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
    assert_indexed_draw_has_index_type();
    assert_instance_attribs_pointed();

    g_nt_gfx.frame_stats.draw_calls++;
    g_nt_gfx.frame_stats.draw_calls_instanced++;
    g_nt_gfx.frame_stats.vertices += num_vertices * instance_count;
    g_nt_gfx.frame_stats.indices += num_indices * instance_count;
    g_nt_gfx.frame_stats.instances += instance_count;
#ifdef NT_TEST_ACCESS
    test_record_draw(0, num_vertices, first_index, num_indices, instance_count);
#endif
    nt_gfx_backend_draw_indexed_instanced(first_index, num_indices, instance_count, s_gfx.bound_index_type);
}

/* ---- Instance buffer ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — NT_ASSERT expansion, not real branching
void nt_gfx_bind_instance_buffer(nt_buffer_t buf, uint32_t byte_offset) {
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
    NT_ASSERT(byte_offset <= s_gfx.buffer_metas[slot].size && "bind_instance_buffer: offset exceeds buffer capacity");
    NT_ASSERT((byte_offset & 3U) == 0 && "bind_instance_buffer: offset must be 4-byte aligned (WebGL2 attrib rule)");
    if (s_gfx.bound_vertex_input != 0) {
        uint32_t vi_slot = nt_pool_slot_index(s_gfx.bound_vertex_input);
        NT_ASSERT(s_gfx.vertex_input_metas[vi_slot].instance_attr_count > 0 && "bind_instance_buffer: bound vertex input declares no instance layout");
        s_gfx.vertex_input_metas[vi_slot].instance_pointed = true;
    } else {
        /* Transitional fallback while pipelines still own instance layouts. */
        NT_ASSERT(s_gfx.bound_pipeline != 0 && "bind_instance_buffer: requires a bound vertex input or pipeline");
        if (s_gfx.bound_pipeline == 0) {
            NT_LOG_ERROR("bind_instance_buffer: no pipeline bound");
            return;
        }
    }
    nt_gfx_backend_bind_instance_buffer(s_gfx.buffer_backends[slot], byte_offset);
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

/* ---- Buffer update ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — NT_ASSERT expansion, not real branching
void nt_gfx_update_buffer(nt_buffer_t buf, uint32_t offset, const void *data, uint32_t size) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        NT_LOG_ERROR("update_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[slot].usage != NT_USAGE_IMMUTABLE && "update_buffer: cannot update immutable buffer");
    NT_ASSERT((data != NULL || size == 0) && "update_buffer: NULL data with nonzero size");
    NT_ASSERT(offset <= s_gfx.buffer_metas[slot].size && "update_buffer: offset exceeds buffer capacity");
    NT_ASSERT(size <= s_gfx.buffer_metas[slot].size - offset && "update_buffer: offset + size exceeds buffer capacity");
    nt_gfx_backend_update_buffer(s_gfx.buffer_backends[slot], offset, data, size);
}

void nt_gfx_begin_segment(const char *name) {
    if (g_nt_gfx.context_lost || name == NULL) {
        return;
    }
    nt_gfx_backend_begin_segment(name);
}

void nt_gfx_end_segment(void) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    nt_gfx_backend_end_segment();
}

bool nt_gfx_poll_segment_time_ns(const char *name, uint64_t *out_ns) {
    if (g_nt_gfx.context_lost || name == NULL || out_ns == NULL) {
        return false;
    }
    return nt_gfx_backend_poll_segment_time_ns(name, out_ns);
}

void nt_gfx_set_gpu_timing_enabled(bool enabled) { nt_gfx_backend_set_gpu_timing_enabled(enabled); }

bool nt_gfx_is_gpu_timing_supported(void) { return nt_gfx_backend_is_gpu_timing_supported(); }

void nt_gfx_orphan_buffer(nt_buffer_t buf, const void *data, uint32_t size) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.buffer_pool, buf.id)) {
        NT_LOG_ERROR("orphan_buffer: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(buf.id);
    NT_ASSERT(s_gfx.buffer_metas[slot].usage == NT_USAGE_DYNAMIC && "orphan_buffer: requires NT_USAGE_DYNAMIC");
    NT_ASSERT(size <= s_gfx.buffer_metas[slot].size && "orphan_buffer: size exceeds buffer capacity");
    nt_gfx_backend_orphan_buffer(s_gfx.buffer_backends[slot], data, size);
}

/* ---- Texture update ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_gfx_update_texture(nt_texture_t tex, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const void *data) {
    if (g_nt_gfx.context_lost) {
        return;
    }
    if (!nt_pool_valid(&s_gfx.texture_pool, tex.id)) {
        NT_LOG_ERROR("update_texture: invalid handle");
        return;
    }
    uint32_t slot = nt_pool_slot_index(tex.id);
    uint8_t stored_format = s_gfx.texture_metas[slot].format;
    bool format_valid = stored_format > (uint8_t)NT_TEXTURE_FORMAT_INVALID && stored_format <= (uint8_t)NT_TEXTURE_FORMAT_DEPTH32F;
    NT_ASSERT(format_valid && "update_texture: invalid stored format");
    if (!format_valid) {
        return;
    }
    NT_ASSERT(!s_gfx.texture_metas[slot].render_target_owned && "update_texture: texture is owned by a render target");
    if (s_gfx.texture_metas[slot].render_target_owned) {
        NT_LOG_ERROR("update_texture: texture is owned by a render target");
        return;
    }
    NT_ASSERT(data != NULL && "update_texture: NULL data pointer");
    NT_ASSERT(w > 0 && h > 0 && "update_texture: zero-size region");
    NT_ASSERT(!s_gfx.texture_metas[slot].compressed && "update_texture: compressed textures cannot be sub-updated");
    NT_ASSERT(s_gfx.texture_metas[slot].mip_count <= 1 && "update_texture: mipmapped textures not supported, use per-level API when available");
    bool is_depth = stored_format >= (uint8_t)NT_TEXTURE_FORMAT_DEPTH16;
    NT_ASSERT(!is_depth && "update_texture: depth texture updates are not supported");
    if (is_depth) {
        return;
    }
    NT_ASSERT(x + w <= s_gfx.texture_metas[slot].width && "update_texture: x+w exceeds texture width");
    NT_ASSERT(y + h <= s_gfx.texture_metas[slot].height && "update_texture: y+h exceeds texture height");
    nt_gfx_backend_update_texture(s_gfx.texture_backends[slot], x, y, w, h, (nt_texture_format_t)stored_format, data);
}

/* ---- Mesh side table helpers ---- */

/* mesh_table_alloc and mesh_handle_make replaced by nt_pool_alloc(&s_gfx.mesh_pool) */

/* ---- Asset activators ---- */

/* BASIS-only: RAW path bakes filter into desc and uses make_texture instead. */
static void texture_attach_default_sampler(uint32_t tex_id, const NtTextureAssetHeaderV2 *hdr) {
    nt_sampler_desc_t sd = {
        .min_filter = (nt_texture_filter_t)hdr->default_min_filter,
        .mag_filter = (nt_texture_filter_t)hdr->default_mag_filter,
        .wrap_u = (nt_texture_wrap_t)hdr->default_wrap_u,
        .wrap_v = (nt_texture_wrap_t)hdr->default_wrap_v,
        .label = NULL,
    };
    nt_sampler_t s = nt_gfx_make_sampler(&sd);
    NT_ASSERT(s.id != 0 && "texture_attach_default_sampler: sampler creation failed");
    uint32_t slot = nt_pool_slot_index(tex_id);
    s_gfx.texture_metas[slot].default_sampler = s;
}

/* Activate a v2 texture (RAW or Basis Universal compressed) */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t activate_texture_impl(const uint8_t *data, uint32_t size) {
    const NtTextureAssetHeaderV2 *hdr2 = (const NtTextureAssetHeaderV2 *)data;

    /* Validate dimensions against GPU caps */
    if (hdr2->width > g_nt_gfx.gpu_caps.max_texture_size || hdr2->height > g_nt_gfx.gpu_caps.max_texture_size) {
        NT_LOG_ERROR("activate_texture: %ux%u exceeds GPU max_texture_size %u", hdr2->width, hdr2->height, g_nt_gfx.gpu_caps.max_texture_size);
        return 0;
    }

    /* Validate data size (subtraction safe — caller verified size >= sizeof header) */
    if (hdr2->data_size > size - sizeof(NtTextureAssetHeaderV2)) {
        NT_LOG_ERROR("activate_texture: v2 blob truncated");
        return 0;
    }

    /* RAW compression: uncompressed pixel data after header */
    if (hdr2->compression == NT_TEXTURE_COMPRESSION_RAW) {
        nt_texture_format_t pixel_fmt;
        uint32_t bpp;
        switch (hdr2->format) {
        case NT_TEXTURE_FORMAT_RGBA8:
            pixel_fmt = NT_TEXTURE_FORMAT_RGBA8;
            bpp = 4;
            break;
        case NT_TEXTURE_FORMAT_RGB8:
            pixel_fmt = NT_TEXTURE_FORMAT_RGB8;
            bpp = 3;
            break;
        case NT_TEXTURE_FORMAT_RG8:
            pixel_fmt = NT_TEXTURE_FORMAT_RG8;
            bpp = 2;
            break;
        case NT_TEXTURE_FORMAT_R8:
            pixel_fmt = NT_TEXTURE_FORMAT_R8;
            bpp = 1;
            break;
        default:
            NT_LOG_ERROR("activate_texture: unsupported format %u", hdr2->format);
            return 0;
        }
        /* Validate data_size matches expected pixel payload (use uint64 to avoid overflow) */
        uint64_t expected = (uint64_t)hdr2->width * (uint64_t)hdr2->height * bpp;
        if (expected > UINT32_MAX || hdr2->data_size < (uint32_t)expected) {
            NT_LOG_ERROR("activate_texture: RAW data_size %u < expected %u", hdr2->data_size, (uint32_t)expected);
            return 0;
        }
        NT_ASSERT(hdr2->width <= UINT16_MAX && hdr2->height <= UINT16_MAX && "activate_texture: dimensions exceed uint16");
        const uint8_t *pixels = data + sizeof(NtTextureAssetHeaderV2);
        nt_texture_desc_t desc = {
            .width = (uint16_t)hdr2->width,
            .height = (uint16_t)hdr2->height,
            .data = pixels,
            .format = pixel_fmt,
            .min_filter = (nt_texture_filter_t)hdr2->default_min_filter,
            .mag_filter = (nt_texture_filter_t)hdr2->default_mag_filter,
            .wrap_u = (nt_texture_wrap_t)hdr2->default_wrap_u,
            .wrap_v = (nt_texture_wrap_t)hdr2->default_wrap_v,
            .gen_mipmaps = (hdr2->flags & NT_TEXTURE_FLAG_GEN_MIPMAPS) != 0,
            .label = NULL,
        };
        return nt_gfx_make_texture(&desc).id;
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
        NT_LOG_ERROR("activate_texture: Basis validate failed (bad data or stub transcoder linked)");
        return 0;
    }

    /* Select transcode target: BC7 > ASTC > ETC2 > RGBA8 */
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

    /* Call backend for per-mip transcode + compressed upload. Pass V3 header
     * sampler defaults through to texture-object state (glTexParameteri) — the
     * RAW path already does this; BASIS was hardcoded LINEAR_MIPMAP_LINEAR/REPEAT
     * pre-V3 and got missed in the header bump. The bound sampler-object
     * normally overrides texture-object state, but if a caller ever issues
     * glBindSampler(0, slot) the unit falls back to this state — so it must
     * match the asset's intended defaults. */
    uint32_t backend =
        nt_gfx_backend_create_texture_compressed(basis_data, basis_size, hdr2->width, hdr2->height, levels, (nt_texture_filter_t)hdr2->default_min_filter,
                                                 (nt_texture_filter_t)hdr2->default_mag_filter, (nt_texture_wrap_t)hdr2->default_wrap_u, (nt_texture_wrap_t)hdr2->default_wrap_v, (uint32_t)target);
    if (backend == 0) {
        NT_LOG_ERROR("activate_texture: transcode failed");
        nt_pool_free(&s_gfx.texture_pool, id);
        return 0;
    }

    uint32_t slot = nt_pool_slot_index(id);
    s_gfx.texture_backends[slot] = backend;
    s_gfx.texture_metas[slot].width = (uint16_t)hdr2->width;
    s_gfx.texture_metas[slot].height = (uint16_t)hdr2->height;
    s_gfx.texture_metas[slot].mip_count = (uint8_t)(levels > 255 ? 255 : levels);
    s_gfx.texture_metas[slot].compressed = true;
    texture_attach_default_sampler(id, hdr2);
    return id;
}

uint32_t nt_gfx_activate_texture(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(NtTextureAssetHeaderV2)) {
        NT_LOG_ERROR("activate_texture: blob too small");
        return 0;
    }
    const NtTextureAssetHeaderV2 *hdr = (const NtTextureAssetHeaderV2 *)data;
    if (hdr->magic != NT_TEXTURE_MAGIC) {
        NT_LOG_ERROR("activate_texture: bad magic");
        return 0;
    }
    if (hdr->version != NT_TEXTURE_VERSION_V2) {
        NT_LOG_ERROR("activate_texture: version %u != %u -- rebuild packs", (uint32_t)hdr->version, (uint32_t)NT_TEXTURE_VERSION_V2);
        return 0;
    }

    return activate_texture_impl(data, size);
}

/* Per-stream half of the pack mesh safety net: type/count/normalized validity,
 * unique name hashes, and the WebGL2 offset/stride alignment rules. */
static bool mesh_streams_valid(const NtStreamDesc *streams, uint8_t stream_count, uint32_t *out_stride) {
    uint32_t stride = 0;
    for (uint8_t i = 0; i < stream_count; i++) {
        if (nt_stream_type_size(streams[i].type) == 0 || streams[i].count < 1 || streams[i].count > 4) {
            NT_LOG_ERROR("activate_mesh: stream[%u] invalid type %u / count %u", i, (uint32_t)streams[i].type, (uint32_t)streams[i].count);
            return false;
        }
        if (streams[i].normalized != 0 && (streams[i].type == NT_STREAM_FLOAT32 || streams[i].type == NT_STREAM_FLOAT16)) {
            NT_LOG_ERROR("activate_mesh: stream[%u] normalized on a float type", i);
            return false;
        }
        /* Duplicate hashes would bind one shader location twice (last wins, silently) */
        for (uint8_t p = 0; p < i; p++) {
            if (streams[p].name_hash == streams[i].name_hash) {
                NT_LOG_ERROR("activate_mesh: stream[%u] duplicates name_hash 0x%08x of stream[%u]", i, streams[i].name_hash, p);
                return false;
            }
        }
        if (stride % nt_stream_type_size(streams[i].type) != 0) {
            NT_LOG_ERROR("activate_mesh: stream[%u] offset %u misaligned for type size %u", i, stride, nt_stream_type_size(streams[i].type));
            return false;
        }
        stride += nt_stream_type_size(streams[i].type) * streams[i].count;
    }
    for (uint8_t i = 0; i < stream_count; i++) {
        if (stride % nt_stream_type_size(streams[i].type) != 0) {
            NT_LOG_ERROR("activate_mesh: stride %u misaligned for stream[%u] type size %u", stride, i, nt_stream_type_size(streams[i].type));
            return false;
        }
    }
    *out_stride = stride;
    return true;
}

/* MESHOPT wire sizing invariants -- everything that must hold BEFORE the
 * decode allocation. 64-bit arithmetic throughout. */
static bool mesh_meshopt_sizes_valid(const NtMeshAssetHeader *hdr, uint32_t idx_elem) {
    if (hdr->index_type == 0 || hdr->index_count == 0 || hdr->index_count % 3 != 0) {
        NT_LOG_ERROR("activate_mesh: MESHOPT wire with index_type %u index_count %u", (uint32_t)hdr->index_type, hdr->index_count);
        return false;
    }
    uint64_t decoded_size = (uint64_t)hdr->index_count * idx_elem;
    /* Decoded size must fit a u32: it feeds malloc, and a wasm size_t wraps at 4 GB */
    if (decoded_size > UINT32_MAX) {
        NT_LOG_ERROR("activate_mesh: decoded index size overflow (index_count %u)", hdr->index_count);
        return false;
    }
    if (hdr->index_data_size == 0 || hdr->index_data_size > decoded_size) {
        NT_LOG_ERROR("activate_mesh: MESHOPT index_data_size %u vs decoded %u", hdr->index_data_size, (uint32_t)decoded_size);
        return false;
    }
    /* codec minimum (header + 1 code byte per triangle + 16-byte tail):
       without this a 17-byte stream could claim hundreds of millions of
       indices and force a huge decode allocation before the decoder rejects */
    uint64_t min_wire = 1ULL + (hdr->index_count / 3) + 16ULL;
    if (hdr->index_data_size < min_wire) {
        NT_LOG_ERROR("activate_mesh: MESHOPT index_data_size %u below codec minimum for %u indices", hdr->index_data_size, hdr->index_count);
        return false;
    }
    return true;
}

/* Runtime safety net for pack mesh blobs (spec: runtime validates magic/version/type/sizes).
 * 64-bit sums so a corrupt size field cannot wrap a check into a pass. */
static bool mesh_blob_valid(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(NtMeshAssetHeader)) {
        NT_LOG_ERROR("activate_mesh: blob too small");
        return false;
    }
    const NtMeshAssetHeader *hdr = (const NtMeshAssetHeader *)data;
    if (hdr->magic != NT_MESH_MAGIC) {
        NT_LOG_ERROR("activate_mesh: bad magic");
        return false;
    }
    if (hdr->version != NT_MESH_VERSION) {
        NT_LOG_ERROR("activate_mesh: version %u != %u -- rebuild packs", (uint32_t)hdr->version, (uint32_t)NT_MESH_VERSION);
        return false;
    }
    if (hdr->index_type > 2) {
        NT_LOG_ERROR("activate_mesh: invalid index_type");
        return false;
    }
    if (hdr->vertex_wire > NT_MESH_WIRE_VTX_SOA || hdr->index_wire > NT_MESH_WIRE_IDX_MESHOPT) {
        NT_LOG_ERROR("activate_mesh: invalid wire tags %u/%u", (uint32_t)hdr->vertex_wire, (uint32_t)hdr->index_wire);
        return false;
    }
    if (hdr->stream_count == 0 || hdr->stream_count > NT_MESH_MAX_STREAMS) {
        NT_LOG_ERROR("activate_mesh: invalid stream_count");
        return false;
    }
    uint32_t streams_size = (uint32_t)hdr->stream_count * (uint32_t)sizeof(NtStreamDesc);
    uint64_t required = (uint64_t)sizeof(NtMeshAssetHeader) + streams_size + hdr->vertex_data_size + hdr->index_data_size;
    if (required > size) {
        NT_LOG_ERROR("activate_mesh: blob truncated");
        return false;
    }
    /* Per-stream descs are pack input feeding glVertexAttribPointer */
    const NtStreamDesc *streams = (const NtStreamDesc *)(data + sizeof(NtMeshAssetHeader));
    uint32_t expected_stride = 0;
    if (!mesh_streams_valid(streams, hdr->stream_count, &expected_stride)) {
        return false;
    }
    if ((uint64_t)hdr->vertex_count * expected_stride != hdr->vertex_data_size) {
        NT_LOG_ERROR("activate_mesh: vertex_data_size %u != vertex_count %u * stride %u", hdr->vertex_data_size, hdr->vertex_count, expected_stride);
        return false;
    }
    static const uint32_t idx_elem_sizes[3] = {0, 2, 4};
    uint32_t idx_elem = idx_elem_sizes[hdr->index_type];
    if (hdr->index_type == 0 && hdr->index_count != 0) {
        NT_LOG_ERROR("activate_mesh: index_count %u with index_type none", hdr->index_count);
        return false;
    }
    if (hdr->index_wire == NT_MESH_WIRE_IDX_MESHOPT) {
        if (!mesh_meshopt_sizes_valid(hdr, idx_elem)) {
            return false;
        }
    } else if ((uint64_t)hdr->index_count * idx_elem != hdr->index_data_size) {
        NT_LOG_ERROR("activate_mesh: index_data_size %u != index_count %u * %u", hdr->index_data_size, hdr->index_count, idx_elem);
        return false;
    }
    /* GL draws meshes only as GL_TRIANGLES: a trailing partial triangle would
       be silently dropped at draw -- reject in the safety net too. Non-indexed
       meshes draw vertex_count, so it carries the same constraint. */
    uint32_t draw_count = (hdr->index_count > 0) ? hdr->index_count : hdr->vertex_count;
    if (draw_count % 3 != 0) {
        NT_LOG_ERROR("activate_mesh: %s count %u is not a multiple of 3", (hdr->index_count > 0) ? "index" : "vertex", draw_count);
        return false;
    }
    return true;
}

#ifdef NT_TEST_ACCESS
static uint32_t s_test_last_mesh_vertex_hash;
static uint32_t s_test_last_mesh_index_hash;
uint32_t nt_gfx_test_last_mesh_vertex_hash(void) { return s_test_last_mesh_vertex_hash; }
uint32_t nt_gfx_test_last_mesh_index_hash(void) { return s_test_last_mesh_index_hash; }
#endif

/* Uploads the IBO from the wire index block (decoding MESHOPT first).
 * *out_ibo stays {0} for non-indexed meshes; returns false on failure
 * (nothing left to clean up). */
static bool mesh_make_ibo(const NtMeshAssetHeader *hdr, const uint8_t *index_data, nt_buffer_t *out_ibo) {
    *out_ibo = (nt_buffer_t){0};
#ifdef NT_TEST_ACCESS
    s_test_last_mesh_index_hash = 0;
#endif
    if (hdr->index_type == 0 || hdr->index_count == 0) {
        return true;
    }
    uint32_t idx_elem = (hdr->index_type == 1) ? 2U : 4U;
    const uint8_t *gpu_index_data = index_data;
    uint32_t gpu_index_size = hdr->index_data_size;
    if (hdr->index_wire == NT_MESH_WIRE_IDX_MESHOPT) {
        gpu_index_size = hdr->index_count * idx_elem; /* fits u32 -- validated */
        uint8_t *idx_tmp = mesh_stage_acquire(gpu_index_size);
        if (!idx_tmp) {
            NT_LOG_ERROR("activate_mesh: index decode alloc failed (%u bytes)", gpu_index_size);
            return false;
        }
        if (!nt_meshwire_decode_indices(idx_tmp, hdr->index_count, idx_elem, index_data, hdr->index_data_size, hdr->vertex_count)) {
            NT_LOG_ERROR("activate_mesh: index wire decode failed");
            return false;
        }
        gpu_index_data = idx_tmp;
    }
#ifdef NT_TEST_ACCESS
    s_test_last_mesh_index_hash = nt_hash32(gpu_index_data, gpu_index_size).value;
#endif
    *out_ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = gpu_index_data,
        .size = gpu_index_size,
        .index_type = hdr->index_type,
        .label = NULL,
    });
    if (out_ibo->id == 0) {
        NT_LOG_ERROR("activate_mesh: IBO creation failed");
        return false;
    }
    return true;
}

uint32_t nt_gfx_activate_mesh(const uint8_t *data, uint32_t size) {
    if (!mesh_blob_valid(data, size)) {
        return 0;
    }
    const NtMeshAssetHeader *hdr = (const NtMeshAssetHeader *)data;
    uint32_t streams_size = (uint32_t)hdr->stream_count * (uint32_t)sizeof(NtStreamDesc);
    const uint8_t *vertex_data = data + sizeof(NtMeshAssetHeader) + streams_size;
    const uint8_t *index_data = vertex_data + hdr->vertex_data_size;

    /* Wire → GPU decode through the shared staging buffer (activation is the
       amortized load path; the same buffer then serves the index decode --
       make_buffer has copied the vertices to the GPU by that point). */
    const uint8_t *gpu_vertex_data = vertex_data;
    if (hdr->vertex_wire == NT_MESH_WIRE_VTX_SOA && hdr->vertex_data_size > 0) {
        uint8_t *soa_tmp = mesh_stage_acquire(hdr->vertex_data_size);
        if (!soa_tmp) {
            NT_LOG_ERROR("activate_mesh: SOA decode alloc failed (%u bytes)", hdr->vertex_data_size);
            return 0;
        }
        const NtStreamDesc *streams = (const NtStreamDesc *)(data + sizeof(NtMeshAssetHeader));
        uint32_t elem_sizes[NT_MESH_MAX_STREAMS];
        for (uint8_t i = 0; i < hdr->stream_count; i++) {
            elem_sizes[i] = nt_stream_type_size(streams[i].type) * streams[i].count;
        }
        if (!nt_meshwire_reinterleave(soa_tmp, vertex_data, hdr->vertex_count, elem_sizes, hdr->stream_count)) {
            NT_LOG_ERROR("activate_mesh: SOA re-interleave failed");
            return 0;
        }
        gpu_vertex_data = soa_tmp;
    }

#ifdef NT_TEST_ACCESS
    s_test_last_mesh_vertex_hash = (hdr->vertex_data_size > 0) ? nt_hash32(gpu_vertex_data, hdr->vertex_data_size).value : 0;
#endif
    nt_buffer_t vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = gpu_vertex_data,
        .size = hdr->vertex_data_size,
        .label = NULL,
    });
    if (vbo.id == 0) {
        NT_LOG_ERROR("activate_mesh: VBO creation failed");
        return 0;
    }

    nt_buffer_t ibo;
    if (!mesh_make_ibo(hdr, index_data, &ibo)) {
        nt_gfx_destroy_buffer(vbo);
        return 0;
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
    s_gfx.mesh_table[slot].layout_hash = nt_hash64(src_streams, (uint32_t)hdr->stream_count * (uint32_t)sizeof(NtStreamDesc)).value;

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
    if (hdr->version != NT_SHADER_CODE_VERSION) {
        NT_LOG_ERROR("activate_shader: version %u != %u -- rebuild packs", (uint32_t)hdr->version, (uint32_t)NT_SHADER_CODE_VERSION);
        return 0;
    }
    if (hdr->stage > NT_SHADER_STAGE_FRAGMENT) {
        NT_LOG_ERROR("activate_shader: invalid stage");
        return 0;
    }
    /* 64-bit sum: a corrupt code_size must not wrap the truncation check into a pass */
    if ((uint64_t)sizeof(NtShaderCodeHeader) + hdr->code_size > size) {
        NT_LOG_ERROR("activate_shader: blob truncated");
        return 0;
    }
    /* The source is consumed as a C string -- the builder always writes the trailing NUL */
    if (hdr->code_size == 0 || data[sizeof(NtShaderCodeHeader) + hdr->code_size - 1] != 0) {
        NT_LOG_ERROR("activate_shader: source not NUL-terminated");
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
