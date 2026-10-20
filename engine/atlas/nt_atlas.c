#include "atlas/nt_atlas.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "log/nt_log.h"
#include "nt_atlas_format.h"
#include "nt_pack_format.h"
#include "resource/nt_resource.h"

_Static_assert(sizeof(nt_atlas_uv_t) == sizeof(NtAtlasUv), "nt_atlas_uv_t must match NtAtlasUv");
_Static_assert(sizeof(nt_texture_region_t) == 48, "nt_texture_region_t layout changed — update translate_region()");

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
 * Removed names keep their entries so they can revive at the same index. */
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
    uint32_t revision;        /* owned snapshot generation */

    /* One owned allocation: positions, then UVs and indices. */
    float (*positions)[2];
    nt_atlas_uv_t *uvs;
    uint16_t *indices;
    uint32_t geometry_capacity; /* bytes */

    /* Open-addressing hash table — power-of-two, linear probing. */
    nt_atlas_hash_entry_t *hash_table;
    uint32_t hash_capacity; /* pow2, >= next_pow2(region_count * 2) */

    // Page texture ids + cached resource handles.
    // on_resolve stores raw ids. on_post_resolve builds the slots after the
    // resolve pass. nt_atlas_get_page_resource stays O(1).
    uint64_t page_resource_ids[NT_ATLAS_MAX_PAGES];
    nt_resource_t page_resources[NT_ATLAS_MAX_PAGES]; /* NT_RESOURCE_INVALID until on_post_resolve */
    uint8_t page_count;

    float ipu; /* intrinsic atlas scale, already baked into positions */
} nt_atlas_data_t;
// #endregion

// #region revision helpers
static void atlas_bump_revision(nt_atlas_data_t *ad) {
    NT_ASSERT(ad != NULL);
    ad->revision++;
    if (ad->revision == 0) {
        ad->revision = 1;
    }
}
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

/* Insert appended names; rebuild only when the table must grow. */
static void hash_update(nt_atlas_data_t *ad, uint32_t first_new) {
    uint32_t cap = next_pow2(ad->region_count * 2U);
    if (cap < 16U) {
        cap = 16U;
    }

    if (cap > ad->hash_capacity) {
        free(ad->hash_table);
        ad->hash_table = (nt_atlas_hash_entry_t *)calloc(cap, sizeof(nt_atlas_hash_entry_t));
        NT_ASSERT(ad->hash_table);
        ad->hash_capacity = cap;
        first_new = 0;
    }

    const uint32_t mask = ad->hash_capacity - 1;
    for (uint32_t i = first_new; i < ad->region_count; i++) {
        uint32_t pos = (uint32_t)(ad->regions[i].name_hash & mask);
        for (uint32_t steps = 0; steps < ad->hash_capacity; steps++) {
            nt_atlas_hash_entry_t *e = &ad->hash_table[pos];
            if (e->region_index == NT_ATLAS_HT_EMPTY) {
                e->name_hash = ad->regions[i].name_hash;
                e->region_index = i + 1U; /* 1-based */
                break;
            }
            pos = (pos + 1U) & mask;
        }
    }
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
    dst->flags = src->flags;
    dst->_pad0 = 0;
    memcpy(dst->slice9_lrtb, src->slice9_lrtb, sizeof(dst->slice9_lrtb));
    memset(dst->_pad, 0, sizeof(dst->_pad));
}
// #endregion

// #region replace_pages
// Replace the page id array wholesale from a new blob.
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
    memset(ad->page_resources, 0, sizeof(ad->page_resources));
    ad->page_count = new_page_count;
}
// #endregion

// #region activator callbacks
static uint32_t atlas_activate(const uint8_t *data, uint32_t size);

/* Payload arrays can be unaligned in pack data; copy bytes into owned arrays. */
typedef struct {
    const NtAtlasHeader *hdr;
    const uint8_t *page_ids_bytes;
    const NtAtlasRegion *regions;
    const uint8_t *positions_bytes;
    uint32_t page_bytes;
    uint32_t position_bytes;
    uint32_t uv_bytes;
    uint32_t index_bytes;
} nt_atlas_blob_view_t;

