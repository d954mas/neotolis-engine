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
    NT_ASSERT(*count < cap && "pipeline cache exhausted -- raise this renderer's cap (desc.max_pipelines or NT_*_RENDERER_MAX_PIPELINES)");
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
_Static_assert(NT_MESH_MAX_STREAMS *NT_RENDERER_MESH_VI_KEY_STREAM_BITS + 2 <= 64, "mesh VI key overflows uint64");
_Static_assert(NT_COLOR_MODE_FLOAT4 < 4, "mesh VI key packs color_mode in 2 bits");

typedef struct {
    uint64_t key;
    nt_vertex_input_t vi;
    uint32_t last_mat; /* most recently resolved material for this VI version */
} nt_renderer_mesh_vi_version_t;

typedef struct {
    nt_renderer_mesh_vi_version_t *versions;
    nt_mesh_t *meshes; /* row ownership includes the mesh generation */
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
        nt_gfx_destroy_vertex_input(cache->versions[i].vi);
        cache->versions[i] = (nt_renderer_mesh_vi_version_t){0};
    }
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
        if (nt_gfx_vertex_input_valid(cache->versions[i].vi)) {
            live++;
        }
    }
    return live;
}

/* The mesh fixes stream types/offsets/stride, so the exact row key needs only
 * presence + location per stream and color mode. Unmapped streams disappear. */
static inline nt_vertex_layout_t nt_renderer_build_mesh_vertex_layout(const nt_material_info_t *mat_info, const nt_gfx_mesh_info_t *mesh_info, uint64_t *out_key) {
    nt_vertex_layout_t layout;
    memset(&layout, 0, sizeof(layout));
    layout.stride = mesh_info->stride;
    uint64_t key = (uint64_t)mat_info->color_mode << (NT_MESH_MAX_STREAMS * NT_RENDERER_MESH_VI_KEY_STREAM_BITS);

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
                                                                         const nt_gfx_mesh_info_t *mesh_info, const nt_vertex_layout_t *instance_layouts, const char *label) {
    const uint32_t slot = nt_pool_slot_index(mesh.id);
    NT_ASSERT(slot != 0 && slot <= cache->mesh_capacity);
    nt_renderer_mesh_vi_version_t *row = &cache->versions[(size_t)(slot - 1) * cache->max_layouts];
    if (cache->meshes[slot - 1].id != mesh.id) {
        /* Bufferless vertex inputs have no buffer-destroy cascade hook. */
        for (uint16_t i = 0; i < cache->max_layouts; i++) {
            nt_gfx_destroy_vertex_input(row[i].vi);
            row[i] = (nt_renderer_mesh_vi_version_t){0};
        }
        cache->meshes[slot - 1] = mesh;
    }
    /* The generational material id pins attr_map + color mode. */
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

    const nt_vertex_input_t vi = nt_gfx_make_vertex_input(&(nt_vertex_input_desc_t){
        .layout = layout,
        .instance_layout = instance_layouts[mat_info->color_mode],
        /* Empty derived layouts support attribute-less gl_VertexID shaders. */
        .vertex_buffer = (layout.attr_count > 0) ? mesh_info->vbo : (nt_buffer_t){0},
        .index_buffer = mesh_info->ibo,
        .label = label,
    });
    if (vi.id == 0) {
        return vi; /* backend/context failure stays uncached so the next miss retries */
    }
    reusable->key = key;
    reusable->vi = vi;
    reusable->last_mat = mat.id;
    return vi;
}

// #endregion

// #region bound state
/* Owns its resolved texture ids by value; hashes, samplers and params are borrowed from
 * nt_material_info_t, or from a sprite cmd whose slot 0 is the atlas page.
 * Hashes are captured so a cmd whose material died still replays its sampler units. */
