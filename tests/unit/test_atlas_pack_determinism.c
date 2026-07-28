/* Default packing keeps density and hull metrics stable; wall-clock time is intentionally excluded. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows SDK must be included early (before stdnoreturn.h from C17 headers) */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* clang-format off */
#include "nt_atlas_format.h"
#include "nt_builder.h"
#include "nt_builder_atlas_geometry.h" /* d4_compose / d4_inverse for the fold relative */
#include "nt_pack_format.h"
#include "ntpack_parse.h"
#include "test_helpers/atlas_dedup_fixture.h"
#include "unity.h"
/* clang-format on */

/* --- Temp directory for test output --- */

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <dirent.h>
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define TMP_DIR "build/tests/tmp"

void setUp(void) {}
void tearDown(void) {}

/* --- Deterministic mini-corpus ---
 * Every sprite's bytes are produced by a pure integer function of (x, y), so the
 * corpus is byte-identical on every run with zero fixture-file / LFS dependency.
 * All sprites carry opaque content (never fully transparent — that trips the
 * empty-mask-after-trim abort, see test_atlas_transparent_trim). */

typedef enum {
    SPR_SOLID,     /* fully opaque coloured rectangle */
    SPR_TRI_LOWER, /* opaque where x >= y (lower-right triangle) */
    SPR_TRI_RAMP,  /* diagonal alpha ramp — AA-ish triangular silhouette */
    SPR_BORDERED,  /* opaque square inset in a transparent border (trim variety) */
    SPR_LSHAPE,    /* left arm + bottom arm — trivial D4 stabiliser, so exactly one
                    * relative transform can ever match it */
    SPR_LSHAPE_Q,  /* the quarter-turn of SPR_LSHAPE at the transposed dims */
} spr_kind_t;

typedef struct {
    const char *name;
    uint32_t w, h;
    spr_kind_t kind;
    uint8_t r, g, b;
} spr_spec_t;

/* 7 sprites: solid rects of varied aspect, two triangles (hard + ramp edge),
 * one fully-opaque square, one inset-border square. Fixed, order-stable. */
/* clang-format off */
static const spr_spec_t k_corpus[] = {
    {"solid_a",   16, 16, SPR_SOLID,     200,  40,  40},
    {"solid_b",   24, 16, SPR_SOLID,      40, 200,  40},
    {"solid_c",   16, 24, SPR_SOLID,      40,  40, 200},
    {"opaque_sq", 20, 20, SPR_SOLID,     220, 220, 220},
    {"tri_lower", 32, 32, SPR_TRI_LOWER, 240, 180,  20},
    {"tri_ramp",  28, 20, SPR_TRI_RAMP,   20, 200, 220},
    {"bordered",  18, 18, SPR_BORDERED,  180,  20, 180},
};
/* clang-format on */
#define CORPUS_COUNT ((int)(sizeof(k_corpus) / sizeof(k_corpus[0])))

/* Left arm + bottom arm. Both arms touch every edge, so alpha-trim recovers the
 * full w x h box and the twin below keeps the exact transposed dims. */
static bool lshape_opaque(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { return x < w / 4 || y >= h - (h / 4); }

static uint8_t gen_alpha(const spr_spec_t *s, uint32_t x, uint32_t y) {
    switch (s->kind) {
    case SPR_SOLID:
        return 255;
    case SPR_TRI_LOWER:
        return (x >= y) ? 255 : 0;
    case SPR_TRI_RAMP: {
        /* Opaque triangle x >= y; a 3px diagonal band ramps 64->255 for an
         * AA-ish edge. Silhouette stays triangular (alpha >= threshold). */
        int32_t d = (int32_t)x - (int32_t)y;
        if (d >= 3) {
            return 255;
        }
        return (d >= 0) ? (uint8_t)(64 + (d * 64)) : 0; /* 64,128,192 across the band */
    }
    case SPR_BORDERED:
        return (x >= 4 && x < s->w - 4 && y >= 4 && y < s->h - 4) ? 255 : 0;
    case SPR_LSHAPE:
        return lshape_opaque(x, y, s->w, s->h) ? 255 : 0;
    case SPR_LSHAPE_Q:
        /* Source L carries the transposed dims; (sx, sy) -> (sh - 1 - sy, sx). */
        return lshape_opaque(y, s->w - 1 - x, s->h, s->w) ? 255 : 0;
    }
    return 0;
}

/* Fill an RGBA buffer from a spec — pure, no globals, byte-identical each call. */
static void gen_sprite(uint8_t *px, const spr_spec_t *s) {
    for (uint32_t y = 0; y < s->h; ++y) {
        for (uint32_t x = 0; x < s->w; ++x) {
            uint8_t *p = px + ((size_t)((y * s->w) + x) * 4);
            p[0] = s->r;
            p[1] = s->g;
            p[2] = s->b;
            p[3] = gen_alpha(s, x, y);
        }
    }
}

/* Pack the whole mini-corpus with nt_atlas_opts_defaults() (NO cache dir → real
 * default path) to `path`, then parse the produced .ntpack into `out`.
 * Returns true on a clean pack + parse. */
static bool pack_and_parse_corpus_with_geometry(const char *path, nt_atlas_shape_t shape, bool use_default_added_area, float added_area_percent, bool sprite_override, nt_bench_atlas_metrics_t *out) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }

    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.shape = shape;
    if (!use_default_added_area) {
        opts.max_added_area_percent = added_area_percent;
    }
    NtAtlasBuild *atlas_build_128 = nt_atlas_begin(ctx, "det_corpus", &opts);

    uint8_t *bufs[CORPUS_COUNT] = {0};
    for (int i = 0; i < CORPUS_COUNT; ++i) {
        const spr_spec_t *s = &k_corpus[i];
        bufs[i] = (uint8_t *)malloc((size_t)s->w * s->h * 4);
        TEST_ASSERT_NOT_NULL(bufs[i]);
        gen_sprite(bufs[i], s);
        /* raw sprites require an explicit name (no path to derive one from). */
        nt_atlas_sprite_opts_t sprite_opts = nt_atlas_sprite_opts_defaults();
        sprite_opts.name = s->name;
        if (sprite_override && s->kind == SPR_TRI_RAMP) {
            sprite_opts.max_added_area_percent = 2.0F;
            sprite_opts.has_max_added_area_percent = true;
            sprite_opts.alpha_threshold = 128;
            sprite_opts.has_alpha_threshold = true;
        }
        nt_atlas_add_raw(atlas_build_128, bufs[i], s->w, s->h, &sprite_opts);
    }

    (void)nt_atlas_commit(atlas_build_128);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);

    for (int i = 0; i < CORPUS_COUNT; ++i) {
        free(bufs[i]);
    }
    if (r != NT_BUILD_OK) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    return nt_bench_parse_ntpack(path, out) == 0;
}

