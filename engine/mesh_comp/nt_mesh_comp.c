#include "mesh_comp/nt_mesh_comp.h"

#include <stdlib.h>

#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"

static nt_comp_storage_t s_storage;
static nt_mesh_t *s_mesh_handles;

/* ---- Callbacks ---- */

static void mesh_default(uint16_t idx) { s_mesh_handles[idx] = NT_MESH_INVALID; }

static void mesh_swap(uint16_t dst, uint16_t src) { s_mesh_handles[dst] = s_mesh_handles[src]; }

static void mesh_on_destroy(nt_entity_t entity) {
    if (nt_comp_storage_has(&s_storage, entity)) {
        nt_comp_storage_remove(&s_storage, entity);
    }
}

/* ---- Lifecycle ---- */

nt_result_t nt_mesh_comp_init(const nt_mesh_comp_desc_t *desc) {
    NT_ASSERT(desc != NULL);
    NT_ASSERT(desc->capacity > 0);

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, mesh_default, mesh_swap);
    if (res != NT_OK) {
        return res;
    }

    s_mesh_handles = (nt_mesh_t *)calloc(desc->capacity, sizeof(nt_mesh_t));
    if (!s_mesh_handles) {
        nt_comp_storage_shutdown(&s_storage);
        return NT_ERR_INIT_FAILED;
    }

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "mesh",
        .has = nt_mesh_comp_has,
        .on_destroy = mesh_on_destroy,
    });

    return NT_OK;
}

void nt_mesh_comp_shutdown(void) {
    free(s_mesh_handles);
    s_mesh_handles = NULL;
    nt_comp_storage_shutdown(&s_storage);
}

/* ---- Operations ---- */

bool nt_mesh_comp_add(nt_entity_t entity) { return nt_comp_storage_add(&s_storage, entity) != NT_INVALID_COMP_INDEX; }

bool nt_mesh_comp_has(nt_entity_t entity) { return nt_comp_storage_has(&s_storage, entity); }

void nt_mesh_comp_remove(nt_entity_t entity) { nt_comp_storage_remove(&s_storage, entity); }

nt_mesh_t *nt_mesh_comp_handle(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return &s_mesh_handles[idx];
}
