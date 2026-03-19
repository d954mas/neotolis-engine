#ifndef NT_MATERIAL_COMP_H
#define NT_MATERIAL_COMP_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"
#include "material/nt_material.h"

typedef struct {
    uint16_t capacity;
} nt_material_comp_desc_t;

/* ---- Defaults ---- */

static inline nt_material_comp_desc_t nt_material_comp_desc_defaults(void) {
    return (nt_material_comp_desc_t){
        .capacity = 256,
    };
}

nt_result_t nt_material_comp_init(const nt_material_comp_desc_t *desc);
void nt_material_comp_shutdown(void);

bool nt_material_comp_add(nt_entity_t entity);
bool nt_material_comp_has(nt_entity_t entity);
void nt_material_comp_remove(nt_entity_t entity);

nt_material_t *nt_material_comp_handle(nt_entity_t entity);

#endif /* NT_MATERIAL_COMP_H */
