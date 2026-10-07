#ifndef NT_RENDERER_SHARED_H
#define NT_RENDERER_SHARED_H

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "pool/nt_pool.h"
#include "resource/nt_resource.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Internal to the renderers -- not a public header, not installed. */

/* The key is the exact desc identity (nt_gfx_pipeline_key); a hit needs no desc compare. */
typedef struct {
    nt_gfx_pipeline_key_t key;
    nt_pipeline_t pipeline;
} nt_renderer_pipeline_entry_t;

static inline nt_pipeline_desc_t nt_renderer_material_pipeline_desc(const nt_material_info_t *material, const char *fallback_label) {
    return (nt_pipeline_desc_t){
        .program = material->program,
        .depth_test = material->depth_test,
        .depth_write = material->depth_write,
        .depth_func = NT_DEPTH_LESS,
        .blend = material->blend,
        .cull_mode = (uint8_t)material->cull_mode,
        .label = material->label != NULL ? material->label : fallback_label,
    };
}

/* Validate matched pipelines because program generations can wrap; dead matches return invalid.
 * Misses also return invalid. Cleanup is deferred to insertion. */
static inline nt_pipeline_t nt_renderer_pipeline_cache_find(const nt_renderer_pipeline_entry_t *entries, uint16_t count, const nt_gfx_pipeline_key_t *key) {
    for (uint16_t i = 0; i < count; i++) {
        if (nt_gfx_pipeline_key_equal(&entries[i].key, key)) {
            return nt_gfx_pipeline_valid(entries[i].pipeline) ? entries[i].pipeline : (nt_pipeline_t){0};
        }
    }
    return (nt_pipeline_t){0};
}

/* Reap dead entries before checking capacity; exhaustion asserts.
 * Leave failed pipeline creation uncached so a later miss retries. */
static inline nt_pipeline_t nt_renderer_pipeline_cache_insert(nt_renderer_pipeline_entry_t *entries, uint16_t *count, uint16_t cap, const nt_gfx_pipeline_key_t *key, const nt_pipeline_desc_t *desc,
                                                              bool *warned) {
    for (uint16_t i = 0; i < *count;) {
        if (!nt_gfx_pipeline_valid(entries[i].pipeline)) {
            entries[i] = entries[--(*count)];
            continue;
        }
        i++;
    }
    /* TRAP omits assert text; the log identifies the renderer that exhausted its cache. */
    if (*count >= cap) {
        NT_LOG_ERROR("pipeline cache exhausted building '%s' -- raise that renderer's cap", (desc->label != NULL) ? desc->label : "(unlabeled)");
    }
    NT_ASSERT(*count < cap && "pipeline cache exhausted -- raise this renderer's cap (its desc max_pipelines or NT_*_RENDERER_MAX_PIPELINES)");
    nt_pipeline_t pip = nt_gfx_make_pipeline(desc);
    if (pip.id == 0) {
        return pip;
    }
    entries[*count].key = *key;
    entries[*count].pipeline = pip;
    (*count)++;
    *warned = false;
    return pip;
}

// #region mesh vertex-input cache

#define NT_RENDERER_MESH_VI_KEY_STREAM_BITS 5
_Static_assert(NT_GFX_MAX_VERTEX_ATTRS <= 16, "mesh VI key packs a location in 4 bits");
_Static_assert(NT_MESH_MAX_STREAMS *NT_RENDERER_MESH_VI_KEY_STREAM_BITS <= 64, "mesh VI key overflows uint64");

typedef struct {
    uint64_t key;
    nt_vertex_input_t vi;
    uint32_t last_mat; /* most recently resolved material for this VI version */
} nt_renderer_mesh_vi_version_t;

typedef struct {
    nt_renderer_mesh_vi_version_t *versions;
    nt_mesh_t *meshes; /* row ownership includes the mesh generation */
    /* No VBO and no IBO: the one version the destroy-buffer cascade cannot reach. It holds
     * nothing mesh-specific, so rows share it and a row reset never has to destroy. */
    nt_vertex_input_t bufferless;
    uint16_t max_layouts;
    uint16_t mesh_capacity;
} nt_renderer_mesh_vi_cache_t;

