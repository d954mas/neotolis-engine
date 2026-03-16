#include "material_comp/nt_material_comp.h"

#include "comp_storage/nt_comp_storage.h"

static nt_comp_storage_t s_storage;

/* ---- Default initializer ---- */

static void material_default(void *comp) {
    nt_material_comp_t *m = (nt_material_comp_t *)comp;
    m->material_handle = 0;
}

/* ---- Destroy callback ---- */

static void material_on_destroy(nt_entity_t entity) {
    if (nt_comp_storage_has(&s_storage, entity)) {
        nt_comp_storage_remove(&s_storage, entity);
    }
}

/* ---- Lifecycle ---- */

nt_result_t nt_material_comp_init(const nt_material_comp_desc_t *desc) {
    if (!desc || desc->capacity == 0) {
        return NT_ERR_INVALID_ARG;
    }

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, sizeof(nt_material_comp_t), material_default);
    if (res != NT_OK) {
        return res;
    }

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "material",
        .has = nt_material_comp_has,
        .on_destroy = material_on_destroy,
    });

    return NT_OK;
}

void nt_material_comp_shutdown(void) { nt_comp_storage_shutdown(&s_storage); }

/* ---- Typed wrappers ---- */

nt_material_comp_t *nt_material_comp_add(nt_entity_t entity) { return (nt_material_comp_t *)nt_comp_storage_add(&s_storage, entity); }

nt_material_comp_t *nt_material_comp_get(nt_entity_t entity) { return (nt_material_comp_t *)nt_comp_storage_get(&s_storage, entity); }

bool nt_material_comp_has(nt_entity_t entity) { return nt_comp_storage_has(&s_storage, entity); }

void nt_material_comp_remove(nt_entity_t entity) { nt_comp_storage_remove(&s_storage, entity); }
