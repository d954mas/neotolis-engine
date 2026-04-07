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
 * rotation is a 3-bit D4 transform mask (bit0=flipH, bit1=flipV, bit2=diagonal). */
typedef struct {
    uint32_t sprite_index; /* index into original sprite array */
    uint32_t page;         /* which atlas page (0-based) */
    uint32_t x, y;         /* placement position in atlas (top-left of cell including extrude) */
    uint32_t trimmed_w;    /* trimmed sprite width */
    uint32_t trimmed_h;    /* trimmed sprite height */
    uint32_t trim_x;       /* trim offset from source image left */
    uint32_t trim_y;       /* trim offset from source image top */
    uint8_t rotation;      /* D4 transform flags */
} AtlasPlacement;

/* Per-call packing statistics (thread-safe: no static globals) */
typedef struct {
    uint64_t or_count;
    uint64_t test_count;
    uint64_t yskip_count;
    uint64_t used_area;
    uint64_t frontier_area;
    uint64_t trim_area;
    uint64_t poly_area;
    uint64_t page_scan_count;
    uint64_t page_prune_count;
    uint64_t page_existing_hit_count;
    uint64_t page_backfill_count;
    uint64_t page_new_count;
    uint64_t relevant_count;
    uint64_t candidate_count;
    uint64_t grid_fallback_count;
    uint64_t nfp_cache_hit_count;
    uint64_t nfp_cache_miss_count;
    uint64_t nfp_cache_collision_count;
    uint64_t orient_dedup_saved_count;
    uint64_t dirty_cell_count;
} PackStats;

/* Pack sprites via NFP/Minkowski vector packing. */
uint32_t vector_pack(const uint32_t *trim_w, const uint32_t *trim_h, Point2D **hull_verts, const uint32_t *hull_counts, uint32_t sprite_count, const nt_atlas_opts_t *opts,
                     AtlasPlacement *out_placements, uint32_t *out_page_count, uint32_t *out_page_w, uint32_t *out_page_h, PackStats *stats, uint32_t thread_count);

#ifdef __cplusplus
}
#endif

#endif /* NT_BUILDER_ATLAS_VPACK_H */
