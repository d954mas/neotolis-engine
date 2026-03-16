#include "transform_comp/nt_transform_comp.h"

#include "comp_storage/nt_comp_storage.h"

static nt_comp_storage_t s_storage;

/* ---- Destroy callback ---- */

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

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, sizeof(nt_transform_comp_t));
    if (res != NT_OK) {
        return res;
    }

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "transform",
        .has = nt_transform_comp_has,
        .on_destroy = transform_on_destroy,
    });

    return NT_OK;
}

void nt_transform_comp_shutdown(void) { nt_comp_storage_shutdown(&s_storage); }

/* ---- Typed wrappers ---- */

nt_transform_comp_t *nt_transform_comp_add(nt_entity_t entity) {
    nt_transform_comp_t *comp = (nt_transform_comp_t *)nt_comp_storage_add(&s_storage, entity);

    glm_vec3_zero(comp->local_position);
    glm_quat_identity(comp->local_rotation);
    glm_vec3_one(comp->local_scale);
    glm_mat4_identity(comp->world_matrix);
    comp->dirty = true;

    return comp;
}

nt_transform_comp_t *nt_transform_comp_get(nt_entity_t entity) { return (nt_transform_comp_t *)nt_comp_storage_get(&s_storage, entity); }

bool nt_transform_comp_has(nt_entity_t entity) { return nt_comp_storage_has(&s_storage, entity); }

void nt_transform_comp_remove(nt_entity_t entity) { nt_comp_storage_remove(&s_storage, entity); }

/* ---- Update ---- */

void nt_transform_comp_update(void) {
    nt_transform_comp_t *data = (nt_transform_comp_t *)s_storage.data;
    for (uint16_t i = 0; i < s_storage.count; i++) {
        nt_transform_comp_t *t = &data[i];
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
