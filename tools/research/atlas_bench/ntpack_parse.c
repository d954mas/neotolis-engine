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

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat guard chain; splitting would separate bounds checks from the reads they protect
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
    const uint64_t regions_end = regions_off + ((uint64_t)h->region_count * sizeof(NtAtlasRegion));
    if (regions_end > blob_size) {
        return -5;
    }

    const uint64_t verts_end = (uint64_t)h->vertex_offset + ((uint64_t)h->total_vertex_count * sizeof(NtAtlasVertex));
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
         * O(n^2) scan keeps the blob parser allocation-free. */
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
                if (p->atlas_u < umin) {
                    umin = p->atlas_u;
                }
                if (p->atlas_u > umax) {
                    umax = p->atlas_u;
                }
                if (p->atlas_v < vmin) {
                    vmin = p->atlas_v;
                }
                if (p->atlas_v > vmax) {
                    vmax = p->atlas_v;
                }
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
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — linear guard-per-read file walk; helpers would detach guards from their reads
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
    const uint64_t entries_end = entries_off + ((uint64_t)hdr->asset_count * sizeof(NtAssetEntry));
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
    const uint64_t pages_end = pages_off + ((uint64_t)page_count * sizeof(uint64_t));
    if (pages_end > atlas->size) {
        free(buf);
        return -10;
    }
    /* Pack assets are only 4-aligned; a direct uint64_t* load here is UB. */
    const uint8_t *page_ids_bytes = ablob + pages_off;

    for (uint16_t p = 0; p < page_count && p < NT_BENCH_MAX_PAGES; p++) {
        uint64_t page_id;
        bool found_page = false;
        memcpy(&page_id, page_ids_bytes + ((uint64_t)p * sizeof(uint64_t)), sizeof(uint64_t));
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

void nt_bench_selected_geometry_destroy(nt_bench_selected_geometry_t *geometry) {
    if (geometry == NULL) {
        return;
    }
    free(geometry->polygon);
    free(geometry->triangle_indices);
    memset(geometry, 0, sizeof(*geometry));
}

static uint32_t bench_rotr32(uint32_t value, uint32_t count) { return (value >> count) | (value << (32U - count)); }

static bool bench_sha256_bytes(const uint8_t *data, size_t size, uint8_t digest[32]) {
    static const uint32_t k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U,
        0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU,
        0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU,
        0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    uint32_t h[8] = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    if (size > SIZE_MAX - 72U) {
        return false;
    }
    size_t padded = size + 1U;
    while ((padded % 64U) != 56U) {
        padded++;
    }
    uint8_t *message = (uint8_t *)calloc(padded + 8U, 1U);
    if (message == NULL) {
        return false;
    }
    memcpy(message, data, size);
    message[size] = 0x80U;
    const uint64_t bits = (uint64_t)size * 8U;
    for (uint32_t i = 0; i < 8U; i++) {
        message[padded + i] = (uint8_t)(bits >> (56U - (i * 8U)));
    }
    for (size_t block = 0; block < padded + 8U; block += 64U) {
        uint32_t w[64];
        for (uint32_t i = 0; i < 16U; i++) {
            const uint8_t *p = message + block + (i * 4U);
            w[i] = ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) | ((uint32_t)p[2] << 8U) | p[3];
        }
        for (uint32_t i = 16U; i < 64U; i++) {
            const uint32_t s0 = bench_rotr32(w[i - 15U], 7U) ^ bench_rotr32(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
            const uint32_t s1 = bench_rotr32(w[i - 2U], 17U) ^ bench_rotr32(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
            w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], x = h[7];
        for (uint32_t i = 0; i < 64U; i++) {
            const uint32_t s1 = bench_rotr32(e, 6U) ^ bench_rotr32(e, 11U) ^ bench_rotr32(e, 25U);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t t1 = x + s1 + ch + k[i] + w[i];
            const uint32_t s0 = bench_rotr32(a, 2U) ^ bench_rotr32(a, 13U) ^ bench_rotr32(a, 22U);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = s0 + maj;
            x = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += x;
    }
    free(message);
    for (uint32_t i = 0; i < 8U; i++) {
        digest[(i * 4U) + 0U] = (uint8_t)(h[i] >> 24U);
        digest[(i * 4U) + 1U] = (uint8_t)(h[i] >> 16U);
        digest[(i * 4U) + 2U] = (uint8_t)(h[i] >> 8U);
        digest[(i * 4U) + 3U] = (uint8_t)h[i];
    }
    return true;
}

int nt_bench_file_sha256_hex(const char *path, char out_hex[65]) {
    if (path == NULL || out_hex == NULL) {
        return -1;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return -2;
    }
    const long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return -3;
    }
    uint8_t *bytes = (uint8_t *)malloc((size_t)length);
    if (bytes == NULL || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes);
        (void)fclose(file);
        return -4;
    }
    (void)fclose(file);
    uint8_t digest[32];
    const bool ok = bench_sha256_bytes(bytes, (size_t)length, digest);
    free(bytes);
    if (!ok) {
        return -5;
    }
    for (uint32_t i = 0; i < 32U; i++) {
        (void)snprintf(out_hex + (i * 2U), 3U, "%02x", digest[i]);
    }
    out_hex[64] = '\0';
    return 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — each serialized read stays adjacent to its fail-closed bounds guard
int nt_bench_parse_selected_geometry(const char *pack_path, uint32_t region_index, uint32_t trim_width, uint32_t trim_height, nt_bench_selected_geometry_t *out) {
    if (pack_path == NULL || out == NULL || trim_width == 0U || trim_height == 0U) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    FILE *file = fopen(pack_path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return -2;
    }
    const long length = ftell(file);
    if (length < 0 || (uint64_t)length < sizeof(NtPackHeader) || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return -3;
    }
    const size_t size = (size_t)length;
    uint8_t *bytes = (uint8_t *)malloc(size);
    if (bytes == NULL || fread(bytes, 1, size, file) != size) {
        free(bytes);
        (void)fclose(file);
        return -4;
    }
    (void)fclose(file);

    const NtPackHeader *pack = (const NtPackHeader *)bytes;
    const uint64_t entries_end = sizeof(NtPackHeader) + ((uint64_t)pack->asset_count * sizeof(NtAssetEntry));
    if (pack->magic != NT_PACK_MAGIC || pack->version != NT_PACK_VERSION || entries_end > size) {
        free(bytes);
        return -5;
    }
    const NtAssetEntry *entries = (const NtAssetEntry *)(bytes + sizeof(NtPackHeader));
    const NtAssetEntry *atlas_entry = NULL;
    for (uint16_t i = 0; i < pack->asset_count; i++) {
        if (entries[i].asset_type == NT_ASSET_ATLAS && entries[i].offset <= size && (uint64_t)entries[i].size <= size - entries[i].offset) {
            atlas_entry = &entries[i];
            break;
        }
    }
    if (atlas_entry == NULL || atlas_entry->size < sizeof(NtAtlasHeader)) {
        free(bytes);
        return -6;
    }
    const uint8_t *blob = bytes + atlas_entry->offset;
    const NtAtlasHeader *atlas = (const NtAtlasHeader *)blob;
    const uint64_t regions_offset = sizeof(NtAtlasHeader) + ((uint64_t)atlas->page_count * sizeof(uint64_t));
    const uint64_t regions_end = regions_offset + ((uint64_t)atlas->region_count * sizeof(NtAtlasRegion));
    const uint64_t vertices_end = (uint64_t)atlas->vertex_offset + ((uint64_t)atlas->total_vertex_count * sizeof(NtAtlasVertex));
    const uint64_t indices_end = (uint64_t)atlas->index_offset + ((uint64_t)atlas->total_index_count * sizeof(uint16_t));
    if (atlas->magic != NT_ATLAS_MAGIC || atlas->version != NT_ATLAS_VERSION || region_index >= atlas->region_count || regions_end > atlas_entry->size || atlas->vertex_offset < regions_end ||
        vertices_end > atlas_entry->size || atlas->index_offset < vertices_end || indices_end > atlas_entry->size) {
        free(bytes);
        return -7;
    }
    const NtAtlasRegion *regions = (const NtAtlasRegion *)(blob + regions_offset);
    const NtAtlasRegion *region = &regions[region_index];
    if (region->vertex_count < 3U || region->vertex_count > NT_POLYGON_MAX_VERTICES || region->index_count == 0U || region->index_count > NT_POLYGON_MAX_TRIANGLE_INDICES ||
        region->index_count % 3U != 0U || (uint64_t)region->vertex_start + region->vertex_count > atlas->total_vertex_count ||
        (uint64_t)region->index_start + region->index_count > atlas->total_index_count) {
        free(bytes);
        return -8;
    }

    Point2D *polygon = (Point2D *)malloc((size_t)region->vertex_count * sizeof(Point2D));
    uint16_t *indices = (uint16_t *)malloc((size_t)region->index_count * sizeof(uint16_t));
    if (polygon == NULL || indices == NULL) {
        free(indices);
        free(polygon);
        free(bytes);
        return -9;
    }
    const NtAtlasVertex *vertices = (const NtAtlasVertex *)(blob + atlas->vertex_offset);
    const uint16_t *serialized_indices = (const uint16_t *)(blob + atlas->index_offset);
    for (uint32_t i = 0; i < region->vertex_count; i++) {
        const NtAtlasVertex *vertex = &vertices[region->vertex_start + i];
        const int32_t y = (int32_t)trim_height - vertex->local_y;
        if (vertex->local_x < 0 || (uint32_t)vertex->local_x > trim_width || y < 0 || (uint32_t)y > trim_height) {
            free(indices);
            free(polygon);
            free(bytes);
            return -10;
        }
        polygon[i].x = vertex->local_x;
        polygon[i].y = y;
    }
    memcpy(indices, serialized_indices + region->index_start, (size_t)region->index_count * sizeof(uint16_t));
    for (uint32_t i = 0; i < region->index_count; i++) {
        if (indices[i] >= region->vertex_count) {
            free(indices);
            free(polygon);
            free(bytes);
            return -11;
        }
    }
    for (uint32_t i = 0; i < region->index_count; i += 3U) {
        const uint16_t swap = indices[i + 1U];
        indices[i + 1U] = indices[i + 2U];
        indices[i + 2U] = swap;
    }

    out->polygon = polygon;
    out->triangle_indices = indices;
    out->vertex_count = region->vertex_count;
    out->triangle_index_count = region->index_count;
    out->source_w = region->source_w;
    out->source_h = region->source_h;
    out->trim_offset_x = region->trim_offset_x;
    out->trim_offset_y = region->trim_offset_y;
    free(bytes);
    return 0;
}
