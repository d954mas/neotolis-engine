#ifndef NT_COMP_STORAGE_H
#define NT_COMP_STORAGE_H

#include "core/nt_types.h"
#include "entity/nt_entity.h"

/* ---- Sentinel for unused sparse slots ---- */

#define NT_INVALID_COMP_INDEX UINT16_MAX

/* ---- Callbacks ---- */

typedef void (*nt_comp_default_fn)(uint16_t dense_idx);              /* init new slot */
typedef void (*nt_comp_swap_fn)(uint16_t dst_idx, uint16_t src_idx); /* move src data to dst on swap-and-pop */

/* ---- Index-only sparse+dense storage ---- */

typedef struct {
    uint16_t *entity_to_index; /* sparse: entity_index -> dense_index [max_entities + 1] */
    uint16_t *index_to_entity; /* reverse: dense_index -> entity_index [capacity] */
    uint16_t count;
    uint16_t capacity;
    nt_comp_default_fn default_fn;
    nt_comp_swap_fn swap_fn;
    bool initialized;
} nt_comp_storage_t;

/* ---- Lifecycle ---- */

nt_result_t nt_comp_storage_init(nt_comp_storage_t *s, uint16_t capacity, nt_comp_default_fn default_fn, nt_comp_swap_fn swap_fn);
void nt_comp_storage_shutdown(nt_comp_storage_t *s);

/* ---- Operations ---- */

uint16_t nt_comp_storage_add(nt_comp_storage_t *s, nt_entity_t entity);
bool nt_comp_storage_has(const nt_comp_storage_t *s, nt_entity_t entity);
void nt_comp_storage_remove(nt_comp_storage_t *s, nt_entity_t entity);

/* ---- Query ---- */

uint16_t nt_comp_storage_count(const nt_comp_storage_t *s);
uint16_t nt_comp_storage_index(const nt_comp_storage_t *s, nt_entity_t entity);

#endif /* NT_COMP_STORAGE_H */
