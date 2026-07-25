#ifndef NT_TEST_ATLAS_DEDUP_FIXTURE_H
#define NT_TEST_ATLAS_DEDUP_FIXTURE_H

/* Deliberately dedup-FRIENDLY: same art bytes across frames, unlike
 * atlas_transform_fixture.h whose per-sprite colours keep dedup out of the
 * transform-mask contract. Header-only keeps it in each caller's compile DB. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* clang-format off */
#include "nt_builder.h"        /* NtAtlasBuild, nt_atlas_add_raw, sprite opts */
#include "nt_pack_format.h"    /* NtPackHeader, NtAssetEntry, NT_ASSET_ATLAS */
#include "nt_atlas_format.h"   /* NtAtlasHeader, NtAtlasRegion, NtAtlasVertex */
#include "nt_texture_format.h" /* NtTextureAssetHeader, RAW/RGBA8 constants */
/* clang-format on */

#define NT_ATLAS_DEDUP_ART_W 16
#define NT_ATLAS_DEDUP_ART_H 12
#define NT_ATLAS_DEDUP_FRAME_COUNT 10
#define NT_ATLAS_DEDUP_STATE_COUNT 4
/* Largest canvas in the table (32 x 26). */
#define NT_ATLAS_DEDUP_MAX_PX (32 * 26 * 4)

/* Every art state reaches all four edges of the art box, so the alpha trim
 * recovers exactly (art_x, art_y, ART_W, ART_H) — what the metadata test pins. */
static inline void nt_atlas_dedup_art_pixel(uint32_t state, uint32_t ax, uint32_t ay, uint8_t out_rgba[4]) {
    const uint32_t w = (uint32_t)NT_ATLAS_DEDUP_ART_W;
    const uint32_t h = (uint32_t)NT_ATLAS_DEDUP_ART_H;
    bool opaque = false;
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    switch (state & 3U) {
    case 0: /* solid block */
        opaque = true;
        r = 220;
        g = 60;
        b = 60;
        break;
    case 1: /* diagonal wedge (lower-right in index space) */
        opaque = ((ax * h) >= (ay * w));
        r = 60;
        g = 210;
        b = 80;
        break;
    case 2: /* ring: 2px frame */
        opaque = (ax < 2U) || (ax >= w - 2U) || (ay < 2U) || (ay >= h - 2U);
        r = 70;
        g = 90;
        b = 230;
        break;
    default: /* L: left arm + bottom arm */
        opaque = (ax < 4U) || (ay >= h - 4U);
        r = 235;
        g = 200;
        b = 40;
        break;
    }
    out_rgba[0] = opaque ? r : 0;
    out_rgba[1] = opaque ? g : 0;
    out_rgba[2] = opaque ? b : 0;
    out_rgba[3] = opaque ? 255 : 0;
}

typedef struct {
    const char *name;
    uint16_t canvas_w;
    uint16_t canvas_h;
    uint16_t art_x;
    uint16_t art_y;
    uint32_t state;
    float origin_x;
    float origin_y;
} nt_atlas_dedup_frame_t;

/* 10 frames over 4 art states (6 folds). Canvas dims and art offsets are all
 * distinct, so the untrimmed bytes — and decoded_hash — differ per frame while
 * the post-trim window is byte-identical within a state. */
/* clang-format off */
static const nt_atlas_dedup_frame_t k_atlas_dedup_frames[NT_ATLAS_DEDUP_FRAME_COUNT] = {
    {"trim_f00", 20, 16,  0,  0, 0, 0.10F, 0.20F},
    {"trim_f01", 24, 20,  3,  5, 1, 0.15F, 0.25F},
    {"trim_f02", 18, 14,  2,  1, 2, 0.20F, 0.30F},
    {"trim_f03", 26, 22,  8,  7, 0, 0.25F, 0.35F},
    {"trim_f04", 22, 18,  5,  2, 3, 0.30F, 0.40F},
    {"trim_f05", 30, 24, 11,  9, 1, 0.35F, 0.45F},
    {"trim_f06", 17, 13,  1,  1, 0, 0.40F, 0.50F},
    {"trim_f07", 28, 20,  6,  4, 2, 0.45F, 0.55F},
    {"trim_f08", 19, 15,  3,  3, 3, 0.50F, 0.60F},
    {"trim_f09", 32, 26, 14, 12, 1, 0.55F, 0.65F},
};
/* clang-format on */

/* Rasterize one frame into px (canvas_w * canvas_h * 4 bytes). */
static inline void atlas_dedup_fixture_fill_frame(const nt_atlas_dedup_frame_t *f, uint8_t *px) {
    memset(px, 0, (size_t)f->canvas_w * f->canvas_h * 4U);
    for (uint32_t ay = 0; ay < (uint32_t)NT_ATLAS_DEDUP_ART_H; ++ay) {
        for (uint32_t ax = 0; ax < (uint32_t)NT_ATLAS_DEDUP_ART_W; ++ax) {
            const uint32_t cx = f->art_x + ax;
            const uint32_t cy = f->art_y + ay;
            nt_atlas_dedup_art_pixel(f->state, ax, ay, px + ((size_t)((cy * f->canvas_w) + cx) * 4U));
        }
    }
}

