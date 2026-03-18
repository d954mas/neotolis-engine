#include "resource/nt_resource.h"

#include "resource/nt_resource_internal.h"

#include <limits.h>
#include <string.h>

#include "log/nt_log.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"

/* ---- FNV-1a constants ---- */

#define NT_FNV1A_OFFSET_BASIS 0x811C9DC5U
#define NT_FNV1A_PRIME 0x01000193U

/* ---- Slot map: resource_id -> slot index, open-addressing hash table ---- */

#define NT_SLOT_MAP_SIZE (NT_RESOURCE_MAX_SLOTS * 2)
_Static_assert(NT_SLOT_MAP_SIZE >= NT_RESOURCE_MAX_SLOTS * 2, "slot_map must be at least 2x MAX_SLOTS");

/* ---- Module state ---- */

static struct {
    NtPackMeta packs[NT_RESOURCE_MAX_PACKS];
    NtAssetMeta assets[NT_RESOURCE_MAX_ASSETS];
    NtResourceSlot slots[NT_RESOURCE_MAX_SLOTS + 1]; /* index 0 reserved */
    uint16_t free_queue[NT_RESOURCE_MAX_SLOTS];
    uint16_t slot_map[NT_SLOT_MAP_SIZE];
    uint16_t queue_top;
    uint32_t asset_hwm;           /* high-water mark in assets[] */
    uint32_t placeholder_texture; /* gfx handle for fallback texture, 0 = none */
    bool needs_resolve;
    bool initialized;
} s_resource;

/* ---- Pack lookup ---- */

static int16_t find_pack(uint32_t pack_id) {
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        if (s_resource.packs[i].mounted == 1 && s_resource.packs[i].pack_id == pack_id) {
            return (int16_t)i;
        }
    }
    return -1;
}

/* ---- Slot map helpers ---- */

static uint16_t slot_map_find(uint32_t resource_id) {
    uint32_t idx = resource_id % NT_SLOT_MAP_SIZE;
    for (uint32_t i = 0; i < NT_SLOT_MAP_SIZE; i++) {
        uint32_t probe = (idx + i) % NT_SLOT_MAP_SIZE;
        uint16_t si = s_resource.slot_map[probe];
        if (si == 0) {
            return 0;
        }
        if (s_resource.slots[si].resource_id == resource_id) {
            return si;
        }
    }
    return 0;
}

static void slot_map_insert(uint32_t resource_id, uint16_t slot_index) {
    uint32_t idx = resource_id % NT_SLOT_MAP_SIZE;
    for (uint32_t i = 0; i < NT_SLOT_MAP_SIZE; i++) {
        uint32_t probe = (idx + i) % NT_SLOT_MAP_SIZE;
        if (s_resource.slot_map[probe] == 0) {
            s_resource.slot_map[probe] = slot_index;
            return;
        }
    }
}

/* ---- Asset slot allocation (reuses holes before appending) ---- */

static uint32_t asset_alloc(void) {
    for (uint32_t i = 0; i < s_resource.asset_hwm; i++) {
        if (s_resource.assets[i].resource_id == 0) {
            return i;
        }
    }
    if (s_resource.asset_hwm >= NT_RESOURCE_MAX_ASSETS) {
        return UINT32_MAX;
    }
    return s_resource.asset_hwm++;
}

/* ---- Lifecycle ---- */

nt_result_t nt_resource_init(const nt_resource_desc_t *desc) {
    (void)desc;

    if (s_resource.initialized) {
        return NT_ERR_INIT_FAILED;
    }

    memset(&s_resource, 0, sizeof(s_resource));

    /* Fill free queue: stack with lowest index on top (first alloc gets 1) */
    s_resource.queue_top = NT_RESOURCE_MAX_SLOTS;
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_SLOTS; i++) {
        s_resource.free_queue[i] = (uint16_t)(NT_RESOURCE_MAX_SLOTS - i); /* top has lowest */
    }

    s_resource.initialized = true;
    return NT_OK;
}

