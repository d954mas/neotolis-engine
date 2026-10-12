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
    NT_ASSERT(*count < cap && "pipeline cache exhausted -- raise this renderer's cap (NT_*_RENDERER_MAX_PIPELINES)");
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

#endif /* NT_RENDERER_SHARED_H */
