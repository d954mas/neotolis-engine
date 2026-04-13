#include "atlas/nt_atlas.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "nt_atlas_format.h"
#include "nt_pack_format.h"
#include "resource/nt_resource.h"

_Static_assert(sizeof(nt_atlas_vertex_t) == 8, "nt_atlas_vertex_t must match NtAtlasVertex (8 bytes)");
_Static_assert(sizeof(nt_texture_region_t) == 40, "nt_texture_region_t layout changed — update translate_region()");

// #region module state
static struct {
    bool initialized;
} s_atlas;
// #endregion

// #region internal data type
/* Hash-table entry — open addressing, linear probing.
 *
 * Sentinels for nt_atlas_hash_entry_t.region_index:
 *   EMPTY    = 0               probe stops here
 *   OCCUPIED = 1 .. N          stored as actual region index + 1
 *
 * Storing (index + 1) means the first registered region (index 0) is
 * distinguishable from an EMPTY slot. The 1-based form is collapsed back
 * to the 0-based index on lookup.
 *
 * The table is rebuilt from scratch after each parse/merge — no DELETED
 * sentinel, no incremental growth, no load-factor tracking. */
#define NT_ATLAS_HT_EMPTY ((uint32_t)0)

typedef struct {
    uint64_t name_hash;    /* ignored when region_index == EMPTY */
    uint32_t region_index; /* EMPTY / (index + 1) */
} nt_atlas_hash_entry_t;

typedef struct nt_atlas_data {
    /* Region metadata — stable append-only, tombstones never reclaim slots */
    nt_texture_region_t *regions;
    uint32_t region_count;    /* high-water including tombstones */
    uint32_t region_capacity; /* allocated size of regions[] */

    /* Owned vertex buffer — fragmentation from shrinking regions accepted */
    nt_atlas_vertex_t *vertices;
    uint32_t vertex_count; /* append cursor */
    uint32_t vertex_capacity;

    /* Owned index buffer */
    uint16_t *indices;
    uint32_t index_count; /* append cursor */
    uint32_t index_capacity;

    /* Open-addressing hash table — power-of-two, linear probing.
     * Rebuilt from scratch on each parse/merge — no incremental growth. */
    nt_atlas_hash_entry_t *hash_table;
    uint32_t hash_capacity; /* pow2, >= next_pow2(live_region_count * 2) */

    // Page texture ids + cached resource handles.
    // on_resolve stores raw ids. on_post_resolve builds the slots after the
    // resolve pass. nt_atlas_get_page_resource stays O(1).
    uint64_t page_resource_ids[NT_ATLAS_MAX_PAGES];
    nt_resource_t page_resources[NT_ATLAS_MAX_PAGES]; /* NT_RESOURCE_INVALID until on_post_resolve */
    uint8_t page_count;
} nt_atlas_data_t;
// #endregion

