/* D4 probe fixture contract: the 'F' images must be pairwise distinct and must
 * carry the dimension swap, or every later relative-transform assertion could
 * pass by symmetry instead of by correctness. */

#include <setjmp.h>
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
#include "nt_builder_atlas_test.h"
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

/* Region i carries d4_compose(placement, rel_i) and image 0 is the root (rel 0), so
 * the quotient against region 0 isolates rel_i whatever orientation the packer gave
 * the shared rectangle. Image i is images[i](F) and the root is F, hence
 * rel_i == d4_inverse(images[i]) exactly. Distinctness alone would pass with flip_h
 * and flip_v swapped anywhere in the search or the emit. */
static void assert_relatives_match_images(const nt_atlas_dedup_region_t *r, const uint8_t *images, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t rel = d4_compose(d4_inverse(r[0].transform), r[i].transform);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(d4_inverse(images[i]), rel, "alias must record the exact relative its image implies");
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
    assert_relatives_match_images(regions, k_f_rotations, 4);
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
    assert_relatives_match_images(regions, k_f_mirrors, 4);
    assert_transforms_in_mask(regions, 4, NT_ATLAS_TRANSFORMS_FLIPS);
}

/* Admission is directional: the fold is gated by the mask of the sprite that
 * joins, never by the root's. Both members carry an explicit mask, so the atlas
 * mask below cannot stand in for either. */
static void collect_masked_pair(const char *path, const char *name, uint8_t root_mask, uint8_t alias_mask, nt_atlas_dedup_region_t *out) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "start_pack failed");
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_IDENTITY;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, name, &opts);
    const uint8_t transforms[2] = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_ROT90};
    const uint8_t masks[2] = {root_mask, alias_mask};
    atlas_dedup_f_fixture_add_masked(atlas, transforms, masks, 2);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(atlas), "masked-pair commit failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_builder_finish_pack(ctx), "masked-pair finish_pack failed");
    nt_builder_free_pack(ctx);
    pack_file_t f;
    pack_file_load(path, &f);
    uint32_t got = 0;
    const bool ok = atlas_dedup_collect_regions(f.bytes, f.len, out, 2, &got);
    pack_file_free(&f);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, got, "one region per image");
}

void test_fold_admission_follows_the_alias_mask(void) {
    /* The rotated copy is added second and its own mask admits the rotation, so
     * an identity-only root is still a legal target — the root is stored at
     * identity either way. */
    nt_atlas_dedup_region_t open_alias[2] = {0};
    collect_masked_pair(TMP_DIR "/dedup_f_mask_alias_open.ntpack", "dedup_f_mask_open", NT_ATLAS_TRANSFORMS_IDENTITY, NT_ATLAS_TRANSFORMS_ALL, open_alias);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(open_alias, 2), "an alias whose own mask admits the rotation must fold onto an identity-only root");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_IDENTITY, open_alias[0].transform, "the root is stored at identity");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(NT_ATLAS_XFORM_IDENTITY, open_alias[1].transform, "the rotated copy must record a non-identity relative");
    assert_transforms_in_mask(&open_alias[1], 1, NT_ATLAS_TRANSFORMS_ALL);

    /* Swapped masks, same pair of images: now the joining sprite may only be
     * stored at identity, and its pixels do not match there. */
    nt_atlas_dedup_region_t closed_alias[2] = {0};
    collect_masked_pair(TMP_DIR "/dedup_f_mask_alias_closed.ntpack", "dedup_f_mask_closed", NT_ATLAS_TRANSFORMS_ALL, NT_ATLAS_TRANSFORMS_IDENTITY, closed_alias);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, atlas_dedup_distinct_placements(closed_alias, 2), "an identity-only alias must not fold onto a rotated root");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_IDENTITY, closed_alias[1].transform, "an identity-only sprite must be stored at identity");
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
 * region's own local->UV map. The colour encodes the source texel, so a wrong
 * relative, a missed dimension swap or an inverted direction all fail here. */
static void assert_region_samples_image(const pack_file_t *pack, const atlas_view_t *view, uint32_t r, uint8_t label, const uint8_t *img, uint32_t iw, uint32_t ih) {
    const NtAtlasRegion *reg = &view->regions[r];
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, reg->vertex_count, "a RECT sprite must emit a 4-vertex quad");
    const NtAtlasVertex *verts = NULL;
    const uint16_t *idx = NULL;
    TEST_ASSERT_TRUE_MESSAGE(atlas_view_region_spans(view, r, &verts, &idx), "region spans outside the blob");
    uv_probe_t probe = {.page = NULL, .page_w = 0, .page_h = 0, .map = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}}, .region = r, .transform = label};
    TEST_ASSERT_TRUE_MESSAGE(solve_local_to_uv(verts, reg->vertex_count, &probe.map), "the region's local->UV map is degenerate");
    TEST_ASSERT_TRUE_MESSAGE(atlas_dedup_read_page_rgba(pack->bytes, pack->len, reg->page_index, &probe.page, &probe.page_w, &probe.page_h), "read the atlas page pixels");

    uint32_t checked = 0;
    for (uint32_t y = 0; y < ih; ++y) {
        for (uint32_t x = 0; x < iw; ++x) {
            const uint8_t *src = img + ((((size_t)y * iw) + x) * 4U);
            if (src[3] != 255U) {
                continue;
            }
            assert_texel_round_trip(&probe, src, x, y, ih);
            ++checked;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, checked, "no opaque texel was sampled");
}

