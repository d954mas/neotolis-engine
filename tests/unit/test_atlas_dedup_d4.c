/* D4 probe fixture contract: the 'F' images must be pairwise distinct and must
 * carry the dimension swap, or every later relative-transform assertion could
 * pass by symmetry instead of by correctness. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

/* clang-format off */
#include "nt_atlas_format.h"
#include "nt_builder.h"
#include "nt_builder_atlas_geometry.h"
#include "nt_pack_format.h"
#include "test_helpers/atlas_dedup_f_fixture.h"
#include "test_helpers/atlas_dedup_fixture.h"
#include "unity.h"
/* clang-format on */

#define TMP_DIR "build/tests/tmp"

void setUp(void) {}
void tearDown(void) {}

static uint8_t *read_bin_file(const char *path, size_t *out_len) {
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
    size_t rd = fread(buf, 1, (size_t)sz, f);
    (void)fclose(f);
    *out_len = rd;
    return buf;
}

/* --- Raw view of the produced atlas blob ---
 * atlas_dedup_collect_regions exposes UV extents only; the alias checks need the
 * per-vertex and per-index bytes. Every offset is guarded with a hard if. */

typedef struct {
    const NtAtlasRegion *regions;
    const NtAtlasVertex *verts;
    const uint16_t *indices;
    uint32_t region_count;
    uint32_t vertex_total;
    uint32_t index_total;
} atlas_view_t;

static bool atlas_view_open(const void *pack_bytes, size_t pack_len, atlas_view_t *out) {
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
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
    const uint64_t idx_end = (uint64_t)ah->index_offset + ((uint64_t)ah->total_index_count * sizeof(uint16_t));
    if (regions_end > asize || verts_end > asize || idx_end > asize) {
        return false;
    }
    out->regions = (const NtAtlasRegion *)(ablob + regions_off);
    out->verts = (const NtAtlasVertex *)(ablob + ah->vertex_offset);
    out->indices = (const uint16_t *)(ablob + ah->index_offset);
    out->region_count = ah->region_count;
    out->vertex_total = ah->total_vertex_count;
    out->index_total = ah->total_index_count;
    return true;
}

/* A region's own vertex and index spans, bounds-checked against the blob totals. */
static bool atlas_view_region_spans(const atlas_view_t *v, uint32_t r, const NtAtlasVertex **out_verts, const uint16_t **out_indices) {
    if (r >= v->region_count) {
        return false;
    }
    const NtAtlasRegion *reg = &v->regions[r];
    if ((uint64_t)reg->vertex_start + reg->vertex_count > v->vertex_total) {
        return false;
    }
    if ((uint64_t)reg->index_start + reg->index_count > v->index_total) {
        return false;
    }
    *out_verts = &v->verts[reg->vertex_start];
    *out_indices = &v->indices[reg->index_start];
    return true;
}

typedef struct {
    uint8_t *bytes;
    size_t len;
} pack_file_t;

static void pack_file_load(const char *path, pack_file_t *out) {
    out->len = 0;
    out->bytes = read_bin_file(path, &out->len);
    TEST_ASSERT_NOT_NULL_MESSAGE(out->bytes, "read produced pack");
}

static void pack_file_free(pack_file_t *f) {
    free(f->bytes);
    f->bytes = NULL;
    f->len = 0;
}

/* --- F pack builders --- */

#define F_MAX_IMAGES 8

/* clang-format off */
static const uint8_t k_f_rotations[4] = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_ROT90, NT_ATLAS_XFORM_ROT180, NT_ATLAS_XFORM_ROT270};
static const uint8_t k_f_mirrors[4]   = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_FLIP_H, NT_ATLAS_XFORM_FLIP_V, NT_ATLAS_XFORM_ROT180};
/* clang-format on */

/* One mask for both the atlas and every sprite, so each sprite's effective mask
 * is exactly `mask` — a sprite inheriting instead would blur the mask gate. */
static bool build_f_pack(const char *path, const char *name, uint8_t mask, const uint8_t *transforms, uint32_t count, bool concave) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (ctx == NULL) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = mask;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, name, &opts);
    if (concave) {
        atlas_dedup_f_fixture_add_concave(atlas, transforms, count, mask, 0U);
    } else {
        atlas_dedup_f_fixture_add(atlas, transforms, count, mask, 0U);
    }
    const nt_build_result_t commit = nt_atlas_commit(atlas);
    const nt_build_result_t finish = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return commit == NT_BUILD_OK && finish == NT_BUILD_OK;
}