// #region hash table helpers
static uint32_t next_pow2(uint32_t v) {
    if (v <= 1) {
        return 1;
    }
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

/* Return the OCCUPIED region_index (0-based, or NT_ATLAS_INVALID_REGION).
 * Stops at EMPTY. */
static uint32_t hash_find(const nt_atlas_data_t *ad, uint64_t name_hash) {
    if (ad->hash_capacity == 0) {
        return NT_ATLAS_INVALID_REGION;
    }
    const uint32_t mask = ad->hash_capacity - 1;
    uint32_t pos = (uint32_t)(name_hash & mask);
    for (uint32_t steps = 0; steps < ad->hash_capacity; steps++) {
        const nt_atlas_hash_entry_t *e = &ad->hash_table[pos];
        if (e->region_index == NT_ATLAS_HT_EMPTY) {
            return NT_ATLAS_INVALID_REGION;
        }
        if (e->name_hash == name_hash) {
            return e->region_index - 1U; /* collapse 1-based to 0-based */
        }
        pos = (pos + 1U) & mask;
    }
    return NT_ATLAS_INVALID_REGION;
}

/* Raw insert into a pre-sized table. Caller guarantees enough EMPTY slots. */
static void hash_insert_raw(nt_atlas_hash_entry_t *table, uint32_t capacity, uint64_t name_hash, uint32_t region_index) {
    NT_ASSERT(capacity > 0);
    const uint32_t mask = capacity - 1;
    uint32_t pos = (uint32_t)(name_hash & mask);
    for (uint32_t steps = 0; steps < capacity; steps++) {
        nt_atlas_hash_entry_t *e = &table[pos];
        if (e->region_index == NT_ATLAS_HT_EMPTY) {
            e->name_hash = name_hash;
            e->region_index = region_index + 1U; /* 1-based */
            return;
        }
        pos = (pos + 1U) & mask;
    }
    NT_ASSERT(0 && "hash table full — capacity invariant violated");
}

/* Free the old hash table and rebuild from live (non-tombstone) regions.
 * Called once after first parse and once after each merge. */
static void hash_rebuild(nt_atlas_data_t *ad) {
    free(ad->hash_table);

    uint32_t live = 0;
    for (uint32_t i = 0; i < ad->region_count; i++) {
        if (ad->regions[i].name_hash != NT_ATLAS_TOMBSTONE_HASH) {
            live++;
        }
    }

    uint32_t cap = next_pow2(live * 2U);
    if (cap < 16U) {
        cap = 16U;
    }

    ad->hash_table = (nt_atlas_hash_entry_t *)calloc(cap, sizeof(nt_atlas_hash_entry_t));
    NT_ASSERT(ad->hash_table);
    ad->hash_capacity = cap;

    for (uint32_t i = 0; i < ad->region_count; i++) {
        if (ad->regions[i].name_hash != NT_ATLAS_TOMBSTONE_HASH) {
            hash_insert_raw(ad->hash_table, cap, ad->regions[i].name_hash, i);
        }
    }
}
// #endregion

// #region growth helpers
static void grow_regions_if_needed(nt_atlas_data_t *ad, uint32_t add) {
    if (ad->region_count + add <= ad->region_capacity) {
        return;
    }
    uint32_t new_cap = ad->region_capacity == 0 ? 16U : ad->region_capacity * 2U;
    while (new_cap < ad->region_count + add) {
        new_cap *= 2U;
    }
    nt_texture_region_t *new_buf = (nt_texture_region_t *)realloc(ad->regions, new_cap * sizeof(nt_texture_region_t));
    NT_ASSERT(new_buf);
    /* Zero the tail so newly-available slots start in a defined state */
    memset(new_buf + ad->region_capacity, 0, (new_cap - ad->region_capacity) * sizeof(nt_texture_region_t));
    ad->regions = new_buf;
    ad->region_capacity = new_cap;
}

// #endregion

// #region translate_region
/* Field-by-field copy from the packed blob struct to the runtime struct.
 * Order differs for padding efficiency; all values are raw (no decode). */
static void translate_region(nt_texture_region_t *dst, const NtAtlasRegion *src) {
    dst->name_hash = src->name_hash;
    dst->vertex_start = src->vertex_start;
    dst->index_start = src->index_start;
    dst->origin_x = src->origin_x;
    dst->origin_y = src->origin_y;
    dst->source_w = src->source_w;
    dst->source_h = src->source_h;
    dst->trim_offset_x = src->trim_offset_x;
    dst->trim_offset_y = src->trim_offset_y;
    dst->vertex_count = src->vertex_count;
    dst->index_count = src->index_count;
    dst->page_index = src->page_index;
    dst->transform = src->transform;
}
// #endregion

// #region replace_pages
// Replace the page id array wholesale from a new blob (D-05).
// Invalidate page handles here. atlas_on_post_resolve() rebuilds them after
// the resolve pass, where nt_resource_request is legal.
// Source is a raw byte pointer because page-id storage may be only 4-byte
// aligned by the pack. memcpy copies the bytes into ad->page_resource_ids.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void replace_pages(nt_atlas_data_t *ad, const uint8_t *new_page_ids_bytes, uint32_t page_bytes, uint8_t new_page_count) {
    NT_ASSERT(new_page_count <= NT_ATLAS_MAX_PAGES);
    NT_ASSERT(page_bytes == (uint32_t)new_page_count * (uint32_t)sizeof(uint64_t));
    if (new_page_count > 0) {
        memcpy(ad->page_resource_ids, new_page_ids_bytes, page_bytes);
    }
    /* Clear tail (including the un-used portion up to NT_ATLAS_MAX_PAGES) */
    if (new_page_count < NT_ATLAS_MAX_PAGES) {
        memset(&ad->page_resource_ids[new_page_count], 0, (NT_ATLAS_MAX_PAGES - new_page_count) * sizeof(uint64_t));
    }
    for (uint32_t i = 0; i < NT_ATLAS_MAX_PAGES; i++) {
        ad->page_resources[i] = NT_RESOURCE_INVALID;
    }
    ad->page_count = new_page_count;
}
// #endregion

