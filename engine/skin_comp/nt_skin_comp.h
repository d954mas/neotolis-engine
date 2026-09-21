#ifndef NT_SKIN_COMP_H
#define NT_SKIN_COMP_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"
#include "skeletal_gpu/nt_skeletal_gpu.h"

/* One by-value deformation binding per skinned mesh entity. The game rewrites
 * it every frame after reserving the frame (skeletal spec, GPU preparation);
 * the component owns nothing — the texture belongs to skeletal_gpu or a bank. */

typedef struct {
    uint16_t capacity;
} nt_skin_comp_desc_t;

/* ---- Defaults ---- */

static inline nt_skin_comp_desc_t nt_skin_comp_desc_defaults(void) {
    return (nt_skin_comp_desc_t){
        .capacity = 256,
    };
}

nt_result_t nt_skin_comp_init(const nt_skin_comp_desc_t *desc);
void nt_skin_comp_shutdown(void);

/* A fresh component holds the zero binding; drawing it is the renderer's assert. */
bool nt_skin_comp_add(nt_entity_t entity);
bool nt_skin_comp_has(nt_entity_t entity);
void nt_skin_comp_remove(nt_entity_t entity);

/* Caller MUST call nt_skin_comp_has() first — traps if entity has no component */
nt_deformation_binding_t *nt_skin_comp_handle(nt_entity_t entity);

#endif /* NT_SKIN_COMP_H */
