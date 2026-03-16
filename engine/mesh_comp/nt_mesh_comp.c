#include "mesh_comp/nt_mesh_comp.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"

/* ---- Internal storage ---- */

static struct {
    nt_mesh_comp_t *data;
    uint16_t *entity_to_index; /* [max_entities + 1] */
    uint16_t *index_to_entity; /* [capacity] */
    uint16_t count;
    uint16_t capacity;
    bool initialized;
} s_storage;

/* ---- Destroy callback ---- */

static void mesh_on_destroy(nt_entity_t entity) {
    if (nt_mesh_comp_has(entity)) {
        nt_mesh_comp_remove(entity);
    }
}

/* ---- Lifecycle ---- */

nt_result_t nt_mesh_comp_init(const nt_mesh_comp_desc_t *desc) {
    if (!desc || desc->capacity == 0) {
        return NT_ERR_INVALID_ARG;
    }
    NT_ASSERT(desc->capacity <= nt_entity_max());

    memset(&s_storage, 0, sizeof(s_storage));
    s_storage.capacity = desc->capacity;

    uint32_t sparse_count = (uint32_t)nt_entity_max() + 1;
    s_storage.data = (nt_mesh_comp_t *)calloc(desc->capacity, sizeof(nt_mesh_comp_t));
    s_storage.entity_to_index = (uint16_t *)malloc(sparse_count * sizeof(uint16_t));
    s_storage.index_to_entity = (uint16_t *)calloc(desc->capacity, sizeof(uint16_t));

    if (!s_storage.data || !s_storage.entity_to_index || !s_storage.index_to_entity) {
        nt_mesh_comp_shutdown();
        return NT_ERR_INIT_FAILED;
    }

    memset(s_storage.entity_to_index, 0xFF, sparse_count * sizeof(uint16_t));

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "mesh",
        .has = nt_mesh_comp_has,
        .on_destroy = mesh_on_destroy,
    });

    s_storage.initialized = true;
    return NT_OK;
}

void nt_mesh_comp_shutdown(void) {
    free(s_storage.data);
    free(s_storage.entity_to_index);
    free(s_storage.index_to_entity);
    memset(&s_storage, 0, sizeof(s_storage));
}

/* ---- Component operations ---- */

nt_mesh_comp_t *nt_mesh_comp_add(nt_entity_t entity) {
    NT_ASSERT(s_storage.initialized);
    NT_ASSERT_ALWAYS(nt_entity_is_alive(entity));

    uint16_t eidx = nt_entity_index(entity);
    NT_ASSERT(s_storage.entity_to_index[eidx] == NT_INVALID_COMP_INDEX);
    NT_ASSERT(s_storage.count < s_storage.capacity);

    uint16_t dense_idx = s_storage.count;
    s_storage.entity_to_index[eidx] = dense_idx;
    s_storage.index_to_entity[dense_idx] = eidx;
    s_storage.count++;

    nt_mesh_comp_t *comp = &s_storage.data[dense_idx];
    memset(comp, 0, sizeof(*comp));
    /* Default: mesh_handle = 0 */

    return comp;
}

nt_mesh_comp_t *nt_mesh_comp_get(nt_entity_t entity) {
    NT_ASSERT(s_storage.initialized);
    NT_ASSERT_ALWAYS(nt_entity_is_alive(entity));

    uint16_t eidx = nt_entity_index(entity);
    NT_ASSERT(s_storage.entity_to_index[eidx] != NT_INVALID_COMP_INDEX);

    return &s_storage.data[s_storage.entity_to_index[eidx]];
}

bool nt_mesh_comp_has(nt_entity_t entity) {
    if (entity.id == 0) {
        return false;
    }
    uint16_t eidx = nt_entity_index(entity);
    return s_storage.entity_to_index[eidx] != NT_INVALID_COMP_INDEX;
}

void nt_mesh_comp_remove(nt_entity_t entity) {
    uint16_t eidx = nt_entity_index(entity);
    uint16_t dense_idx = s_storage.entity_to_index[eidx];
    NT_ASSERT(dense_idx != NT_INVALID_COMP_INDEX);

    uint16_t last_idx = s_storage.count - 1;
    if (dense_idx != last_idx) {
        uint16_t last_entity = s_storage.index_to_entity[last_idx];
        s_storage.data[dense_idx] = s_storage.data[last_idx];
        s_storage.index_to_entity[dense_idx] = last_entity;
        s_storage.entity_to_index[last_entity] = dense_idx;
    }
    s_storage.entity_to_index[eidx] = NT_INVALID_COMP_INDEX;
    s_storage.count--;
}