static bool pack_and_parse_corpus_with_added_area(const char *path, float added_area_percent, nt_bench_atlas_metrics_t *out) {
    return pack_and_parse_corpus_with_geometry(path, NT_ATLAS_SHAPE_CONCAVE_CONTOUR, false, added_area_percent, false, out);
}

static bool pack_and_parse_default_corpus(const char *path, nt_bench_atlas_metrics_t *out) { return pack_and_parse_corpus_with_geometry(path, NT_ATLAS_SHAPE_CONCAVE_CONTOUR, true, 0.0F, false, out); }

/* Round a density to 1e-6 fixed point — exact-integer equality across two packs
 * avoids raw double-bit comparison flagging benign last-ULP noise as regression. */
static int64_t density_fixed(double d) { return llround(d * 1000000.0); }

static bool files_are_identical(const char *a_path, const char *b_path) {
    FILE *a = fopen(a_path, "rb");
    FILE *b = fopen(b_path, "rb");
    if (!a || !b) {
        if (a) {
            (void)fclose(a);
        }
        if (b) {
            (void)fclose(b);
        }
        return false;
    }

    bool equal = true;
    uint8_t a_buf[4096];
    uint8_t b_buf[4096];
    for (;;) {
        size_t a_size = fread(a_buf, 1, sizeof(a_buf), a);
        size_t b_size = fread(b_buf, 1, sizeof(b_buf), b);
        if (a_size != b_size || memcmp(a_buf, b_buf, a_size) != 0) {
            equal = false;
            break;
        }
        if (a_size < sizeof(a_buf)) {
            equal = feof(a) != 0 && feof(b) != 0;
            break;
        }
    }

    (void)fclose(a);
    (void)fclose(b);
    return equal;
}

static bool pack_default_corpus_with_threads(const char *path, uint32_t thread_count) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, thread_count);

    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "det_default_threads", NULL);
    uint8_t *bufs[CORPUS_COUNT] = {0};
    for (int i = 0; i < CORPUS_COUNT; ++i) {
        const spr_spec_t *s = &k_corpus[i];
        bufs[i] = (uint8_t *)malloc((size_t)s->w * s->h * 4);
        TEST_ASSERT_NOT_NULL(bufs[i]);
        gen_sprite(bufs[i], s);
        nt_atlas_sprite_opts_t sprite_opts = nt_atlas_sprite_opts_defaults();
        sprite_opts.name = s->name;
        nt_atlas_add_raw(atlas, bufs[i], s->w, s->h, &sprite_opts);
    }

    (void)nt_atlas_commit(atlas);
    nt_build_result_t result = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    for (int i = 0; i < CORPUS_COUNT; ++i) {
        free(bufs[i]);
    }
    return result == NT_BUILD_OK;
}

/* Two in-process packs of the SAME corpus at default opts must produce identical
 * region/page/vertex/hull counts and a bit-stable density. */
void test_default_metrics_stable_across_two_packs(void) {
    nt_bench_atlas_metrics_t a;
    nt_bench_atlas_metrics_t b;
    TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_default_corpus(TMP_DIR "/det_corpus_a.ntpack", &a), "default pack/parse A failed");
    TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_default_corpus(TMP_DIR "/det_corpus_b.ntpack", &b), "default pack/parse B failed");

    TEST_ASSERT_EQUAL_UINT16(a.region_count, b.region_count);
    TEST_ASSERT_EQUAL_UINT16(a.page_count, b.page_count);
    TEST_ASSERT_EQUAL_UINT32(a.total_vertex_count, b.total_vertex_count);
    TEST_ASSERT_EQUAL_UINT32(a.hull_vert_total, b.hull_vert_total);
    TEST_ASSERT_EQUAL_INT64(density_fixed(a.density_fill_texture), density_fixed(b.density_fill_texture));
    TEST_ASSERT_EQUAL_INT64(density_fixed(a.density_fill_frontier), density_fixed(b.density_fill_frontier));
}

