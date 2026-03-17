#ifndef NT_RESOURCE_H
#define NT_RESOURCE_H

#include "core/nt_types.h"

/* ---- Compile-time limits (overridable via -D) ---- */

#ifndef NT_RESOURCE_MAX_PACKS
#define NT_RESOURCE_MAX_PACKS 16
#endif

#ifndef NT_RESOURCE_MAX_ASSETS
#define NT_RESOURCE_MAX_ASSETS 4096
#endif

#ifndef NT_RESOURCE_MAX_SLOTS
#define NT_RESOURCE_MAX_SLOTS 1024
#endif

#ifndef NT_RESOURCE_STEP_BUDGET
#define NT_RESOURCE_STEP_BUDGET 8
#endif

/* ---- Resource handle ---- */

typedef struct {
    uint32_t id;
} nt_resource_t;

#define NT_RESOURCE_INVALID ((nt_resource_t){0})

/* ---- Handle encoding: lower 16 bits = slot index, upper 16 bits = generation ---- */

static inline uint16_t nt_resource_slot_index(nt_resource_t r) { return (uint16_t)(r.id & 0xFFFF); }

static inline uint16_t nt_resource_generation(nt_resource_t r) { return (uint16_t)(r.id >> 16); }

/* ---- Descriptor ---- */

typedef struct {
    uint8_t _reserved; /* placeholder for future config */
} nt_resource_desc_t;

/* ---- Lifecycle ---- */

nt_result_t nt_resource_init(const nt_resource_desc_t *desc);
void nt_resource_shutdown(void);
void nt_resource_step(void);

/* ---- Pack management ---- */

nt_result_t nt_resource_mount(uint32_t pack_id, int16_t priority);
void nt_resource_unmount(uint32_t pack_id);
nt_result_t nt_resource_set_priority(uint32_t pack_id, int16_t new_priority);

/* ---- Pack parsing ---- */

nt_result_t nt_resource_parse_pack(uint32_t pack_id, const uint8_t *blob, uint32_t blob_size);

/* ---- Resource access ---- */

nt_resource_t nt_resource_request(uint32_t resource_id, uint8_t asset_type);
uint32_t nt_resource_get(nt_resource_t handle);
bool nt_resource_is_ready(nt_resource_t handle);
uint8_t nt_resource_get_state(nt_resource_t handle);

/* ---- Virtual packs ---- */

nt_result_t nt_resource_create_pack(uint32_t pack_id, int16_t priority);
nt_result_t nt_resource_register(uint32_t pack_id, uint32_t resource_id, uint8_t asset_type, uint32_t gfx_handle);
void nt_resource_unregister(uint32_t pack_id, uint32_t resource_id);

/* ---- Placeholder ---- */

void nt_resource_set_placeholder(uint8_t asset_type, uint32_t gfx_handle);

/* ---- Hash utility ---- */

uint32_t nt_resource_hash(const char *name);

/* ---- Test access (test-only) ---- */

#ifdef NT_RESOURCE_TEST_ACCESS
void nt_resource_test_set_asset_state(uint32_t resource_id, uint16_t pack_index, uint8_t state, uint32_t runtime_handle);
#endif

#endif /* NT_RESOURCE_H */