// #region activator callbacks
/* D-10: trivial no-op activator. The real work happens in on_resolve;
 * activate only materializes a stable runtime winner handle for stacking. */
static uint32_t atlas_activate(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    return 1; /* non-zero fake handle marks slot READY */
}

/* D-11: deactivate must NOT touch user_data — on_cleanup owns that lifecycle. */
static void atlas_deactivate(uint32_t runtime_handle) { (void)runtime_handle; }

/* Blob views carved out of raw bytes once the header has been validated.
 *
 * Pack asset alignment (NT_PACK_ASSET_ALIGN, 4) does NOT guarantee 8-byte or
 * 16-byte alignment of the inner atlas payload, so types with stricter
 * alignment than pack-1 (uint64_t page_ids, uint16_t indices) are kept as
 * raw byte pointers. Consumers read them via memcpy into an aligned
 * destination — this avoids UBSan -fsanitize=alignment trips on the
 * data pointer that Phase D hands to the resolve callback. The packed
 * NtAtlasRegion / NtAtlasVertex structs have alignof==1 thanks to
 * pragma pack(1), so pointer casts over those are legal. */
typedef struct {
    const NtAtlasHeader *hdr;
    const uint8_t *page_ids_bytes;
    const NtAtlasRegion *regions;
    const NtAtlasVertex *verts;
    const uint8_t *indices_bytes;
    uint32_t page_bytes;
    uint32_t vertex_bytes;
    uint32_t index_bytes;
} nt_atlas_blob_view_t;