static void collect_f_regions(const char *path, const char *name, uint8_t mask, const uint8_t *transforms, uint32_t count, bool concave, nt_atlas_dedup_region_t *out) {
    TEST_ASSERT_TRUE_MESSAGE(build_f_pack(path, name, mask, transforms, count, concave), "F pack build failed");
    pack_file_t f;
    pack_file_load(path, &f);
    uint32_t got = 0;
    const bool ok = atlas_dedup_collect_regions(f.bytes, f.len, out, count, &got);
    pack_file_free(&f);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(count, got, "one region per image");
}

static void assert_transforms_pairwise_distinct(const nt_atlas_dedup_region_t *r, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t j = i + 1; j < count; ++j) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(r[i].transform, r[j].transform, "each folded copy must record its own relative transform");
        }
    }
}

static void assert_transforms_in_mask(const nt_atlas_dedup_region_t *r, uint32_t count, uint8_t mask) {
    for (uint32_t i = 0; i < count; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(r[i].transform < 8U, "transform value out of D4 range");
        TEST_ASSERT_TRUE_MESSAGE((mask & (uint8_t)(1U << r[i].transform)) != 0U, "region transform outside its own effective mask");
    }
}

void test_f_fixture_images_pairwise_distinct(void) { TEST_ASSERT_TRUE_MESSAGE(atlas_dedup_f_images_pairwise_distinct(), "the 8 D4 images of the F glyph must be pairwise distinct"); }

void test_f_fixture_dims_swap_on_diagonal(void) {
    uint8_t px[NT_ATLAS_F_MAX_PX];
    for (uint8_t t = 0; t < 8; ++t) {
        uint16_t w = 0;
        uint16_t h = 0;
        atlas_dedup_f_fill(t, px, &w, &h);
        uint32_t expect_w = 0;
        uint32_t expect_h = 0;
        d4_dims_after(t, (uint32_t)NT_ATLAS_F_W, (uint32_t)NT_ATLAS_F_H, &expect_w, &expect_h);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)expect_w, w, "image width must follow d4_dims_after");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)expect_h, h, "image height must follow d4_dims_after");
        /* Non-vacuity: the base box is 9x13, so a diagonal transform must swap. */
        const bool diagonal = (t & 4U) != 0U;
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(diagonal ? NT_ATLAS_F_H : NT_ATLAS_F_W, w, "diagonal transforms must swap the box dims");
    }
}

void test_f_fixture_packs(void) {
    const char *path = TMP_DIR "/dedup_f_rotations.ntpack";
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "start_pack failed");
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ROTATIONS;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "dedup_f", &opts);
    const uint8_t rotations[4] = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_ROT90, NT_ATLAS_XFORM_ROT180, NT_ATLAS_XFORM_ROT270};
    atlas_dedup_f_fixture_add(atlas, rotations, 4, NT_ATLAS_TRANSFORMS_ROTATIONS, 0U);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(atlas), "F fixture commit failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_builder_finish_pack(ctx), "F fixture finish_pack failed");
    nt_builder_free_pack(ctx);

    size_t len = 0;
    uint8_t *bytes = read_bin_file(path, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(bytes, "read produced pack");
    nt_atlas_dedup_region_t regions[8];
    uint32_t count = 0;
    bool ok = atlas_dedup_collect_regions(bytes, len, regions, 8, &count);
    free(bytes);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from F pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, count, "one region per rotation image");
}

/* --- DEDUP-04: folding is mask-gated and records a per-copy relative --- */

void test_four_rotations_fold_to_one_placement(void) {
    nt_atlas_dedup_region_t regions[F_MAX_IMAGES] = {0};
    collect_f_regions(TMP_DIR "/dedup_f_fold_rot.ntpack", "dedup_f_fold_rot", NT_ATLAS_TRANSFORMS_ROTATIONS, k_f_rotations, 4, false, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 4), "four rotated copies must share one placement");
    assert_transforms_pairwise_distinct(regions, 4);
    assert_transforms_in_mask(regions, 4, NT_ATLAS_TRANSFORMS_ROTATIONS);
}

