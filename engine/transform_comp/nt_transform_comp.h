#ifndef NT_TRANSFORM_COMP_H
#define NT_TRANSFORM_COMP_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"
#include "math/nt_math.h"

/* ---- Descriptor ---- */

typedef struct {
    uint16_t capacity;
} nt_transform_comp_desc_t;

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

#endif /* NT_TRANSFORM_COMP_H */
