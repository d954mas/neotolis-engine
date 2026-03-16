#include "transform_comp/nt_transform_comp.h"

#include <stdlib.h>
#include <string.h>

#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"

static nt_comp_storage_t s_storage;

/* ---- Data arrays (parallel to dense index) ---- */

typedef struct {
    vec3 position;
    vec4 rotation; /* quaternion (cglm versor) */
    vec3 scale;
} nt_trs_t; /* 40 bytes — fits one cache line */

static bool *s_dirty;
static nt_trs_t *s_trs;
static mat4 *s_world_matrices;

/* ---- Callbacks ---- */

static void transform_default(uint16_t idx) {
    s_dirty[idx] = true;
    glm_vec3_zero(s_trs[idx].position);
    glm_quat_identity(s_trs[idx].rotation);
    glm_vec3_one(s_trs[idx].scale);
    glm_mat4_identity(s_world_matrices[idx]);
}

static void transform_swap(uint16_t dst, uint16_t src) {
    s_dirty[dst] = s_dirty[src];
    s_trs[dst] = s_trs[src];
    glm_mat4_copy(s_world_matrices[src], s_world_matrices[dst]);
}

static void transform_on_destroy(nt_entity_t entity) {
    if (nt_comp_storage_has(&s_storage, entity)) {
        nt_comp_storage_remove(&s_storage, entity);
    }
}

/* ---- Lifecycle ---- */

nt_result_t nt_transform_comp_init(const nt_transform_comp_desc_t *desc) {
    if (!desc || desc->capacity == 0) {
        return NT_ERR_INVALID_ARG;
    }

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, transform_default, transform_swap);
    if (res != NT_OK) {
        return res;
    }

    uint16_t cap = desc->capacity;
    s_dirty = (bool *)calloc(cap, sizeof(bool));
    s_trs = (nt_trs_t *)calloc(cap, sizeof(nt_trs_t));
    s_world_matrices = (mat4 *)calloc(cap, sizeof(mat4));

    if (!s_dirty || !s_trs || !s_world_matrices) {
        nt_transform_comp_shutdown();
        return NT_ERR_INIT_FAILED;
    }

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "transform",
        .has = nt_transform_comp_has,
        .on_destroy = transform_on_destroy,
    });

    return NT_OK;
}

void nt_transform_comp_shutdown(void) {
    free(s_dirty);
    free(s_trs);
    free(s_world_matrices);
    s_dirty = NULL;
    s_trs = NULL;
    s_world_matrices = NULL;
    nt_comp_storage_shutdown(&s_storage);
}

/* ---- Per-entity operations ---- */

bool nt_transform_comp_add(nt_entity_t entity) { return nt_comp_storage_add(&s_storage, entity) != NT_INVALID_COMP_INDEX; }

bool nt_transform_comp_has(nt_entity_t entity) { return nt_comp_storage_has(&s_storage, entity); }

void nt_transform_comp_remove(nt_entity_t entity) { nt_comp_storage_remove(&s_storage, entity); }

/* ---- Field access ---- */

float *nt_transform_comp_position(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT_ALWAYS(idx != NT_INVALID_COMP_INDEX);
    return s_trs[idx].position;
}

float *nt_transform_comp_rotation(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT_ALWAYS(idx != NT_INVALID_COMP_INDEX);
    return s_trs[idx].rotation;
}

float *nt_transform_comp_scale(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT_ALWAYS(idx != NT_INVALID_COMP_INDEX);
    return s_trs[idx].scale;
}

bool *nt_transform_comp_dirty(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT_ALWAYS(idx != NT_INVALID_COMP_INDEX);
    return &s_dirty[idx];
}

const float *nt_transform_comp_world_matrix(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT_ALWAYS(idx != NT_INVALID_COMP_INDEX);
    return (const float *)s_world_matrices[idx];
}

/* ---- System ---- */

void nt_transform_comp_update(void) {
    uint16_t count = nt_comp_storage_count(&s_storage);
    for (uint16_t i = 0; i < count; i++) {
        if (!s_dirty[i]) {
            continue;
        }

        nt_trs_t *trs = &s_trs[i];
        mat4 translation;
        mat4 rotation;
        mat4 scale_mat;
        mat4 temp;
        glm_translate_make(translation, trs->position);
        glm_quat_mat4(trs->rotation, rotation);
        glm_scale_make(scale_mat, trs->scale);
        glm_mat4_mul(translation, rotation, temp);
        glm_mat4_mul(temp, scale_mat, s_world_matrices[i]);

        s_dirty[i] = false;
    }
}
