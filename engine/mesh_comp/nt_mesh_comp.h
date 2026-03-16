#ifndef NT_MESH_COMP_H
#define NT_MESH_COMP_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"

typedef struct {
    uint32_t mesh_handle; /* opaque mesh asset reference (typed handle in Phase 24+) */
} nt_mesh_comp_t;

typedef struct {
    uint16_t capacity;
} nt_mesh_comp_desc_t;

nt_result_t nt_mesh_comp_init(const nt_mesh_comp_desc_t *desc);
void nt_mesh_comp_shutdown(void);
nt_mesh_comp_t *nt_mesh_comp_add(nt_entity_t entity);
nt_mesh_comp_t *nt_mesh_comp_get(nt_entity_t entity);
bool nt_mesh_comp_has(nt_entity_t entity);
void nt_mesh_comp_remove(nt_entity_t entity);

#endif /* NT_MESH_COMP_H */