static void assert_region_samples_its_own_image(const pack_file_t *pack, const atlas_view_t *view, uint32_t r, uint8_t transform) {
    uint8_t img[NT_ATLAS_F_MAX_PX];
    uint16_t iw = 0;
    uint16_t ih = 0;
    atlas_dedup_f_fill(transform, img, &iw, &ih);
    assert_region_samples_image(pack, view, r, transform, img, (uint32_t)iw, (uint32_t)ih);
}

static void assert_uv_decode_for_images(const char *path, const char *name, uint8_t mask, const uint8_t *images) {
    TEST_ASSERT_TRUE_MESSAGE(build_f_pack(path, name, mask, images, 4, false), "UV-decode pack build failed");
    pack_file_t pack;
    pack_file_load(path, &pack);
    atlas_view_t view;
    const bool ok = atlas_view_open(pack.bytes, pack.len, &view);
    if (!ok) {
        pack_file_free(&pack);
        TEST_FAIL_MESSAGE("open the produced atlas blob");
        return;
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, view.region_count, "one region per image");
    /* Non-vacuity: 4 unfolded regions would each trivially sample their own
     * standalone pixels, passing this oracle without any D4 fold happening. */
    nt_atlas_dedup_region_t folded[4] = {0};
    uint32_t folded_count = 0;
    TEST_ASSERT_TRUE_MESSAGE(atlas_dedup_collect_regions(pack.bytes, pack.len, folded, 4, &folded_count), "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(folded, 4), "the images must fold before UV decode proves anything");
    for (uint32_t r = 0; r < 4; ++r) {
        assert_region_samples_its_own_image(&pack, &view, r, images[r]);
    }
    pack_file_free(&pack);
}

void test_alias_uv_decode_samples_its_own_source_pixel(void) { assert_uv_decode_for_images(TMP_DIR "/dedup_f_uv_decode.ntpack", "dedup_f_uv", NT_ATLAS_TRANSFORMS_ROTATIONS, k_f_rotations); }

/* Mirrors are where the texel mapping (w-1-x) and the corner mapping (w-x) diverge,
 * so the one oracle that reads real texels has to run on them too. */
void test_mirror_alias_uv_decode_samples_its_own_source_pixel(void) {
    assert_uv_decode_for_images(TMP_DIR "/dedup_f_uv_decode_flip.ntpack", "dedup_f_uv_flip", NT_ATLAS_TRANSFORMS_FLIPS, k_f_mirrors);
}

/* The UV write quantizes with +0.5 truncation, so the round decode is exact
 * for any page dimension up to 65535. */
static uint32_t uv_to_page_px(uint16_t uv, uint32_t page_dim) { return (uint32_t)((((double)uv * (double)page_dim) / 65535.0) + 0.5); }

/* Each ring texel must equal the placed edge texel it extrudes (corners skipped —
 * they belong to the corner band, whose fill rule differs per axis order). */
static void assert_ring_replicates_edges(const uint8_t *page, uint32_t pw, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1, uint32_t extrude) {
    for (uint32_t e = 1; e <= extrude; ++e) {
        for (uint32_t x = x0; x < x1; ++x) {
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(page + ((((size_t)y0 * pw) + x) * 4U), page + ((((size_t)(y0 - e) * pw) + x) * 4U), 4, "top ring must replicate the top edge");
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(page + ((((size_t)(y1 - 1U) * pw) + x) * 4U), page + ((((size_t)(y1 - 1U + e) * pw) + x) * 4U), 4, "bottom ring must replicate the bottom edge");
        }
        for (uint32_t y = y0; y < y1; ++y) {
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(page + ((((size_t)y * pw) + x0) * 4U), page + ((((size_t)y * pw) + (x0 - e)) * 4U), 4, "left ring must replicate the left edge");
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(page + ((((size_t)y * pw) + (x1 - 1U)) * 4U), page + ((((size_t)y * pw) + (x1 - 1U + e)) * 4U), 4, "right ring must replicate the right edge");
        }
    }
}

/* The interior-texel oracle above never reads the extruded band; only this test
 * would catch a ring written from the joining sprite's untransformed edge. */
void test_extruded_ring_replicates_the_shared_placement_edge(void) {
    enum { RING_EXTRUDE = 2 };
    const char *path = TMP_DIR "/dedup_f_extrude.ntpack";
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "start_pack failed");
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ROTATIONS;
    opts.shape = NT_ATLAS_SHAPE_RECT; /* extrude > 0 requires the RECT packing mode */
    opts.extrude = RING_EXTRUDE;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "dedup_f_extrude", &opts);
    const uint8_t pair[2] = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_ROT90};
    atlas_dedup_f_fixture_add(atlas, pair, 2, NT_ATLAS_TRANSFORMS_ROTATIONS, 0U);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(atlas), "extrude pack commit failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_builder_finish_pack(ctx), "extrude pack finish failed");
    nt_builder_free_pack(ctx);

    pack_file_t pack;
    pack_file_load(path, &pack);
    nt_atlas_dedup_region_t regions[2] = {0};
    uint32_t count = 0;
    TEST_ASSERT_TRUE_MESSAGE(atlas_dedup_collect_regions(pack.bytes, pack.len, regions, 2, &count), "collect regions from extrude pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, count, "one region per sprite");
    /* Non-vacuity: without the fold there is no SHARED ring to test. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 2), "the rotated pair must fold");

    const uint8_t *page = NULL;
    uint32_t pw = 0;
    uint32_t ph = 0;
    TEST_ASSERT_TRUE_MESSAGE(atlas_dedup_read_page_rgba(pack.bytes, pack.len, regions[0].page_index, &page, &pw, &ph), "read the atlas page pixels");
    const uint32_t x0 = uv_to_page_px(regions[0].u_min, pw);
    const uint32_t x1 = uv_to_page_px(regions[0].u_max, pw);
    const uint32_t y0 = uv_to_page_px(regions[0].v_min, ph);
    const uint32_t y1 = uv_to_page_px(regions[0].v_max, ph);
    TEST_ASSERT_TRUE_MESSAGE(x1 > x0 && y1 > y0, "degenerate placement box");
    TEST_ASSERT_TRUE_MESSAGE(x0 >= RING_EXTRUDE && y0 >= RING_EXTRUDE && x1 + RING_EXTRUDE <= pw && y1 + RING_EXTRUDE <= ph, "the extrude band must fit the page");
    assert_ring_replicates_edges(page, pw, x0, x1, y0, y1, (uint32_t)RING_EXTRUDE);
    pack_file_free(&pack);
}

/* One equal-content run may hold two groups: a member whose own mask forbids the
 * relative it would need becomes a second root, and later members fold onto the
 * FIRST eligible root in add order — even though the second root here matches the
 * third image at identity. Fold targets are roots only, tried in add order. */
void test_mixed_mask_run_keeps_two_roots_and_folds_onto_the_first(void) {
    const char *path = TMP_DIR "/dedup_f_two_roots.ntpack";
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "start_pack failed");
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_IDENTITY;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "dedup_f_two_roots", &opts);
    const uint8_t transforms[3] = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_ROT90, NT_ATLAS_XFORM_ROT90};
    const uint8_t masks[3] = {NT_ATLAS_TRANSFORMS_ALL, NT_ATLAS_TRANSFORMS_IDENTITY, NT_ATLAS_TRANSFORMS_ALL};
    /* Not atlas_dedup_f_fixture_add_masked: it derives names from the transform
     * value, and this run deliberately repeats ROT90. */
    uint8_t fpx[NT_ATLAS_F_MAX_PX];
    for (uint32_t i = 0; i < 3; ++i) {
        uint16_t w = 0;
        uint16_t h = 0;
        atlas_dedup_f_fill(transforms[i], fpx, &w, &h);
        char name[16];
        (void)snprintf(name, sizeof(name), "two_roots_%u", (unsigned)i);
        nt_atlas_sprite_opts_t so = nt_atlas_sprite_opts_defaults();
        so.name = name;
        so.shape = NT_ATLAS_SPRITE_SHAPE_RECT;
        so.allowed_transforms = masks[i];
        nt_atlas_add_raw(atlas, fpx, w, h, &so);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(atlas), "two-roots commit failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_builder_finish_pack(ctx), "two-roots finish_pack failed");
    nt_builder_free_pack(ctx);

    pack_file_t f;
    pack_file_load(path, &f);
    nt_atlas_dedup_region_t regions[3] = {0};
    uint32_t got = 0;
    const bool ok = atlas_dedup_collect_regions(f.bytes, f.len, regions, 3, &got);
    pack_file_free(&f);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, got, "one region per image");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, atlas_dedup_distinct_placements(regions, 3), "the identity-masked rotation must stay a second root");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[0].page_index, regions[2].page_index, "the open-masked copy must fold onto the first root");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[0].u_min, regions[2].u_min, "the open-masked copy must fold onto the first root");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[0].v_min, regions[2].v_min, "the open-masked copy must fold onto the first root");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_IDENTITY, regions[1].transform, "an identity-only second root is stored at identity");
    /* Quotient, not the raw transform — the packer may orient the shared rectangle. */
    const uint8_t rel = d4_compose(d4_inverse(regions[0].transform), regions[2].transform);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(NT_ATLAS_XFORM_IDENTITY, rel, "the fold must go through a non-identity relative");
}