void test_zero_added_area_metrics_stable_across_two_packs(void) {
    nt_bench_atlas_metrics_t a;
    nt_bench_atlas_metrics_t b;
    TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus_with_added_area(TMP_DIR "/det_zero_a.ntpack", 0.0F, &a), "0% pack/parse A failed");
    TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus_with_added_area(TMP_DIR "/det_zero_b.ntpack", 0.0F, &b), "0% pack/parse B failed");

    TEST_ASSERT_EQUAL_UINT16(a.region_count, b.region_count);
    TEST_ASSERT_EQUAL_UINT16(a.page_count, b.page_count);
    TEST_ASSERT_EQUAL_UINT32(a.total_vertex_count, b.total_vertex_count);
    TEST_ASSERT_EQUAL_UINT32(a.hull_vert_total, b.hull_vert_total);
    TEST_ASSERT_EQUAL_INT64(density_fixed(a.density_fill_texture), density_fixed(b.density_fill_texture));
    TEST_ASSERT_EQUAL_INT64(density_fixed(a.density_fill_frontier), density_fixed(b.density_fill_frontier));
}

void test_positive_added_area_pack_bytes_repeat(void) {
    const nt_atlas_shape_t shapes[] = {NT_ATLAS_SHAPE_CONCAVE_CONTOUR, NT_ATLAS_SHAPE_CONVEX_HULL};
    const char *a_paths[] = {TMP_DIR "/det_concave_positive_a.ntpack", TMP_DIR "/det_convex_positive_a.ntpack"};
    const char *b_paths[] = {TMP_DIR "/det_concave_positive_b.ntpack", TMP_DIR "/det_convex_positive_b.ntpack"};
    for (uint32_t shape = 0; shape < 2; shape++) {
        nt_bench_atlas_metrics_t a;
        nt_bench_atlas_metrics_t b;
        TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus_with_geometry(a_paths[shape], shapes[shape], false, 1.5F, true, &a), "positive pack/parse A failed");
        TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus_with_geometry(b_paths[shape], shapes[shape], false, 1.5F, true, &b), "positive pack/parse B failed");
        TEST_ASSERT_TRUE_MESSAGE(files_are_identical(a_paths[shape], b_paths[shape]), "positive-area atlas bytes are not deterministic");
        TEST_ASSERT_EQUAL_UINT32(a.hull_vert_total, b.hull_vert_total);
    }
}

void test_default_added_area_threads_are_byte_deterministic(void) {
    const char *single = TMP_DIR "/det_default_threads_1.ntpack";
    const char *parallel = TMP_DIR "/det_default_threads_4.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(pack_default_corpus_with_threads(single, 1), "default 10% single-thread pack failed");
    TEST_ASSERT_TRUE_MESSAGE(pack_default_corpus_with_threads(parallel, 4), "default 10% four-thread pack failed");
    TEST_ASSERT_TRUE_MESSAGE(files_are_identical(single, parallel), "default 10% atlas bytes differ between 1 and 4 threads");
}

/* These pins move only with an intentional default-output or cache-key change. */
#define PIN_REGION_COUNT 7
#define PIN_PAGE_COUNT 1
#define PIN_HULL_VERT_TOTAL 30
#define PIN_DENSITY_FILL_TEXTURE 0.298702
#define PIN_DENSITY_FILL_FRONTIER 0.428994
#define PIN_DENSITY_TOL 1e-4

void test_metrics_match_pinned_baseline(void) {
    nt_bench_atlas_metrics_t m;
    TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_default_corpus(TMP_DIR "/det_corpus_pin.ntpack", &m), "default pack/parse failed");

    TEST_ASSERT_EQUAL_UINT16(PIN_REGION_COUNT, m.region_count);
    TEST_ASSERT_EQUAL_UINT16(PIN_PAGE_COUNT, m.page_count);
    TEST_ASSERT_EQUAL_UINT32(PIN_HULL_VERT_TOTAL, m.hull_vert_total);
    TEST_ASSERT_INT64_WITHIN_MESSAGE((int64_t)(PIN_DENSITY_TOL * 1000000.0), density_fixed(PIN_DENSITY_FILL_TEXTURE), density_fixed(m.density_fill_texture),
                                     "density_fill_texture drifted from pinned baseline");
    TEST_ASSERT_INT64_WITHIN_MESSAGE((int64_t)(PIN_DENSITY_TOL * 1000000.0), density_fixed(PIN_DENSITY_FILL_FRONTIER), density_fixed(m.density_fill_frontier),
                                     "density_fill_frontier drifted from pinned baseline");
}

/* Extra per-sprite margin must be split evenly around content. */

