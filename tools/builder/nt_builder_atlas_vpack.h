#ifndef NT_BUILDER_ATLAS_VPACK_H
#define NT_BUILDER_ATLAS_VPACK_H

/*
 * Vector atlas packer — NFP/Minkowski sprite placement.
 *
 * Owns the shared types produced/consumed by the pipeline (AtlasPlacement,
 * PackStats, ATLAS_MAX_PAGES) since the packer is the sole producer of
 * placements and counters. Pipeline code in nt_builder_atlas.c includes
 * this header to receive results.
 */

#include <stdint.h>

#include "nt_builder.h"                /* nt_atlas_opts_t */
#include "nt_builder_atlas_geometry.h" /* Point2D */

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of atlas pages (each page = one texture). */
#ifndef ATLAS_MAX_PAGES
#define ATLAS_MAX_PAGES 64
#endif

/* Sprite placement result after packing.
 * transform is a 3-bit D4 mask: bit0=flipH, bit1=flipV, bit2=diagonal.
 * Apply order: diagonal → flipH → flipV. 0 = identity.
 *
 * NOTE: this struct is serialized directly to the atlas cache file via fwrite.
 * Trailing padding after the uint8_t transform field is written to disk.
 * Allocate placement arrays with calloc (or memset) so padding bytes are
 * deterministic — otherwise cache files contain uninitialized memory and diff
 * noisily between runs. The _Static_assert locks the expected size; adding
 * fields forces a fresh cache format review. */
typedef struct {
    uint32_t sprite_index; /* index into original sprite array */
    uint32_t page;         /* which atlas page (0-based) */
    uint32_t x, y;         /* placement position in atlas (top-left of cell including extrude) */
    uint32_t trimmed_w;    /* trimmed sprite width */
    uint32_t trimmed_h;    /* trimmed sprite height */
    uint32_t trim_x;       /* trim offset from source image left */
    uint32_t trim_y;       /* trim offset from source image top */
    uint8_t transform;     /* D4 transform flags */
} AtlasPlacement;
_Static_assert(sizeof(AtlasPlacement) == 36, "AtlasPlacement size is baked into the atlas cache file; any change must bump ATLAS_CACHE_KEY_VERSION");

/* Per-call packing statistics (thread-safe: no static globals) */
typedef struct {
    uint64_t or_count;                /* Clipper2 NFP build calls */
    uint64_t test_count;              /* candidate scans (point-in-NFP tests) */
    uint64_t used_area;               /* sum of final page area (post POT) */
    uint64_t frontier_area;           /* sum of final page area pre-POT */
    uint64_t trim_area;               /* sum of trimmed sprite areas */
    uint64_t poly_area;               /* sum of inflated polygon areas */
    uint64_t page_scan_count;         /* number of try_page calls */
    uint64_t page_existing_hit_count; /* sprites placed on a non-empty page */
    uint64_t page_new_count;          /* new pages allocated */
    uint64_t nfp_cache_hit_count;     /* NFP cache hits */
    uint64_t nfp_cache_miss_count;    /* NFP cache misses (Clipper2 invoked) */
} PackStats;

/* Zero all PackStats counters in one place. */
void pack_stats_reset(PackStats *stats);

/* Pack sprites via NFP/Minkowski vector packing. */
uint32_t vector_pack(const uint32_t *trim_w, const uint32_t *trim_h, Point2D **hull_verts, const uint32_t *hull_counts, uint32_t sprite_count, const nt_atlas_opts_t *opts,
                     AtlasPlacement *out_placements, uint32_t *out_page_count, uint32_t *out_page_w, uint32_t *out_page_h, PackStats *stats, uint32_t thread_count);

#ifdef __cplusplus
}
#endif

#endif /* NT_BUILDER_ATLAS_VPACK_H */