/* The counter-case that proves the fold is mask-gated, not accidental. */
void test_identity_mask_never_folds_rotations(void) {
    nt_atlas_dedup_region_t regions[F_MAX_IMAGES] = {0};
    collect_f_regions(TMP_DIR "/dedup_f_identity.ntpack", "dedup_f_identity", NT_ATLAS_TRANSFORMS_IDENTITY, k_f_rotations, 4, false, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, atlas_dedup_distinct_placements(regions, 4), "an identity-only mask must give every rotation its own placement");
    for (uint32_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_IDENTITY, regions[i].transform, "an identity-only mask must emit identity only");
    }
}

/* Mirrors, not only rotations: a flip is the case where the texel mapping
 * (w-1-x) and the corner mapping (w-x) disagree. */
void test_mirrors_fold_under_flips_mask(void) {
    nt_atlas_dedup_region_t regions[F_MAX_IMAGES] = {0};
    collect_f_regions(TMP_DIR "/dedup_f_fold_flip.ntpack", "dedup_f_fold_flip", NT_ATLAS_TRANSFORMS_FLIPS, k_f_mirrors, 4, false, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 4), "four mirrored copies must share one placement");
    assert_transforms_pairwise_distinct(regions, 4);
    assert_transforms_in_mask(regions, 4, NT_ATLAS_TRANSFORMS_FLIPS);
}

/* --- DEDUP-04: each alias decodes to its own source pixels through the page --- */

/* u = a*lx + b*ly + c over the region's own y-up local space. */
typedef struct {
    double u[3];
    double v[3];
} local_to_uv_t;

/* Cramer's rule on three correspondences; false when they are collinear. */
static bool solve_plane(const double x[3], const double y[3], const double val[3], double out[3]) {
    const double det = ((x[1] - x[0]) * (y[2] - y[0])) - ((x[2] - x[0]) * (y[1] - y[0]));
    if (det < 0.5 && det > -0.5) {
        return false;
    }
    out[0] = (((val[1] - val[0]) * (y[2] - y[0])) - ((val[2] - val[0]) * (y[1] - y[0]))) / det;
    out[1] = (((x[1] - x[0]) * (val[2] - val[0])) - ((x[2] - x[0]) * (val[1] - val[0]))) / det;
    out[2] = val[0] - (out[0] * x[0]) - (out[1] * y[0]);
    return true;
}