/* Read region 0's min/max atlas UV from a single-sprite pack. Only the atlas
 * blob's own fields are read (offsets guarded against the file size), so this is
 * a self-contained reader independent of the aggregate bench parser. */
static bool region0_uv_extents(const char *path, uint16_t *umin, uint16_t *umax, uint16_t *vmin, uint16_t *vmax) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    (void)fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    if (sz < (long)sizeof(NtPackHeader)) {
        (void)fclose(f);
        return false;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        (void)fclose(f);
        return false;
    }
    bool ok = fread(buf, 1, (size_t)sz, f) == (size_t)sz;
    (void)fclose(f);
    if (!ok) {
        free(buf);
        return false;
    }

    const NtPackHeader *hdr = (const NtPackHeader *)buf;
    const NtAssetEntry *entries = (const NtAssetEntry *)(buf + sizeof(NtPackHeader));
    const NtAssetEntry *atlas = NULL;
    for (uint16_t i = 0; i < hdr->asset_count; i++) {
        if (entries[i].asset_type == NT_ASSET_ATLAS) {
            atlas = &entries[i];
            break;
        }
    }
    if (!atlas || (uint64_t)atlas->offset + atlas->size > (uint64_t)sz) {
        free(buf);
        return false;
    }

    const uint8_t *ablob = buf + atlas->offset;
    const NtAtlasHeader *ah = (const NtAtlasHeader *)ablob;
    uint32_t regions_off = (uint32_t)sizeof(NtAtlasHeader) + ((uint32_t)ah->page_count * (uint32_t)sizeof(uint64_t));
    const NtAtlasRegion *regions = (const NtAtlasRegion *)(ablob + regions_off);
    const NtAtlasVertex *verts = (const NtAtlasVertex *)(ablob + ah->vertex_offset);

    uint16_t u0 = UINT16_MAX;
    uint16_t u1 = 0;
    uint16_t v0 = UINT16_MAX;
    uint16_t v1 = 0;
    uint32_t vstart = regions[0].vertex_start;
    uint32_t nv = regions[0].vertex_count;
    for (uint32_t j = 0; j < nv; j++) {
        const NtAtlasVertex *p = &verts[vstart + j];
        if (p->atlas_u < u0) {
            u0 = p->atlas_u;
        }
        if (p->atlas_u > u1) {
            u1 = p->atlas_u;
        }
        if (p->atlas_v < v0) {
            v0 = p->atlas_v;
        }
        if (p->atlas_v > v1) {
            v1 = p->atlas_v;
        }
    }
    *umin = u0;
    *umax = u1;
    *vmin = v0;
    *vmax = v1;
    free(buf);
    return true;
}

/* Serialized UV alone cannot prove the composed-pixel offset. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool page0_opaque_bounds(const char *path, uint32_t *out_minx, uint32_t *out_maxx, uint32_t *out_miny, uint32_t *out_maxy, uint32_t *out_w, uint32_t *out_h) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    (void)fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    if (sz < (long)sizeof(NtPackHeader)) {
        (void)fclose(f);
        return false;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        (void)fclose(f);
        return false;
    }
    bool ok = fread(buf, 1, (size_t)sz, f) == (size_t)sz;
    (void)fclose(f);
    if (!ok) {
        free(buf);
        return false;
    }

    const NtPackHeader *hdr = (const NtPackHeader *)buf;
    const NtAssetEntry *entries = (const NtAssetEntry *)(buf + sizeof(NtPackHeader));
    const NtAssetEntry *atlas = NULL;
    for (uint16_t i = 0; i < hdr->asset_count; i++) {
        if (entries[i].asset_type == NT_ASSET_ATLAS) {
            atlas = &entries[i];
            break;
        }
    }
    if (!atlas || (uint64_t)atlas->offset + atlas->size > (uint64_t)sz || atlas->size < sizeof(NtAtlasHeader) + sizeof(uint64_t)) {
        free(buf);
        return false;
    }

    const uint8_t *ablob = buf + atlas->offset;
    const NtAtlasHeader *ah = (const NtAtlasHeader *)ablob;
    if (ah->page_count == 0) {
        free(buf);
        return false;
    }
    uint64_t page0_id;
    memcpy(&page0_id, ablob + sizeof(NtAtlasHeader), sizeof(page0_id));

    const NtAssetEntry *tex = NULL;
    for (uint16_t i = 0; i < hdr->asset_count; i++) {
        if (entries[i].asset_type == NT_ASSET_TEXTURE && entries[i].resource_id == page0_id) {
            tex = &entries[i];
            break;
        }
    }
    if (!tex || (uint64_t)tex->offset + tex->size > (uint64_t)sz || tex->size < sizeof(NtTextureAssetHeader)) {
        free(buf);
        return false;
    }

    const uint8_t *tblob = buf + tex->offset;
    const NtTextureAssetHeader *th = (const NtTextureAssetHeader *)tblob;
    uint32_t w = th->width;
    uint32_t h = th->height;
    if (th->compression != 0 || (uint64_t)sizeof(NtTextureAssetHeader) + (uint64_t)w * h * 4 > tex->size) {
        free(buf); /* RAW only + guard the pixel span */
        return false;
    }
    const uint8_t *px = tblob + sizeof(NtTextureAssetHeader);

    uint32_t minx = w;
    uint32_t maxx = 0;
    uint32_t miny = h;
    uint32_t maxy = 0;
    bool any = false;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            if (px[((size_t)((y * w) + x) * 4) + 3] >= 128) { /* opaque = alpha >= mid */
                any = true;
                minx = x < minx ? x : minx;
                maxx = x > maxx ? x : maxx;
                miny = y < miny ? y : miny;
                maxy = y > maxy ? y : maxy;
            }
        }
    }
    free(buf);
    if (!any) {
        return false;
    }
    *out_minx = minx;
    *out_maxx = maxx;
    *out_miny = miny;
    *out_maxy = maxy;
    *out_w = w;
    *out_h = h;
    return true;
}

