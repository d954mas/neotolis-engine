#ifndef NT_ATLAS_H
#define NT_ATLAS_H

#include <stdint.h>

#include "core/nt_assert.h" /* NT_ASSERT_MODE gates the dev-only resolve warn below */
#include "core/nt_types.h"
#include "nt_atlas_format.h"
#include "resource/nt_resource.h"
#if NT_ASSERT_MODE == NT_ASSERT_FULL
#include "log/nt_log.h"
#endif

/* ---- Public constants ---- */

#define NT_ATLAS_INVALID_REGION ((uint32_t)0xFFFFFFFFU)
#define NT_ATLAS_TOMBSTONE_HASH ((uint64_t)0xFFFFFFFFFFFFFFFFULL)

#ifndef NT_ATLAS_MAX_PAGES
#define NT_ATLAS_MAX_PAGES 8
#endif

/* ---- Public types ---- */

/* Self-resolving "sprite-in-atlas" handle. region==NT_ATLAS_INVALID_REGION resolves lazily under
 * is_ready and memoizes back into ref->region (the type's defined contract, not a hidden side-effect);
 * atlas.id==0 = unset/no-art. Field order {name_hash, atlas, region} packs to 16 B with zero padding. */
typedef struct {
    uint64_t name_hash;  /*  0: xxh64 of region name (stable identity) */
    nt_resource_t atlas; /*  8: handle; may be set BEFORE atlas data loads */
    uint32_t region;     /* 12: NT_ATLAS_INVALID_REGION = resolve lazily; else resolved index */
} nt_atlas_region_ref_t;
_Static_assert(sizeof(nt_atlas_region_ref_t) == 16, "nt_atlas_region_ref_t stable ABI (8B hash + 4B handle + 4B region)");

/* Construct unresolved: region=INVALID resolves lazily on first emit under a ready atlas. */
static inline nt_atlas_region_ref_t nt_atlas_ref(nt_resource_t atlas, uint64_t name_hash) { return (nt_atlas_region_ref_t){.name_hash = name_hash, .atlas = atlas, .region = NT_ATLAS_INVALID_REGION}; }

/* Construct pre-resolved with a known index (skips the lazy probe). */
static inline nt_atlas_region_ref_t nt_atlas_ref_idx(nt_resource_t atlas, uint64_t name_hash, uint32_t region) {
    return (nt_atlas_region_ref_t){.name_hash = name_hash, .atlas = atlas, .region = region};
}

/* Mirrors NtAtlasVertex from shared/include/nt_atlas_format.h (8 bytes, same field order).
 * Runtime stores it identically; nt_atlas precomputes float positions/UVs before sprite batching.
 * Update both structs together when changing fields. */
typedef struct {
    int16_t local_x;
    int16_t local_y;
    uint16_t atlas_u;
    uint16_t atlas_v;
} nt_atlas_vertex_t;

/* Runtime region struct — blob counterpart: NtAtlasRegion (nt_atlas_format.h, v6).
 *
 * Field order differs from NtAtlasRegion to minimize padding and keep hot
 * fields (name_hash, vertex_start, index_start) first. All values are raw:
 * UVs are the packed 0..65535 uint16 atlas_u/v, origin is the normalized
 * float from the builder, and transform is the orientation byte untouched.
 * translate_region() copies slice9_lrtb from NtAtlasRegion at resolve time.
 * Update both structs + translate_region() together when changing fields.
 *
 * Total: 48 bytes on 64-bit (uint64_t alignment drives 8-byte boundary). */
typedef struct {
    uint64_t name_hash;      /*  0: xxh64 of region name (always a real hash at runtime) */
    uint32_t vertex_start;   /*  8: index into nt_atlas_data_t.vertices[] */
    uint32_t index_start;    /* 12: index into nt_atlas_data_t.indices[]  */
    float origin_x;          /* 16: normalized pivot 0..1 (may lie outside) */
    float origin_y;          /* 20 */
    uint16_t source_w;       /* 24: pre-trim source image width */
    uint16_t source_h;       /* 26 */
    int16_t trim_offset_x;   /* 28: pixels stripped from left edge */
    int16_t trim_offset_y;   /* 30 */
    uint8_t vertex_count;    /* 32: 0 = dead marker (removed by merge or degenerate) */
    uint8_t index_count;     /* 33 */
    uint8_t page_index;      /* 34 */
    uint8_t transform;       /* 35: orientation — bit0=flipH, bit1=flipV, bit2=diagonal */
    uint8_t flags;           /* 36: builder-authored render hints */
    uint8_t _pad0;           /* 37: alignment padding for uint16 */
    uint16_t slice9_lrtb[4]; /* 38: slice9 borders [left, right, top, bottom]; all zero = no slice9 */
    uint8_t _pad[2];         /* 46: alignment padding to 48 bytes */
} nt_texture_region_t;

