#include "resource/nt_resource.h"

#include "resource/nt_resource_internal.h"

#include <string.h>

#include "log/nt_log.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"

/* ---- FNV-1a constants ---- */

#define NT_FNV1A_OFFSET_BASIS 0x811C9DC5U
#define NT_FNV1A_PRIME 0x01000193U

/* ---- Module state ---- */

static struct {
    NtPackMeta packs[NT_RESOURCE_MAX_PACKS];
    NtAssetMeta assets[NT_RESOURCE_MAX_ASSETS];
    NtResourceSlot slots[NT_RESOURCE_MAX_SLOTS + 1]; /* index 0 reserved */
    uint16_t generations[NT_RESOURCE_MAX_SLOTS + 1];
    uint16_t free_queue[NT_RESOURCE_MAX_SLOTS];
    uint16_t queue_top;
    uint32_t asset_count;            /* next free index in assets[] */
    uint32_t placeholder_handles[4]; /* indexed by asset_type, 0=unused */
    bool needs_resolve;
    bool initialized;
} s_resource;

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

    /* Resolve each active slot to best available AssetMeta entry */
    for (uint16_t si = 1; si <= NT_RESOURCE_MAX_SLOTS; si++) {
        NtResourceSlot *slot = &s_resource.slots[si];
        if (slot->resource_id == 0) {
            continue; /* unused slot */
        }

        /* Scan assets[] for matching resource_id, find best READY entry */
        int32_t best_idx = -1;
        int16_t best_priority = -32768; /* INT16_MIN */
        uint16_t best_pack_idx = 0;
        uint8_t best_any_state = NT_ASSET_STATE_REGISTERED;

        for (uint32_t ai = 0; ai < s_resource.asset_count; ai++) {
            NtAssetMeta *meta = &s_resource.assets[ai];
            if (meta->resource_id != slot->resource_id) {
                continue;
            }

            /* Track best available state from any matching entry */
            if (meta->state > best_any_state) {
                best_any_state = meta->state;
            }

            if (meta->state != NT_ASSET_STATE_READY) {
                continue; /* only READY entries compete for resolve */
            }

            /* Check if this pack is still mounted */
            if (meta->pack_index >= NT_RESOURCE_MAX_PACKS || s_resource.packs[meta->pack_index].mounted == 0) {
                continue;
            }

            int16_t prio = s_resource.packs[meta->pack_index].priority;

            /* Higher priority wins. Equal priority: higher pack_index wins (last mounted) */
            if (best_idx < 0 || prio > best_priority || (prio == best_priority && meta->pack_index >= best_pack_idx)) {
                best_idx = (int32_t)ai;
                best_priority = prio;
                best_pack_idx = meta->pack_index;
            }
        }

        if (best_idx >= 0) {
            /* Found a READY entry */
            slot->gfx_handle = s_resource.assets[best_idx].runtime_handle;
            slot->state = NT_ASSET_STATE_READY;
        } else {
            /* No READY entry; use placeholder if available */
            uint8_t type = slot->asset_type;
            if (type < 4 && s_resource.placeholder_handles[type] != 0) {
                slot->gfx_handle = s_resource.placeholder_handles[type];
            } else {
                slot->gfx_handle = 0;
            }
            slot->state = best_any_state;
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

    /* Increment generation (skip 0: reserved for invalid handles) */
    s_resource.generations[index]++;
    if (s_resource.generations[index] == 0) {
        s_resource.generations[index] = 1;
    }
    uint16_t gen = s_resource.generations[index];

    NtResourceSlot *slot = &s_resource.slots[index];
    slot->resource_id = resource_id;
    slot->gfx_handle = 0;
    slot->generation = gen;
    slot->asset_type = asset_type;
    slot->state = NT_ASSET_STATE_REGISTERED;

    return resource_make(index, gen);
}

/* ---- Pack management ---- */

nt_result_t nt_resource_mount(uint32_t pack_id, int16_t priority) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    /* Check for duplicate pack_id */
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        if (s_resource.packs[i].mounted == 1 && s_resource.packs[i].pack_id == pack_id) {
            return NT_ERR_INVALID_ARG; /* duplicate */
        }
    }

    /* Find free slot in packs[] */
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        if (s_resource.packs[i].mounted == 0) {
            memset(&s_resource.packs[i], 0, sizeof(NtPackMeta));
            s_resource.packs[i].pack_id = pack_id;
            s_resource.packs[i].priority = priority;
            s_resource.packs[i].pack_type = NT_PACK_FILE;
            s_resource.packs[i].mounted = 1;
            s_resource.needs_resolve = true;
            return NT_OK;
        }
    }

    return NT_ERR_INVALID_ARG; /* no free slot */
}

void nt_resource_unmount(uint32_t pack_id) {
    if (!s_resource.initialized) {
        return;
    }

    /* Find pack by pack_id */
    int16_t pack_idx = -1;
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        if (s_resource.packs[i].mounted == 1 && s_resource.packs[i].pack_id == pack_id) {
            pack_idx = (int16_t)i;
            break;
        }
    }
    if (pack_idx < 0) {
        return; /* not found: silent no-op */
    }

    /* Clear AssetMeta entries belonging to this pack */
    NtPackMeta *pack = &s_resource.packs[pack_idx];
    for (uint16_t i = 0; i < pack->asset_count; i++) {
        uint32_t asset_idx = (uint32_t)pack->asset_start + i;
        if (asset_idx < NT_RESOURCE_MAX_ASSETS) {
            s_resource.assets[asset_idx].resource_id = 0;
        }
    }

    /* Clear pack slot */
    memset(pack, 0, sizeof(NtPackMeta));
    s_resource.needs_resolve = true;
}