void nt_resource_shutdown(void) { memset(&s_resource, 0, sizeof(s_resource)); }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_resource_step(void) {
    if (!s_resource.initialized || !s_resource.needs_resolve) {
        return; /* O(1) fast path when nothing changed */
    }

    /* Per-slot tiebreak tracking (static to avoid stack pressure on WASM) */
    static uint16_t resolve_pack[NT_RESOURCE_MAX_SLOTS + 1];

    /* Phase 1: Reset all active slots */
    for (uint16_t si = 1; si <= NT_RESOURCE_MAX_SLOTS; si++) {
        NtResourceSlot *slot = &s_resource.slots[si];
        if (slot->resource_id == 0) {
            continue;
        }
        slot->runtime_handle = 0;
        slot->state = NT_ASSET_STATE_REGISTERED;
        slot->resolve_prio = INT16_MIN;
        resolve_pack[si] = 0;
    }

    /* Phase 2: Single pass over assets — O(A) via slot_map lookup */
    for (uint32_t ai = 0; ai < s_resource.asset_hwm; ai++) {
        NtAssetMeta *meta = &s_resource.assets[ai];
        if (meta->resource_id == 0) {
            continue;
        }
        if (meta->pack_index >= NT_RESOURCE_MAX_PACKS || s_resource.packs[meta->pack_index].mounted == 0) {
            continue;
        }

        uint16_t si = slot_map_find(meta->resource_id);
        if (si == 0) {
            continue;
        }

        NtResourceSlot *slot = &s_resource.slots[si];

        /* Track best available state from any matching entry */
        if (meta->state > slot->state && slot->state != NT_ASSET_STATE_READY) {
            slot->state = meta->state;
        }

        if (meta->state != NT_ASSET_STATE_READY) {
            continue;
        }

        int16_t prio = s_resource.packs[meta->pack_index].priority;

        /* Higher priority wins. Equal priority: higher pack_index wins (last mounted) */
        if (prio > slot->resolve_prio || (prio == slot->resolve_prio && meta->pack_index >= resolve_pack[si])) {
            slot->runtime_handle = meta->runtime_handle;
            slot->state = NT_ASSET_STATE_READY;
            slot->resolve_prio = prio;
            resolve_pack[si] = meta->pack_index;
        }
    }

    /* Phase 3: Texture placeholder fallback */
    if (s_resource.placeholder_texture != 0) {
        for (uint16_t si = 1; si <= NT_RESOURCE_MAX_SLOTS; si++) {
            NtResourceSlot *slot = &s_resource.slots[si];
            if (slot->resource_id == 0) {
                continue;
            }
            if (slot->state == NT_ASSET_STATE_READY) {
                continue;
            }
            if (slot->asset_type == NT_ASSET_TEXTURE) {
                slot->runtime_handle = s_resource.placeholder_texture;
            }
        }
    }

    s_resource.needs_resolve = false;
}

/* ---- Hash utility ---- */

uint32_t nt_resource_hash(const char *name) {
    if (!name) {
        return NT_FNV1A_OFFSET_BASIS;
    }
    uint32_t hash = NT_FNV1A_OFFSET_BASIS;
    for (const char *p = name; *p != '\0'; p++) {
        hash ^= (uint8_t)*p;
        hash *= NT_FNV1A_PRIME;
    }
    return hash;
}

/* ---- Slot allocation helpers ---- */

static inline nt_resource_t resource_make(uint16_t index, uint16_t gen) { return (nt_resource_t){.id = ((uint32_t)gen << 16) | index}; }

