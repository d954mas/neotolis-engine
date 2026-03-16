#include "mesh_comp/nt_mesh_comp.h"

#include "comp_storage/nt_comp_storage.h"

static nt_comp_storage_t s_storage;

/* ---- Destroy callback ---- */

static void mesh_on_destroy(nt_entity_t entity) {
    if (nt_comp_storage_has(&s_storage, entity)) {
        nt_comp_storage_remove(&s_storage, entity);
    }
}

/* ---- Lifecycle ---- */

nt_result_t nt_mesh_comp_init(const nt_mesh_comp_desc_t *desc) {
    if (!desc || desc->capacity == 0) {
        return NT_ERR_INVALID_ARG;
    }

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, sizeof(nt_mesh_comp_t));
    if (res != NT_OK) {
        return res;
    }

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "mesh",
        .has = nt_mesh_comp_has,
        .on_destroy = mesh_on_destroy,
    });

    return NT_OK;
}

void nt_mesh_comp_shutdown(void) { nt_comp_storage_shutdown(&s_storage); }

/* ---- Typed wrappers ---- */

nt_mesh_comp_t *nt_mesh_comp_add(nt_entity_t entity) { return (nt_mesh_comp_t *)nt_comp_storage_add(&s_storage, entity); }

nt_mesh_comp_t *nt_mesh_comp_get(nt_entity_t entity) { return (nt_mesh_comp_t *)nt_comp_storage_get(&s_storage, entity); }

bool nt_mesh_comp_has(nt_entity_t entity) { return nt_comp_storage_has(&s_storage, entity); }

void nt_mesh_comp_remove(nt_entity_t entity) { nt_comp_storage_remove(&s_storage, entity); }