nt_result_t nt_resource_set_priority(uint32_t pack_id, int16_t new_priority) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    /* Find pack by pack_id */
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        if (s_resource.packs[i].mounted == 1 && s_resource.packs[i].pack_id == pack_id) {
            s_resource.packs[i].priority = new_priority;
            s_resource.needs_resolve = true;
            return NT_OK;
        }
    }

    return NT_ERR_INVALID_ARG; /* pack not found */
}

/* ---- Pack parsing ---- */

nt_result_t nt_resource_parse_pack(uint32_t pack_id, const uint8_t *blob, uint32_t blob_size) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    /* Find mounted pack by pack_id */
    int16_t pack_idx = -1;
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        if (s_resource.packs[i].mounted == 1 && s_resource.packs[i].pack_id == pack_id) {
            pack_idx = (int16_t)i;
            break;
        }
    }
    if (pack_idx < 0) {
        return NT_ERR_INVALID_ARG; /* pack not mounted */
    }

    /* Validate blob size */
    if (blob_size < sizeof(NtPackHeader)) {
        return NT_ERR_INVALID_ARG;
    }

    const NtPackHeader *h = (const NtPackHeader *)blob;

    /* Validate magic */
    if (h->magic != NT_PACK_MAGIC) {
        return NT_ERR_INVALID_ARG;
    }

    /* Validate version */
    if (h->version > NT_PACK_VERSION_MAX) {
        return NT_ERR_INVALID_ARG;
    }

    /* Validate header_size */
    if (h->header_size > blob_size) {
        return NT_ERR_INVALID_ARG;
    }

    /* Validate total_size */
    if (h->total_size != blob_size) {
        return NT_ERR_INVALID_ARG;
    }

    /* Validate CRC32 */
    uint32_t data_size = blob_size - h->header_size;
    uint32_t computed = nt_crc32(blob + h->header_size, data_size);
    if (computed != h->checksum) {
        return NT_ERR_INVALID_ARG;
    }

    /* Parse asset entries */
    const NtAssetEntry *entries = (const NtAssetEntry *)(blob + sizeof(NtPackHeader));

    uint16_t asset_start = (uint16_t)s_resource.asset_count;

    for (uint16_t i = 0; i < h->asset_count; i++) {
        if (s_resource.asset_count >= NT_RESOURCE_MAX_ASSETS) {
            nt_log_error("nt_resource: asset array full");
            break;
        }

        uint32_t idx = s_resource.asset_count;
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

        s_resource.asset_count++;
    }

    /* Update pack metadata */
    NtPackMeta *pack = &s_resource.packs[pack_idx];
    pack->asset_start = asset_start;
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

    /* Scan existing slots for matching resource_id (idempotent) */
    for (uint16_t i = 1; i <= NT_RESOURCE_MAX_SLOTS; i++) {
        if (s_resource.slots[i].resource_id == resource_id) {
            return resource_make(i, s_resource.slots[i].generation);
        }
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
    if (s_resource.generations[index] != gen) {
        return 0; /* stale handle */
    }

    return s_resource.slots[index].gfx_handle;
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
    if (s_resource.generations[index] != gen) {
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
    if (s_resource.generations[index] != gen) {
        return NT_ASSET_STATE_FAILED; /* stale handle */
    }

    return s_resource.slots[index].state;
}

/* ---- Virtual packs (stubs) ---- */

nt_result_t nt_resource_create_pack(uint32_t pack_id, int16_t priority) {
    (void)pack_id;
    (void)priority;
    return NT_ERR_INVALID_ARG; /* Stub: implemented in 24-03 */
}

nt_result_t nt_resource_register(uint32_t pack_id, uint32_t resource_id, uint8_t asset_type, uint32_t gfx_handle) {
    (void)pack_id;
    (void)resource_id;
    (void)asset_type;
    (void)gfx_handle;
    return NT_ERR_INVALID_ARG; /* Stub: implemented in 24-03 */
}

void nt_resource_unregister(uint32_t pack_id, uint32_t resource_id) {
    (void)pack_id;
    (void)resource_id;
    /* Stub: implemented in 24-03 */
}

/* ---- Placeholder (stub) ---- */

void nt_resource_set_placeholder(uint8_t asset_type, uint32_t gfx_handle) {
    (void)asset_type;
    (void)gfx_handle;
    /* Stub: implemented in 24-03 */
}

/* ---- Test access (test-only) ---- */

#ifdef NT_RESOURCE_TEST_ACCESS

void nt_resource_test_set_asset_state(uint32_t resource_id, uint16_t pack_index, uint8_t state, uint32_t runtime_handle) {
    for (uint32_t i = 0; i < s_resource.asset_count; i++) {
        if (s_resource.assets[i].resource_id == resource_id && s_resource.assets[i].pack_index == pack_index) {
            s_resource.assets[i].state = state;
            s_resource.assets[i].runtime_handle = runtime_handle;
            s_resource.needs_resolve = true;
            return;
        }
    }
}

#endif /* NT_RESOURCE_TEST_ACCESS */
