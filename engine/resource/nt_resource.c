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

void nt_resource_step(void) {
    if (!s_resource.initialized) {
        return;
    }
    /* Stub: clear resolve flag (full resolve logic in Plan 02) */
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

/* ---- Pack management ---- */

nt_result_t nt_resource_mount(uint32_t pack_id, int16_t priority) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    /* Find free slot in packs[] */
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        if (s_resource.packs[i].mounted == 0) {
            memset(&s_resource.packs[i], 0, sizeof(NtPackMeta));
            s_resource.packs[i].pack_id = pack_id;
            s_resource.packs[i].priority = priority;
            s_resource.packs[i].pack_type = NT_PACK_FILE;
            s_resource.packs[i].mounted = 1;
            return NT_OK;
        }
    }

    return NT_ERR_INVALID_ARG; /* no free slot */
}

void nt_resource_unmount(uint32_t pack_id) {
    (void)pack_id;
    /* Stub: implemented in 24-02 */
}

nt_result_t nt_resource_set_priority(uint32_t pack_id, int16_t new_priority) {
    (void)pack_id;
    (void)new_priority;
    return NT_ERR_INVALID_ARG; /* Stub: implemented in 24-02 */
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

/* ---- Resource access (stubs) ---- */

nt_resource_t nt_resource_request(uint32_t resource_id, uint8_t asset_type) {
    (void)resource_id;
    (void)asset_type;
    return NT_RESOURCE_INVALID; /* Stub: implemented in 24-02 */
}

uint32_t nt_resource_get(nt_resource_t handle) {
    (void)handle;
    return 0; /* Stub: implemented in 24-02 */
}

bool nt_resource_is_ready(nt_resource_t handle) {
    (void)handle;
    return false; /* Stub: implemented in 24-02 */
}

uint8_t nt_resource_get_state(nt_resource_t handle) {
    (void)handle;
    return 0; /* Stub: implemented in 24-02 */
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
