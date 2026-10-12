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
static inline nt_pipeline_t nt_renderer_pipeline_cache_insert(nt_renderer_pipeline_entry_t *entries, uint16_t *count, uint16_t cap, const nt_gfx_pipeline_key_t *key, const nt_pipeline_desc_t *desc) {
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
    return pip;
}

/* Every declared vec4 param. Uniforms are program state; sampler units are fixed at link and
 * nobody writes them. */
static inline void nt_renderer_set_material_uniforms(const nt_material_info_t *mi) {
    for (uint8_t p = 0; p < mi->param_count; p++) {
        nt_gfx_set_uniform_vec4((nt_hash32_t){.value = mi->param_name_hashes[p]}, mi->params[p]);
    }
}

// #region mesh draw
/* Pipeline cache and instance layout of one mesh renderer; vertex inputs belong to the meshes. */
typedef struct {
    nt_renderer_pipeline_entry_t *pipelines; /* [max_pipelines] */
    nt_instance_layout_t instance_layout;
    const char *label; /* pipeline fallback label */
    uint16_t max_pipelines;
    uint16_t pipeline_count;
} nt_renderer_mesh_caches_t;

static inline nt_result_t nt_renderer_mesh_caches_init(nt_renderer_mesh_caches_t *c, uint16_t max_pipelines, const nt_vertex_layout_t *instance_layout, const char *label) {
    NT_ASSERT(max_pipelines > 0);
    *c = (nt_renderer_mesh_caches_t){.instance_layout = nt_gfx_make_instance_layout(instance_layout), .label = label, .max_pipelines = max_pipelines};
    c->pipelines = (nt_renderer_pipeline_entry_t *)calloc(max_pipelines, sizeof(nt_renderer_pipeline_entry_t));
    if (c->pipelines == NULL) {
        NT_LOG_ERROR("failed to allocate pipeline cache");
        return NT_ERR_INIT_FAILED;
    }
    return NT_OK;
}

static inline void nt_renderer_mesh_caches_reset(nt_renderer_mesh_caches_t *c) {
    for (uint16_t i = 0; i < c->pipeline_count; i++) {
        nt_gfx_destroy_pipeline(c->pipelines[i].pipeline);
    }
    c->pipeline_count = 0;
}

static inline void nt_renderer_mesh_caches_shutdown(nt_renderer_mesh_caches_t *c) {
    nt_renderer_mesh_caches_reset(c);
    free(c->pipelines);
    c->pipelines = NULL;
}

/* One mesh draw call (a core draw or one draw_list): the instance buffer it draws from, the last
 * resolved run and the material state it applied. Start it with nt_renderer_mesh_draw_begin; it
 * never outlives the call, because a pass or a foreign pipeline between calls invalidates what it
 * remembers. */
typedef struct {
    nt_material_t material;
    nt_mesh_t mesh;
    nt_pipeline_t pipeline;
    nt_vertex_input_t vertex_input;
    nt_instance_layout_t instance_layout;
    nt_buffer_t instances;
    uint32_t applied_material;
    uint32_t applied_supplied;
} nt_renderer_mesh_draw_t;

static inline nt_renderer_mesh_draw_t nt_renderer_mesh_draw_begin(const nt_renderer_mesh_caches_t *c, nt_buffer_t instances) {
    return (nt_renderer_mesh_draw_t){.instance_layout = c->instance_layout, .instances = instances};
}

/* The pipeline is program x render state; layouts live on the meshes' vertex inputs. 0 = create failed. */
static inline nt_pipeline_t nt_renderer_mesh_pipeline(nt_renderer_mesh_caches_t *c, const nt_material_info_t *mi) {
    const nt_pipeline_desc_t desc = nt_renderer_material_pipeline_desc(mi, c->label);
    const nt_gfx_pipeline_key_t key = nt_gfx_pipeline_key(&desc);
    const nt_pipeline_t found = nt_renderer_pipeline_cache_find(c->pipelines, c->pipeline_count, &key);
    return found.id != 0 ? found : nt_renderer_pipeline_cache_insert(c->pipelines, &c->pipeline_count, c->max_pipelines, &key, &desc);
}

/* Resolves pipeline and vertex input, reusing the previous run's on equal handles. False skips
 * the run: the program is not ready (linking or lost) or a create failed (retried by the next
 * run). The mesh's vertex input for this call's buffer does not depend on the material. */
static inline bool nt_renderer_mesh_resolve(nt_renderer_mesh_caches_t *c, nt_renderer_mesh_draw_t *d, nt_material_t material, const nt_material_info_t *mi, nt_mesh_t mesh) {
    if (!nt_gfx_program_ready(mi->program)) {
        return false;
    }
    if (material.id != d->material.id) {
        d->pipeline = nt_renderer_mesh_pipeline(c, mi);
    }
    if (mesh.id != d->mesh.id || d->vertex_input.id == 0) {
        d->vertex_input = nt_gfx_mesh_vertex_input(mesh, d->instances);
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
    nt_gfx_bind_vertex_input_instanced(d->vertex_input, d->instance_layout, d->instances, offset);
    if (mesh_info->index_count > 0) {
        nt_gfx_draw_indexed_instanced(0, mesh_info->index_count, mesh_info->vertex_count, count);
    } else {
        nt_gfx_draw_instanced(0, mesh_info->vertex_count, count);
    }
}
// #endregion

#endif /* NT_RENDERER_SHARED_H */