/* Carves the serialized sections. Asserts the layout the builder writes; the resource core
 * checked the header. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static nt_atlas_blob_view_t atlas_carve_blob(const uint8_t *data, uint32_t size) {
    const NtAtlasHeader *hdr = (const NtAtlasHeader *)data;
    NT_ASSERT(hdr->page_count <= NT_ATLAS_MAX_PAGES && "atlas page_count over the cap");
    const uint64_t page_bytes = (uint64_t)hdr->page_count * sizeof(uint64_t);
    const uint64_t region_bytes = (uint64_t)hdr->region_count * sizeof(NtAtlasRegion);
    const uint64_t position_bytes = (uint64_t)hdr->total_vertex_count * sizeof(float[2]);
    const uint64_t uv_bytes = (uint64_t)hdr->total_vertex_count * sizeof(NtAtlasUv);
    const uint64_t index_bytes = (uint64_t)hdr->total_index_count * sizeof(uint16_t);
    const uint64_t vertex_offset = sizeof(NtAtlasHeader) + page_bytes + region_bytes;
    NT_ASSERT(hdr->vertex_offset == vertex_offset && hdr->index_offset == vertex_offset + position_bytes + uv_bytes && hdr->index_offset + index_bytes <= size &&
              "atlas sections not packed inside the blob");

    const uint8_t *page_ids_bytes = data + sizeof(NtAtlasHeader);
    const NtAtlasRegion *regions = (const NtAtlasRegion *)(page_ids_bytes + page_bytes);
    for (uint32_t i = 0; i < hdr->region_count; i++) {
        const NtAtlasRegion *region = &regions[i];
        NT_ASSERT(region->name_hash != NT_ATLAS_TOMBSTONE_HASH && "atlas region name hash is the tombstone");
        /* Also bounds the page slot read in nt_atlas_get_region_handles. */
        NT_ASSERT(region->page_index < hdr->page_count && "atlas region on an undeclared page");
        NT_ASSERT(region->vertex_start <= hdr->total_vertex_count && region->vertex_count <= hdr->total_vertex_count - region->vertex_start && region->index_start <= hdr->total_index_count &&
                  region->index_count <= hdr->total_index_count - region->index_start && "atlas region span out of range");
    }
    return (nt_atlas_blob_view_t){
        .hdr = hdr,
        .page_ids_bytes = page_ids_bytes,
        .regions = regions,
        .positions_bytes = data + hdr->vertex_offset,
        .page_bytes = (uint32_t)page_bytes,
        .position_bytes = (uint32_t)position_bytes,
        .uv_bytes = (uint32_t)uv_bytes,
        .index_bytes = (uint32_t)index_bytes,
    };
}

/* AUX_BACKED slots become READY after activate, so the layout asserts precede publication. */
static uint32_t atlas_activate(const uint8_t *data, uint32_t size) {
    (void)atlas_carve_blob(data, size);
    return 1U;
}

