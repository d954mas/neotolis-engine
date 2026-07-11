#include "ntpack_parse.h"

#include <math.h>
#include <string.h>

#include "nt_atlas_format.h"

/* atlas_u/atlas_v are normalized 0-65535 over the atlas; scale to [0,1] UV so
 * areas come out in normalized-UV^2 units regardless of page pixel size. */
#define NT_BENCH_UV_INV (1.0 / 65535.0)

int nt_bench_parse_atlas_blob(const uint8_t *blob, size_t blob_size, nt_bench_atlas_metrics_t *out) {
    if (blob == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    if (blob_size < sizeof(NtAtlasHeader)) {
        return -2;
    }

    const NtAtlasHeader *h = (const NtAtlasHeader *)blob;
    if (h->magic != NT_ATLAS_MAGIC || h->version != NT_ATLAS_VERSION) {
        return -3;
    }
    if (h->page_count > NT_BENCH_MAX_PAGES) {
        return -4; /* corrupt header must not force an unbounded page walk */
    }

    /* All bounds in uint64_t so a corrupt count can never wrap the comparison. */
    const uint64_t page_bytes = (uint64_t)h->page_count * sizeof(uint64_t);
    const uint64_t regions_off = (uint64_t)sizeof(NtAtlasHeader) + page_bytes;
    const uint64_t regions_end = regions_off + (uint64_t)h->region_count * sizeof(NtAtlasRegion);
    if (regions_end > blob_size) {
        return -5;
    }

    const uint64_t verts_end = (uint64_t)h->vertex_offset + (uint64_t)h->total_vertex_count * sizeof(NtAtlasVertex);
    if (verts_end > blob_size) {
        return -6;
    }

    const NtAtlasRegion *regions = (const NtAtlasRegion *)(blob + regions_off);
    const NtAtlasVertex *verts = (const NtAtlasVertex *)(blob + h->vertex_offset);

    uint32_t hull_min = UINT32_MAX;
    uint32_t hull_max = 0;
    uint32_t hull_total = 0;
    double poly_area = 0.0;
    double bbox_area = 0.0;

    for (uint16_t i = 0; i < h->region_count; i++) {
        const uint32_t vstart = regions[i].vertex_start;
        const uint32_t nv = regions[i].vertex_count;
        if ((uint64_t)vstart + nv > h->total_vertex_count) {
            return -7; /* region vertex window escapes the vertex array */
        }

        hull_total += nv;
        if (nv < hull_min) {
            hull_min = nv;
        }
        if (nv > hull_max) {
            hull_max = nv;
        }

        if (nv >= 3) {
            double shoelace = 0.0;
            uint16_t umin = UINT16_MAX;
            uint16_t umax = 0;
            uint16_t vmin = UINT16_MAX;
            uint16_t vmax = 0;
            for (uint32_t j = 0; j < nv; j++) {
                const NtAtlasVertex *p = &verts[vstart + j];
                const NtAtlasVertex *q = &verts[vstart + ((j + 1) % nv)];
                shoelace += (double)p->atlas_u * (double)q->atlas_v - (double)q->atlas_u * (double)p->atlas_v;
                if (p->atlas_u < umin)
                    umin = p->atlas_u;
                if (p->atlas_u > umax)
                    umax = p->atlas_u;
                if (p->atlas_v < vmin)
                    vmin = p->atlas_v;
                if (p->atlas_v > vmax)
                    vmax = p->atlas_v;
            }
            poly_area += fabs(shoelace) * 0.5 * NT_BENCH_UV_INV * NT_BENCH_UV_INV;
            bbox_area += (double)(umax - umin) * (double)(vmax - vmin) * NT_BENCH_UV_INV * NT_BENCH_UV_INV;
        }
    }

    if (h->region_count == 0) {
        hull_min = 0;
    }

    out->region_count = h->region_count;
    out->page_count = h->page_count;
    out->total_vertex_count = h->total_vertex_count;
    out->hull_vert_total = hull_total;
    out->hull_vert_min = hull_min;
    out->hull_vert_max = hull_max;
    out->poly_area_uv = poly_area;
    out->trim_bbox_area_uv = bbox_area;
    return 0;
}
