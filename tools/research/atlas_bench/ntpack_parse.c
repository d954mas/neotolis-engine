#include "ntpack_parse.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nt_atlas_format.h"
#include "nt_pack_format.h"
#include "nt_texture_format.h"

/* atlas_u/atlas_v are normalized 0-65535 over the atlas; scale to [0,1] UV so
 * areas come out in normalized-UV^2 units regardless of page pixel size. */
#define NT_BENCH_UV_INV (1.0 / 65535.0)

/* Full-file walk (nt_bench_parse_ntpack, impl in 78-03): after NtPackHeader,
 * the NtAssetEntry with asset_type==NT_ASSET_ATLAS holds this blob; page pixel
 * dims live in the paired NT_ASSET_TEXTURE entry, not here (blob has UVs only). */
/* Dedup: region_count counts input sprites; total_vertex_count is the shared
 * (deduplicated) vertex pool, so hull_vert_total can exceed it via aliasing. */

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

    /* Per-page used-extent tracking (in that page's 0-65535 UV). page_index is
     * skipped when >= page_count so the page_count==0 mock stays valid. */
    uint16_t pumin[NT_BENCH_MAX_PAGES];
    uint16_t pumax[NT_BENCH_MAX_PAGES];
    uint16_t pvmin[NT_BENCH_MAX_PAGES];
    uint16_t pvmax[NT_BENCH_MAX_PAGES];
    for (uint16_t p = 0; p < NT_BENCH_MAX_PAGES; p++) {
        pumin[p] = UINT16_MAX;
        pumax[p] = 0;
        pvmin[p] = UINT16_MAX;
        pvmax[p] = 0;
    }

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

        /* Dedup: identical sprites alias the same deduplicated vertex span, so
         * they occupy the SAME atlas pixels. Count each unique span's polygon
         * area ONCE — matching the packer's poly_area (unique-set fill). Hull
         * counts above stay per-region (region_count == input-sprite count).
         * O(n^2) scan keeps the blob parser allocation-free (T-78-02). */
        bool is_unique_span = true;
        for (uint16_t j = 0; j < i; j++) {
            if (regions[j].vertex_start == vstart) {
                is_unique_span = false;
                break;
            }
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
            const double region_area = fabs(shoelace) * 0.5 * NT_BENCH_UV_INV * NT_BENCH_UV_INV;
            bbox_area += (double)(umax - umin) * (double)(vmax - vmin) * NT_BENCH_UV_INV * NT_BENCH_UV_INV;
            if (is_unique_span) {
                poly_area += region_area;
            }

            const uint8_t page = regions[i].page_index;
            if (page < h->page_count && page < NT_BENCH_MAX_PAGES) {
                if (is_unique_span) {
                    out->page_poly_area_uv[page] += region_area;
                }
                if (umin < pumin[page]) {
                    pumin[page] = umin;
                }
                if (umax > pumax[page]) {
                    pumax[page] = umax;
                }
                if (vmin < pvmin[page]) {
                    pvmin[page] = vmin;
                }
                if (vmax > pvmax[page]) {
                    pvmax[page] = vmax;
                }
            }
        }
    }

    for (uint16_t p = 0; p < h->page_count && p < NT_BENCH_MAX_PAGES; p++) {
        if (pumax[p] >= pumin[p] && pvmax[p] >= pvmin[p]) {
            out->page_bbox_area_uv[p] = (double)(pumax[p] - pumin[p]) * (double)(pvmax[p] - pvmin[p]) * NT_BENCH_UV_INV * NT_BENCH_UV_INV;
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

/* Locate the single NT_ASSET_ATLAS entry, feed its blob to the blob parser,
 * then recover each page's pixel dims from the paired NT_ASSET_TEXTURE asset
 * (matched by resource_id — the atlas page_ids[] key it). Densities are then
 * poly_px / page_px (texture) and poly_px / used-bbox_px (frontier), summed
 * over pages so they line up with the packer's own BENCH fill numbers.
 * Every offset is an explicit if-guard — safe against a truncated/corrupt file
 * regardless of NT_ASSERT mode; the read buffer is freed on every exit. */
int nt_bench_parse_ntpack(const char *pack_path, nt_bench_atlas_metrics_t *out) {
    if (pack_path == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    FILE *file = fopen(pack_path, "rb");
    if (file == NULL) {
        return -2;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return -3;
    }
    long file_size_long = ftell(file);
    if (file_size_long < 0 || (uint64_t)file_size_long < sizeof(NtPackHeader)) {
        (void)fclose(file);
        return -4;
    }
    (void)fseek(file, 0, SEEK_SET);
    const size_t file_size = (size_t)file_size_long;

    uint8_t *buf = (uint8_t *)malloc(file_size);
    if (buf == NULL) {
        (void)fclose(file);
        return -5;
    }
    if (fread(buf, 1, file_size, file) != file_size) {
        free(buf);
        (void)fclose(file);
        return -6;
    }
    (void)fclose(file);

    const NtPackHeader *hdr = (const NtPackHeader *)buf;
    if (hdr->magic != NT_PACK_MAGIC || hdr->version != NT_PACK_VERSION) {
        free(buf);
        return -7;
    }

    const uint64_t entries_off = sizeof(NtPackHeader);
    const uint64_t entries_end = entries_off + (uint64_t)hdr->asset_count * sizeof(NtAssetEntry);
    if (entries_end > file_size) {
        free(buf);
        return -8;
    }
    const NtAssetEntry *entries = (const NtAssetEntry *)(buf + entries_off);

    /* Single NT_ASSET_ATLAS entry (atlas_bench packs exactly one atlas). */
    const NtAssetEntry *atlas = NULL;
    for (uint16_t i = 0; i < hdr->asset_count; i++) {
        if (entries[i].asset_type == NT_ASSET_ATLAS && entries[i].offset < file_size && (uint64_t)entries[i].size <= file_size - entries[i].offset) {
            atlas = &entries[i];
            break;
        }
    }
    if (atlas == NULL) {
        free(buf);
        return -9;
    }

    const int rc = nt_bench_parse_atlas_blob(buf + atlas->offset, atlas->size, out);
    if (rc != 0) {
        free(buf);
        return rc;
    }

    /* Atlas header already validated by the blob parser; read page_ids to match
     * each page to its texture asset. page_count is capped at NT_BENCH_MAX_PAGES. */
    const uint8_t *ablob = buf + atlas->offset;
    const NtAtlasHeader *ah = (const NtAtlasHeader *)ablob;
    const uint16_t page_count = ah->page_count;
    const uint64_t pages_off = sizeof(NtAtlasHeader);
    const uint64_t pages_end = pages_off + (uint64_t)page_count * sizeof(uint64_t);
    if (pages_end > atlas->size) {
        free(buf);
        return -10;
    }
    /* Pack assets are only 4-aligned; a direct uint64_t* load here is UB. */
    const uint8_t *page_ids_bytes = ablob + pages_off;

    for (uint16_t p = 0; p < page_count && p < NT_BENCH_MAX_PAGES; p++) {
        uint64_t page_id;
        bool found_page = false;
        memcpy(&page_id, page_ids_bytes + (uint64_t)p * sizeof(uint64_t), sizeof(uint64_t));
        for (uint16_t i = 0; i < hdr->asset_count; i++) {
            if (entries[i].asset_type != NT_ASSET_TEXTURE || entries[i].resource_id != page_id) {
                continue;
            }
            if (entries[i].offset < file_size && (uint64_t)entries[i].size <= file_size - entries[i].offset && entries[i].size >= sizeof(NtTextureAssetHeader)) {
                const NtTextureAssetHeader *th = (const NtTextureAssetHeader *)(buf + entries[i].offset);
                if (th->magic == NT_TEXTURE_MAGIC && th->version == NT_TEXTURE_VERSION && th->width > 0 && th->height > 0) {
                    out->page_w[p] = th->width;
                    out->page_h[p] = th->height;
                    found_page = true;
                }
            }
            break;
        }
        if (!found_page) {
            free(buf);
            return -11;
        }
    }

    double poly_px = 0.0;
    double tex_px = 0.0;
    double frontier_px = 0.0;
    for (uint16_t p = 0; p < page_count && p < NT_BENCH_MAX_PAGES; p++) {
        const double area = (double)out->page_w[p] * (double)out->page_h[p];
        poly_px += out->page_poly_area_uv[p] * area;
        frontier_px += out->page_bbox_area_uv[p] * area;
        tex_px += area;
    }
    out->density_fill_texture = (tex_px > 0.0) ? (poly_px / tex_px) : 0.0;
    out->density_fill_frontier = (frontier_px > 0.0) ? (poly_px / frontier_px) : 0.0;

    free(buf);
    return 0;
}
