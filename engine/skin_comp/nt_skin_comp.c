#include "skin_comp/nt_skin_comp.h"

#include <stdlib.h>

#include "comp_storage/nt_comp_storage.h"
#include "core/nt_assert.h"
#if NT_INTROSPECT_ENABLED
#include "introspect/nt_introspect.h"
#endif

static nt_comp_storage_t s_storage;
static nt_deformation_binding_t *s_bindings;

/* ---- Callbacks ---- */

static void skin_default(uint16_t idx) { s_bindings[idx] = (nt_deformation_binding_t){0}; }

static void skin_swap(uint16_t dst, uint16_t src) { s_bindings[dst] = s_bindings[src]; }

static void skin_on_destroy(nt_entity_t entity) {
    if (nt_comp_storage_has(&s_storage, entity)) {
        nt_comp_storage_remove(&s_storage, entity);
    }
}

#if NT_INTROSPECT_ENABLED
/* texture 0 with zero origins is the never-reserved binding the renderer asserts on. */
static void skin_describe(nt_entity_t entity, nt_introspect_sink *s) {
    const nt_deformation_binding_t *b = nt_skin_comp_handle(entity);
    s->field_ref(s, "texture", NT_REF_HANDLE, b->texture.id);
    s->field_u64(s, "x0", b->x0);
    s->field_u64(s, "y0", b->y0);
    s->field_u64(s, "x1", b->x1);
    s->field_u64(s, "y1", b->y1);
    s->field_f32(s, "alpha", b->alpha);
}
#endif

/* ---- Lifecycle ---- */

nt_result_t nt_skin_comp_init(const nt_skin_comp_desc_t *desc) {
    NT_ASSERT(desc != NULL);
    NT_ASSERT(desc->capacity > 0);

    nt_result_t res = nt_comp_storage_init(&s_storage, desc->capacity, skin_default, skin_swap);
    if (res != NT_OK) {
        return res;
    }

    s_bindings = (nt_deformation_binding_t *)calloc(desc->capacity, sizeof(nt_deformation_binding_t));
    if (!s_bindings) {
        nt_comp_storage_shutdown(&s_storage);
        return NT_ERR_INIT_FAILED;
    }

    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "skin",
        .has = nt_skin_comp_has,
        .on_destroy = skin_on_destroy,
#if NT_INTROSPECT_ENABLED
        .describe = skin_describe,
#endif
    });

    return NT_OK;
}

void nt_skin_comp_shutdown(void) {
    free(s_bindings);
    s_bindings = NULL;
    nt_comp_storage_shutdown(&s_storage);
}

/* ---- Operations ---- */

bool nt_skin_comp_add(nt_entity_t entity) { return nt_comp_storage_add(&s_storage, entity) != NT_INVALID_COMP_INDEX; }

bool nt_skin_comp_has(nt_entity_t entity) { return nt_comp_storage_has(&s_storage, entity); }

void nt_skin_comp_remove(nt_entity_t entity) { nt_comp_storage_remove(&s_storage, entity); }

nt_deformation_binding_t *nt_skin_comp_handle(nt_entity_t entity) {
    uint16_t idx = nt_comp_storage_index(&s_storage, entity);
    NT_ASSERT(idx != NT_INVALID_COMP_INDEX);
    return &s_bindings[idx];
}