typedef struct {
    uint8_t tex_count;
    const uint32_t *tex_name_hashes;                 /* [tex_count] */
    uint32_t resolved_tex[NT_MATERIAL_MAX_TEXTURES]; /* [tex_count]; 0 = unresolved */
    const nt_sampler_t *tex_samplers;                /* [tex_count]; .id == 0 = texture default */
    uint8_t param_count;
    const uint32_t *param_name_hashes; /* [param_count] */
    const float (*params)[4];
} nt_renderer_material_view_t;

/* Zero-init = nothing bound; lives for ONE draw_list or flush. Material uniforms replay on
 * a material change or a pipeline change. Texture and sampler binds are deduplicated by the
 * GL backend, so the renderer tracks only what it replays itself. */
typedef struct {
    uint32_t pipeline;
    uint32_t vertex_input;
    uint32_t material;
} nt_renderer_bound_t;

static inline void nt_renderer_bind_pipeline(nt_renderer_bound_t *b, nt_pipeline_t p) {
    if (p.id == b->pipeline) {
        return;
    }
    nt_gfx_bind_pipeline(p);
    b->pipeline = p.id;
    /* Uniforms are program state and the new pipeline may sit on another program,
     * so the same material has to write them again. */
    b->material = 0;
}

static inline void nt_renderer_bind_vertex_input(nt_renderer_bound_t *b, nt_vertex_input_t vi) {
    if (vi.id == b->vertex_input) {
        return;
    }
    nt_gfx_bind_vertex_input(vi);
    b->vertex_input = vi.id;
}

/* Stateless program state: every vec4 param. Sampler units are fixed at link and
 * nobody writes them. */
static inline void nt_renderer_set_material_uniforms(const nt_renderer_material_view_t *v) {
    for (uint8_t p = 0; p < v->param_count; p++) {
        nt_gfx_set_uniform_vec4((nt_hash32_t){.value = v->param_name_hashes[p]}, v->params[p]);
    }
}

static inline void nt_renderer_apply_material_uniforms(nt_renderer_bound_t *b, uint32_t material_id, const nt_renderer_material_view_t *v) {
    if (material_id == b->material) {
        return;
    }
    nt_renderer_set_material_uniforms(v);
    b->material = material_id;
}

/* Semantic set: gfx ignores inactive names, validates active coverage, then
 * resolves every texture and sampler before issuing backend binds. */
static inline void nt_renderer_apply_texture_slots(const nt_renderer_material_view_t *v) {
    nt_gfx_texture_binding_t bindings[NT_MATERIAL_MAX_TEXTURES];
    for (uint8_t t = 0; t < v->tex_count; t++) {
        bindings[t] = (nt_gfx_texture_binding_t){
            .name = {.value = v->tex_name_hashes[t]},
            .texture = {.id = v->resolved_tex[t]},
            .sampler = v->tex_samplers[t],
        };
    }
    nt_gfx_apply_texture_bindings(bindings, v->tex_count);
}

static inline nt_renderer_material_view_t nt_renderer_material_view(const nt_material_info_t *mi) {
    nt_renderer_material_view_t view = {
        .tex_count = mi->tex_count,
        .tex_name_hashes = mi->tex_name_hashes,
        .tex_samplers = mi->tex_samplers,
        .param_count = mi->param_count,
        .param_name_hashes = mi->param_name_hashes,
        .params = mi->params,
    };
    for (uint8_t t = 0; t < mi->tex_count; t++) {
        view.resolved_tex[t] = nt_resource_get(mi->tex_resources[t]);
    }
    return view;
}
// #endregion

/* Warn once to explain skipped draws without per-frame spam; pipeline insertion re-arms the flag. */
static inline void nt_renderer_warn_program_not_ready(bool *warned, const nt_material_info_t *mat_info) {
    if (*warned) {
        return;
    }
    NT_LOG_WARN("skipping '%s': its program is not ready -- assign one with nt_material_set_program, and after a context loss invalidate NT_ASSET_SHADER_CODE so the stages come back",
                (mat_info != NULL && mat_info->label != NULL) ? mat_info->label : "(unlabeled)");
    *warned = true;
}

#endif /* NT_RENDERER_SHARED_H */