/* A tight page exposes asymmetric placement of surplus margin. */
void test_margin_override_content_centered(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/margin_center.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.margin = 0;
    opts.extrude = 0;
    opts.padding = 0;
    opts.power_of_two = false; /* tight page → page edges hug the reserved footprint */
    NtAtlasBuild *atlas_build_396 = nt_atlas_begin(ctx, "center", &opts);

    enum { SQ = 20, MARGIN_OVERRIDE = 8 };
    uint8_t *px = (uint8_t *)malloc((size_t)SQ * SQ * 4);
    TEST_ASSERT_NOT_NULL(px);
    for (size_t i = 0; i < (size_t)SQ * SQ; i++) {
        px[(i * 4) + 0] = 200;
        px[(i * 4) + 1] = 120;
        px[(i * 4) + 2] = 40;
        px[(i * 4) + 3] = 255;
    }
    nt_atlas_add_raw(atlas_build_396, px, SQ, SQ, &(nt_atlas_sprite_opts_t){.name = "sq", .origin_x = 0.5F, .origin_y = 0.5F, .shape = NT_ATLAS_SPRITE_SHAPE_RECT, .margin = MARGIN_OVERRIDE});
    (void)nt_atlas_commit(atlas_build_396);

    uint32_t n = 0;
    (void)nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, n, "square with margin override must pack cleanly");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);
    free(px);

    uint16_t umin = 0;
    uint16_t umax = 0;
    uint16_t vmin = 0;
    uint16_t vmax = 0;
    TEST_ASSERT_TRUE_MESSAGE(region0_uv_extents(path, &umin, &umax, &vmin, &vmax), "parse produced pack");

    /* Centered ⇒ left inset (umin) == right inset (full-umax) ⇒ umin+umax == full.
     * Tolerance absorbs UV quantization at the two content edges. */
    const int32_t full = 65535;
    const int32_t tol = 4;
    TEST_ASSERT_INT_WITHIN_MESSAGE(tol, full, (int32_t)umin + (int32_t)umax, "content not horizontally centered in its margin cell");
    TEST_ASSERT_INT_WITHIN_MESSAGE(tol, full, (int32_t)vmin + (int32_t)vmax, "content not vertically centered in its margin cell");
    /* The surplus must actually offset the content — guards against a degenerate
     * origin-anchored region that would satisfy the sum only by both edges at 0. */
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(0, umin, "left inset must be nonzero (margin surplus present)");
    TEST_ASSERT_GREATER_THAN_UINT16_MESSAGE(0, vmin, "top inset must be nonzero (margin surplus present)");

    /* Pixel bounds independently verify the serialized centering contract. */
    uint32_t cx0 = 0;
    uint32_t cx1 = 0;
    uint32_t cy0 = 0;
    uint32_t cy1 = 0;
    uint32_t pw = 0;
    uint32_t ph = 0;
    TEST_ASSERT_TRUE_MESSAGE(page0_opaque_bounds(path, &cx0, &cx1, &cy0, &cy1, &pw, &ph), "decode page 0 pixels");
    uint32_t left = cx0;
    uint32_t right = pw - 1 - cx1;
    uint32_t top = cy0;
    uint32_t bottom = ph - 1 - cy1;
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, (int32_t)left, (int32_t)right, "composed content not horizontally centered in page pixels");
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, (int32_t)top, (int32_t)bottom, "composed content not vertically centered in page pixels");
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, left, "left pixel inset must be nonzero (margin surplus present)");
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, top, "top pixel inset must be nonzero (margin surplus present)");
    /* And the serialized UV must point AT those opaque pixels: umin/vmin map back
     * to the content's top-left pixel within a texel (guards a stale-UV compose). */
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, (int32_t)cx0, (int32_t)((uint64_t)umin * pw / 65535), "region umin does not map to the opaque content left edge");
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, (int32_t)cy0, (int32_t)((uint64_t)vmin * ph / 65535), "region vmin does not map to the opaque content top edge");
    (void)remove(path);
}

/* Count atlas_*.bin cache files (atlas cache entries) in `dir`. A distinct key
 * writes a distinct file, so the count is a proxy for cache-key distinctness. */
