#include "atlas/nt_atlas.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "nt_atlas_format.h"
#include "nt_pack_format.h"
#include "resource/nt_resource.h"

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
 *   DELETED  = UINT32_MAX      probe continues past (tombstone)
 *   OCCUPIED = 1 .. N          stored as actual region index + 1
 *
 * Storing (index + 1) means the first registered region (index 0) is
 * distinguishable from an EMPTY slot. The 1-based form is collapsed back
 * to the 0-based index on lookup. */
#define NT_ATLAS_HT_EMPTY ((uint32_t)0)
#define NT_ATLAS_HT_DELETED ((uint32_t)0xFFFFFFFFU)

typedef struct {
    uint64_t name_hash;    /* ignored when region_index == EMPTY */
    uint32_t region_index; /* EMPTY / DELETED / (index + 1) */
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

    /* Open-addressing hash table — power-of-two, linear probing */
    nt_atlas_hash_entry_t *hash_table;
    uint32_t hash_capacity; /* pow2, >= next_pow2(region_count * 2) */
    uint32_t hash_used;     /* OCCUPIED + DELETED count; triggers rehash > cap/2 */

    /* Page texture ids + lazily resolved handles (see R1 in 48-RESEARCH.md:
     * nt_resource_request is illegal inside on_resolve, so we cache the ids
     * at parse time and resolve handles on first nt_atlas_get_page_resource
     * call — which runs OUTSIDE any callback). */
    uint64_t page_resource_ids[NT_ATLAS_MAX_PAGES];
    nt_resource_t page_resources[NT_ATLAS_MAX_PAGES]; /* NT_RESOURCE_INVALID until first get */
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
 * Skips DELETED slots, stops at EMPTY. */
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
        if (e->region_index != NT_ATLAS_HT_DELETED && e->name_hash == name_hash) {
            return e->region_index - 1U; /* collapse 1-based to 0-based */
        }
        pos = (pos + 1U) & mask;
    }
    return NT_ATLAS_INVALID_REGION;
}

/* Raw insert — caller must have ensured capacity/rehash policy.
 * Overwrites the first EMPTY or DELETED slot found along the probe chain. */
static void hash_insert_raw(nt_atlas_hash_entry_t *table, uint32_t capacity, uint64_t name_hash, uint32_t region_index) {
    NT_ASSERT(capacity > 0);
    const uint32_t mask = capacity - 1;
    uint32_t pos = (uint32_t)(name_hash & mask);
    for (uint32_t steps = 0; steps < capacity; steps++) {
        nt_atlas_hash_entry_t *e = &table[pos];
        if (e->region_index == NT_ATLAS_HT_EMPTY || e->region_index == NT_ATLAS_HT_DELETED) {
            e->name_hash = name_hash;
            e->region_index = region_index + 1U; /* 1-based */
            return;
        }
        pos = (pos + 1U) & mask;
    }
    NT_ASSERT(0 && "hash table full — capacity invariant violated");
}

/* Rebuild the hash table from the live (non-tombstone) regions, dropping any
 * DELETED markers. Used by hash_insert when the load factor (OCCUPIED+DELETED)
 * exceeds half of hash_capacity. */
static void hash_rehash(nt_atlas_data_t *ad, uint32_t new_capacity) {
    NT_ASSERT((new_capacity & (new_capacity - 1)) == 0); /* pow2 */
    nt_atlas_hash_entry_t *new_table = (nt_atlas_hash_entry_t *)calloc(new_capacity, sizeof(nt_atlas_hash_entry_t));
    NT_ASSERT(new_table);

    uint32_t live = 0;
    for (uint32_t i = 0; i < ad->region_count; i++) {
        const nt_texture_region_t *r = &ad->regions[i];
        if (r->name_hash == NT_ATLAS_TOMBSTONE_HASH) {
            continue;
        }
        hash_insert_raw(new_table, new_capacity, r->name_hash, i);
        live++;
    }

    free(ad->hash_table);
    ad->hash_table = new_table;
    ad->hash_capacity = new_capacity;
    ad->hash_used = live;
}

static void hash_insert(nt_atlas_data_t *ad, uint64_t name_hash, uint32_t region_index) {
    /* Grow / rehash if load factor > 0.5. hash_used includes DELETED so this
     * also compacts out tombstones once they dominate. */
    if (ad->hash_capacity == 0 || (ad->hash_used + 1U) * 2U > ad->hash_capacity) {
        uint32_t new_cap = ad->hash_capacity == 0 ? 16U : ad->hash_capacity * 2U;
        hash_rehash(ad, new_cap);
    }
    hash_insert_raw(ad->hash_table, ad->hash_capacity, name_hash, region_index);
    ad->hash_used++;
}

