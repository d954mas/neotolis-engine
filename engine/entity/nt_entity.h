#ifndef NT_ENTITY_H
#define NT_ENTITY_H

#include "core/nt_types.h"

/* ---- Entity handle ---- */

typedef struct {
    uint32_t id;
} nt_entity_t;

#define NT_ENTITY_INVALID ((nt_entity_t){0})

/* ---- Handle encoding: lower 16 bits = index, upper 16 bits = generation ---- */

static inline uint16_t nt_entity_index(nt_entity_t e) { return (uint16_t)(e.id & 0xFFFF); }

static inline uint16_t nt_entity_generation(nt_entity_t e) { return (uint16_t)(e.id >> 16); }

/* ---- Descriptor ---- */

typedef struct {
    uint16_t max_entities;
} nt_entity_desc_t;

/* ---- Defaults ---- */

static inline nt_entity_desc_t nt_entity_desc_defaults(void) {
    return (nt_entity_desc_t){
        .max_entities = 256,
    };
}

/* ---- Component storage registration ---- */

#define NT_MAX_COMP_STORAGES 16

typedef bool (*nt_comp_has_fn)(nt_entity_t entity);
typedef void (*nt_comp_on_destroy_fn)(nt_entity_t entity);

typedef struct {
    const char *name;
    nt_comp_has_fn has;
    nt_comp_on_destroy_fn on_destroy;
} nt_comp_storage_reg_t;

/* ---- Public API ---- */

nt_result_t nt_entity_init(const nt_entity_desc_t *desc);
void nt_entity_shutdown(void);

nt_entity_t nt_entity_create(void);
void nt_entity_destroy(nt_entity_t entity);
bool nt_entity_is_alive(nt_entity_t entity);

void nt_entity_set_enabled(nt_entity_t entity, bool enabled);
bool nt_entity_is_enabled(nt_entity_t entity);

void nt_entity_register_storage(const nt_comp_storage_reg_t *reg);

uint16_t nt_entity_max(void);

#endif /* NT_ENTITY_H */