/* --- composed transform: non-identity placement x non-identity relative --- */

#define AO_W 44
#define AO_H 8
#define AO_TWIN_A 2
#define AO_TWIN_B 3

/* Colour encodes the source texel, so the art is asymmetric under every D4
 * element except the horizontal mirror that relates the two twins. */
static void ao_fill(uint8_t *px, bool mirrored) {
    for (uint32_t y = 0; y < (uint32_t)AO_H; ++y) {
        for (uint32_t x = 0; x < (uint32_t)AO_W; ++x) {
            const uint32_t sx = mirrored ? ((uint32_t)AO_W - 1U - x) : x;
            uint8_t *p = px + ((((size_t)y * AO_W) + x) * 4U);
            p[0] = (uint8_t)(sx * 5U);
            p[1] = (uint8_t)(y * 30U);
            p[2] = 200U;
            p[3] = 255U;
        }
    }
}

/* Every dedup etalon packs its group at an identity placement, where
 * d4_compose(placement, rel) == d4_compose(rel, placement) == rel — a swapped
 * composition in the emit stage is invisible there. This layout (proven in the
 * twin-mask test) transposes the shared rectangle AND the pair folds through a
 * mirror, so the composed transform differs from both factors and the UV decode
 * is the oracle that catches a swapped argument order. */