/* ---- Public API ---- */

/* Register NT_ASSET_ATLAS activator + resolve callbacks with nt_resource.
 * Call after nt_resource_init(). Must be called exactly once. */
nt_result_t nt_atlas_init(void);

/* Monotonic snapshot revision. Increments whenever the owned atlas snapshot
 * is rebuilt from a newly published winner. */
uint32_t nt_atlas_revision(nt_resource_t atlas);

/* Return the number of regions (including tombstones from merge). */
uint32_t nt_atlas_region_count(nt_resource_t atlas);

/* Return atlas texture page count. Returns 0 while unresolved. */
uint8_t nt_atlas_page_count(nt_resource_t atlas);

/* O(1) amortized lookup of a region by its name hash.
 * Returns a stable index for any name that was ever present (alive -> live
 * index; removed -> dead index that draws nothing; revived -> the SAME index).
 * NT_ATLAS_INVALID_REGION only for names never present. */
uint32_t nt_atlas_find_region(nt_resource_t atlas, uint64_t name_hash);

/* Resolve-and-memoize: under a ready atlas, find by name and write the index into the ref.
 * After return region is a valid index (emit) or still INVALID (skip: not ready or bad name).
 * One-shot — tombstone-reclaim keeps the index stable for life, so no revision compare. */
static inline void nt_atlas_resolve_ref(nt_atlas_region_ref_t *ref) {
    if (ref->region == NT_ATLAS_INVALID_REGION && ref->atlas.id != 0U && nt_resource_is_ready(ref->atlas)) {
        ref->region = nt_atlas_find_region(ref->atlas, ref->name_hash);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
        /* Ready atlas + name never present = a typo'd name_hash, not a load race: a removed region keeps a
         * valid (dead) index, so INVALID here means the name was never packed. Dev-only warn; release stays a
         * silent skip so a missing optional asset never traps a shipped game. */
        if (ref->region == NT_ATLAS_INVALID_REGION) {
            /* Per-message dedup via nt_log: each distinct name_hash warns once. Plain variant — this
             * inline lands in game/example TUs without NT_LOG_DOMAIN. */
            nt_log_warn_unique("nt_atlas_resolve_ref: name_hash 0x%016llx not present in ready atlas %u (typo?)", (unsigned long long)ref->name_hash, (unsigned)ref->atlas.id);
        }
#endif
    }
}

/* O(1) pointer access by region index.
 * Always returns a non-NULL pointer when index < region_count. Dead/removed
 * regions return a struct with vertex_count==0 / index_count==0 — the renderer
 * naturally produces zero draws without a NULL branch in the hot path.
 * Out-of-range indices trip NT_ASSERT (caller bug). */
const nt_texture_region_t *nt_atlas_get_region(nt_resource_t atlas, uint32_t index);

/* Return the texture resource handle for a page index.
 * Page handles are requested and cached in the atlas post-resolve phase,
 * outside the resolve loop, so this remains an O(1) array read.
 * Out-of-range trips NT_ASSERT. */
nt_resource_t nt_atlas_get_page_resource(nt_resource_t atlas, uint8_t page_index);

/* ---- Precomputed projections + pixels_per_unit ---- */

/* Asserts atlas resolved; returns 1.0F if metadata absent (ipu == 0). */
float nt_atlas_get_pixels_per_unit(nt_resource_t atlas);

/* Returns 1/pixels_per_unit directly (the value the atlas stores internally
 * for cached_pos baking). Sprite renderer wants ipu and would otherwise do
 * 1/get_pixels_per_unit() = 1/(1/ipu) — two divisions for the same number. */
float nt_atlas_get_inverse_pixels_per_unit(nt_resource_t atlas);

/* Cached projections: 1/pixels_per_unit baked into pos. UVs are NOT cached
 * separately — atlas stores them as u16 0..65535 in the blob, sprite
 * vertex format uses USHORT2N (normalizes to [0,1] in shader at no cost),
 * so the renderer reads them straight from the raw vertex slice. */