/* Mark a slot as DELETED. Probes continue past it; a future rehash drops it. */
static void hash_delete(nt_atlas_data_t *ad, uint64_t name_hash) {
    if (ad->hash_capacity == 0) {
        return;
    }
    const uint32_t mask = ad->hash_capacity - 1;
    uint32_t pos = (uint32_t)(name_hash & mask);
    for (uint32_t steps = 0; steps < ad->hash_capacity; steps++) {
        nt_atlas_hash_entry_t *e = &ad->hash_table[pos];
        if (e->region_index == NT_ATLAS_HT_EMPTY) {
            return;
        }
        if (e->region_index != NT_ATLAS_HT_DELETED && e->name_hash == name_hash) {
            e->region_index = NT_ATLAS_HT_DELETED;
            /* hash_used stays — DELETED still occupies the slot budget */
            return;
        }
        pos = (pos + 1U) & mask;
    }
}
// #endregion

// #region growth helpers
/* The growth helpers are defined here so Plan 02 can splice in the real merge
 * path without touching this TU's top-level structure. Plan 01's first-parse
 * branch sizes buffers exactly from the header and never invokes these — they
 * are marked to silence "static function unused" warnings via explicit forward
 * use in the merge stub further down. */
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

static void grow_vertices_if_needed(nt_atlas_data_t *ad, uint32_t add) {
    if (ad->vertex_count + add <= ad->vertex_capacity) {
        return;
    }
    uint32_t new_cap = ad->vertex_capacity == 0 ? 64U : ad->vertex_capacity * 2U;
    while (new_cap < ad->vertex_count + add) {
        new_cap *= 2U;
    }
    nt_atlas_vertex_t *new_buf = (nt_atlas_vertex_t *)realloc(ad->vertices, new_cap * sizeof(nt_atlas_vertex_t));
    NT_ASSERT(new_buf);
    ad->vertices = new_buf;
    ad->vertex_capacity = new_cap;
}

