#ifndef NT_BENCH_NTPACK_PARSE_H
#define NT_BENCH_NTPACK_PARSE_H

#include <stddef.h>
#include <stdint.h>

/* Atlas-blob metric parser for the AUDIT-01 benchmark. Reads region/hull/area
 * numbers straight from the produced atlas binary (D-02: never scrapes the
 * drifting BENCH log line, zero packer-production coupling). */

/* Page-array cap; a corrupt header cannot force an unbounded page walk. */
#define NT_BENCH_MAX_PAGES 64

typedef struct {
    uint16_t region_count;
    uint16_t page_count;
    uint32_t total_vertex_count;
    uint32_t hull_vert_total; /* sum of per-region vertex_count */
    uint32_t hull_vert_min;
    uint32_t hull_vert_max;
    double poly_area_uv;      /* summed hull polygon area, normalized-UV^2 units */
    double trim_bbox_area_uv; /* summed per-region UV bbox area, normalized-UV^2 */

    /* Per-page geometry in that page's own UV^2 (atlas_u/v normalize per page).
     * Blob parser fills the _uv accumulators; the full-file parser fills the
     * pixel dims from the paired texture asset and derives the densities. */
    double page_poly_area_uv[NT_BENCH_MAX_PAGES]; /* per-page hull poly area */
    double page_bbox_area_uv[NT_BENCH_MAX_PAGES]; /* per-page used-extent bbox */

    /* Filled only by nt_bench_parse_ntpack (need the texture-asset pixel dims). */
    uint32_t page_w[NT_BENCH_MAX_PAGES];
    uint32_t page_h[NT_BENCH_MAX_PAGES];
    double density_fill_texture;  /* Σ poly_px / Σ page_px  (packer fill_texture) */
    double density_fill_frontier; /* Σ poly_px / Σ used-bbox_px (packer fill_frontier) */
} nt_bench_atlas_metrics_t;

/* Parse one atlas asset blob (NtAtlasHeader + pages + regions + verts).
 * Returns 0 on success, negative on malformed input. Hard-guards every offset
 * against blob_size — safe in every NT_ASSERT mode (asserts are void in OFF). */
int nt_bench_parse_atlas_blob(const uint8_t *blob, size_t blob_size, nt_bench_atlas_metrics_t *out);

/* Parse a full .ntpack file: locates the NT_ASSET_ATLAS entry and fills page
 * pixel dims from the paired NT_ASSET_TEXTURE entry. Implemented in plan 78-03. */
int nt_bench_parse_ntpack(const char *pack_path, nt_bench_atlas_metrics_t *out);

#endif /* NT_BENCH_NTPACK_PARSE_H */