void test_transposed_placement_composes_with_the_relative(void) {
    const char *path = TMP_DIR "/dedup_ao_compose.ntpack";
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "start_pack failed");
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ALL;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "dedup_ao_compose", &opts);

    uint8_t px[(size_t)AO_W * AO_H * 4U];
    const uint16_t filler_dims[3][2] = {{8, 44}, {8, 44}, {8, 40}};
    const char *filler_names[3] = {"ao_w0", "ao_w1", "ao_w3"};
    for (int i = 0; i < 2; ++i) {
        const uint32_t texels = (uint32_t)filler_dims[i][0] * filler_dims[i][1];
        for (uint32_t t = 0; t < texels; ++t) {
            px[(t * 4U) + 0U] = (uint8_t)(60 + (i * 30));
            px[(t * 4U) + 1U] = 90U;
            px[(t * 4U) + 2U] = 60U;
            px[(t * 4U) + 3U] = 255U;
        }
        nt_atlas_sprite_opts_t so = nt_atlas_sprite_opts_defaults();
        so.name = filler_names[i];
        nt_atlas_add_raw(atlas, px, filler_dims[i][0], filler_dims[i][1], &so);
    }
    for (int i = 0; i < 2; ++i) {
        ao_fill(px, i == 1);
        nt_atlas_sprite_opts_t so = nt_atlas_sprite_opts_defaults();
        so.name = (i == 0) ? "ao_twin_a" : "ao_twin_b";
        nt_atlas_add_raw(atlas, px, AO_W, AO_H, &so);
    }
    {
        const uint32_t texels = (uint32_t)filler_dims[2][0] * filler_dims[2][1];
        for (uint32_t t = 0; t < texels; ++t) {
            px[(t * 4U) + 0U] = 120U;
            px[(t * 4U) + 1U] = 90U;
            px[(t * 4U) + 2U] = 60U;
            px[(t * 4U) + 3U] = 255U;
        }
        nt_atlas_sprite_opts_t so = nt_atlas_sprite_opts_defaults();
        so.name = filler_names[2];
        nt_atlas_add_raw(atlas, px, filler_dims[2][0], filler_dims[2][1], &so);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(atlas), "compose fixture commit failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_builder_finish_pack(ctx), "compose fixture finish_pack failed");
    nt_builder_free_pack(ctx);

    pack_file_t pack;
    pack_file_load(path, &pack);
    nt_atlas_dedup_region_t regions[5] = {0};
    uint32_t got = 0;
    TEST_ASSERT_TRUE_MESSAGE(atlas_dedup_collect_regions(pack.bytes, pack.len, regions, 5, &got), "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, got, "one region per sprite");

    /* Non-vacuity in both factors: the pair folds, the placement is transposed,
     * and the relative is engaged (the twins carry different transforms). */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[AO_TWIN_A].page_index, regions[AO_TWIN_B].page_index, "the mirrored twins must share one placement");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[AO_TWIN_A].u_min, regions[AO_TWIN_B].u_min, "the mirrored twins must share one placement");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[AO_TWIN_A].v_min, regions[AO_TWIN_B].v_min, "the mirrored twins must share one placement");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(NT_ATLAS_XFORM_IDENTITY, regions[AO_TWIN_A].transform, "control: the packer must transpose the folded pair");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(regions[AO_TWIN_A].transform, regions[AO_TWIN_B].transform, "the mirror relative must reach the stored transform");

    atlas_view_t view;
    const bool ok = atlas_view_open(pack.bytes, pack.len, &view);
    if (!ok) {
        pack_file_free(&pack);
        TEST_FAIL_MESSAGE("open the produced atlas blob");
        return;
    }
    uint8_t img[(size_t)AO_W * AO_H * 4U];
    ao_fill(img, false);
    assert_region_samples_image(&pack, &view, AO_TWIN_A, regions[AO_TWIN_A].transform, img, AO_W, AO_H);
    ao_fill(img, true);
    assert_region_samples_image(&pack, &view, AO_TWIN_B, regions[AO_TWIN_B].transform, img, AO_W, AO_H);
    pack_file_free(&pack);
}

