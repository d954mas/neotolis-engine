#ifndef NT_COMP_STORAGE_H
#define NT_COMP_STORAGE_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"

/* ---- Sentinel for unused sparse slots ---- */

#define NT_INVALID_COMP_INDEX UINT16_MAX

/* ---- Component default initializer ---- */

typedef void (*nt_comp_default_fn)(void *comp);

/* ---- Generic sparse+dense component storage ---- */

typedef struct {
    void *data;                    /* dense component data [capacity] */
    uint16_t *entity_to_index;     /* sparse: entity_index -> dense_index [max_entities + 1] */
    uint16_t *index_to_entity;     /* reverse: dense_index -> entity_index [capacity] */
    uint16_t count;                /* current number of components */
    uint16_t capacity;             /* max components (from descriptor) */
    size_t element_size;           /* sizeof one component */
    nt_comp_default_fn default_fn; /* called on add to set defaults (NULL = zero-init) */
    bool initialized;
} nt_comp_storage_t;

/* ---- Storage lifecycle ---- */

nt_result_t nt_comp_storage_init(nt_comp_storage_t *s, uint16_t capacity, size_t element_size, nt_comp_default_fn default_fn);
void nt_comp_storage_shutdown(nt_comp_storage_t *s);

/* ---- Storage operations (return void*, callers cast to typed pointer) ---- */

void *nt_comp_storage_add(nt_comp_storage_t *s, nt_entity_t entity);
void *nt_comp_storage_get(nt_comp_storage_t *s, nt_entity_t entity);
bool nt_comp_storage_has(const nt_comp_storage_t *s, nt_entity_t entity);
void nt_comp_storage_remove(nt_comp_storage_t *s, nt_entity_t entity);

#endif /* NT_COMP_STORAGE_H */