static inline nt_vertex_type_t nt_renderer_stream_to_vertex_type(uint8_t type) {
    switch (type) {
    case NT_STREAM_UINT8:
        return NT_VERTEX_UINT8;
    case NT_STREAM_INT8:
        return NT_VERTEX_INT8;
    case NT_STREAM_UINT16:
        return NT_VERTEX_UINT16;
    case NT_STREAM_INT16:
        return NT_VERTEX_INT16;
    case NT_STREAM_FLOAT16:
        return NT_VERTEX_HALF;
    case NT_STREAM_FLOAT32:
        return NT_VERTEX_FLOAT;
    default:
        NT_LOG_ERROR("unmapped stream type in vertex layout");
        return NT_VERTEX_FLOAT;
    }
}

static inline nt_result_t nt_renderer_mesh_vi_cache_init(nt_renderer_mesh_vi_cache_t *cache, uint16_t max_layouts) {
    NT_ASSERT(cache != NULL);
    NT_ASSERT(max_layouts > 0);
    memset(cache, 0, sizeof(*cache));
    cache->max_layouts = max_layouts;
    cache->mesh_capacity = nt_gfx_max_meshes();
    NT_ASSERT(cache->mesh_capacity > 0 && "mesh VI cache init requires nt_gfx_init first");

    cache->versions = (nt_renderer_mesh_vi_version_t *)calloc((size_t)cache->mesh_capacity * max_layouts, sizeof(nt_renderer_mesh_vi_version_t));
    cache->meshes = (nt_mesh_t *)calloc(cache->mesh_capacity, sizeof(nt_mesh_t));
    if (cache->versions == NULL || cache->meshes == NULL) {
        free(cache->meshes);
        free(cache->versions);
        memset(cache, 0, sizeof(*cache));
        NT_LOG_ERROR("failed to allocate mesh vertex-input cache");
        return NT_ERR_INIT_FAILED;
    }
    return NT_OK;
}

static inline void nt_renderer_mesh_vi_cache_reset(nt_renderer_mesh_vi_cache_t *cache) {
    if (cache->versions == NULL) {
        return;
    }
    const size_t count = (size_t)cache->mesh_capacity * cache->max_layouts;
    for (size_t i = 0; i < count; i++) {
        if (cache->versions[i].vi.id != cache->bufferless.id) {
            nt_gfx_destroy_vertex_input(cache->versions[i].vi);
        }
        cache->versions[i] = (nt_renderer_mesh_vi_version_t){0};
    }
    nt_gfx_destroy_vertex_input(cache->bufferless);
    cache->bufferless = NT_VERTEX_INPUT_INVALID;
    memset(cache->meshes, 0, (size_t)cache->mesh_capacity * sizeof(nt_mesh_t));
}

static inline void nt_renderer_mesh_vi_cache_shutdown(nt_renderer_mesh_vi_cache_t *cache) {
    nt_renderer_mesh_vi_cache_reset(cache);
    free(cache->meshes);
    free(cache->versions);
    memset(cache, 0, sizeof(*cache));
}

static inline uint32_t nt_renderer_mesh_vi_cache_live_count(const nt_renderer_mesh_vi_cache_t *cache) {
    uint32_t live = 0;
    const size_t count = (size_t)cache->mesh_capacity * cache->max_layouts;
    for (size_t i = 0; i < count; i++) {
        if (cache->versions[i].vi.id != cache->bufferless.id && nt_gfx_vertex_input_valid(cache->versions[i].vi)) {
            live++;
        }
    }
    return live + (nt_gfx_vertex_input_valid(cache->bufferless) ? 1U : 0U);
}

/* The mesh fixes stream types/offsets/stride, so the exact row key needs only
 * presence + location per stream. Unmapped streams disappear. */