static uint32_t count_atlas_cache_files(const char *dir) {
    uint32_t count = 0;
#ifdef _WIN32
    char pattern[512];
    (void)snprintf(pattern, sizeof(pattern), "%s\\atlas_*.bin", dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            count++;
        } while (FindNextFileA(hFind, &fd));
        (void)FindClose(hFind);
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) { // NOLINT(concurrency-mt-unsafe)
            size_t len = strlen(ent->d_name);
            if (len > 10 && strncmp(ent->d_name, "atlas_", 6) == 0 && strcmp(ent->d_name + len - 4, ".bin") == 0) {
                count++;
            }
        }
        (void)closedir(d);
    }
#endif
    return count;
}

/* Remove prior atlas cache .bin files so counts reflect the current test run. */
static void clear_atlas_cache_files(const char *cache) {
#ifdef _WIN32
    char pattern[512];
    (void)snprintf(pattern, sizeof(pattern), "%s\\atlas_*.bin", cache);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            char p[512];
            (void)snprintf(p, sizeof(p), "%s\\%s", cache, fd.cFileName);
            (void)DeleteFileA(p);
        } while (FindNextFileA(hFind, &fd));
        (void)FindClose(hFind);
    }
#else
    DIR *d = opendir(cache);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) { // NOLINT(concurrency-mt-unsafe)
            size_t len = strlen(ent->d_name);
            if (len > 4 && strcmp(ent->d_name + len - 4, ".bin") == 0) {
                char p[512];
                (void)snprintf(p, sizeof(p), "%s/%s", cache, ent->d_name);
                (void)remove(p);
            }
        }
        (void)closedir(d);
    }
#endif
}

/* Pack the mini-corpus to `path` through the on-disk cache `cache` at atlas mask.
 * sprite0_mask: per-sprite allowed_transforms for sprite 0 (0 = inherit atlas mask). */
static bool pack_corpus_masked(const char *path, const char *cache, uint8_t mask, uint8_t sprite0_mask) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_builder_set_cache_dir(ctx, cache);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = mask;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "det_mask", &opts);
    uint8_t *bufs[CORPUS_COUNT] = {0};
    for (int i = 0; i < CORPUS_COUNT; ++i) {
        const spr_spec_t *s = &k_corpus[i];
        bufs[i] = (uint8_t *)malloc((size_t)s->w * s->h * 4);
        TEST_ASSERT_NOT_NULL(bufs[i]);
        gen_sprite(bufs[i], s);
        nt_atlas_sprite_opts_t sprite_opts = nt_atlas_sprite_opts_defaults();
        sprite_opts.name = s->name;
        if (i == 0) {
            sprite_opts.allowed_transforms = sprite0_mask;
        }
        nt_atlas_add_raw(atlas, bufs[i], s->w, s->h, &sprite_opts);
    }
    (void)nt_atlas_commit(atlas);
    nt_build_result_t result = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    for (int i = 0; i < CORPUS_COUNT; ++i) {
        free(bufs[i]);
    }
    return result == NT_BUILD_OK;
}

/* allowed_transforms participates in the atlas cache key: the same corpus at
 * two masks writes two distinct cache entries. */
