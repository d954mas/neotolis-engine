#include "material_comp/nt_material_comp.h"

#include <stdlib.h>

#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"

static nt_comp_storage_t s_storage;
static nt_material_t *s_material_handles;

/* ---- Callbacks ---- */

static void material_default(uint16_t idx) { s_material_handles[idx] = NT_MATERIAL_INVALID; }

static void material_swap(uint16_t dst, uint16_t src) { s_material_handles[dst] = s_material_handles[src]; }

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

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, material_default, material_swap);
    if (res != NT_OK) {
        return res;
    }

    s_material_handles = (nt_material_t *)calloc(desc->capacity, sizeof(nt_material_t));
    if (!s_material_handles) {
        nt_comp_storage_shutdown(&s_storage);
        return NT_ERR_INIT_FAILED;
    }

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "material",
        .has = nt_material_comp_has,
        .on_destroy = material_on_destroy,
    });

    return NT_OK;
}

void nt_material_comp_shutdown(void) {
    free(s_material_handles);
    s_material_handles = NULL;
    nt_comp_storage_shutdown(&s_storage);
}

/* ---- Operations ---- */

bool nt_material_comp_add(nt_entity_t entity) { return nt_comp_storage_add(&s_storage, entity) != NT_INVALID_COMP_INDEX; }

bool nt_material_comp_has(nt_entity_t entity) { return nt_comp_storage_has(&s_storage, entity); }

void nt_material_comp_remove(nt_entity_t entity) { nt_comp_storage_remove(&s_storage, entity); }

nt_material_t *nt_material_comp_handle(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return &s_material_handles[idx];
}