static bool mul_u32_checked(uint32_t lhs, uint32_t rhs, uint32_t *out) {
    NT_ASSERT(out != NULL);
    if (rhs == 0) {
        *out = 0;
        return true;
    }
    if (lhs > UINT32_MAX / rhs) {
        return false;
    }
    *out = lhs * rhs;
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool atlas_try_validate_and_carve_blob(const uint8_t *data, uint32_t size, nt_atlas_blob_view_t *out) {
    if (data == NULL || out == NULL || size < sizeof(NtAtlasHeader)) {
        return false;
    }

    const NtAtlasHeader *hdr = (const NtAtlasHeader *)data;
    if (hdr->magic != NT_ATLAS_MAGIC || hdr->version != NT_ATLAS_VERSION || hdr->page_count > NT_ATLAS_MAX_PAGES) {
        return false;
    }

    uint32_t page_bytes = 0;
    uint32_t region_bytes = 0;
    uint32_t vertex_bytes = 0;
    uint32_t index_bytes = 0;
    if (!mul_u32_checked((uint32_t)hdr->page_count, (uint32_t)sizeof(uint64_t), &page_bytes) || !mul_u32_checked((uint32_t)hdr->region_count, (uint32_t)sizeof(NtAtlasRegion), &region_bytes) ||
        !mul_u32_checked(hdr->total_vertex_count, (uint32_t)sizeof(NtAtlasVertex), &vertex_bytes) || !mul_u32_checked(hdr->total_index_count, (uint32_t)sizeof(uint16_t), &index_bytes)) {
        return false;
    }

    uint32_t meta_end = (uint32_t)sizeof(NtAtlasHeader);
    if (page_bytes > size - meta_end) {
        return false;
    }
    meta_end += page_bytes;
    if (region_bytes > size - meta_end) {
        return false;
    }
    meta_end += region_bytes;

    if (hdr->vertex_offset != meta_end) {
        return false;
    }
    if (vertex_bytes > size - hdr->vertex_offset) {
        return false;
    }

    const uint32_t expected_index_offset = hdr->vertex_offset + vertex_bytes;
    if (hdr->index_offset != expected_index_offset) {
        return false;
    }
    if (index_bytes > size - hdr->index_offset) {
        return false;
    }

    const uint8_t *page_ids_bytes = data + sizeof(NtAtlasHeader);
    const NtAtlasRegion *regions = (const NtAtlasRegion *)(page_ids_bytes + page_bytes);
    for (uint32_t i = 0; i < hdr->region_count; i++) {
        const NtAtlasRegion *region = &regions[i];
        if (hdr->page_count > 0 && region->page_index >= hdr->page_count) {
            return false;
        }
        if (region->vertex_start > hdr->total_vertex_count || region->vertex_count > hdr->total_vertex_count - region->vertex_start) {
            return false;
        }
        if (region->index_start > hdr->total_index_count || region->index_count > hdr->total_index_count - region->index_start) {
            return false;
        }
    }

    out->hdr = hdr;
    out->page_ids_bytes = page_ids_bytes;
    out->regions = regions;
    out->verts = (hdr->total_vertex_count > 0) ? (const NtAtlasVertex *)(data + hdr->vertex_offset) : NULL;
    out->indices_bytes = (hdr->total_index_count > 0) ? data + hdr->index_offset : NULL;
    out->page_bytes = page_bytes;
    out->vertex_bytes = vertex_bytes;
    out->index_bytes = index_bytes;
    return true;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_atlas_data_t *first_parse_allocate(const NtAtlasHeader *hdr) {
    nt_atlas_data_t *ad = (nt_atlas_data_t *)calloc(1, sizeof(*ad));
    NT_ASSERT(ad);

    ad->region_capacity = (hdr->region_count == 0) ? 16U : (uint32_t)hdr->region_count;
    ad->regions = (nt_texture_region_t *)calloc(ad->region_capacity, sizeof(nt_texture_region_t));
    NT_ASSERT(ad->regions);

    ad->vertex_capacity = (hdr->total_vertex_count == 0) ? 64U : hdr->total_vertex_count;
    ad->vertices = (nt_atlas_vertex_t *)malloc(ad->vertex_capacity * sizeof(nt_atlas_vertex_t));
    NT_ASSERT(ad->vertices);

    ad->index_capacity = (hdr->total_index_count == 0) ? 128U : hdr->total_index_count;
    ad->indices = (uint16_t *)malloc(ad->index_capacity * sizeof(uint16_t));
    NT_ASSERT(ad->indices);

    ad->hash_table = NULL;
    ad->hash_capacity = 0;
    return ad;
}

static void replace_payload_buffers(nt_atlas_data_t *ad, const nt_atlas_blob_view_t *v) {
    const NtAtlasHeader *hdr = v->hdr;
    if (hdr->total_vertex_count > ad->vertex_capacity) {
        nt_atlas_vertex_t *new_buf = (nt_atlas_vertex_t *)realloc(ad->vertices, (size_t)hdr->total_vertex_count * sizeof(nt_atlas_vertex_t));
        NT_ASSERT(new_buf);
        ad->vertices = new_buf;
        ad->vertex_capacity = hdr->total_vertex_count;
    }
    if (hdr->total_index_count > ad->index_capacity) {
        uint16_t *new_buf = (uint16_t *)realloc(ad->indices, (size_t)hdr->total_index_count * sizeof(uint16_t));
        NT_ASSERT(new_buf);
        ad->indices = new_buf;
        ad->index_capacity = hdr->total_index_count;
    }

    /* Bulk-copy the shared payload arrays verbatim from the blob. This keeps
     * duplicate regions sharing the same vertex/index slices exactly as the
     * builder serialized them. Indices are copied as raw bytes because the
     * source alignment can be as weak as 1 byte. */
    if (v->vertex_bytes > 0) {
        memcpy(ad->vertices, v->verts, v->vertex_bytes);
    }
    if (v->index_bytes > 0) {
        memcpy(ad->indices, v->indices_bytes, v->index_bytes);
    }
    ad->vertex_count = hdr->total_vertex_count;
    ad->index_count = hdr->total_index_count;
}

static void first_parse_fill(nt_atlas_data_t *ad, const nt_atlas_blob_view_t *v) {
    replace_payload_buffers(ad, v);

    for (uint32_t i = 0; i < v->hdr->region_count; i++) {
        translate_region(&ad->regions[i], &v->regions[i]);
    }
    ad->region_count = v->hdr->region_count;

    hash_rebuild(ad);

    replace_pages(ad, v->page_ids_bytes, v->page_bytes, (uint8_t)v->hdr->page_count);
}

/* Two-pass merge: Pass 1 walks new_regions and updates common or appends new;
 * Pass 2 walks existing regions and tombstones any that are missing from the
 * new blob. After both passes, page ids are replaced wholesale (D-05).
 *
 * The shared vertex/index payload arrays are copied wholesale from the new
 * blob before the metadata pass. That preserves duplicate region sharing
 * (`vertex_start/index_start`) exactly as serialized by the builder while
 * stable region indices are maintained in regions[]. */

static void merge_existing(nt_atlas_data_t *ad, const nt_atlas_blob_view_t *v) {
    const NtAtlasHeader *hdr = v->hdr;
    const NtAtlasRegion *new_regions = v->regions;
    replace_payload_buffers(ad, v);

    /* Pre-scan: count genuinely new regions to pre-grow region storage. */
    uint32_t new_only_count = 0;
    for (uint32_t i = 0; i < hdr->region_count; i++) {
        if (hash_find(ad, new_regions[i].name_hash) == NT_ATLAS_INVALID_REGION) {
            new_only_count++;
        }
    }
    grow_regions_if_needed(ad, new_only_count);

    /* Seen-bitset: track which existing regions appear in the new blob.
     * Filled during pass 1 (common hits), consumed in pass 2 (unseen → tombstone). */
    const uint32_t pre_merge_count = ad->region_count;
    uint8_t *seen = (uint8_t *)calloc(1, (pre_merge_count + 7U) / 8U);
    NT_ASSERT(seen);

    // #region pass 1: common+new
    for (uint32_t i = 0; i < hdr->region_count; i++) {
        const NtAtlasRegion *nr = &new_regions[i];
        const uint64_t h = nr->name_hash;
        const uint32_t existing_idx = hash_find(ad, h);

        if (existing_idx != NT_ATLAS_INVALID_REGION) {
            // #region common: in-place metadata update
            seen[existing_idx / 8U] |= (uint8_t)(1U << (existing_idx % 8U));
            translate_region(&ad->regions[existing_idx], nr);
            // #endregion
        } else {
            /* NEW: append region. Hash table rebuilt after both passes. */
            const uint32_t new_idx = ad->region_count++;
            translate_region(&ad->regions[new_idx], nr);
        }
    }
    // #endregion

    // #region pass 2: tombstones
    for (uint32_t i = 0; i < pre_merge_count; i++) {
        nt_texture_region_t *r = &ad->regions[i];
        if (r->name_hash == NT_ATLAS_TOMBSTONE_HASH) {
            continue; /* already dead from a previous merge */
        }
        const bool still_present = (seen[i / 8U] >> (i % 8U)) & 1U;
        if (!still_present) {
            NT_LOG_WARN("atlas merge: region 0x%016llx removed (not in new blob)", (unsigned long long)r->name_hash);
            r->name_hash = NT_ATLAS_TOMBSTONE_HASH;
            r->vertex_count = 0;
            r->index_count = 0;
        }
    }
    // #endregion

    free(seen);

    /* Rebuild hash table from scratch — exact size for live regions. */
    hash_rebuild(ad);

    /* Replace page resource ids wholesale per D-05. */
    replace_pages(ad, v->page_ids_bytes, v->page_bytes, (uint8_t)hdr->page_count);
}

static void atlas_on_resolve(const uint8_t *data, uint32_t size, uint32_t runtime_handle, void **user_data) {
    (void)runtime_handle; /* always 1 — our no-op activator's fake handle */

    /* Blob eviction edge case (R2 / §Q2): if the winning blob is no longer
     * resident, keep existing user_data as-is. */
    if (data == NULL || size == 0) {
        return;
    }

    nt_atlas_blob_view_t view;
    NT_ASSERT(atlas_try_validate_and_carve_blob(data, size, &view));

    nt_atlas_data_t *ad = (nt_atlas_data_t *)*user_data;
    if (ad == NULL) {
        ad = first_parse_allocate(view.hdr);
        first_parse_fill(ad, &view);
        *user_data = ad;
        return;
    }

    /* ---- Merge path (Plan 02) — diff new blob against existing regions
     * by name_hash. Implements D-03 stable-index semantics:
     *   - payload arrays are replaced wholesale from the new blob, preserving
     *     any shared vertex/index slices serialized by the builder
     *   - common: update metadata in place
     *   - new:    append region with fresh index, hash-insert
     *   - removed: tombstone region (name_hash = TOMBSTONE_HASH,
     *              vertex_count = 0)
     * Region indices for surviving regions NEVER shift. */
    merge_existing(ad, &view);
}

static void atlas_on_cleanup(void *user_data) {
    if (user_data == NULL) {
        return;
    }
    nt_atlas_data_t *ad = (nt_atlas_data_t *)user_data;
    free(ad->regions);
    free(ad->vertices);
    free(ad->indices);
    free(ad->hash_table);
    free(ad);
}

static void atlas_on_post_resolve(const uint8_t *data, uint32_t size, nt_resource_t atlas, uint32_t runtime_handle, void *user_data) {
    (void)runtime_handle;
    (void)atlas;

    /* If the winner changed to an atlas whose blob is currently unavailable,
     * atlas_on_resolve kept the existing user_data untouched. Do not request
     * pages from stale data — wait for a
     * later resolve with resident bytes. */
    if (data == NULL || size == 0 || user_data == NULL) {
        return;
    }

    nt_atlas_data_t *ad = (nt_atlas_data_t *)user_data;
    for (uint8_t i = 0; i < ad->page_count; i++) {
        nt_hash64_t rid = (nt_hash64_t){ad->page_resource_ids[i]};
        ad->page_resources[i] = nt_resource_request(rid, NT_ASSET_TEXTURE);
        NT_ASSERT(ad->page_resources[i].id != 0 && "page texture slot request failed");
    }
}
// #endregion

// #region public api
nt_result_t nt_atlas_init(void) {
    NT_ASSERT(!s_atlas.initialized && "nt_atlas_init called twice");
    nt_resource_set_activator(NT_ASSET_ATLAS, atlas_activate, atlas_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_ATLAS, atlas_on_resolve, atlas_on_cleanup);
    nt_resource_set_post_resolve_callback(NT_ASSET_ATLAS, atlas_on_post_resolve);
    nt_resource_set_behavior_flags(NT_ASSET_ATLAS, NT_RESOURCE_BEHAVIOR_PUBLISH_REQUIRES_AUX | NT_RESOURCE_BEHAVIOR_AUTO_RELOAD_ON_AUX_MISS);
    s_atlas.initialized = true;
    return NT_OK;
}

uint32_t nt_atlas_find_region(nt_resource_t atlas, uint64_t name_hash) {
    nt_atlas_data_t *ad = (nt_atlas_data_t *)nt_resource_get_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_find_region on unresolved atlas");
    if (ad->region_count == 0) {
        return NT_ATLAS_INVALID_REGION;
    }
    return hash_find(ad, name_hash);
}

const nt_texture_region_t *nt_atlas_get_region(nt_resource_t atlas, uint32_t index) {
    nt_atlas_data_t *ad = (nt_atlas_data_t *)nt_resource_get_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_get_region on unresolved atlas");
    NT_ASSERT(index < ad->region_count && "nt_atlas_get_region index out of range");
    return &ad->regions[index];
}

nt_resource_t nt_atlas_get_page_resource(nt_resource_t atlas, uint8_t page_index) {
    nt_atlas_data_t *ad = (nt_atlas_data_t *)nt_resource_get_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_get_page_resource on unresolved atlas");
    NT_ASSERT(page_index < ad->page_count && "page_index out of range");
    return ad->page_resources[page_index];
}
// #endregion

// #region test access
#ifdef NT_ATLAS_TEST_ACCESS

const struct nt_atlas_data *nt_atlas_test_get_data(nt_resource_t atlas) { return (const nt_atlas_data_t *)nt_resource_get_user_data(atlas); }

uint32_t nt_atlas_test_find_region_raw(const struct nt_atlas_data *ad, uint64_t name_hash) {
    NT_ASSERT(ad != NULL);
    if (ad->region_count == 0) {
        return NT_ATLAS_INVALID_REGION;
    }
    return hash_find(ad, name_hash);
}

const nt_texture_region_t *nt_atlas_test_get_region_raw(const struct nt_atlas_data *ad, uint32_t index) {
    NT_ASSERT(ad != NULL);
    NT_ASSERT(index < ad->region_count);
    return &ad->regions[index];
}

uint32_t nt_atlas_test_region_count(const struct nt_atlas_data *ad) {
    NT_ASSERT(ad != NULL);
    return ad->region_count;
}

uint32_t nt_atlas_test_vertex_count(const struct nt_atlas_data *ad) {
    NT_ASSERT(ad != NULL);
    return ad->vertex_count;
}

uint32_t nt_atlas_test_index_count(const struct nt_atlas_data *ad) {
    NT_ASSERT(ad != NULL);
    return ad->index_count;
}

uint8_t nt_atlas_test_page_count(const struct nt_atlas_data *ad) {
    NT_ASSERT(ad != NULL);
    return ad->page_count;
}

uint64_t nt_atlas_test_page_resource_id(const struct nt_atlas_data *ad, uint8_t page_index) {
    NT_ASSERT(ad != NULL);
    NT_ASSERT(page_index < ad->page_count);
    return ad->page_resource_ids[page_index];
}

void nt_atlas_test_drive_resolve(const uint8_t *data, uint32_t size, void **user_data) {
    NT_ASSERT(user_data != NULL);
    /* runtime_handle is ignored by atlas_on_resolve (we cast it to void),
     * so any non-zero value is fine. Tests pass 1 to mirror the real flow. */
    atlas_on_resolve(data, size, 1, user_data);
}

void nt_atlas_test_drive_cleanup(void *user_data) { atlas_on_cleanup(user_data); }

uint32_t nt_atlas_test_page_resource_handle(const struct nt_atlas_data *ad, uint8_t page_index) {
    NT_ASSERT(ad != NULL);
    NT_ASSERT(page_index < ad->page_count);
    return ad->page_resources[page_index].id;
}

void nt_atlas_test_reset(void) { s_atlas.initialized = false; }

bool nt_atlas_test_validate_header(const uint8_t *data, uint32_t size) {
    nt_atlas_blob_view_t ignored;
    return atlas_try_validate_and_carve_blob(data, size, &ignored);
}

#endif /* NT_ATLAS_TEST_ACCESS */
// #endregion