static nt_resource_t slot_alloc(uint32_t resource_id, uint8_t asset_type) {
    if (s_resource.queue_top == 0) {
        return NT_RESOURCE_INVALID; /* pool full */
    }

    s_resource.queue_top--;
    uint16_t index = s_resource.free_queue[s_resource.queue_top];

    NtResourceSlot *slot = &s_resource.slots[index];

    /* Increment generation (skip 0: reserved for invalid handles) */
    slot->generation++;
    if (slot->generation == 0) {
        slot->generation = 1;
    }

    slot->resource_id = resource_id;
    slot->runtime_handle = 0;
    slot->resolve_prio = INT16_MIN;
    slot->asset_type = asset_type;
    slot->state = NT_ASSET_STATE_REGISTERED;

    slot_map_insert(resource_id, index);

    return resource_make(index, slot->generation);
}

/* ---- Pack management ---- */

static nt_result_t pack_alloc(uint32_t pack_id, int16_t priority, uint8_t pack_type) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    if (find_pack(pack_id) >= 0) {
        nt_log_error("nt_resource: pack already mounted");
        return NT_ERR_INVALID_ARG;
    }

    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        if (s_resource.packs[i].mounted == 0) {
            memset(&s_resource.packs[i], 0, sizeof(NtPackMeta));
            s_resource.packs[i].pack_id = pack_id;
            s_resource.packs[i].priority = priority;
            s_resource.packs[i].pack_type = pack_type;
            s_resource.packs[i].mounted = 1;
            s_resource.needs_resolve = true;
            return NT_OK;
        }
    }

    nt_log_error("nt_resource: pack slots full");
    return NT_ERR_INVALID_ARG;
}

nt_result_t nt_resource_mount(uint32_t pack_id, int16_t priority) { return pack_alloc(pack_id, priority, NT_PACK_FILE); }

void nt_resource_unmount(uint32_t pack_id) {
    if (!s_resource.initialized) {
        return;
    }

    int16_t pack_idx = find_pack(pack_id);
    if (pack_idx < 0) {
        return;
    }

    /* Clear all assets belonging to this pack (unified for file and virtual) */
    for (uint32_t i = 0; i < s_resource.asset_hwm; i++) {
        if (s_resource.assets[i].pack_index == (uint16_t)pack_idx && s_resource.assets[i].resource_id != 0) {
            s_resource.assets[i].resource_id = 0;
        }
    }

    memset(&s_resource.packs[pack_idx], 0, sizeof(NtPackMeta));
    s_resource.needs_resolve = true;
}

nt_result_t nt_resource_set_priority(uint32_t pack_id, int16_t new_priority) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    int16_t idx = find_pack(pack_id);
    if (idx < 0) {
        nt_log_error("nt_resource: set_priority pack not found");
        return NT_ERR_INVALID_ARG;
    }

    s_resource.packs[idx].priority = new_priority;
    s_resource.needs_resolve = true;
    return NT_OK;
}

/* ---- Pack parsing ---- */

