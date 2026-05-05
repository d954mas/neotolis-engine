#ifndef NT_TRANSFORM_COMP_H
#define NT_TRANSFORM_COMP_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"
#include "math/nt_math.h"

/* ---- Descriptor ---- */

typedef struct {
    uint16_t capacity;
} nt_transform_comp_desc_t;

/* ---- Defaults ---- */

static inline nt_transform_comp_desc_t nt_transform_comp_desc_defaults(void) {
    return (nt_transform_comp_desc_t){
        .capacity = 256,
    };
}

/* ---- Lifecycle ---- */

nt_result_t nt_transform_comp_init(const nt_transform_comp_desc_t *desc);
void nt_transform_comp_shutdown(void);

/* ---- Per-entity operations ---- */

bool nt_transform_comp_add(nt_entity_t entity);
bool nt_transform_comp_has(nt_entity_t entity);
void nt_transform_comp_remove(nt_entity_t entity);

/* ---- Field access (returns pointer into dense array, valid until next add/remove) ---- */

float *nt_transform_comp_position(nt_entity_t entity); /* vec3 */
float *nt_transform_comp_rotation(nt_entity_t entity); /* vec4 quaternion */
float *nt_transform_comp_scale(nt_entity_t entity);    /* vec3 */
bool *nt_transform_comp_dirty(nt_entity_t entity);
const float *nt_transform_comp_world_matrix(nt_entity_t entity); /* mat4, read-only */

/* ---- System ---- */

void nt_transform_comp_update(void);

/* ---- Bulk SoA view (read-only) ----
 *
 * For renderers / systems that iterate many entities per frame and want to
 * skip the per-entity accessor overhead. Returns sparse_indices (entity_idx
 * -> dense_idx) plus dense world_matrix array. Pointers are stable for the
 * module lifetime; values shift on add/remove (swap-and-pop). */

typedef struct {
    uint16_t count;
    const uint16_t *sparse_indices;    /* entity_index -> dense_idx; NT_INVALID_COMP_INDEX if absent */
    const float (*world_matrices)[16]; /* dense_idx -> mat4 (16 floats) */
} nt_transform_comp_view_t;

nt_transform_comp_view_t nt_transform_comp_view(void);

#endif /* NT_TRANSFORM_COMP_H */