/* per_frame_dedup may be NULL; a 0 entry means "inherit the atlas flag". */
static inline void atlas_dedup_fixture_add_opts(NtAtlasBuild *atlas, const uint8_t *per_frame_dedup) {
    uint8_t px[NT_ATLAS_DEDUP_MAX_PX];
    for (uint32_t i = 0; i < (uint32_t)NT_ATLAS_DEDUP_FRAME_COUNT; ++i) {
        const nt_atlas_dedup_frame_t *f = &k_atlas_dedup_frames[i];
        atlas_dedup_fixture_fill_frame(f, px);
        nt_atlas_sprite_opts_t opts = nt_atlas_sprite_opts_defaults();
        opts.name = f->name; /* raw sprites require an explicit name */
        opts.origin_x = f->origin_x;
        opts.origin_y = f->origin_y;
        opts.dedup = (per_frame_dedup != NULL) ? per_frame_dedup[i] : 0U;
        nt_atlas_add_raw(atlas, px, f->canvas_w, f->canvas_h, &opts);
    }
}

static inline void atlas_dedup_fixture_add(NtAtlasBuild *atlas) { atlas_dedup_fixture_add_opts(atlas, NULL); }

typedef struct {
    uint16_t index;
    uint8_t transform;
    uint8_t page_index;
    uint8_t vertex_count;
    uint8_t index_count;
    uint8_t flags;
    uint32_t vertex_start;
    uint32_t index_start;
    int16_t trim_offset_x;
    int16_t trim_offset_y;
    uint16_t source_w;
    uint16_t source_h;
    float origin_x;
    float origin_y;
    uint16_t slice9_lrtb[4];
    uint16_t u_min;
    uint16_t v_min;
    uint16_t u_max;
    uint16_t v_max;
} nt_atlas_dedup_region_t;

/* Locate the first asset entry of `type`, returning its bounds-checked blob. */
static inline const uint8_t *atlas_dedup_find_asset(const void *pack_bytes, size_t pack_len, uint8_t type, uint32_t nth, uint32_t *out_size) {
    if (pack_bytes == NULL || pack_len < sizeof(NtPackHeader)) {
        return NULL;
    }
    const uint8_t *buf = (const uint8_t *)pack_bytes;
    const NtPackHeader *hdr = (const NtPackHeader *)buf;
    if (hdr->magic != NT_PACK_MAGIC || hdr->version != NT_PACK_VERSION) {
        return NULL;
    }
    const uint64_t entries_end = sizeof(NtPackHeader) + ((uint64_t)hdr->asset_count * sizeof(NtAssetEntry));
    if (entries_end > pack_len) {
        return NULL;
    }
    const NtAssetEntry *entries = (const NtAssetEntry *)(buf + sizeof(NtPackHeader));
    uint32_t seen = 0;
    for (uint16_t i = 0; i < hdr->asset_count; ++i) {
        if (entries[i].asset_type != type) {
            continue;
        }
        if (entries[i].offset >= pack_len || (uint64_t)entries[i].size > pack_len - entries[i].offset) {
            return NULL;
        }
        if (seen == nth) {
            *out_size = entries[i].size;
            return buf + entries[i].offset;
        }
        ++seen;
    }
    return NULL;
}

