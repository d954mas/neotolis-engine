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
#include "nt_pack_format.h"
#include "ntpack_parse.h"
#include "unity.h"
/* clang-format on */

/* --- Temp directory for test output --- */

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
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

/* Fill an RGBA buffer from a spec — pure, no globals, byte-identical each call. */
static void gen_sprite(uint8_t *px, const spr_spec_t *s) {
    for (uint32_t y = 0; y < s->h; ++y) {
        for (uint32_t x = 0; x < s->w; ++x) {
            uint8_t a = 0;
            switch (s->kind) {
            case SPR_SOLID:
                a = 255;
                break;
            case SPR_TRI_LOWER:
                a = (x >= y) ? 255 : 0;
                break;
            case SPR_TRI_RAMP: {
                /* Opaque triangle x >= y; a 3px diagonal band ramps 64->255 for an
                 * AA-ish edge. Silhouette stays triangular (alpha >= threshold). */
                int32_t d = (int32_t)x - (int32_t)y;
                if (d >= 3) {
                    a = 255;
                } else if (d >= 0) {
                    a = (uint8_t)(64 + (d * 64)); /* 64,128,192 across the band */
                } else {
                    a = 0;
                }
                break;
            }
            case SPR_BORDERED: {
                bool inside = (x >= 4 && x < s->w - 4 && y >= 4 && y < s->h - 4);
                a = inside ? 255 : 0;
                break;
            }
            }
            uint8_t *p = px + ((size_t)((y * s->w) + x) * 4);
            p[0] = s->r;
            p[1] = s->g;
            p[2] = s->b;
            p[3] = a;
        }
    }
}

/* Pack the whole mini-corpus with nt_atlas_opts_defaults() (NO cache dir → real
 * default path) to `path`, then parse the produced .ntpack into `out`.
 * Returns true on a clean pack + parse. */
static bool pack_and_parse_corpus_with_geometry(const char *path, nt_atlas_shape_t shape, float added_area_percent, bool sprite_override, nt_bench_atlas_metrics_t *out) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }

    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.shape = shape;
    opts.max_added_area_percent = added_area_percent;
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
    return pack_and_parse_corpus_with_geometry(path, NT_ATLAS_SHAPE_CONCAVE_CONTOUR, added_area_percent, false, out);
}

static bool pack_and_parse_corpus(const char *path, nt_bench_atlas_metrics_t *out) { return pack_and_parse_corpus_with_added_area(path, 0.0F, out); }

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
void test_metrics_stable_across_two_packs(void) {
    nt_bench_atlas_metrics_t a;
    nt_bench_atlas_metrics_t b;
    TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus(TMP_DIR "/det_corpus_a.ntpack", &a), "pack/parse A failed");
    TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus(TMP_DIR "/det_corpus_b.ntpack", &b), "pack/parse B failed");

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
        TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus_with_geometry(a_paths[shape], shapes[shape], 1.5F, true, &a), "positive pack/parse A failed");
        TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus_with_geometry(b_paths[shape], shapes[shape], 1.5F, true, &b), "positive pack/parse B failed");
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
    TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus(TMP_DIR "/det_corpus_pin.ntpack", &m), "pack/parse failed");

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_metrics_stable_across_two_packs);
    RUN_TEST(test_positive_added_area_pack_bytes_repeat);
    RUN_TEST(test_default_added_area_threads_are_byte_deterministic);
    RUN_TEST(test_metrics_match_pinned_baseline);
    RUN_TEST(test_margin_override_content_centered);
    return UNITY_END();
}