/* --- DEDUP-04: an alias carries the same geometry as a standalone pack --- */

#define FILLER_W 20
#define FILLER_H 12

/* An unrelated sprite so a standalone pack is a real multi-sprite pack. Its
 * colour is outside the F's encoded range, so it can never join the group. */
static void add_filler(NtAtlasBuild *atlas) {
    uint8_t px[FILLER_W * FILLER_H * 4];
    for (size_t i = 0; i < (size_t)FILLER_W * FILLER_H; ++i) {
        px[(i * 4U) + 0U] = 10;
        px[(i * 4U) + 1U] = 240;
        px[(i * 4U) + 2U] = 90;
        px[(i * 4U) + 3U] = 255;
    }
    nt_atlas_sprite_opts_t o = nt_atlas_sprite_opts_defaults();
    o.name = "filler";
    nt_atlas_add_raw(atlas, px, (uint16_t)FILLER_W, (uint16_t)FILLER_H, &o);
}

static bool build_f_standalone_pack(const char *path, const char *name, uint8_t mask, uint8_t transform, bool concave) {
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
    const uint8_t one[1] = {transform};
    if (concave) {
        atlas_dedup_f_fixture_add_concave(atlas, one, 1, mask, 0U);
    } else {
        atlas_dedup_f_fixture_add(atlas, one, 1, mask, 0U);
    }
    add_filler(atlas);
    const nt_build_result_t commit = nt_atlas_commit(atlas);
    const nt_build_result_t finish = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return commit == NT_BUILD_OK && finish == NT_BUILD_OK;
}

/* Twice the signed ring area. local_y is y-up, so a builder PNG-CCW ring reads
 * CW here — the magnitude is what the triangulation must reproduce. */
static int64_t ring_area2(const NtAtlasVertex *v, uint32_t n) {
    int64_t a2 = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const NtAtlasVertex *p = &v[i];
        const NtAtlasVertex *q = &v[(i + 1U) % n];
        a2 += ((int64_t)p->local_x * q->local_y) - ((int64_t)q->local_x * p->local_y);
    }
    return a2 < 0 ? -a2 : a2;
}

static int64_t tri_area2(const NtAtlasVertex *a, const NtAtlasVertex *b, const NtAtlasVertex *c) {
    return (((int64_t)b->local_x - a->local_x) * ((int64_t)c->local_y - a->local_y)) - (((int64_t)c->local_x - a->local_x) * ((int64_t)b->local_y - a->local_y));
}

/* The polygon as a ring, ignoring which vertex the builder happened to start at.
 * A hull inherited from the root would carry the root's coordinates and match at
 * no rotation. */
static bool rings_match_up_to_rotation(const NtAtlasVertex *a, const NtAtlasVertex *b, uint32_t n) {
    for (uint32_t s = 0; s < n; ++s) {
        bool same = true;
        for (uint32_t i = 0; i < n && same; ++i) {
            const NtAtlasVertex *bv = &b[(i + s) % n];
            same = (a[i].local_x == bv->local_x) && (a[i].local_y == bv->local_y);
        }
        if (same) {
            return true;
        }
    }
    return false;
}

/* Every emitted ring is rotated to its lexicographically smallest vertex, so an alias
 * and a standalone pack of the same image agree at rotation zero — not merely up to
 * one. This is what makes the index block, and therefore the render flags, agree. */
static bool rings_match_exactly(const NtAtlasVertex *a, const NtAtlasVertex *b, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        if (a[i].local_x != b[i].local_x || a[i].local_y != b[i].local_y) {
            return false;
        }
    }
    return true;
}