/* Solve the region's own local->UV map from the first non-collinear triple. */
static bool solve_local_to_uv(const NtAtlasVertex *v, uint32_t n, local_to_uv_t *out) {
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = i + 1; j < n; ++j) {
            for (uint32_t k = j + 1; k < n; ++k) {
                const double x[3] = {(double)v[i].local_x, (double)v[j].local_x, (double)v[k].local_x};
                const double y[3] = {(double)v[i].local_y, (double)v[j].local_y, (double)v[k].local_y};
                const double us[3] = {(double)v[i].atlas_u, (double)v[j].atlas_u, (double)v[k].atlas_u};
                const double vs[3] = {(double)v[i].atlas_v, (double)v[j].atlas_v, (double)v[k].atlas_v};
                if (solve_plane(x, y, us, out->u) && solve_plane(x, y, vs, out->v)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static double eval_plane(const double p[3], double x, double y) { return (p[0] * x) + (p[1] * y) + p[2]; }

typedef struct {
    const uint8_t *page;
    uint32_t page_w;
    uint32_t page_h;
    local_to_uv_t map;
    uint32_t region;
    uint8_t transform;
} uv_probe_t;

/* Clamped index: a builder bug must not turn into an out-of-bounds read. */
static const uint8_t *page_texel_at(const uv_probe_t *probe, double fx, double fy) {
    uint32_t px = (fx < 0.0) ? 0U : (uint32_t)fx;
    uint32_t py = (fy < 0.0) ? 0U : (uint32_t)fy;
    px = (px < probe->page_w) ? px : probe->page_w - 1U;
    py = (py < probe->page_h) ? py : probe->page_h - 1U;
    return probe->page + ((((size_t)py * probe->page_w) + px) * 4U);
}

/* One opaque texel: map its trim-local corner-space centre through the region's
 * own local->UV map, decode to page pixels and compare RGB. */
static void assert_texel_round_trip(const uv_probe_t *probe, const uint8_t *src, uint32_t x, uint32_t y, uint32_t img_h) {
    const double lx = (double)x + 0.5;
    const double ly = (double)img_h - ((double)y + 0.5);
    const double fx = (eval_plane(probe->map.u, lx, ly) * (double)probe->page_w) / 65535.0;
    const double fy = (eval_plane(probe->map.v, lx, ly) * (double)probe->page_h) / 65535.0;
    TEST_ASSERT_TRUE_MESSAGE(fx >= 0.0 && fy >= 0.0 && fx < (double)probe->page_w && fy < (double)probe->page_h, "a decoded UV landed outside the page");
    const uint8_t *dst = page_texel_at(probe, fx, fy);
    if (memcmp(src, dst, 3) == 0) {
        return;
    }
    char msg[192];
    (void)snprintf(msg, sizeof(msg), "region %u (image t%u) texel (%u,%u) expected rgb %u,%u,%u but the page holds %u,%u,%u", probe->region, (unsigned)probe->transform, x, y, src[0], src[1], src[2],
                   dst[0], dst[1], dst[2]);
    TEST_FAIL_MESSAGE(msg);
}

/* Every opaque texel of ONE image must sample back its own colour through that
 * region's own local->UV map. The F colour encodes the source texel, so a wrong
 * relative, a missed dimension swap or an inverted direction all fail here. */
static void assert_region_samples_its_own_image(const pack_file_t *pack, const atlas_view_t *view, uint32_t r, uint8_t transform) {
    const NtAtlasRegion *reg = &view->regions[r];
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, reg->vertex_count, "a RECT sprite must emit a 4-vertex quad");
    const NtAtlasVertex *verts = NULL;
    const uint16_t *idx = NULL;
    TEST_ASSERT_TRUE_MESSAGE(atlas_view_region_spans(view, r, &verts, &idx), "region spans outside the blob");
    uv_probe_t probe = {.page = NULL, .page_w = 0, .page_h = 0, .map = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, .region = r, .transform = transform};
    TEST_ASSERT_TRUE_MESSAGE(solve_local_to_uv(verts, reg->vertex_count, &probe.map), "the region's local->UV map is degenerate");
    TEST_ASSERT_TRUE_MESSAGE(atlas_dedup_read_page_rgba(pack->bytes, pack->len, reg->page_index, &probe.page, &probe.page_w, &probe.page_h), "read the atlas page pixels");

    uint8_t img[NT_ATLAS_F_MAX_PX];
    uint16_t iw = 0;
    uint16_t ih = 0;
    atlas_dedup_f_fill(transform, img, &iw, &ih);
    uint32_t checked = 0;
    for (uint32_t y = 0; y < (uint32_t)ih; ++y) {
        for (uint32_t x = 0; x < (uint32_t)iw; ++x) {
            const uint8_t *src = img + ((((size_t)y * iw) + x) * 4U);
            if (src[3] != 255U) {
                continue;
            }
            assert_texel_round_trip(&probe, src, x, y, (uint32_t)ih);
            ++checked;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, checked, "no opaque texel was sampled");
}

void test_alias_uv_decode_samples_its_own_source_pixel(void) {
    const char *path = TMP_DIR "/dedup_f_uv_decode.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_f_pack(path, "dedup_f_uv", NT_ATLAS_TRANSFORMS_ROTATIONS, k_f_rotations, 4, false), "UV-decode pack build failed");
    pack_file_t pack;
    pack_file_load(path, &pack);
    atlas_view_t view;
    const bool ok = atlas_view_open(pack.bytes, pack.len, &view);
    if (!ok) {
        pack_file_free(&pack);
        TEST_FAIL_MESSAGE("open the produced atlas blob");
        return;
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, view.region_count, "one region per rotation image");
    for (uint32_t r = 0; r < 4; ++r) {
        assert_region_samples_its_own_image(&pack, &view, r, k_f_rotations[r]);
    }
    pack_file_free(&pack);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_f_fixture_images_pairwise_distinct);
    RUN_TEST(test_f_fixture_dims_swap_on_diagonal);
    RUN_TEST(test_f_fixture_packs);
    RUN_TEST(test_four_rotations_fold_to_one_placement);
    RUN_TEST(test_identity_mask_never_folds_rotations);
    RUN_TEST(test_mirrors_fold_under_flips_mask);
    RUN_TEST(test_alias_uv_decode_samples_its_own_source_pixel);
    return UNITY_END();
}