static inline nt_vertex_layout_t nt_renderer_build_mesh_vertex_layout(const nt_material_info_t *mat_info, const nt_gfx_mesh_info_t *mesh_info, uint64_t *out_key) {
    nt_vertex_layout_t layout;
    memset(&layout, 0, sizeof(layout));
    layout.stride = mesh_info->stride;
    uint64_t key = 0;

    uint16_t offset = 0;
    for (uint8_t si = 0; si < mesh_info->stream_count; si++) {
        const NtStreamDesc *stream = &mesh_info->streams[si];
        uint8_t location = 0;
        bool found = false;
        for (uint8_t ai = 0; ai < mat_info->attr_map_count; ai++) {
            if (mat_info->attr_map_hashes[ai] == stream->name_hash) {
                location = mat_info->attr_map_locations[ai];
                found = true;
                break;
            }
        }
        if (found) {
            NT_ASSERT(layout.attr_count < NT_GFX_MAX_VERTEX_ATTRS);
            key |= (1ULL | (uint64_t)location << 1) << (si * NT_RENDERER_MESH_VI_KEY_STREAM_BITS);
            layout.attrs[layout.attr_count] = (nt_vertex_attr_t){
                .location = location,
                .type = nt_renderer_stream_to_vertex_type(stream->type),
                .count = stream->count,
                .normalized = stream->normalized != 0,
                .offset = offset,
            };
            layout.attr_count++;
        }
        offset += (uint16_t)(nt_stream_type_size(stream->type) * stream->count);
    }
    *out_key = key;
    return layout;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static inline nt_vertex_input_t nt_renderer_mesh_vi_cache_find_or_create(nt_renderer_mesh_vi_cache_t *cache, nt_material_t mat, nt_mesh_t mesh, const nt_material_info_t *mat_info,
                                                                         const nt_gfx_mesh_info_t *mesh_info, const nt_vertex_layout_t *instance_layout, const char *label) {
    const uint32_t slot = nt_pool_slot_index(mesh.id);
    NT_ASSERT(slot != 0 && slot <= cache->mesh_capacity);
    nt_renderer_mesh_vi_version_t *row = &cache->versions[(size_t)(slot - 1) * cache->max_layouts];
    if (cache->meshes[slot - 1].id != mesh.id) {
        /* The old mesh's buffer destroys took its own versions; the shared bufferless one stays. */
        memset(row, 0, sizeof(*row) * cache->max_layouts);
        cache->meshes[slot - 1] = mesh;
    }
    /* The generational material id pins attr_map. */
    for (uint16_t i = 0; i < cache->max_layouts; i++) {
        if (row[i].last_mat == mat.id && nt_gfx_vertex_input_valid(row[i].vi)) {
            return row[i].vi;
        }
    }

    uint64_t key = 0;
    const nt_vertex_layout_t layout = nt_renderer_build_mesh_vertex_layout(mat_info, mesh_info, &key);
    nt_renderer_mesh_vi_version_t *reusable = NULL;
    for (uint16_t i = 0; i < cache->max_layouts; i++) {
        nt_renderer_mesh_vi_version_t *entry = &row[i];
        const bool live = nt_gfx_vertex_input_valid(entry->vi);
        if (entry->key == key && live) {
            entry->last_mat = mat.id;
            return entry->vi;
        }
        if (reusable == NULL && !live) {
            reusable = entry;
        }
    }
    if (reusable == NULL) {
        NT_LOG_ERROR("%s vertex-input versions exhausted -- raise max_mesh_layouts", label != NULL ? label : "mesh renderer");
    }
    /* Crash instead of hiding VAO churn behind version eviction. */
    NT_ASSERT(reusable != NULL && "mesh vertex-input versions exhausted -- raise renderer max_mesh_layouts");
    if (reusable == NULL) {
        return NT_VERTEX_INPUT_INVALID;
    }

    nt_vertex_input_t vi;
    if (layout.attr_count == 0 && mesh_info->ibo.id == 0) {
        /* Empty derived layouts support attribute-less gl_VertexID shaders. */
        if (!nt_gfx_vertex_input_valid(cache->bufferless)) {
            cache->bufferless = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){.instance_layout = *instance_layout, .label = label});
        }
        vi = cache->bufferless;
    } else {
        vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
            .layout = layout,
            .instance_layout = *instance_layout,
            .vertex_buffer = (layout.attr_count > 0) ? mesh_info->vbo : (nt_buffer_t){0},
            .index_buffer = mesh_info->ibo,
            .label = label,
        });
    }
    if (vi.id == 0) {
        return vi; /* backend/context failure stays uncached so the next miss retries */
    }
    reusable->key = key;
    reusable->vi = vi;
    reusable->last_mat = mat.id;
    return vi;
}

