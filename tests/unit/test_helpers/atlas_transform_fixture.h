#ifndef NT_TEST_ATLAS_TRANSFORM_FIXTURE_H
#define NT_TEST_ATLAS_TRANSFORM_FIXTURE_H

/* Header-only (static inline): the fixture needs no TU of its own, so it can
 * never become an orphan in the compile DB.
 *
 * The caller owns nt_atlas_opts_t and sets the transform control — this fixture
 * only builds sprites and dumps regions, so the SAME fixture is packed by the
 * legacy bool here and by the new mask in the later assertion, bit-for-bit. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* clang-format off */
#include "nt_builder.h"      /* NtAtlasBuild, nt_atlas_add_raw, sprite opts */
#include "nt_pack_format.h"  /* NtPackHeader, NtAssetEntry, NT_ASSET_ATLAS */
#include "nt_atlas_format.h" /* NtAtlasHeader, NtAtlasRegion, NtAtlasVertex */
#include "ntpack_parse.h"    /* documented dump dependency (shared pack parsing) */
/* clang-format on */

typedef enum {
    NT_ATLAS_FIX_STRIP,  /* fully opaque rectangle (thin strips rotate best) */
    NT_ATLAS_FIX_LSHAPE, /* opaque L (left arm + bottom arm), asymmetric hull */
    NT_ATLAS_FIX_TRI,    /* opaque where x >= y (lower-right triangle) */
    NT_ATLAS_FIX_BORDER, /* opaque square inset in a transparent border */
} nt_atlas_fixture_kind_t;

typedef struct {
    const char *name;
    uint16_t w;
    uint16_t h;
    nt_atlas_fixture_kind_t kind;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} nt_atlas_fixture_spec_t;

/* 10 order-stable sprites. Four strongly asymmetric, dimension-swapping strips
 * (6x40/40x6, 8x44/44x8) give the packer a rotation win so the transform path
 * actually fires; the rest add hull variety. Every sprite keeps opaque content
 * after trim (no empty-mask abort). */
/* clang-format off */
static const nt_atlas_fixture_spec_t k_atlas_fixture_sprites[] = {
    {"strip_tall_a",  6, 40, NT_ATLAS_FIX_STRIP,  210,  50,  50},
    {"strip_wide_a", 40,  6, NT_ATLAS_FIX_STRIP,   50, 210,  50},
    {"strip_tall_b",  8, 44, NT_ATLAS_FIX_STRIP,   50,  50, 210},
    {"strip_wide_b", 44,  8, NT_ATLAS_FIX_STRIP,  210, 210,  50},
    {"lshape_a",     24, 24, NT_ATLAS_FIX_LSHAPE, 230, 130,  30},
    {"lshape_b",     20, 30, NT_ATLAS_FIX_LSHAPE,  30, 200, 220},
    {"tri_a",        24, 24, NT_ATLAS_FIX_TRI,    180,  30, 180},
    {"rect_a",       28, 18, NT_ATLAS_FIX_STRIP,  120, 200,  90},
    {"rect_b",       18, 28, NT_ATLAS_FIX_STRIP,   90, 120, 200},
    {"border_a",     22, 30, NT_ATLAS_FIX_BORDER, 200, 120,  40},
};
/* clang-format on */

#define NT_ATLAS_FIXTURE_COUNT ((int)(sizeof(k_atlas_fixture_sprites) / sizeof(k_atlas_fixture_sprites[0])))
#define NT_ATLAS_FIXTURE_MAX_PX (48 * 48 * 4)

/* Pure function of (x, y) — byte-identical on every run, no rand/time/file I/O. */
static inline uint8_t nt_atlas_fixture_alpha(const nt_atlas_fixture_spec_t *s, uint32_t x, uint32_t y) {
    switch (s->kind) {
    case NT_ATLAS_FIX_STRIP:
        return 255;
    case NT_ATLAS_FIX_LSHAPE: {
        uint32_t arm = 8;
        bool op = (x < arm) || (y >= (uint32_t)s->h - arm);
        return op ? 255 : 0;
    }
    case NT_ATLAS_FIX_TRI:
        return (x >= y) ? 255 : 0;
    case NT_ATLAS_FIX_BORDER: {
        bool inside = (x >= 4 && x < (uint32_t)s->w - 4 && y >= 4 && y < (uint32_t)s->h - 4);
        return inside ? 255 : 0;
    }
    }
    return 255;
}