void test_allowed_transforms_changes_cache_key(void) {
    const char *cache = TMP_DIR "/atlas_cache_mask_dir";
    (void)MKDIR(TMP_DIR);
    (void)MKDIR(cache);
    clear_atlas_cache_files(cache);
    TEST_ASSERT_EQUAL_UINT32(0, count_atlas_cache_files(cache));

    TEST_ASSERT_TRUE_MESSAGE(pack_corpus_masked(TMP_DIR "/det_mask_all.ntpack", cache, NT_ATLAS_TRANSFORMS_ALL, 0), "ALL-mask pack failed");
    TEST_ASSERT_EQUAL_UINT32(1, count_atlas_cache_files(cache));
    TEST_ASSERT_TRUE_MESSAGE(pack_corpus_masked(TMP_DIR "/det_mask_identity.ntpack", cache, NT_ATLAS_TRANSFORMS_IDENTITY, 0), "IDENTITY-mask pack failed");
    /* Two entries ⇒ the mask changed the cache key. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, count_atlas_cache_files(cache), "allowed_transforms must change the atlas cache key");
}

/* A fixed atlas mask makes the second cache entry prove that the raw
 * per-sprite transform override participates in the key. */
void test_sprite_transforms_override_changes_cache_key(void) {
    const char *cache = TMP_DIR "/atlas_cache_sprite_mask_dir";
    (void)MKDIR(TMP_DIR);
    (void)MKDIR(cache);
    clear_atlas_cache_files(cache);
    TEST_ASSERT_EQUAL_UINT32(0, count_atlas_cache_files(cache));

    TEST_ASSERT_TRUE_MESSAGE(pack_corpus_masked(TMP_DIR "/det_smask_inherit.ntpack", cache, NT_ATLAS_TRANSFORMS_ALL, 0), "inherit-override pack failed");
    TEST_ASSERT_EQUAL_UINT32(1, count_atlas_cache_files(cache));
    TEST_ASSERT_TRUE_MESSAGE(pack_corpus_masked(TMP_DIR "/det_smask_identity.ntpack", cache, NT_ATLAS_TRANSFORMS_ALL, NT_ATLAS_TRANSFORMS_IDENTITY), "sprite-override pack failed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, count_atlas_cache_files(cache), "per-sprite transforms override must change the atlas cache key");
}

/* --- DEDUP-03: repack stability and alias-root stability --- */

/* k_corpus holds no duplicate pair, so it cannot say anything about dedup. This
 * corpus adds a quarter-turned twin. The L shape is deliberate: a solid rectangle
 * matches under all four diagonal relatives, and a transpose is self-inverse, so
 * neither could tell a correct search direction from an inverted one. */
/* clang-format off */
static const spr_spec_t k_dup_corpus[] = {
    {"dup_tall",  16, 24, SPR_LSHAPE,    120, 180,  60},
    {"solo_ramp", 28, 20, SPR_TRI_RAMP,   20, 200, 220},
    {"dup_wide",  24, 16, SPR_LSHAPE_Q,  120, 180,  60}, /* quarter-turn of dup_tall */
    {"solo_tri",  32, 32, SPR_TRI_LOWER, 240, 180,  20},
    {"solo_box",  18, 18, SPR_BORDERED,  180,  20, 180},
};
/* clang-format on */
#define DUP_CORPUS_COUNT ((int)(sizeof(k_dup_corpus) / sizeof(k_dup_corpus[0])))
#define DUP_ROOT 0
#define DUP_ALIAS 2

/* clang-format off */
static const int k_dup_order_declared[] = {0, 1, 2, 3, 4};
/* Unrelated sprites permuted; the duplicate pair keeps its relative add order. */
static const int k_dup_order_shuffled[] = {4, 0, 3, 2, 1};
/* The group's OWN add order swapped — the control for the root assertion. */
static const int k_dup_order_swapped[]  = {2, 1, 0, 3, 4};
/* clang-format on */

static bool pack_dup_corpus_threads(const char *path, const int *order, uint32_t threads) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, threads);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "det_dup", &opts);
    for (int i = 0; i < DUP_CORPUS_COUNT; ++i) {
        const spr_spec_t *s = &k_dup_corpus[order[i]];
        uint8_t *px = (uint8_t *)malloc((size_t)s->w * s->h * 4);
        TEST_ASSERT_NOT_NULL(px);
        gen_sprite(px, s);
        nt_atlas_sprite_opts_t sprite_opts = nt_atlas_sprite_opts_defaults();
        sprite_opts.name = s->name;
        nt_atlas_add_raw(atlas, px, (uint16_t)s->w, (uint16_t)s->h, &sprite_opts);
        free(px);
    }
    (void)nt_atlas_commit(atlas);
    nt_build_result_t result = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return result == NT_BUILD_OK;
}

static bool pack_dup_corpus(const char *path, const int *order) { return pack_dup_corpus_threads(path, order, 1); }

static uint8_t *read_pack_bytes(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    (void)fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        (void)fclose(f);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        (void)fclose(f);
        return NULL;
    }
    *out_len = fread(buf, 1, (size_t)sz, f);
    (void)fclose(f);
    return buf;
}

static void collect_dup_regions(const char *path, nt_atlas_dedup_region_t *out) {
    size_t len = 0;
    uint8_t *bytes = read_pack_bytes(path, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(bytes, "read produced pack");
    uint32_t got = 0;
    const bool ok = atlas_dedup_collect_regions(bytes, len, out, (uint32_t)DUP_CORPUS_COUNT, &got);
    free(bytes);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)DUP_CORPUS_COUNT, got, "one region per sprite");
}

/* Region order is add order, so a corpus entry's region index is its position. */
static int region_of(const int *order, int corpus_index) {
    for (int i = 0; i < DUP_CORPUS_COUNT; ++i) {
        if (order[i] == corpus_index) {
            return i;
        }
    }
    return -1;
}

/* The pair shares one placement, and the relative from root to alias is the fold's
 * own relative transform. The packer may orient the shared rectangle, so neither
 * region's transform is identity on its own — only their quotient is stable. */
static uint8_t dup_pair_relative(const char *path, const int *order, int root_entry, int alias_entry) {
    nt_atlas_dedup_region_t regions[8] = {0};
    collect_dup_regions(path, regions);
    const int ri = region_of(order, root_entry);
    const int ai = region_of(order, alias_entry);
    TEST_ASSERT_TRUE_MESSAGE(ri >= 0 && ai >= 0, "corpus entry missing from the add order");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[ri].page_index, regions[ai].page_index, "the twin must share the root's page");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[ri].u_min, regions[ai].u_min, "the twin must share the root's placement U");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[ri].v_min, regions[ai].v_min, "the twin must share the root's placement V");
    return d4_compose(d4_inverse(regions[ri].transform), regions[ai].transform);
}

/* A quarter-turn is not self-inverse, so this reads the search DIRECTION, not just
 * that some fold happened — swapping the pair's add order must invert it. */
static uint8_t assert_dup_root_is(const char *path, const int *order, int root_entry, int alias_entry, const char *what) {
    const uint8_t rel = dup_pair_relative(path, order, root_entry, alias_entry);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(NT_ATLAS_XFORM_IDENTITY, rel, what);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(rel, d4_inverse(rel), "the twin's relative must be a quarter-turn, or direction is unobservable");
    return rel;
}