// #endregion

/* Every declared vec4 param. Uniforms are program state; sampler units are fixed at link and
 * nobody writes them. */
static inline void nt_renderer_set_material_uniforms(const nt_material_info_t *mi) {
    for (uint8_t p = 0; p < mi->param_count; p++) {
        nt_gfx_set_uniform_vec4((nt_hash32_t){.value = mi->param_name_hashes[p]}, mi->params[p]);
    }
}

/* Warn once to explain skipped draws without per-frame spam; pipeline insertion re-arms the flag. */
static inline void nt_renderer_warn_program_not_ready(bool *warned, const nt_material_info_t *mat_info) {
    if (*warned) {
        return;
    }
    NT_LOG_WARN("skipping '%s': its program is not ready -- assign one with nt_material_set_program, and after a context loss invalidate NT_ASSET_SHADER_CODE so the stages come back",
                (mat_info != NULL && mat_info->label != NULL) ? mat_info->label : "(unlabeled)");
    *warned = true;
}

// #region mesh draw
/* Pipeline and vertex-input caches of one mesh renderer. */
typedef struct {
    nt_renderer_pipeline_entry_t *pipelines; /* [max_pipelines] */
    nt_renderer_mesh_vi_cache_t vi_cache;
    const nt_vertex_layout_t *instance_layout;
    const char *label; /* pipeline fallback and vertex-input label */
    uint16_t max_pipelines;
    uint16_t pipeline_count;
    /* One-shot so a load-time skip does not spam; re-armed when a pipeline is built. */
    bool warned_program_not_ready;
} nt_renderer_mesh_caches_t;

static inline nt_result_t nt_renderer_mesh_caches_init(nt_renderer_mesh_caches_t *c, uint16_t max_pipelines, uint16_t max_mesh_layouts, const nt_vertex_layout_t *instance_layout, const char *label) {
    NT_ASSERT(max_pipelines > 0);
    *c = (nt_renderer_mesh_caches_t){.instance_layout = instance_layout, .label = label, .max_pipelines = max_pipelines};
    c->pipelines = (nt_renderer_pipeline_entry_t *)calloc(max_pipelines, sizeof(nt_renderer_pipeline_entry_t));
    if (c->pipelines == NULL) {
        NT_LOG_ERROR("failed to allocate pipeline cache");
        return NT_ERR_INIT_FAILED;
    }
    if (nt_renderer_mesh_vi_cache_init(&c->vi_cache, max_mesh_layouts) != NT_OK) {
        free(c->pipelines);
        c->pipelines = NULL;
        return NT_ERR_INIT_FAILED;
    }
    return NT_OK;
}

static inline void nt_renderer_mesh_caches_reset(nt_renderer_mesh_caches_t *c) {
    for (uint16_t i = 0; i < c->pipeline_count; i++) {
        nt_gfx_destroy_pipeline(c->pipelines[i].pipeline);
    }
    c->pipeline_count = 0;
    nt_renderer_mesh_vi_cache_reset(&c->vi_cache);
    c->warned_program_not_ready = false;
}

static inline void nt_renderer_mesh_caches_shutdown(nt_renderer_mesh_caches_t *c) {
    nt_renderer_mesh_caches_reset(c);
    nt_renderer_mesh_vi_cache_shutdown(&c->vi_cache);
    free(c->pipelines);
    c->pipelines = NULL;
}

/* One mesh draw call (a core draw or one draw_list): the last resolved run and the material
 * state it applied. Zero-init; it never outlives the call, because a pass or a foreign
 * pipeline between calls invalidates what it remembers. */
typedef struct {
    nt_material_t material;
    nt_mesh_t mesh;
    nt_pipeline_t pipeline;
    nt_vertex_input_t vertex_input;
    uint32_t applied_material;
    uint32_t applied_supplied;
} nt_renderer_mesh_draw_t;

