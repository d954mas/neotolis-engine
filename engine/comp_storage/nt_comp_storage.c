#include "comp_storage/nt_comp_storage.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"

/* ---- Lifecycle ---- */

nt_result_t nt_comp_storage_init(nt_comp_storage_t *s, uint16_t capacity, nt_comp_default_fn default_fn,
                                 nt_comp_swap_fn swap_fn) {
    NT_ASSERT(s);
    NT_ASSERT(default_fn);
    NT_ASSERT(swap_fn);
    if (capacity == 0) {
        return NT_ERR_INVALID_ARG;
    }
    NT_ASSERT_ALWAYS(nt_entity_max() > 0);
    NT_ASSERT(capacity <= nt_entity_max());

    memset(s, 0, sizeof(*s));
    s->capacity = capacity;
    s->default_fn = default_fn;
    s->swap_fn = swap_fn;

    uint32_t sparse_count = (uint32_t)nt_entity_max() + 1;
    s->entity_to_index = (uint16_t *)malloc(sparse_count * sizeof(uint16_t));
    s->index_to_entity = (uint16_t *)calloc(capacity, sizeof(uint16_t));

    if (!s->entity_to_index || !s->index_to_entity) {
        nt_comp_storage_shutdown(s);
        return NT_ERR_INIT_FAILED;
    }

    memset(s->entity_to_index, 0xFF, sparse_count * sizeof(uint16_t));
    s->initialized = true;
    return NT_OK;
}

void nt_comp_storage_shutdown(nt_comp_storage_t *s) {
    free(s->entity_to_index);
    free(s->index_to_entity);
    memset(s, 0, sizeof(*s));
}

/* ---- Operations ---- */

uint16_t nt_comp_storage_add(nt_comp_storage_t *s, nt_entity_t entity) {
    NT_ASSERT(s->initialized);
    NT_ASSERT_ALWAYS(nt_entity_is_alive(entity));

    uint16_t eidx = nt_entity_index(entity);

    if (s->entity_to_index[eidx] != NT_INVALID_COMP_INDEX) {
        NT_ASSERT(false);
        return NT_INVALID_COMP_INDEX;
    }
    if (s->count >= s->capacity) {
        NT_ASSERT(false);
        return NT_INVALID_COMP_INDEX;
    }

    uint16_t dense_idx = s->count;
    s->entity_to_index[eidx] = dense_idx;
    s->index_to_entity[dense_idx] = eidx;
    s->count++;

    s->default_fn(dense_idx);
    return dense_idx;
}

bool nt_comp_storage_has(const nt_comp_storage_t *s, nt_entity_t entity) {
    if (entity.id == 0) {
        return false;
    }
    if (!nt_entity_is_alive(entity)) {
        return false;
    }
    uint16_t eidx = nt_entity_index(entity);
    return s->entity_to_index[eidx] != NT_INVALID_COMP_INDEX;
}

void nt_comp_storage_remove(nt_comp_storage_t *s, nt_entity_t entity) {
    NT_ASSERT_ALWAYS(nt_entity_is_alive(entity));

    uint16_t eidx = nt_entity_index(entity);
    uint16_t dense_idx = s->entity_to_index[eidx];

    if (dense_idx == NT_INVALID_COMP_INDEX || s->count == 0) {
        return;
    }

    uint16_t last_idx = s->count - 1;
    if (dense_idx != last_idx) {
        s->swap_fn(dense_idx, last_idx);
        uint16_t last_entity = s->index_to_entity[last_idx];
        s->index_to_entity[dense_idx] = last_entity;
        s->entity_to_index[last_entity] = dense_idx;
    }
    s->entity_to_index[eidx] = NT_INVALID_COMP_INDEX;
    s->count--;
}

/* ---- Query ---- */

uint16_t nt_comp_storage_count(const nt_comp_storage_t *s) { return s->count; }

uint16_t nt_comp_storage_index(const nt_comp_storage_t *s, nt_entity_t entity) {
    NT_ASSERT_ALWAYS(nt_entity_is_alive(entity));
    uint16_t eidx = nt_entity_index(entity);
    return s->entity_to_index[eidx];
}
