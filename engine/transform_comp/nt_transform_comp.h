#ifndef NT_TRANSFORM_COMP_H
#define NT_TRANSFORM_COMP_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"
#include "math/nt_math.h"

/* ---- Sentinel for unused sparse slots ---- */

#define NT_INVALID_COMP_INDEX UINT16_MAX

/* ---- Component data ---- */

typedef struct {
    vec3 local_position;
    vec4 local_rotation; /* quaternion as vec4 (cglm versor) */
    vec3 local_scale;
    mat4 world_matrix;
    bool dirty;
} nt_transform_comp_t;

/* ---- Descriptor ---- */

typedef struct {
    uint16_t capacity;
} nt_transform_comp_desc_t;

/* ---- Public API ---- */

nt_result_t nt_transform_comp_init(const nt_transform_comp_desc_t *desc);
void nt_transform_comp_shutdown(void);

nt_transform_comp_t *nt_transform_comp_add(nt_entity_t entity);
nt_transform_comp_t *nt_transform_comp_get(nt_entity_t entity);
bool nt_transform_comp_has(nt_entity_t entity);
void nt_transform_comp_remove(nt_entity_t entity);

void nt_transform_comp_update(void);

#endif /* NT_TRANSFORM_COMP_H */