/* The single pack-introspection point the dedup assertions build on. Every
 * offset is guarded with a hard if — NT_ASSERT_MODE=OFF is a shipping config. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat guard-per-read pack walk; splitting detaches guards from reads
static inline bool atlas_dedup_collect_regions(const void *pack_bytes, size_t pack_len, nt_atlas_dedup_region_t *out, uint32_t cap, uint32_t *out_count) {
    if (out == NULL || out_count == NULL) {
        return false;
    }
    *out_count = 0;
    uint32_t asize = 0;
    const uint8_t *ablob = atlas_dedup_find_asset(pack_bytes, pack_len, (uint8_t)NT_ASSET_ATLAS, 0, &asize);
    if (ablob == NULL || asize < sizeof(NtAtlasHeader)) {
        return false;
    }
    const NtAtlasHeader *ah = (const NtAtlasHeader *)ablob;
    if (ah->magic != NT_ATLAS_MAGIC || ah->version != NT_ATLAS_VERSION) {
        return false;
    }
    const uint64_t regions_off = sizeof(NtAtlasHeader) + ((uint64_t)ah->page_count * sizeof(uint64_t));
    const uint64_t regions_end = regions_off + ((uint64_t)ah->region_count * sizeof(NtAtlasRegion));
    const uint64_t verts_end = (uint64_t)ah->vertex_offset + ((uint64_t)ah->total_vertex_count * sizeof(NtAtlasVertex));
    if (regions_end > asize || verts_end > asize) {
        return false;
    }
    if (ah->region_count > cap) {
        return false;
    }
    const NtAtlasRegion *regions = (const NtAtlasRegion *)(ablob + regions_off);
    const NtAtlasVertex *verts = (const NtAtlasVertex *)(ablob + ah->vertex_offset);

    for (uint16_t i = 0; i < ah->region_count; ++i) {
        const uint32_t vstart = regions[i].vertex_start;
        const uint32_t nv = regions[i].vertex_count;
        if ((uint64_t)vstart + nv > ah->total_vertex_count) {
            return false;
        }
        nt_atlas_dedup_region_t *r = &out[i];
        r->index = i;
        r->transform = regions[i].transform;
        r->page_index = regions[i].page_index;
        r->vertex_count = regions[i].vertex_count;
        r->index_count = regions[i].index_count;
        r->flags = regions[i].flags;
        r->vertex_start = regions[i].vertex_start;
        r->index_start = regions[i].index_start;
        r->trim_offset_x = regions[i].trim_offset_x;
        r->trim_offset_y = regions[i].trim_offset_y;
        r->source_w = regions[i].source_w;
        r->source_h = regions[i].source_h;
        r->origin_x = regions[i].origin_x;
        r->origin_y = regions[i].origin_y;
        for (uint32_t k = 0; k < 4; ++k) {
            r->slice9_lrtb[k] = regions[i].slice9_lrtb[k];
        }
        uint16_t umin = UINT16_MAX;
        uint16_t umax = 0;
        uint16_t vmin = UINT16_MAX;
        uint16_t vmax = 0;
        for (uint32_t j = 0; j < nv; ++j) {
            const NtAtlasVertex *p = &verts[vstart + j];
            umin = (p->atlas_u < umin) ? p->atlas_u : umin;
            umax = (p->atlas_u > umax) ? p->atlas_u : umax;
            vmin = (p->atlas_v < vmin) ? p->atlas_v : vmin;
            vmax = (p->atlas_v > vmax) ? p->atlas_v : vmax;
        }
        if (nv == 0) {
            umin = 0;
            vmin = 0;
        }
        r->u_min = umin;
        r->v_min = vmin;
        r->u_max = umax;
        r->v_max = vmax;
    }
    *out_count = ah->region_count;
    return true;
}

/* The "N frames -> M placements" measurement. */
static inline uint32_t atlas_dedup_distinct_placements(const nt_atlas_dedup_region_t *r, uint32_t count) {
    uint32_t distinct = 0;
    for (uint32_t i = 0; i < count; ++i) {
        bool seen = false;
        for (uint32_t j = 0; j < i; ++j) {
            if (r[j].page_index == r[i].page_index && r[j].u_min == r[i].u_min && r[j].v_min == r[i].v_min) {
                seen = true;
                break;
            }
        }
        distinct += seen ? 0U : 1U;
    }
    return distinct;
}

/* Borrowed pointer to the mip-0 RGBA8 texels of the nth texture page. */
static inline bool atlas_dedup_read_page_rgba(const void *pack_bytes, size_t pack_len, uint32_t page_index, const uint8_t **out_px, uint32_t *out_w, uint32_t *out_h) {
    if (out_px == NULL || out_w == NULL || out_h == NULL) {
        return false;
    }
    uint32_t tsize = 0;
    const uint8_t *tblob = atlas_dedup_find_asset(pack_bytes, pack_len, (uint8_t)NT_ASSET_TEXTURE, page_index, &tsize);
    if (tblob == NULL || tsize < sizeof(NtTextureAssetHeader)) {
        return false;
    }
    const NtTextureAssetHeader *th = (const NtTextureAssetHeader *)tblob;
    if (th->magic != NT_TEXTURE_MAGIC || th->version != NT_TEXTURE_VERSION) {
        return false;
    }
    if (th->compression != (uint8_t)NT_TEXTURE_COMPRESSION_RAW || th->format != (uint16_t)NT_TEXTURE_FORMAT_RGBA8) {
        return false;
    }
    if (th->width == 0 || th->height == 0 || th->width > 0xFFFFU || th->height > 0xFFFFU) {
        return false;
    }
    const uint64_t need = (uint64_t)th->width * th->height * 4U;
    if (need > th->data_size || (uint64_t)sizeof(NtTextureAssetHeader) + th->data_size > tsize) {
        return false;
    }
    *out_px = tblob + sizeof(NtTextureAssetHeader);
    *out_w = th->width;
    *out_h = th->height;
    return true;
}

#endif /* NT_TEST_ATLAS_DEDUP_FIXTURE_H */