static void grow_indices_if_needed(nt_atlas_data_t *ad, uint32_t add) {
    if (ad->index_count + add <= ad->index_capacity) {
        return;
    }
    uint32_t new_cap = ad->index_capacity == 0 ? 128U : ad->index_capacity * 2U;
    while (new_cap < ad->index_count + add) {
        new_cap *= 2U;
    }
    uint16_t *new_buf = (uint16_t *)realloc(ad->indices, new_cap * sizeof(uint16_t));
    NT_ASSERT(new_buf);
    ad->indices = new_buf;
    ad->index_capacity = new_cap;
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
/* Replace the page id array wholesale from a new blob (D-05).
 * Clears all cached nt_resource_t handles — the new blob may reference
 * entirely different textures even at the same page_index. */
static void replace_pages(nt_atlas_data_t *ad, const uint64_t *new_page_ids, uint8_t new_page_count) {
    NT_ASSERT(new_page_count <= NT_ATLAS_MAX_PAGES);
    if (new_page_count > 0) {
        memcpy(ad->page_resource_ids, new_page_ids, new_page_count * sizeof(uint64_t));
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
 * activate exists only so the resource state machine transitions
 * REGISTERED -> READY so nt_resource_is_ready() reports true. */
static uint32_t atlas_activate(const uint8_t *data, uint32_t size) {
    (void)data;
    (void)size;
    return 1; /* non-zero fake handle marks slot READY */
}

/* D-11: deactivate must NOT touch user_data — on_cleanup owns that lifecycle. */
static void atlas_deactivate(uint32_t runtime_handle) { (void)runtime_handle; }

/* Blob views carved out of raw bytes once the header has been validated. */
typedef struct {
    const NtAtlasHeader *hdr;
    const uint64_t *page_ids;
    const NtAtlasRegion *regions;
    const NtAtlasVertex *verts;
    const uint16_t *indices;
    uint32_t vertex_bytes;
    uint32_t index_bytes;
} nt_atlas_blob_view_t;

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void validate_and_carve_blob(const uint8_t *data, uint32_t size, nt_atlas_blob_view_t *out) {
    NT_ASSERT(size >= sizeof(NtAtlasHeader));
    const NtAtlasHeader *hdr = (const NtAtlasHeader *)data;
    NT_ASSERT(hdr->magic == NT_ATLAS_MAGIC);
    NT_ASSERT(hdr->version == NT_ATLAS_VERSION);
    NT_ASSERT(hdr->page_count <= NT_ATLAS_MAX_PAGES);

    const uint32_t page_bytes = (uint32_t)hdr->page_count * (uint32_t)sizeof(uint64_t);
    const uint32_t region_bytes = (uint32_t)hdr->region_count * (uint32_t)sizeof(NtAtlasRegion);
    const uint32_t vertex_bytes = hdr->total_vertex_count * (uint32_t)sizeof(NtAtlasVertex);
    const uint32_t index_bytes = hdr->total_index_count * (uint32_t)sizeof(uint16_t);
    NT_ASSERT(size >= (uint32_t)sizeof(NtAtlasHeader) + page_bytes + region_bytes);
    NT_ASSERT(hdr->total_vertex_count == 0 || hdr->vertex_offset + vertex_bytes <= size);
    NT_ASSERT(hdr->total_index_count == 0 || hdr->index_offset + index_bytes <= size);

    out->hdr = hdr;
    out->page_ids = (const uint64_t *)(data + sizeof(NtAtlasHeader));
    out->regions = (const NtAtlasRegion *)((const uint8_t *)out->page_ids + page_bytes);
    out->verts = (hdr->total_vertex_count > 0) ? (const NtAtlasVertex *)(data + hdr->vertex_offset) : NULL;
    out->indices = (hdr->total_index_count > 0) ? (const uint16_t *)(data + hdr->index_offset) : NULL;
    out->vertex_bytes = vertex_bytes;
    out->index_bytes = index_bytes;
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

    uint32_t hcap = next_pow2(ad->region_capacity * 2U);
    if (hcap < 16U) {
        hcap = 16U;
    }
    ad->hash_capacity = hcap;
    ad->hash_table = (nt_atlas_hash_entry_t *)calloc(ad->hash_capacity, sizeof(nt_atlas_hash_entry_t));
    NT_ASSERT(ad->hash_table);
    ad->hash_used = 0;
    return ad;
}

static void first_parse_fill(nt_atlas_data_t *ad, const nt_atlas_blob_view_t *v) {
    /* Bulk-copy all vertices and indices. After this, every region's
     * original vertex_start/index_start is still correct because the
     * owned buffers are a verbatim snapshot of the blob arrays. */
    if (v->vertex_bytes > 0) {
        memcpy(ad->vertices, v->verts, v->vertex_bytes);
    }
    if (v->index_bytes > 0) {
        memcpy(ad->indices, v->indices, v->index_bytes);
    }
    ad->vertex_count = v->hdr->total_vertex_count;
    ad->index_count = v->hdr->total_index_count;

    for (uint32_t i = 0; i < v->hdr->region_count; i++) {
        translate_region(&ad->regions[i], &v->regions[i]);
        hash_insert(ad, v->regions[i].name_hash, i);
    }
    ad->region_count = v->hdr->region_count;

    replace_pages(ad, v->page_ids, (uint8_t)v->hdr->page_count);
}

static void atlas_on_resolve(const uint8_t *data, uint32_t size, uint32_t runtime_handle, void **user_data) {
    (void)runtime_handle; /* always 1 — our no-op activator's fake handle */

    /* Blob eviction edge case (R2 / §Q2): if the winning blob is no longer
     * resident, keep existing user_data as-is. */
    if (data == NULL || size == 0) {
        return;
    }

    nt_atlas_blob_view_t view;
    validate_and_carve_blob(data, size, &view);

    nt_atlas_data_t *ad = (nt_atlas_data_t *)*user_data;
    if (ad == NULL) {
        ad = first_parse_allocate(view.hdr);
        first_parse_fill(ad, &view);
        *user_data = ad;
        return;
    }

    /* Merge branch — Plan 02. The grow_* and hash_delete helpers below are
     * kept defined so Plan 02 only splices in the diff body. Touch them here
     * via a never-executed reference to satisfy -Wunused-function without
     * producing any runtime side effects. */
    (void)grow_regions_if_needed;
    (void)grow_vertices_if_needed;
    (void)grow_indices_if_needed;
    (void)hash_delete;
    NT_ASSERT(0 && "atlas on_resolve merge branch not implemented — Plan 02");
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
// #endregion

// #region public api
nt_result_t nt_atlas_init(void) {
    NT_ASSERT(!s_atlas.initialized && "nt_atlas_init called twice");
    nt_resource_set_activator(NT_ASSET_ATLAS, atlas_activate, atlas_deactivate);
    nt_resource_set_resolve_callbacks(NT_ASSET_ATLAS, atlas_on_resolve, atlas_on_cleanup);
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

    /* Lazy resolve: first call per (atlas, page) requests the texture
     * resource handle and caches it. This runs OUTSIDE any callback context,
     * so nt_resource_request is legal here (see R1 in 48-RESEARCH.md). */
    if (ad->page_resources[page_index].id == 0) {
        nt_hash64_t rid = (nt_hash64_t){ad->page_resource_ids[page_index]};
        ad->page_resources[page_index] = nt_resource_request(rid, NT_ASSET_TEXTURE);
    }
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

#endif /* NT_ATLAS_TEST_ACCESS */
// #endregion