void test_repeat_build_is_byte_identical_with_dedup(void) {
    const char *a_path = TMP_DIR "/det_dup_a.ntpack";
    const char *b_path = TMP_DIR "/det_dup_b.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(pack_dup_corpus(a_path, k_dup_order_declared), "dedup corpus pack A failed");
    TEST_ASSERT_TRUE_MESSAGE(pack_dup_corpus(b_path, k_dup_order_declared), "dedup corpus pack B failed");
    /* The corpus must actually fold, or byte-identity says nothing about dedup. */
    assert_dup_root_is(a_path, k_dup_order_declared, DUP_ROOT, DUP_ALIAS, "the first-added member of the pair is the root");
    char sha_a[65];
    char sha_b[65];
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_file_sha256_hex(a_path, sha_a), "hash dedup pack A");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_file_sha256_hex(b_path, sha_b), "hash dedup pack B");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(sha_a, sha_b, "two dedup builds of one corpus must be byte-identical");
}

/* The dedup search itself runs pre-pack on one thread, but the packer and the
 * stats replay do not. The oracle is the 1-thread pack: two 4-thread runs could
 * agree on stable-but-thread-count-dependent bytes and still poison a cache
 * shared across thread counts. */
void test_dedup_threads_are_byte_deterministic(void) {
    const char *st_path = TMP_DIR "/det_dup_mt_st.ntpack";
    const char *a_path = TMP_DIR "/det_dup_mt_a.ntpack";
    const char *b_path = TMP_DIR "/det_dup_mt_b.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(pack_dup_corpus_threads(st_path, k_dup_order_declared, 1), "single-thread dedup pack failed");
    TEST_ASSERT_TRUE_MESSAGE(pack_dup_corpus_threads(a_path, k_dup_order_declared, 4), "threaded dedup pack A failed");
    TEST_ASSERT_TRUE_MESSAGE(pack_dup_corpus_threads(b_path, k_dup_order_declared, 4), "threaded dedup pack B failed");
    assert_dup_root_is(a_path, k_dup_order_declared, DUP_ROOT, DUP_ALIAS, "the threaded build must still fold the pair");
    char sha_st[65];
    char sha_a[65];
    char sha_b[65];
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_file_sha256_hex(st_path, sha_st), "hash single-thread dedup pack");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_file_sha256_hex(a_path, sha_a), "hash threaded dedup pack A");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_file_sha256_hex(b_path, sha_b), "hash threaded dedup pack B");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(sha_a, sha_b, "two threaded dedup builds must be byte-identical");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(sha_st, sha_a, "threaded dedup bytes must match the single-threaded pack");
}

void test_alias_root_survives_unrelated_reordering(void) {
    const char *declared = TMP_DIR "/det_dup_declared.ntpack";
    const char *shuffled = TMP_DIR "/det_dup_shuffled.ntpack";
    const char *swapped = TMP_DIR "/det_dup_swapped.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(pack_dup_corpus(declared, k_dup_order_declared), "declared-order pack failed");
    TEST_ASSERT_TRUE_MESSAGE(pack_dup_corpus(shuffled, k_dup_order_shuffled), "shuffled-order pack failed");
    /* Placement coordinates may legitimately move when the packer sees a different
     * input order, so the claim is about the root's identity, not the pack bytes. */
    const uint8_t rel = assert_dup_root_is(declared, k_dup_order_declared, DUP_ROOT, DUP_ALIAS, "the first-added member of the pair is the root");
    const uint8_t rel_shuffled = assert_dup_root_is(shuffled, k_dup_order_shuffled, DUP_ROOT, DUP_ALIAS, "reordering unrelated sprites must not move the alias root");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(rel, rel_shuffled, "an unrelated permutation must not change the fold relative");
    /* Control: swapping the pair's OWN add order does move the root, which inverts
     * the relative — the assertion above would be blind to a self-inverse one. */
    TEST_ASSERT_TRUE_MESSAGE(pack_dup_corpus(swapped, k_dup_order_swapped), "swapped-order pack failed");
    const uint8_t rel_swapped = assert_dup_root_is(swapped, k_dup_order_swapped, DUP_ALIAS, DUP_ROOT, "swapping the pair's own add order must move the root");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(d4_inverse(rel), rel_swapped, "swapping the add order must invert the fold relative");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_default_metrics_stable_across_two_packs);
    RUN_TEST(test_zero_added_area_metrics_stable_across_two_packs);
    RUN_TEST(test_positive_added_area_pack_bytes_repeat);
    RUN_TEST(test_default_added_area_threads_are_byte_deterministic);
    RUN_TEST(test_metrics_match_pinned_baseline);
    RUN_TEST(test_margin_override_content_centered);
    RUN_TEST(test_allowed_transforms_changes_cache_key);
    RUN_TEST(test_sprite_transforms_override_changes_cache_key);
    RUN_TEST(test_repeat_build_is_byte_identical_with_dedup);
    RUN_TEST(test_dedup_threads_are_byte_deterministic);
    RUN_TEST(test_alias_root_survives_unrelated_reordering);
    return UNITY_END();
}