/* Blob triangles read world-CCW and must tile this region's own ring exactly —
 * an index block inherited from a differently-oriented root would not. */
static void assert_indices_tile_ring(const NtAtlasVertex *v, uint32_t n, const uint16_t *idx, uint32_t idx_count, const char *what) {
    TEST_ASSERT_TRUE_MESSAGE(idx_count % 3U == 0U, "index_count must be a multiple of 3");
    int64_t sum = 0;
    for (uint32_t t = 0; t + 2U < idx_count; t += 3U) {
        TEST_ASSERT_TRUE_MESSAGE(idx[t] < n && idx[t + 1U] < n && idx[t + 2U] < n, "triangle index outside the vertex ring");
        const int64_t a2 = tri_area2(&v[idx[t]], &v[idx[t + 1U]], &v[idx[t + 2U]]);
        TEST_ASSERT_TRUE_MESSAGE(a2 > 0, what);
        sum += a2;
    }
    TEST_ASSERT_EQUAL_INT64_MESSAGE(ring_area2(v, n), sum, "the triangles must tile the region's own polygon exactly");
}

static void assert_quad_flag_matches_counts(const NtAtlasRegion *reg) {
    if ((reg->flags & NT_ATLAS_REGION_FLAG_QUAD_MASK) == 0U) {
        return;
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, reg->vertex_count, "a QUAD_* flag implies vertex_count == 4");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(6, reg->index_count, "a QUAD_* flag implies index_count == 6");
}

/* An aliased region against the same image packed standalone. vertex_start,
 * index_start, the UVs and transform may differ; nothing else may. */
static void assert_region_pair_equivalent(const atlas_view_t *av, uint32_t ar, const atlas_view_t *bv, uint32_t br, const char *what) {
    const NtAtlasRegion *a = &av->regions[ar];
    const NtAtlasRegion *b = &bv->regions[br];
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(b->vertex_count, a->vertex_count, what);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(b->index_count, a->index_count, what);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(b->flags, a->flags, what);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(b->source_w, a->source_w, what);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(b->source_h, a->source_h, what);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(b->trim_offset_x, a->trim_offset_x, what);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(b->trim_offset_y, a->trim_offset_y, what);
    assert_quad_flag_matches_counts(a);
    assert_quad_flag_matches_counts(b);

    const NtAtlasVertex *avx = NULL;
    const uint16_t *aidx = NULL;
    const NtAtlasVertex *bvx = NULL;
    const uint16_t *bidx = NULL;
    TEST_ASSERT_TRUE_MESSAGE(atlas_view_region_spans(av, ar, &avx, &aidx), "alias region spans outside the blob");
    TEST_ASSERT_TRUE_MESSAGE(atlas_view_region_spans(bv, br, &bvx, &bidx), "standalone region spans outside the blob");
    TEST_ASSERT_TRUE_MESSAGE(rings_match_up_to_rotation(avx, bvx, a->vertex_count), what);
    TEST_ASSERT_TRUE_MESSAGE(rings_match_exactly(avx, bvx, a->vertex_count), "canonical rotation must make the two rings identical, not merely congruent");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(bidx, aidx, (size_t)a->index_count * sizeof(uint16_t), "identical rings must triangulate identically");
    assert_indices_tile_ring(avx, a->vertex_count, aidx, a->index_count, "alias triangles must be world-CCW");
    assert_indices_tile_ring(bvx, b->vertex_count, bidx, b->index_count, "standalone triangles must be world-CCW");
}

static void assert_folded_matches_standalone(bool concave, const char *tag, uint8_t mask, const uint8_t *images) {
    char apath[256];
    (void)snprintf(apath, sizeof(apath), "%s/dedup_f_std_%s.ntpack", TMP_DIR, tag);
    TEST_ASSERT_TRUE_MESSAGE(build_f_pack(apath, "dedup_f_std", mask, images, 4, concave), "folded pack build failed");
    pack_file_t a;
    pack_file_load(apath, &a);
    atlas_view_t av;
    TEST_ASSERT_TRUE_MESSAGE(atlas_view_open(a.bytes, a.len, &av), "open the folded atlas blob");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, av.region_count, "one region per image");
    /* Non-vacuity: unfolded regions match their standalone packs by construction,
     * so this oracle proves nothing unless the fold actually happened. */
    nt_atlas_dedup_region_t folded[4] = {0};
    uint32_t folded_count = 0;
    TEST_ASSERT_TRUE_MESSAGE(atlas_dedup_collect_regions(a.bytes, a.len, folded, 4, &folded_count), "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(folded, 4), "the images must fold before alias geometry proves anything");

    for (uint32_t r = 0; r < 4; ++r) {
        char bpath[256];
        (void)snprintf(bpath, sizeof(bpath), "%s/dedup_f_alone_%s_%u.ntpack", TMP_DIR, tag, r);
        TEST_ASSERT_TRUE_MESSAGE(build_f_standalone_pack(bpath, "dedup_f_alone", mask, images[r], concave), "standalone pack build failed");
        pack_file_t b;
        pack_file_load(bpath, &b);
        atlas_view_t bv;
        TEST_ASSERT_TRUE_MESSAGE(atlas_view_open(b.bytes, b.len, &bv), "open the standalone atlas blob");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, bv.region_count, "standalone pack holds the image plus the filler");
        assert_region_pair_equivalent(&av, r, &bv, 0, "an alias must carry the geometry of the same image packed standalone");
        pack_file_free(&b);
    }
    pack_file_free(&a);
}

