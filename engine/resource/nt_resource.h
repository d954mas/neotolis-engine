#ifndef NT_RESOURCE_H
#define NT_RESOURCE_H

#include "core/nt_types.h"
#include "hash/nt_hash.h"

/* ---- Compile-time limits (overridable via -D) ---- */

#ifndef NT_RESOURCE_MAX_PACKS
#define NT_RESOURCE_MAX_PACKS 16
#endif

#ifndef NT_RESOURCE_MAX_ASSETS
#define NT_RESOURCE_MAX_ASSETS 2048
#endif

#ifndef NT_RESOURCE_MAX_SLOTS
#define NT_RESOURCE_MAX_SLOTS 2048
#endif

/* ---- Activation time budget (ms per nt_resource_step call) ---- */

#ifndef NT_RESOURCE_ACTIVATE_TIME_BUDGET_MS
#define NT_RESOURCE_ACTIVATE_TIME_BUDGET_MS 8.0f
#endif

/* ---- Pack state (game-visible for polling) ---- */

typedef enum {
    NT_PACK_STATE_NONE = 0,    /* not loaded */
    NT_PACK_STATE_REQUESTED,   /* I/O request issued */
    NT_PACK_STATE_DOWNLOADING, /* receiving data (progress available) */
    NT_PACK_STATE_LOADED,      /* data received, not yet parsed */
    NT_PACK_STATE_READY,       /* parsed, assets registered */
    NT_PACK_STATE_FAILED,      /* load failed (may retry) */
} nt_pack_state_t;

/* ---- Activator callback types ----
 * WARNING: callbacks must not call resource API (mount/unmount/request/step).
 * They fire during resolve iteration — modifying resource state is UB. */

typedef uint32_t (*nt_activate_fn)(const uint8_t *data, uint32_t size);
typedef void (*nt_deactivate_fn)(uint32_t runtime_handle);
/* data may be NULL when the winner's pack blob is not resident
 * (placeholder, virtual pack, or evicted file-pack blob).
 * data pointer is only valid for the duration of this call — copy if needed. */
typedef void (*nt_resolve_fn)(const uint8_t *data, uint32_t size, uint32_t runtime_handle, void **user_data);
typedef void (*nt_cleanup_fn)(void *user_data);

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

nt_result_t nt_resource_mount(nt_hash32_t pack_id, int16_t priority);
void nt_resource_unmount(nt_hash32_t pack_id);
nt_result_t nt_resource_set_priority(nt_hash32_t pack_id, int16_t new_priority);

/* ---- Pack parsing ---- */

nt_result_t nt_resource_parse_pack(nt_hash32_t pack_id, const uint8_t *blob, uint32_t blob_size);

/* ---- Resource access ---- */

nt_resource_t nt_resource_request(nt_hash64_t resource_id, uint8_t asset_type);
uint32_t nt_resource_get(nt_resource_t handle);
bool nt_resource_is_ready(nt_resource_t handle);
uint8_t nt_resource_get_state(nt_resource_t handle);

/* Get raw blob data pointer (after NtBlobAssetHeader). Returns NULL if not ready or not a blob.
 * Returned pointer is a view into pack memory — valid until pack blob is evicted.
 * Caller must copy data if it needs to persist beyond the current frame. */
const uint8_t *nt_resource_get_blob(nt_resource_t handle, uint32_t *out_size);

/* Get metadata for a resource by kind hash. Returns pointer to metadata bytes
 * in resident pack memory, NULL if absent. Pointer valid until pack unmount.
 * kind = nt_hash64_str("tag").value for example.
 * Returns NULL + size 0 for absent metadata. */
const void *nt_resource_get_meta(nt_resource_t handle, uint64_t kind, uint32_t *out_size);

/* ---- Virtual packs ---- */

nt_result_t nt_resource_create_pack(nt_hash32_t pack_id, int16_t priority);
nt_result_t nt_resource_register(nt_hash32_t pack_id, nt_hash64_t resource_id, uint8_t asset_type, uint32_t runtime_handle);
void nt_resource_unregister(nt_hash32_t pack_id, nt_hash64_t resource_id);

/* ---- Placeholder ---- */

void nt_resource_set_placeholder_texture(nt_hash64_t resource_id);

/* ---- Pack loading ---- */

nt_result_t nt_resource_load_file(nt_hash32_t pack_id, const char *path);
nt_result_t nt_resource_load_url(nt_hash32_t pack_id, const char *url);
nt_result_t nt_resource_load_auto(nt_hash32_t pack_id, const char *path);
nt_pack_state_t nt_resource_pack_state(nt_hash32_t pack_id);
void nt_resource_pack_progress(nt_hash32_t pack_id, uint32_t *received, uint32_t *total);

/* ---- Activator registration ---- */

void nt_resource_set_activator(uint8_t asset_type, nt_activate_fn activate, nt_deactivate_fn deactivate);
void nt_resource_set_resolve_callbacks(uint8_t asset_type, nt_resolve_fn on_resolve, nt_cleanup_fn on_cleanup);
void *nt_resource_get_user_data(nt_resource_t handle);

/* ---- Activation time budget ---- */

/* Set max milliseconds spent activating assets per nt_resource_step() call.
 * 0 = unlimited (activate all pending assets immediately).
 * Default: NT_RESOURCE_ACTIVATE_TIME_BUDGET_MS (8ms). */
void nt_resource_set_activate_time_budget(float max_ms);

/* ---- Retry policy ---- */

void nt_resource_set_retry_policy(uint32_t max_attempts, uint32_t base_delay_ms, uint32_t max_delay_ms);

/* ---- Blob policy ---- */

void nt_resource_set_blob_policy(nt_hash32_t pack_id, uint8_t policy, uint32_t ttl_ms);

/* ---- Context loss recovery ---- */

void nt_resource_invalidate(uint8_t asset_type);

/* ---- Debug: dump loaded pack contents to log ---- */

void nt_resource_dump_pack(nt_hash32_t pack_id);

/* ---- Test access (test-only) ---- */

#ifdef NT_RESOURCE_TEST_ACCESS
void nt_resource_test_set_asset_state(nt_hash64_t resource_id, uint16_t pack_index, uint8_t state, uint32_t runtime_handle);
#endif

#endif /* NT_RESOURCE_H */
