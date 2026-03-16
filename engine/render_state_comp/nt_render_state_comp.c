#include "render_state_comp/nt_render_state_comp.h"

#include "comp_storage/nt_comp_storage.h"

static nt_comp_storage_t s_storage;

/* ---- Default initializer ---- */

static void render_state_default(void *comp) {
    nt_render_state_comp_t *rs = (nt_render_state_comp_t *)comp;
    rs->tag = 0;
    rs->visible = true;
    rs->color[0] = 1.0F;
    rs->color[1] = 1.0F;
    rs->color[2] = 1.0F;
    rs->color[3] = 1.0F;
}

/* ---- Destroy callback ---- */

static void render_state_on_destroy(nt_entity_t entity) {
    if (nt_comp_storage_has(&s_storage, entity)) {
        nt_comp_storage_remove(&s_storage, entity);
    }
}

/* ---- Lifecycle ---- */

nt_result_t nt_render_state_comp_init(const nt_render_state_comp_desc_t *desc) {
    if (!desc || desc->capacity == 0) {
        return NT_ERR_INVALID_ARG;
    }

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, sizeof(nt_render_state_comp_t), render_state_default);
    if (res != NT_OK) {
        return res;
    }

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "render_state",
        .has = nt_render_state_comp_has,
        .on_destroy = render_state_on_destroy,
    });

    return NT_OK;
}

void nt_render_state_comp_shutdown(void) { nt_comp_storage_shutdown(&s_storage); }

/* ---- Typed wrappers ---- */

nt_render_state_comp_t *nt_render_state_comp_add(nt_entity_t entity) { return (nt_render_state_comp_t *)nt_comp_storage_add(&s_storage, entity); }

nt_render_state_comp_t *nt_render_state_comp_get(nt_entity_t entity) { return (nt_render_state_comp_t *)nt_comp_storage_get(&s_storage, entity); }

bool nt_render_state_comp_has(nt_entity_t entity) { return nt_comp_storage_has(&s_storage, entity); }

void nt_render_state_comp_remove(nt_entity_t entity) { nt_comp_storage_remove(&s_storage, entity); }
