#include "transform_comp/nt_transform_comp.h"

#include "core/nt_assert.h"

#include <stdlib.h>
#include <string.h>

/* ---- Internal storage ---- */

static struct {
    nt_transform_comp_t *data; /* dense component data [capacity] */
    uint16_t *entity_to_index; /* sparse: entity_index -> dense_index [max_entities + 1] */
    uint16_t *index_to_entity; /* reverse: dense_index -> entity_index [capacity] */
    uint16_t count;            /* current number of components */
    uint16_t capacity;         /* max components (from descriptor) */
    bool initialized;
} s_storage;

/* ---- Destroy callback ---- */

static void transform_on_destroy(nt_entity_t entity) {
    if (nt_transform_comp_has(entity)) {
        nt_transform_comp_remove(entity);
    }
}

/* ---- Lifecycle ---- */

nt_result_t nt_transform_comp_init(const nt_transform_comp_desc_t *desc) {
    if (!desc || desc->capacity == 0) {
        return NT_ERR_INVALID_ARG;
    }

    NT_ASSERT(desc->capacity <= nt_entity_max());

    uint16_t max_entities = nt_entity_max();
    uint32_t sparse_count = (uint32_t)max_entities + 1;

    s_storage.data = (nt_transform_comp_t *)calloc(desc->capacity, sizeof(nt_transform_comp_t));
    s_storage.entity_to_index = (uint16_t *)malloc(sparse_count * sizeof(uint16_t));
    s_storage.index_to_entity = (uint16_t *)calloc(desc->capacity, sizeof(uint16_t));

    if (!s_storage.data || !s_storage.entity_to_index || !s_storage.index_to_entity) {
        nt_transform_comp_shutdown();
        return NT_ERR_INIT_FAILED;
    }

    /* All sparse entries start as invalid */
    memset(s_storage.entity_to_index, 0xFF, sparse_count * sizeof(uint16_t));

    s_storage.count = 0;
    s_storage.capacity = desc->capacity;
    s_storage.initialized = true;

    /* Register with entity system for auto-cleanup on destroy */
    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "transform",
        .has = nt_transform_comp_has,
        .on_destroy = transform_on_destroy,
    });

    return NT_OK;
}

void nt_transform_comp_shutdown(void) {
    free(s_storage.data);
    free(s_storage.entity_to_index);
    free(s_storage.index_to_entity);
    memset(&s_storage, 0, sizeof(s_storage));
}

/* ---- Component operations ---- */

nt_transform_comp_t *nt_transform_comp_add(nt_entity_t entity) {
    NT_ASSERT_ALWAYS(nt_entity_is_alive(entity));

    uint16_t eidx = nt_entity_index(entity);
    NT_ASSERT(s_storage.entity_to_index[eidx] == NT_INVALID_COMP_INDEX);
    NT_ASSERT(s_storage.count < s_storage.capacity);

    uint16_t dense_idx = s_storage.count;
    s_storage.entity_to_index[eidx] = dense_idx;
    s_storage.index_to_entity[dense_idx] = eidx;
    s_storage.count++;

    /* Zero-init then set defaults */
    nt_transform_comp_t *comp = &s_storage.data[dense_idx];
    memset(comp, 0, sizeof(*comp));
    glm_vec3_zero(comp->local_position);
    glm_quat_identity(comp->local_rotation);
    glm_vec3_one(comp->local_scale);
    glm_mat4_identity(comp->world_matrix);
    comp->dirty = true;

    return comp;
}

nt_transform_comp_t *nt_transform_comp_get(nt_entity_t entity) {
    NT_ASSERT_ALWAYS(nt_entity_is_alive(entity));

    uint16_t eidx = nt_entity_index(entity);
    uint16_t dense_idx = s_storage.entity_to_index[eidx];
    NT_ASSERT(dense_idx != NT_INVALID_COMP_INDEX);

    return &s_storage.data[dense_idx];
}

bool nt_transform_comp_has(nt_entity_t entity) {
    if (entity.id == 0) {
        return false;
    }
    uint16_t eidx = nt_entity_index(entity);
    if (eidx == 0 || eidx > nt_entity_max()) {
        return false;
    }
    return s_storage.entity_to_index[eidx] != NT_INVALID_COMP_INDEX;
}

void nt_transform_comp_remove(nt_entity_t entity) {
    uint16_t eidx = nt_entity_index(entity);
    uint16_t dense_idx = s_storage.entity_to_index[eidx];

    if (dense_idx == NT_INVALID_COMP_INDEX) {
        return;
    }

    uint16_t last_idx = s_storage.count - 1;

    if (dense_idx != last_idx) {
        /* Swap with last element */
        s_storage.data[dense_idx] = s_storage.data[last_idx];
        uint16_t last_entity = s_storage.index_to_entity[last_idx];
        s_storage.index_to_entity[dense_idx] = last_entity;
        s_storage.entity_to_index[last_entity] = dense_idx;
    }

    s_storage.entity_to_index[eidx] = NT_INVALID_COMP_INDEX;
    s_storage.count--;
}

/* ---- Update ---- */

void nt_transform_comp_update(void) {
    for (uint16_t i = 0; i < s_storage.count; i++) {
        nt_transform_comp_t *t = &s_storage.data[i];
        if (!t->dirty) {
            continue;
        }

        /* Build world_matrix = T * R * S */
        mat4 translation;
        mat4 rotation;
        mat4 scale_mat;
        mat4 temp;
        glm_translate_make(translation, t->local_position);
        glm_quat_mat4(t->local_rotation, rotation);
        glm_scale_make(scale_mat, t->local_scale);
        glm_mat4_mul(translation, rotation, temp);
        glm_mat4_mul(temp, scale_mat, t->world_matrix);

        t->dirty = false;
    }
}
