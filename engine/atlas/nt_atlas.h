#ifndef NT_ATLAS_H
#define NT_ATLAS_H

#include <stdint.h>

#include "core/nt_types.h"
#include "hash/nt_hash.h"
#include "resource/nt_resource.h"

/* ---- Public constants ---- */

#define NT_ATLAS_INVALID_REGION ((uint32_t)0xFFFFFFFFU)
#define NT_ATLAS_TOMBSTONE_HASH ((uint64_t)0xFFFFFFFFFFFFFFFFULL)

#ifndef NT_ATLAS_MAX_PAGES
#define NT_ATLAS_MAX_PAGES 64
#endif

/* ---- Public types ---- */

/* Mirrors NtAtlasVertex from shared/include/nt_atlas_format.h (8 bytes).
 * Runtime stores it identically — no decode; the sprite renderer (Phase 50)
 * applies the int→float conversion and the D4 transform at batch time. */
typedef struct {
    int16_t local_x;
    int16_t local_y;
    uint16_t atlas_u;
    uint16_t atlas_v;
} nt_atlas_vertex_t;

/* Runtime region struct.
 *
 * Field order differs from NtAtlasRegion to minimize padding and keep hot
 * fields (name_hash, vertex_start, index_start) first. All values are raw:
 * UVs are the packed 0..65535 uint16 atlas_u/v, origin is the normalized
 * float from the builder, and transform is the D4 byte untouched.
 *
 * Total: 40 bytes on 64-bit (36 used + 4 tail padding). */
typedef struct {
    uint64_t name_hash;    /*  0: xxh64 of region name (or NT_ATLAS_TOMBSTONE_HASH) */
    uint32_t vertex_start; /*  8: index into nt_atlas_data_t.vertices[] */
    uint32_t index_start;  /* 12: index into nt_atlas_data_t.indices[]  */
    float origin_x;        /* 16: normalized pivot 0..1 (may lie outside) */
    float origin_y;        /* 20 */
    uint16_t source_w;     /* 24: pre-trim source image width */
    uint16_t source_h;     /* 26 */
    int16_t trim_offset_x; /* 28: pixels stripped from left edge */
    int16_t trim_offset_y; /* 30 */
    uint8_t vertex_count;  /* 32: 0 = tombstone (and also degenerate) */
    uint8_t index_count;   /* 33 */
    uint8_t page_index;    /* 34 */
    uint8_t transform;     /* 35: D4 flags — bit0=flipH, bit1=flipV, bit2=diagonal */
    /* 4 bytes tail padding */
} nt_texture_region_t;

/* ---- Public API ---- */

/* Register NT_ASSET_ATLAS activator + resolve callbacks with nt_resource.
 * Call after nt_resource_init(). Must be called exactly once. */
nt_result_t nt_atlas_init(void);

/* O(1) amortized lookup of a region by its name hash.
 * Returns NT_ATLAS_INVALID_REGION if the hash is not present (including
 * tombstoned regions that have been removed by a later merge). */
uint32_t nt_atlas_find_region(nt_resource_t atlas, uint64_t name_hash);

/* O(1) pointer access by region index.
 * Always returns a non-NULL pointer when index < region_count. Tombstoned
 * regions return a struct with vertex_count==0 / index_count==0 — the renderer
 * naturally produces zero draws without a NULL branch in the hot path.
 * Out-of-range indices trip NT_ASSERT (caller bug). */
const nt_texture_region_t *nt_atlas_get_region(nt_resource_t atlas, uint32_t index);

/* Resolve a page index to its texture resource handle.
 * First call per (atlas, page) performs a single nt_resource_request() for the
 * page's resource id (this is safe: get_page_resource runs OUTSIDE the resolve
 * callback, unlike parse-time which cannot call the resource API). Subsequent
 * calls return the cached handle. */
nt_resource_t nt_atlas_get_page_resource(nt_resource_t atlas, uint8_t page_index);

/* ---- Test access (compiled only when NT_ATLAS_TEST_ACCESS is defined) ---- */

#ifdef NT_ATLAS_TEST_ACCESS

/* Opaque forward declaration — the real nt_atlas_data_t lives in nt_atlas.c.
 * Tests poke at it via the raw-access helpers below, without needing a full
 * resource-system setup. */
struct nt_atlas_data;

/* Return the raw nt_atlas_data_t* stashed in a resource slot's user_data.
 * Used by tests that DO drive the resource system end-to-end. */
const struct nt_atlas_data *nt_atlas_test_get_data(nt_resource_t atlas);

/* Bypass nt_resource_get_user_data and look up a region on a raw
 * nt_atlas_data_t* directly. Mirrors nt_atlas_find_region(). */
uint32_t nt_atlas_test_find_region_raw(const struct nt_atlas_data *ad, uint64_t name_hash);

/* Bypass nt_resource_get_user_data and read a region array entry directly.
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

#endif /* NT_ATLAS_TEST_ACCESS */

#endif /* NT_ATLAS_H */
