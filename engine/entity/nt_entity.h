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

#define NT_MAX_COMP_STORAGES 128

/* Stable dense component id == the storage's registration-order index (0 .. count-1). Run-stable and
   array-indexable (like Bevy ComponentId / Unity-DOTS TypeIndex), but NOT serialization-stable:
   registration order can change between builds — resolve a name to an id once, never persist it. */
typedef uint8_t nt_comp_id_t;
#define NT_COMP_ID_INVALID ((nt_comp_id_t)0xFF)
_Static_assert(NT_MAX_COMP_STORAGES <= 0xFF, "nt_comp_id_t (uint8_t) must index every storage");

typedef bool (*nt_comp_has_fn)(nt_entity_t entity);
typedef void (*nt_comp_on_destroy_fn)(nt_entity_t entity);

/* Forward decls of the dev-only introspection types (defined in introspect/nt_introspect.h). Let a
   component register an optional self-describe (read) + apply (write) without entity depending on
   introspect or devapi. */
typedef struct nt_introspect_sink nt_introspect_sink;
typedef void (*nt_comp_describe_fn)(nt_entity_t entity, nt_introspect_sink *sink);
typedef struct nt_write_value nt_write_value;
/* Apply one typed value to writable field `key` through the component's real setter. `dry_run`
   validates (kind/arity/range) without mutating, to back whole-or-nothing batch writes. Returns false
   and sets *err_msg on an unknown/read-only key or a validation reject; the accepted-key set IS the
   writable field set. Populate ONLY under #if NT_INTROSPECT_WRITE_ENABLED; NULL = read-only. */
typedef bool (*nt_comp_apply_fn)(nt_entity_t entity, const char *key, const nt_write_value *v, bool dry_run, const char **err_msg);

typedef struct {
    const char *name;
    nt_comp_has_fn has;
    nt_comp_on_destroy_fn on_destroy;
    nt_comp_describe_fn describe; /* read; optional (NULL = not introspectable); populate ONLY under #if NT_INTROSPECT_ENABLED */
    nt_comp_apply_fn apply;       /* write; optional (NULL = read-only); populate ONLY under #if NT_INTROSPECT_WRITE_ENABLED */
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

/* Enumeration accessor: live handle for slot `index` (1..nt_entity_max()), or
 * NT_ENTITY_INVALID if the slot is dead/empty. Never reconstructs a stale handle. */
nt_entity_t nt_entity_at_index(uint16_t index);

/* ---- Registered storages (generic tooling: introspection / devapi entity.*) ---- */

/* Number of registered component storages (0..NT_MAX_COMP_STORAGES). Ids run 0..count-1. */
uint8_t nt_entity_storage_count(void);
/* Registration record for stable id `id` (0..count-1), or NULL when out of range. */
const nt_comp_storage_reg_t *nt_entity_storage_at(nt_comp_id_t id);

/* Resolve a storage name to its stable id, or NT_COMP_ID_INVALID if unregistered. O(count) strcmp —
   the single sanctioned name->id point; call once, cache the id, never per entity. */
nt_comp_id_t nt_entity_storage_find(const char *name);

/* O(1): does entity `e` carry the component with stable id `id`? Asserts id < count. */
bool nt_entity_has_comp(nt_entity_t e, nt_comp_id_t id);

/* Fill out_ids[0..min(return,max)) with the stable ids of every component present on `e`, in
   registration order; returns the TOTAL present count (return > max signals truncation). out_ids may
   be NULL only when max==0 (count-only). Dead entity -> 0. Poll-backed: O(count) O(1) has() calls. */
uint8_t nt_entity_components(nt_entity_t e, nt_comp_id_t *out_ids, uint8_t max);

#endif /* NT_ENTITY_H */