/* Keep shared spans as serialized; every live element is overwritten before publication. */
static void replace_geometry(nt_atlas_data_t *ad, const nt_atlas_blob_view_t *v) {
    const uint32_t bytes = v->position_bytes + v->uv_bytes + v->index_bytes;
    /* Empty regions still expose non-NULL slices. */
    const uint32_t capacity = bytes < sizeof(float[2]) ? (uint32_t)sizeof(float[2]) : bytes;
    if (capacity > ad->geometry_capacity) {
        free(ad->positions);
        ad->positions = (float(*)[2])malloc(capacity);
        NT_ASSERT(ad->positions);
        ad->geometry_capacity = capacity;
    }
    if (bytes > 0) {
        memcpy(ad->positions, v->positions_bytes, bytes);
    }
    ad->uvs = (nt_atlas_uv_t *)((uint8_t *)ad->positions + v->position_bytes);
    ad->indices = (uint16_t *)((uint8_t *)ad->uvs + v->uv_bytes);
    ad->ipu = v->hdr->inverse_pixels_per_unit;
    NT_ASSERT(isfinite(ad->ipu) && ad->ipu > 0.0F && "atlas blob: builder guarantees a positive finite inverse_pixels_per_unit");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void atlas_on_resolve(const uint8_t *data, uint32_t size, uint32_t runtime_handle, void **user_data) {
    (void)runtime_handle; /* always 1 */

    /* Keep the published snapshot until the replacement blob is resident. */
    if (data == NULL || size == 0) {
        return;
    }

    const nt_atlas_blob_view_t view = atlas_carve_blob(data, size);

    nt_atlas_data_t *ad = (nt_atlas_data_t *)*user_data;

    // #region first parse
    if (ad == NULL) {
        const NtAtlasHeader *hdr = view.hdr;

        // #region allocate
        ad = (nt_atlas_data_t *)calloc(1, sizeof(*ad));
        NT_ASSERT(ad);

        ad->region_capacity = (hdr->region_count == 0) ? 16U : (uint32_t)hdr->region_count;
        ad->regions = (nt_texture_region_t *)malloc((size_t)ad->region_capacity * sizeof(nt_texture_region_t));
        NT_ASSERT(ad->regions);
        // #endregion

        replace_geometry(ad, &view);

        for (uint32_t i = 0; i < hdr->region_count; i++) {
            translate_region(&ad->regions[i], &view.regions[i]);
        }
        ad->region_count = hdr->region_count;
        atlas_bump_revision(ad);

        hash_update(ad, 0);
        replace_pages(ad, view.page_ids_bytes, view.page_bytes, (uint8_t)hdr->page_count);

        *user_data = ad;
        return;
    }
    // #endregion

    // #region merge
    const NtAtlasHeader *hdr = view.hdr;
    replace_geometry(ad, &view);

    const uint32_t pre_merge_count = ad->region_count;
    for (uint32_t i = 0; i < pre_merge_count; i++) {
        ad->regions[i].vertex_start = UINT32_MAX;
    }

    // #region update and append
    for (uint32_t i = 0; i < hdr->region_count; i++) {
        const NtAtlasRegion *nr = &view.regions[i];
        uint32_t index = hash_find(ad, nr->name_hash);
        if (index == NT_ATLAS_INVALID_REGION) {
            if (ad->region_count == ad->region_capacity) {
                uint32_t new_cap = ad->region_capacity * 2U;
                while (new_cap < hdr->region_count) {
                    new_cap *= 2U;
                }
                nt_texture_region_t *new_buf = (nt_texture_region_t *)realloc(ad->regions, (size_t)new_cap * sizeof(nt_texture_region_t));
                NT_ASSERT(new_buf);
                ad->regions = new_buf;
                ad->region_capacity = new_cap;
            }
            index = ad->region_count++;
        }
        translate_region(&ad->regions[index], nr);
    }
    // #endregion

    // #region missing names
    for (uint32_t i = 0; i < pre_merge_count; i++) {
        nt_texture_region_t *r = &ad->regions[i];
        if (r->vertex_start == UINT32_MAX) {
            NT_LOG_WARN("atlas merge: region 0x%016llx removed (not in new blob)", (unsigned long long)r->name_hash);
            r->vertex_start = 0;
            r->index_start = 0;
            r->vertex_count = 0;
            r->index_count = 0;
            r->page_index = 0;
        }
    }
    // #endregion

    atlas_bump_revision(ad);
    if (ad->region_count != pre_merge_count) {
        hash_update(ad, pre_merge_count);
    }
    replace_pages(ad, view.page_ids_bytes, view.page_bytes, (uint8_t)hdr->page_count);

    // #endregion
}

static void atlas_on_cleanup(void *user_data) {
    if (user_data == NULL) {
        return;
    }
    nt_atlas_data_t *ad = (nt_atlas_data_t *)user_data;
    free(ad->regions);
    free(ad->hash_table);
    free(ad->positions);
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
    nt_resource_register_type(
        NT_ASSET_ATLAS,
        &(nt_resource_type_desc_t){
            .activate = atlas_activate, .on_resolve = atlas_on_resolve, .on_cleanup = atlas_on_cleanup, .on_post_resolve = atlas_on_post_resolve, .behavior_flags = NT_RESOURCE_BEHAVIOR_AUX_BACKED});
    s_atlas.initialized = true;
    return NT_OK;
}

uint32_t nt_atlas_revision(nt_resource_t atlas) {
    NT_ASSERT(nt_resource_get_asset_type(atlas) == NT_ASSET_ATLAS && "nt_atlas_revision: handle is not an atlas resource");
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_revision on unresolved atlas");
    return ad->revision;
}

uint32_t nt_atlas_region_count(nt_resource_t atlas) {
    NT_ASSERT(nt_resource_get_asset_type(atlas) == NT_ASSET_ATLAS && "nt_atlas_region_count: handle is not an atlas resource");
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_region_count on unresolved atlas");
    return ad->region_count;
}

uint8_t nt_atlas_page_count(nt_resource_t atlas) {
    NT_ASSERT(nt_resource_get_asset_type(atlas) == NT_ASSET_ATLAS && "nt_atlas_page_count: handle is not an atlas resource");
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_page_count on unresolved atlas");
    return ad->page_count;
}

uint32_t nt_atlas_find_region(nt_resource_t atlas, uint64_t name_hash) {
    NT_ASSERT(nt_resource_get_asset_type(atlas) == NT_ASSET_ATLAS && "nt_atlas_find_region: handle is not an atlas resource");
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_find_region on unresolved atlas");
    if (ad->region_count == 0) {
        return NT_ATLAS_INVALID_REGION;
    }
    return hash_find(ad, name_hash);
}

const nt_texture_region_t *nt_atlas_get_region(nt_resource_t atlas, uint32_t index) {
    NT_ASSERT(nt_resource_get_asset_type(atlas) == NT_ASSET_ATLAS && "nt_atlas_get_region: handle is not an atlas resource");
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_get_region on unresolved atlas");
    NT_ASSERT(index < ad->region_count && "nt_atlas_get_region index out of range");
    return &ad->regions[index];
}

nt_resource_t nt_atlas_get_page_resource(nt_resource_t atlas, uint8_t page_index) {
    NT_ASSERT(nt_resource_get_asset_type(atlas) == NT_ASSET_ATLAS && "nt_atlas_get_page_resource: handle is not an atlas resource");
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_get_page_resource on unresolved atlas");
    NT_ASSERT(page_index < ad->page_count && "page_index out of range");
    return ad->page_resources[page_index];
}

// #region geometry accessors
float nt_atlas_get_pixels_per_unit(nt_resource_t atlas) {
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_get_pixels_per_unit on unresolved atlas");
    NT_ASSERT(ad->ipu > 0.0F && "atlas ipu must be positive");
    return 1.0F / ad->ipu;
}

float nt_atlas_get_inverse_pixels_per_unit(nt_resource_t atlas) {
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_get_inverse_pixels_per_unit on unresolved atlas");
    NT_ASSERT(ad->ipu > 0.0F && "atlas ipu must be positive");
    return ad->ipu;
}

const float (*nt_atlas_get_region_positions(nt_resource_t atlas, uint32_t region_index))[2] {
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_get_region_positions on unresolved atlas");
    NT_ASSERT(region_index < ad->region_count && "nt_atlas_get_region_positions: region_index out of range");
    return (const float(*)[2]) & ad->positions[ad->regions[region_index].vertex_start];
}

const nt_atlas_uv_t *nt_atlas_get_region_uvs(nt_resource_t atlas, uint32_t region_index) {
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_get_region_uvs on unresolved atlas");
    NT_ASSERT(region_index < ad->region_count && "nt_atlas_get_region_uvs: region_index out of range");
    return &ad->uvs[ad->regions[region_index].vertex_start];
}

const uint16_t *nt_atlas_get_region_indices(nt_resource_t atlas, uint32_t region_index) {
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_get_region_indices on unresolved atlas");
    NT_ASSERT(region_index < ad->region_count && "nt_atlas_get_region_indices: region_index out of range");
    return &ad->indices[ad->regions[region_index].index_start];
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_atlas_get_region_handles(nt_resource_t atlas, uint32_t region_index, nt_atlas_region_handles_t *out) {
    NT_ASSERT(out != NULL && "nt_atlas_get_region_handles: out must be non-NULL");
    NT_ASSERT(nt_resource_get_asset_type(atlas) == NT_ASSET_ATLAS && "nt_atlas_get_region_handles: handle is not an atlas resource");
    const nt_atlas_data_t *ad = (const nt_atlas_data_t *)nt_resource_peek_user_data(atlas);
    NT_ASSERT(ad != NULL && "nt_atlas_get_region_handles on unresolved atlas");
    NT_ASSERT(region_index < ad->region_count && "nt_atlas_get_region_handles: region_index out of range");
    NT_ASSERT(ad->ipu > 0.0F && "atlas ipu must be positive");

    const nt_texture_region_t *r = &ad->regions[region_index];
    out->region = r;
    out->positions = (const float(*)[2]) & ad->positions[r->vertex_start];
    out->uvs = &ad->uvs[r->vertex_start];
    out->indices = &ad->indices[r->index_start];
    out->page_resource = ad->page_resources[r->page_index];
    out->ipu = ad->ipu;
}
// #endregion

// #endregion

// #region test access
#ifdef NT_TEST_ACCESS

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

uint32_t nt_atlas_test_activate(const uint8_t *data, uint32_t size) { return atlas_activate(data, size); }

uint32_t nt_atlas_test_page_resource_handle(const struct nt_atlas_data *ad, uint8_t page_index) {
    NT_ASSERT(ad != NULL);
    NT_ASSERT(page_index < ad->page_count);
    return ad->page_resources[page_index].id;
}

void nt_atlas_test_reset(void) { s_atlas.initialized = false; }

const float (*nt_atlas_test_positions(const struct nt_atlas_data *ad))[2] {
    NT_ASSERT(ad != NULL);
    return (const float(*)[2])ad->positions;
}

const nt_atlas_uv_t *nt_atlas_test_uvs(const struct nt_atlas_data *ad) {
    NT_ASSERT(ad != NULL);
    return ad->uvs;
}

float nt_atlas_test_ipu(const struct nt_atlas_data *ad) {
    NT_ASSERT(ad != NULL);
    return ad->ipu;
}

#endif /* NT_TEST_ACCESS */
// #endregion