/* Resolves pipeline and vertex input, reusing the previous run's on equal handles (creating
 * them on a cache miss). False skips the run: the program is not ready (warned once) or a
 * create failed (retried by the next run). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
static inline bool nt_renderer_mesh_resolve(nt_renderer_mesh_caches_t *c, nt_renderer_mesh_draw_t *d, nt_material_t material, const nt_material_info_t *mi, nt_mesh_t mesh,
                                            const nt_gfx_mesh_info_t *mesh_info) {
    if (!nt_gfx_program_ready(mi->program)) {
        nt_renderer_warn_program_not_ready(&c->warned_program_not_ready, mi);
        return false;
    }
    const bool material_changed = material.id != d->material.id;
    if (material_changed) {
        /* Layouts live on the vertex-input versions; the pipeline is program x render state. */
        const nt_pipeline_desc_t desc = nt_renderer_material_pipeline_desc(mi, c->label);
        const nt_gfx_pipeline_key_t key = nt_gfx_pipeline_key(&desc);
        d->pipeline = nt_renderer_pipeline_cache_find(c->pipelines, c->pipeline_count, &key);
        if (d->pipeline.id == 0) {
            d->pipeline = nt_renderer_pipeline_cache_insert(c->pipelines, &c->pipeline_count, c->max_pipelines, &key, &desc, &c->warned_program_not_ready);
        }
    }
    /* Vertex-input identity is (mesh row, material-derived layout): a mesh change re-resolves too. */
    if (material_changed || mesh.id != d->mesh.id) {
        d->vertex_input = (d->pipeline.id != 0) ? nt_renderer_mesh_vi_cache_find_or_create(&c->vi_cache, material, mesh, mi, mesh_info, c->instance_layout, c->label) : NT_VERTEX_INPUT_INVALID;
    }
    if (d->pipeline.id == 0 || d->vertex_input.id == 0) {
        d->material = (nt_material_t){0};
        d->mesh = (nt_mesh_t){0};
        return false;
    }
    d->material = material;
    d->mesh = mesh;
    return true;
}

/* Pipeline first: uniforms and the texture set land on its program. A nonzero supplied texture
 * replaces the declaration named supplied_name. */
static inline void nt_renderer_mesh_record(nt_renderer_mesh_draw_t *d, const nt_material_info_t *mi, const nt_gfx_mesh_info_t *mesh_info, uint32_t supplied_name, nt_texture_t supplied,
                                           uint32_t offset, uint32_t count) {
    nt_gfx_bind_pipeline(d->pipeline);
    const bool material_changed = d->material.id != d->applied_material;
    if (material_changed) {
        for (uint8_t p = 0; p < mi->param_count; p++) {
            nt_gfx_set_uniform_vec4((nt_hash32_t){.value = mi->param_name_hashes[p]}, mi->params[p]);
        }
    }
    if (material_changed || supplied.id != d->applied_supplied) {
        nt_gfx_texture_binding_t bindings[NT_MATERIAL_MAX_TEXTURES];
        for (uint8_t t = 0; t < mi->tex_count; t++) {
            const bool replaced = supplied.id != 0 && mi->tex_name_hashes[t] == supplied_name;
            bindings[t] = (nt_gfx_texture_binding_t){
                .name = {.value = mi->tex_name_hashes[t]},
                .texture = replaced ? supplied : (nt_texture_t){.id = nt_resource_get(mi->tex_resources[t])},
                .sampler = replaced ? NT_SAMPLER_DEFAULT : mi->tex_samplers[t],
            };
        }
        nt_gfx_apply_texture_bindings(bindings, mi->tex_count);
    }
    d->applied_material = d->material.id;
    d->applied_supplied = supplied.id;
    nt_gfx_bind_vertex_input(d->vertex_input);
    nt_gfx_bind_instance_buffer(nt_gfx_frame_buffer(NT_GFX_FRAME_VERTEX), offset);
    if (mesh_info->index_count > 0) {
        nt_gfx_draw_indexed_instanced(0, mesh_info->index_count, mesh_info->vertex_count, count);
    } else {
        nt_gfx_draw_instanced(0, mesh_info->vertex_count, count);
    }
}
// #endregion

#endif /* NT_RENDERER_SHARED_H */