const float (*nt_atlas_get_region_cached_pos(nt_resource_t atlas, uint32_t region_index))[2];
const nt_atlas_vertex_t *nt_atlas_get_region_raw_vertices(nt_resource_t atlas, uint32_t region_index);
const uint16_t *nt_atlas_get_region_indices(nt_resource_t atlas, uint32_t region_index);

/* One resolve + one region lookup; replaces six separate getters. */
typedef struct {
    const nt_texture_region_t *region;
    const float (*cached_pos)[2];
    const nt_atlas_vertex_t *raw_vertices;
    const uint16_t *indices;
    nt_resource_t page_resource;
    float ipu;
} nt_atlas_region_handles_t;

void nt_atlas_get_region_handles(nt_resource_t atlas, uint32_t region_index, nt_atlas_region_handles_t *out);

// #region test_access
#ifdef NT_TEST_ACCESS

/* Opaque forward declaration — the real nt_atlas_data_t lives in nt_atlas.c.
 * Tests poke at it via the raw-access helpers below, without needing a full
 * resource-system setup. */
struct nt_atlas_data;

/* Return the raw nt_atlas_data_t* stashed in a resource slot's user_data.
 * Used by tests that DO drive the resource system end-to-end. */
const struct nt_atlas_data *nt_atlas_test_get_data(nt_resource_t atlas);

/* Bypass nt_resource_peek_user_data and look up a region on a raw
 * nt_atlas_data_t* directly. Mirrors nt_atlas_find_region(). */
uint32_t nt_atlas_test_find_region_raw(const struct nt_atlas_data *ad, uint64_t name_hash);

/* Bypass nt_resource_peek_user_data and read a region array entry directly.
 * Mirrors nt_atlas_get_region(). */
const nt_texture_region_t *nt_atlas_test_get_region_raw(const struct nt_atlas_data *ad, uint32_t index);

/* Test-only accessors for internal counters so tests can assert buffer state
 * without reaching into the private struct layout. */
uint32_t nt_atlas_test_region_count(const struct nt_atlas_data *ad);
uint32_t nt_atlas_test_vertex_count(const struct nt_atlas_data *ad);
uint32_t nt_atlas_test_index_count(const struct nt_atlas_data *ad);
uint8_t nt_atlas_test_page_count(const struct nt_atlas_data *ad);
uint64_t nt_atlas_test_page_resource_id(const struct nt_atlas_data *ad, uint8_t page_index);

/* Direct wrappers around the static atlas_on_resolve / atlas_on_cleanup
 * callbacks so tests can drive first-parse and cleanup without standing up a
 * full resource system. data/size/user_data forward 1:1 to the callback. */
void nt_atlas_test_drive_resolve(const uint8_t *data, uint32_t size, void **user_data);
void nt_atlas_test_drive_cleanup(void *user_data);

/* Return the cached page_resources[page_index] slot handle's .id field.
 * 0 means NT_RESOURCE_INVALID (not yet post-resolve primed). */
uint32_t nt_atlas_test_page_resource_handle(const struct nt_atlas_data *ad, uint8_t page_index);

/* Reset module-level initialized flag so tests can re-init after
 * nt_resource_shutdown(). Production code has no nt_atlas_shutdown. */
void nt_atlas_test_reset(void);

/* Mirrors the header validation logic in atlas_on_resolve (magic, version,
 * size bounds) but returns false on failure instead of asserting.
 * Gives automated coverage of the validation path without needing a
 * death-test harness. */
bool nt_atlas_test_validate_header(const uint8_t *data, uint32_t size);

/* Test-only accessors for cached arrays + raw vertex blob + ipu.
 * Tests using nt_atlas_test_drive_resolve (no resource system) need
 * direct access since the public getters require an nt_resource_t. */
const float (*nt_atlas_test_cached_pos(const struct nt_atlas_data *ad))[2];
const nt_atlas_vertex_t *nt_atlas_test_raw_vertices(const struct nt_atlas_data *ad);
float nt_atlas_test_ipu(const struct nt_atlas_data *ad);
/* Test-only setter for ipu — used by direct-drive tests to simulate the
 * post_resolve metadata read path without standing up a resource system. */
void nt_atlas_test_set_ipu_and_recompute(struct nt_atlas_data *ad, float ipu);

#endif
// #endregion

#endif /* NT_ATLAS_H */
