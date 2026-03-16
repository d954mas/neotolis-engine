#ifndef NT_MESH_COMP_H
#define NT_MESH_COMP_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"

typedef struct {
    uint16_t capacity;
} nt_mesh_comp_desc_t;

nt_result_t nt_mesh_comp_init(const nt_mesh_comp_desc_t *desc);
void nt_mesh_comp_shutdown(void);

bool nt_mesh_comp_add(nt_entity_t entity);
bool nt_mesh_comp_has(nt_entity_t entity);
void nt_mesh_comp_remove(nt_entity_t entity);

uint32_t *nt_mesh_comp_handle(nt_entity_t entity); /* opaque mesh asset reference */

#endif /* NT_MESH_COMP_H */
