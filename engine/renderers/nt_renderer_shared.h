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

/* A note's material field: its label, or its handle as material_id when it has none, so unlabeled
 * materials stay apart. */
static inline nt_gfx_note_field_t nt_renderer_note_material(const nt_material_info_t *material, nt_material_t handle) {
    return material->label != NULL ? NT_NOTE_STR("material", material->label) : NT_NOTE_U32("material_id", handle.id);
}

#endif /* NT_RENDERER_SHARED_H */