/* Add the fixed sprite set. Caller must have called nt_atlas_begin first and
 * owns the atlas options (this fixture never touches the transform control). */
static inline void atlas_transform_fixture_add(NtAtlasBuild *atlas) {
    uint8_t px[NT_ATLAS_FIXTURE_MAX_PX];
    for (int i = 0; i < NT_ATLAS_FIXTURE_COUNT; ++i) {
        const nt_atlas_fixture_spec_t *s = &k_atlas_fixture_sprites[i];
        for (uint32_t y = 0; y < s->h; ++y) {
            for (uint32_t x = 0; x < s->w; ++x) {
                uint8_t *p = px + ((size_t)((y * s->w) + x) * 4);
                p[0] = s->r;
                p[1] = s->g;
                p[2] = s->b;
                p[3] = nt_atlas_fixture_alpha(s, x, y);
            }
        }
        nt_atlas_sprite_opts_t opts = nt_atlas_sprite_opts_defaults();
        opts.name = s->name; /* raw sprites require an explicit name */
        nt_atlas_add_raw(atlas, px, s->w, s->h, &opts);
    }
}

/* Write one `index transform x y w h` line per region, sorted by region index.
 * x/y/w/h are the region's atlas-UV bbox (0-65535) — a placement fingerprint
 * that flips value under rotation. Self-contained pack walk (bench metrics do
 * not expose per-region transform); every offset is guarded against pack_len. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat guard-per-read pack walk; splitting detaches guards from reads
static inline bool atlas_transform_fixture_dump_regions(const void *pack_bytes, size_t pack_len, FILE *out) {
    if (pack_bytes == NULL || out == NULL || pack_len < sizeof(NtPackHeader)) {
        return false;
    }
    const uint8_t *buf = (const uint8_t *)pack_bytes;
    const NtPackHeader *hdr = (const NtPackHeader *)buf;
    if (hdr->magic != NT_PACK_MAGIC || hdr->version != NT_PACK_VERSION) {
        return false;
    }
    const uint64_t entries_end = sizeof(NtPackHeader) + ((uint64_t)hdr->asset_count * sizeof(NtAssetEntry));
    if (entries_end > pack_len) {
        return false;
    }
    const NtAssetEntry *entries = (const NtAssetEntry *)(buf + sizeof(NtPackHeader));
    const NtAssetEntry *atlas = NULL;
    for (uint16_t i = 0; i < hdr->asset_count; ++i) {
        if (entries[i].asset_type == NT_ASSET_ATLAS && entries[i].offset < pack_len && (uint64_t)entries[i].size <= pack_len - entries[i].offset) {
            atlas = &entries[i];
            break;
        }
    }
    if (atlas == NULL || atlas->size < sizeof(NtAtlasHeader)) {
        return false;
    }
    const uint8_t *ablob = buf + atlas->offset;
    const NtAtlasHeader *ah = (const NtAtlasHeader *)ablob;
    if (ah->magic != NT_ATLAS_MAGIC || ah->version != NT_ATLAS_VERSION) {
        return false;
    }
    const uint64_t regions_off = sizeof(NtAtlasHeader) + ((uint64_t)ah->page_count * sizeof(uint64_t));
    const uint64_t regions_end = regions_off + ((uint64_t)ah->region_count * sizeof(NtAtlasRegion));
    const uint64_t verts_end = (uint64_t)ah->vertex_offset + ((uint64_t)ah->total_vertex_count * sizeof(NtAtlasVertex));
    if (regions_end > atlas->size || verts_end > atlas->size) {
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
        uint16_t umin = UINT16_MAX;
        uint16_t umax = 0;
        uint16_t vmin = UINT16_MAX;
        uint16_t vmax = 0;
        for (uint32_t j = 0; j < nv; ++j) {
            const NtAtlasVertex *p = &verts[vstart + j];
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
        if (nv == 0) {
            umin = 0;
            vmin = 0;
        }
        (void)fprintf(out, "%u %u %u %u %u %u\n", (unsigned)i, (unsigned)regions[i].transform, (unsigned)umin, (unsigned)vmin, (unsigned)(umax - umin), (unsigned)(vmax - vmin));
    }
    return true;
}

#endif /* NT_TEST_ATLAS_TRANSFORM_FIXTURE_H */