void test_alias_region_matches_standalone_pack(void) { assert_folded_matches_standalone(false, "rect", NT_ATLAS_TRANSFORMS_ROTATIONS, k_f_rotations); }

/* The derived concave hull, not only the RECT quad. */
void test_concave_alias_hull_matches_standalone(void) { assert_folded_matches_standalone(true, "concave", NT_ATLAS_TRANSFORMS_ROTATIONS, k_f_rotations); }

/* All four rotations are even-parity, so only a mirror exercises polygon_transform's
 * ring reversal. The flag comparison inside assert_region_pair_equivalent is weak on
 * this fixture — a RECT F carries no QUAD_* bit even standalone — but the vertex ring,
 * the winding and the triangle tiling all bite here. */
void test_mirrored_alias_region_matches_standalone_pack(void) { assert_folded_matches_standalone(false, "mirror_rect", NT_ATLAS_TRANSFORMS_FLIPS, k_f_mirrors); }

void test_mirrored_concave_alias_hull_matches_standalone(void) { assert_folded_matches_standalone(true, "mirror_concave", NT_ATLAS_TRANSFORMS_FLIPS, k_f_mirrors); }

void test_mirrored_alias_winding_is_world_ccw(void) {
    const char *path = TMP_DIR "/dedup_f_winding.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_f_pack(path, "dedup_f_winding", NT_ATLAS_TRANSFORMS_FLIPS, k_f_mirrors, 4, true), "mirror pack build failed");
    pack_file_t pack;
    pack_file_load(path, &pack);
    atlas_view_t view;
    TEST_ASSERT_TRUE_MESSAGE(atlas_view_open(pack.bytes, pack.len, &view), "open the produced atlas blob");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, view.region_count, "one region per mirror image");
    for (uint32_t r = 0; r < 4; ++r) {
        const NtAtlasVertex *v = NULL;
        const uint16_t *idx = NULL;
        TEST_ASSERT_TRUE_MESSAGE(atlas_view_region_spans(&view, r, &v, &idx), "region spans outside the blob");
        assert_indices_tile_ring(v, view.regions[r].vertex_count, idx, view.regions[r].index_count, "a mirrored alias triangle is not world-CCW");
        assert_quad_flag_matches_counts(&view.regions[r]);
    }
    pack_file_free(&pack);
}

/* --- A wrong relative transform is rejected, not shipped --- */

/* The forced-relative case longjmps out of the middle of nt_atlas_commit, so the
 * pipeline's allocations are abandoned on purpose. LSan (CI Debug only) must not
 * read that as a builder leak. */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/lsan_interface.h>
#define NT_TEST_LSAN_DISABLE() __lsan_disable()
#define NT_TEST_LSAN_ENABLE() __lsan_enable()
#endif
#elif defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#define NT_TEST_LSAN_DISABLE() __lsan_disable()
#define NT_TEST_LSAN_ENABLE() __lsan_enable()
#endif
#ifndef NT_TEST_LSAN_DISABLE
#define NT_TEST_LSAN_DISABLE() ((void)0)
#define NT_TEST_LSAN_ENABLE() ((void)0)
#endif

static jmp_buf s_build_assert_jmp;
static const char *s_build_assert_expr;

static void trap_build_assert(const char *expr, const char *file, int line) {
    s_build_assert_expr = expr;
    (void)file;
    (void)line;
    longjmp(s_build_assert_jmp, 1);
}

/* Byte-identical twins under distinct names: the group's true relative is
 * identity, so any forced non-identity relative is provably wrong. Concave shape
 * keeps the hull orientation-sensitive — the D4 image of a box is a box. */
static void add_f_twins(NtAtlasBuild *atlas) {
    uint8_t px[NT_ATLAS_F_MAX_PX];
    uint16_t w = 0;
    uint16_t h = 0;
    atlas_dedup_f_fill(NT_ATLAS_XFORM_IDENTITY, px, &w, &h);
    static const char *names[2] = {"twin_root", "twin_alias"};
    for (uint32_t i = 0; i < 2; ++i) {
        nt_atlas_sprite_opts_t o = nt_atlas_sprite_opts_defaults();
        o.name = names[i];
        nt_atlas_add_raw(atlas, px, w, h, &o);
    }
}

