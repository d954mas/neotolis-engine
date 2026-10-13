#ifndef NT_RENDERER_SHARED_H
#define NT_RENDERER_SHARED_H

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "material/nt_material.h"

#include <stdbool.h>
#include <stdint.h>

/* Internal to the renderers -- not a public header, not installed. */

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

/* Every declared vec4 param. Uniforms are program state; sampler units are fixed at link and
 * nobody writes them. */
static inline void nt_renderer_set_material_uniforms(const nt_material_info_t *mi) {
    for (uint8_t p = 0; p < mi->param_count; p++) {
        nt_gfx_set_uniform_vec4((nt_hash32_t){.value = mi->param_name_hashes[p]}, mi->params[p]);
    }
}

#endif /* NT_RENDERER_SHARED_H */
