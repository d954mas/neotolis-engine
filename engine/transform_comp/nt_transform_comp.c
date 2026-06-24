#include "transform_comp/nt_transform_comp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"
#if NT_INTROSPECT_ENABLED || NT_INTROSPECT_WRITE_ENABLED
#include "introspect/nt_introspect.h"
#endif

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

#if NT_INTROSPECT_ENABLED
static void transform_describe(nt_entity_t entity, nt_introspect_sink *s) {
    s->field_floats(s, "position", nt_transform_comp_position(entity), 3);
    s->field_floats(s, "rotation", nt_transform_comp_rotation(entity), 4);
    s->field_floats(s, "scale", nt_transform_comp_scale(entity), 3);
}
#endif

#if NT_INTROSPECT_WRITE_ENABLED
/* world_matrix has no case: derived/read-only, so it falls through to bad_params. */
static bool transform_apply(nt_entity_t entity, const char *key, const nt_write_value *v, bool dry_run, const char **err_msg) {
    if (strcmp(key, "position") == 0) {
        if (v->kind != NT_WV_VEC3) {
            *err_msg = "transform.position expects 3 numbers";
            return false;
        }
        if (!dry_run) {
            nt_transform_comp_set_position(entity, v->as.v[0], v->as.v[1], v->as.v[2]);
        }
        return true;
    }
    if (strcmp(key, "scale") == 0) {
        if (v->kind != NT_WV_VEC3) {
            *err_msg = "transform.scale expects 3 numbers";
            return false;
        }
        if (!dry_run) {
            nt_transform_comp_set_scale(entity, v->as.v[0], v->as.v[1], v->as.v[2]);
        }
        return true;
    }
    if (strcmp(key, "rotation") == 0) {
        if (v->kind != NT_WV_VEC4) {
            *err_msg = "transform.rotation expects 4 numbers";
            return false;
        }
        /* Reject a degenerate quaternion BEFORE set_rotation: cglm normalize snaps ||q||~0 to identity.
           !isfinite guards an overflow-magnitude quat (||q||^2 -> +Inf passes the <= test, then cglm
           divides by sqrt(Inf) and silently stores a zero quaternion). */
        float n2 = (v->as.v[0] * v->as.v[0]) + (v->as.v[1] * v->as.v[1]) + (v->as.v[2] * v->as.v[2]) + (v->as.v[3] * v->as.v[3]);
        if (!isfinite(n2) || n2 <= 1e-12F) {
            *err_msg = "transform.rotation must be a non-zero quaternion";
            return false;
        }
        if (!dry_run) {
            nt_transform_comp_set_rotation(entity, v->as.v);
        }
        return true;
    }
    *err_msg = "unknown or read-only field for transform";
    return false;
}
#endif

/* ---- Lifecycle ---- */

nt_result_t nt_transform_comp_init(const nt_transform_comp_desc_t *desc) {
    NT_ASSERT(desc != NULL);
    NT_ASSERT(desc->capacity > 0);

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
#if NT_INTROSPECT_ENABLED
        .describe = transform_describe,
#endif
#if NT_INTROSPECT_WRITE_ENABLED
        .apply = transform_apply,
#endif
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
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return s_trs[idx].position;
}

float *nt_transform_comp_rotation(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return s_trs[idx].rotation;
}

float *nt_transform_comp_scale(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return s_trs[idx].scale;
}

bool *nt_transform_comp_dirty(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return &s_dirty[idx];
}

/* ---- Field mutation (the safe write path: maintains dirty so the world matrix recomputes) ---- */

void nt_transform_comp_set_position(nt_entity_t entity, float x, float y, float z) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    s_trs[idx].position[0] = x;
    s_trs[idx].position[1] = y;
    s_trs[idx].position[2] = z;
    s_dirty[idx] = true;
}

void nt_transform_comp_set_scale(nt_entity_t entity, float x, float y, float z) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    s_trs[idx].scale[0] = x;
    s_trs[idx].scale[1] = y;
    s_trs[idx].scale[2] = z;
    s_dirty[idx] = true;
}

void nt_transform_comp_set_rotation(nt_entity_t entity, const float q[4]) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    /* Caller rejects a degenerate q first: glm_quat_normalize_to snaps ||q||~0 to identity silently. */
    versor src = {q[0], q[1], q[2], q[3]};
    glm_quat_normalize_to(src, s_trs[idx].rotation);
    s_dirty[idx] = true;
}

const float *nt_transform_comp_world_matrix(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
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

/* ---- Bulk SoA view ---- */

nt_transform_comp_view_t nt_transform_comp_view(void) {
    return (nt_transform_comp_view_t){
        .count = nt_comp_storage_count(&s_storage),
        .sparse_indices = nt_comp_storage_sparse(&s_storage),
        .world_matrices = (const float(*)[16])s_world_matrices,
    };
}