static NtBuilderContext *begin_twin_pack(const char *path, NtAtlasBuild **out_atlas) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "start_pack failed");
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "dedup_f_twins", &opts);
    add_f_twins(atlas);
    *out_atlas = atlas;
    return ctx;
}

/* Build the twin pack and require both regions on one identity placement. */
static void assert_twins_fold_at_identity(const char *path, const char *what) {
    NtAtlasBuild *atlas = NULL;
    NtBuilderContext *ctx = begin_twin_pack(path, &atlas);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(atlas), what);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_builder_finish_pack(ctx), what);
    nt_builder_free_pack(ctx);
    pack_file_t f;
    pack_file_load(path, &f);
    nt_atlas_dedup_region_t regions[2] = {0};
    uint32_t count = 0;
    const bool ok = atlas_dedup_collect_regions(f.bytes, f.len, regions, 2, &count);
    pack_file_free(&f);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, count, "one region per twin");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 2), "byte-identical twins must share one placement");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_IDENTITY, regions[0].transform, what);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_IDENTITY, regions[1].transform, what);
}

void test_wrong_relative_transform_is_rejected(void) {
    assert_twins_fold_at_identity(TMP_DIR "/dedup_f_twins_ok.ntpack", "the twins must fold at identity before the control is armed");

    /* ROT180 preserves the 9x13 box, so the dims guard passes and the coverage
     * proof is the gate under test. */
    nt_atlas_test_force_alias_rel(NT_ATLAS_XFORM_ROT180);
    NtAtlasBuild *atlas = NULL;
    NtBuilderContext *ctx = begin_twin_pack(TMP_DIR "/dedup_f_twins_forced.ntpack", &atlas);
    nt_build_assert_handler = trap_build_assert;
    s_build_assert_expr = NULL;
    NT_TEST_LSAN_DISABLE();
    if (setjmp(s_build_assert_jmp) == 0) {
        (void)nt_atlas_commit(atlas);
        NT_TEST_LSAN_ENABLE();
        nt_build_assert_handler = NULL;
        nt_atlas_test_force_alias_rel(0xFFU);
        nt_builder_free_pack(ctx);
        TEST_FAIL_MESSAGE("a wrong relative transform must abort the build, not produce a pack");
        return;
    }
    NT_TEST_LSAN_ENABLE();
    nt_build_assert_handler = NULL;
    nt_atlas_test_force_alias_rel(0xFFU);
    /* The alias-derivation message, not just "proof": the serialize-stage re-proof
     * carries its own and would let a deleted pipeline_reprove_alias_geometry pass. */
    const bool proof_rejected = s_build_assert_expr != NULL && strstr(s_build_assert_expr, "alias geometry proof mismatch") != NULL;
    nt_builder_free_pack(ctx);
    TEST_ASSERT_TRUE_MESSAGE(proof_rejected, "a wrong relative must be rejected by the alias geometry proof");

    /* And the same build succeeds again — the hook is provably disarmed. */
    assert_twins_fold_at_identity(TMP_DIR "/dedup_f_twins_disarmed.ntpack", "the forced-relative hook must not leak into a later case");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_f_fixture_images_pairwise_distinct);
    RUN_TEST(test_f_fixture_dims_swap_on_diagonal);
    RUN_TEST(test_f_fixture_packs);
    RUN_TEST(test_four_rotations_fold_to_one_placement);
    RUN_TEST(test_identity_mask_never_folds_rotations);
    RUN_TEST(test_mirrors_fold_under_flips_mask);
    RUN_TEST(test_fold_admission_follows_the_alias_mask);
    RUN_TEST(test_alias_uv_decode_samples_its_own_source_pixel);
    RUN_TEST(test_mirror_alias_uv_decode_samples_its_own_source_pixel);
    RUN_TEST(test_extruded_ring_replicates_the_shared_placement_edge);
    RUN_TEST(test_mixed_mask_run_keeps_two_roots_and_folds_onto_the_first);
    RUN_TEST(test_transposed_placement_composes_with_the_relative);
    RUN_TEST(test_alias_region_matches_standalone_pack);
    RUN_TEST(test_concave_alias_hull_matches_standalone);
    RUN_TEST(test_mirrored_alias_region_matches_standalone_pack);
    RUN_TEST(test_mirrored_concave_alias_hull_matches_standalone);
    RUN_TEST(test_mirrored_alias_winding_is_world_ccw);
    RUN_TEST(test_wrong_relative_transform_is_rejected);
    return UNITY_END();
}