nt_result_t nt_resource_parse_pack(uint32_t pack_id, const uint8_t *blob, uint32_t blob_size) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    int16_t pack_idx = find_pack(pack_id);
    if (pack_idx < 0) {
        nt_log_error("nt_resource: parse_pack not mounted");
        return NT_ERR_INVALID_ARG;
    }

    /* Validate blob size */
    if (blob_size < sizeof(NtPackHeader)) {
        nt_log_error("nt_resource: blob too small");
        return NT_ERR_INVALID_ARG;
    }

    const NtPackHeader *h = (const NtPackHeader *)blob;

    /* Validate magic */
    if (h->magic != NT_PACK_MAGIC) {
        nt_log_error("nt_resource: bad magic");
        return NT_ERR_INVALID_ARG;
    }

    /* Validate version */
    if (h->version > NT_PACK_VERSION_MAX) {
        nt_log_error("nt_resource: unsupported version");
        return NT_ERR_INVALID_ARG;
    }

    /* Validate header_size */
    if (h->header_size > blob_size) {
        nt_log_error("nt_resource: header_size exceeds blob");
        return NT_ERR_INVALID_ARG;
    }

    /* Validate total_size */
    if (h->total_size != blob_size) {
        nt_log_error("nt_resource: total_size mismatch");
        return NT_ERR_INVALID_ARG;
    }

    /* Validate asset entries fit in header region */
    uint32_t entries_end = (uint32_t)sizeof(NtPackHeader) + (uint32_t)h->asset_count * (uint32_t)sizeof(NtAssetEntry);
    if (entries_end > h->header_size) {
        nt_log_error("nt_resource: entries overflow header region");
        return NT_ERR_INVALID_ARG;
    }

    /* Validate CRC32 */
    uint32_t data_size = blob_size - h->header_size;
    uint32_t computed = nt_crc32(blob + h->header_size, data_size);
    if (computed != h->checksum) {
        nt_log_error("nt_resource: CRC32 mismatch");
        return NT_ERR_INVALID_ARG;
    }

    /* Parse asset entries */
    const NtAssetEntry *entries = (const NtAssetEntry *)(blob + sizeof(NtPackHeader));

    for (uint16_t i = 0; i < h->asset_count; i++) {
        uint32_t idx = asset_alloc();
        if (idx == UINT32_MAX) {
            nt_log_error("nt_resource: asset array full");
            break;
        }

        NtAssetMeta *meta = &s_resource.assets[idx];
        meta->resource_id = entries[i].resource_id;
        meta->asset_type = entries[i].asset_type;
        meta->state = NT_ASSET_STATE_REGISTERED;
        meta->format_version = entries[i].format_version;
        meta->pack_index = (uint16_t)pack_idx;
        meta->_pad = 0;
        meta->offset = entries[i].offset;
        meta->size = entries[i].size;
        meta->runtime_handle = 0;
    }

    /* Update pack metadata */
    NtPackMeta *pack = &s_resource.packs[pack_idx];
    pack->asset_count = h->asset_count;
    pack->blob = blob;
    pack->blob_size = blob_size;

    s_resource.needs_resolve = true;

    return NT_OK;
}

/* ---- Resource access ---- */

nt_resource_t nt_resource_request(uint32_t resource_id, uint8_t asset_type) {
    if (!s_resource.initialized) {
        return NT_RESOURCE_INVALID;
    }

    /* O(1) lookup via slot map (idempotent) */
    uint16_t existing = slot_map_find(resource_id);
    if (existing != 0) {
        return resource_make(existing, s_resource.slots[existing].generation);
    }

    /* Not found: allocate new slot */
    nt_resource_t handle = slot_alloc(resource_id, asset_type);
    if (handle.id != 0) {
        s_resource.needs_resolve = true;
    }
    return handle;
}

uint32_t nt_resource_get(nt_resource_t handle) {
    if (handle.id == 0) {
        return 0;
    }

    uint16_t index = nt_resource_slot_index(handle);
    uint16_t gen = nt_resource_generation(handle);

    if (index == 0 || index > NT_RESOURCE_MAX_SLOTS) {
        return 0;
    }
    if (s_resource.slots[index].generation != gen) {
        return 0; /* stale handle */
    }

    return s_resource.slots[index].runtime_handle;
}

bool nt_resource_is_ready(nt_resource_t handle) {
    if (handle.id == 0) {
        return false;
    }

    uint16_t index = nt_resource_slot_index(handle);
    uint16_t gen = nt_resource_generation(handle);

    if (index == 0 || index > NT_RESOURCE_MAX_SLOTS) {
        return false;
    }
    if (s_resource.slots[index].generation != gen) {
        return false; /* stale handle */
    }

    return s_resource.slots[index].state == NT_ASSET_STATE_READY;
}

uint8_t nt_resource_get_state(nt_resource_t handle) {
    if (handle.id == 0) {
        return NT_ASSET_STATE_FAILED;
    }

    uint16_t index = nt_resource_slot_index(handle);
    uint16_t gen = nt_resource_generation(handle);

    if (index == 0 || index > NT_RESOURCE_MAX_SLOTS) {
        return NT_ASSET_STATE_FAILED;
    }
    if (s_resource.slots[index].generation != gen) {
        return NT_ASSET_STATE_FAILED; /* stale handle */
    }

    return s_resource.slots[index].state;
}

/* ---- Virtual packs ---- */

nt_result_t nt_resource_create_pack(uint32_t pack_id, int16_t priority) { return pack_alloc(pack_id, priority, NT_PACK_VIRTUAL); }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_result_t nt_resource_register(uint32_t pack_id, uint32_t resource_id, uint8_t asset_type, uint32_t runtime_handle) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    int16_t pack_idx = find_pack(pack_id);
    if (pack_idx < 0) {
        nt_log_error("nt_resource: register pack not found");
        return NT_ERR_INVALID_ARG;
    }
    if (s_resource.packs[pack_idx].pack_type != NT_PACK_VIRTUAL) {
        nt_log_error("nt_resource: register on non-virtual pack");
        return NT_ERR_INVALID_ARG;
    }

    /* Check for duplicate: update existing entry */
    for (uint32_t i = 0; i < s_resource.asset_hwm; i++) {
        if (s_resource.assets[i].resource_id == resource_id && s_resource.assets[i].pack_index == (uint16_t)pack_idx) {
            s_resource.assets[i].runtime_handle = runtime_handle;
            s_resource.assets[i].state = NT_ASSET_STATE_READY;
            s_resource.needs_resolve = true;
            return NT_OK;
        }
    }

    uint32_t idx = asset_alloc();
    if (idx == UINT32_MAX) {
        nt_log_error("nt_resource: asset array full");
        return NT_ERR_INVALID_ARG;
    }

    NtAssetMeta *meta = &s_resource.assets[idx];
    meta->resource_id = resource_id;
    meta->asset_type = asset_type;
    meta->state = NT_ASSET_STATE_READY;
    meta->format_version = 0;
    meta->pack_index = (uint16_t)pack_idx;
    meta->_pad = 0;
    meta->offset = 0;
    meta->size = 0;
    meta->runtime_handle = runtime_handle;

    s_resource.packs[pack_idx].asset_count++;
    s_resource.needs_resolve = true;

    return NT_OK;
}

void nt_resource_unregister(uint32_t pack_id, uint32_t resource_id) {
    if (!s_resource.initialized) {
        return;
    }

    int16_t pack_idx = find_pack(pack_id);
    if (pack_idx < 0) {
        return;
    }

    for (uint32_t i = 0; i < s_resource.asset_hwm; i++) {
        if (s_resource.assets[i].resource_id == resource_id && s_resource.assets[i].pack_index == (uint16_t)pack_idx) {
            s_resource.assets[i].resource_id = 0; /* mark as free */
            if (s_resource.packs[pack_idx].asset_count > 0) {
                s_resource.packs[pack_idx].asset_count--;
            }
            s_resource.needs_resolve = true;
            return;
        }
    }
}

/* ---- Placeholder ---- */

void nt_resource_set_placeholder_texture(uint32_t runtime_handle) {
    if (!s_resource.initialized) {
        return;
    }
    s_resource.placeholder_texture = runtime_handle;
    s_resource.needs_resolve = true;
}

/* ---- Test access (test-only) ---- */

#ifdef NT_RESOURCE_TEST_ACCESS

void nt_resource_test_set_asset_state(uint32_t resource_id, uint16_t pack_index, uint8_t state, uint32_t runtime_handle) {
    for (uint32_t i = 0; i < s_resource.asset_hwm; i++) {
        if (s_resource.assets[i].resource_id == resource_id && s_resource.assets[i].pack_index == pack_index) {
            s_resource.assets[i].state = state;
            s_resource.assets[i].runtime_handle = runtime_handle;
            s_resource.needs_resolve = true;
            return;
        }
    }
}

#endif /* NT_RESOURCE_TEST_ACCESS */
