/* Live-LSan coverage for graceful atlas failure paths. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* clang-format off */
#include "nt_builder.h"
#include "nt_builder_atlas_test.h"
#include "unity.h"
/* clang-format on */

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#define RMDIR(p) rmdir(p)
#endif

#define TMP_DIR "build/tests/tmp"

void setUp(void) {}
void tearDown(void) {}

/* Fully-opaque N×N RGBA sprite; `red` keeps decoded_hash unique so dedup cannot
 * collapse distinct sprites in the pages-exhausted case. */
static uint8_t *make_opaque(uint32_t n, uint8_t red) {
    uint8_t *px = (uint8_t *)malloc((size_t)n * n * 4);
    for (size_t i = 0; i < (size_t)n * n; i++) {
        px[i * 4] = red;
        px[(i * 4) + 1] = 50;
        px[(i * 4) + 2] = 100;
        px[(i * 4) + 3] = 255;
    }
    return px;
}

/* Copy a name into a fixed NT_BUILD_ERR_NAME_MAX record buffer for format tests. */
static void error_copy_test_name(char *dst, const char *src) { (void)snprintf(dst, NT_BUILD_ERR_NAME_MAX, "%s", src); }

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) {
        (void)fclose(f);
        return true;
    }
    return false;
}

static void write_oversized_png_header(const char *path, uint32_t width, uint32_t height) {
    uint8_t png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x01,
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    png[16] = (uint8_t)(width >> 24U);
    png[17] = (uint8_t)(width >> 16U);
    png[18] = (uint8_t)(width >> 8U);
    png[19] = (uint8_t)width;
    png[20] = (uint8_t)(height >> 24U);
    png[21] = (uint8_t)(height >> 16U);
    png[22] = (uint8_t)(height >> 8U);
    png[23] = (uint8_t)height;
    FILE *f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_size_t(sizeof(png), fwrite(png, 1, sizeof(png), f));
    (void)fclose(f);
}

static void write_zero_dimension_png_header(const char *path, bool zero_width) {
    uint8_t png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    png[16 + (zero_width ? 3 : 7)] = 0;
    FILE *f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_size_t(sizeof(png), fwrite(png, 1, sizeof(png), f));
    (void)fclose(f);
}

/* Fully transparent (alpha=0) N×N RGBA sprite → fails alpha-trim. */
static uint8_t *make_transparent(uint32_t n) {
    uint8_t *px = (uint8_t *)calloc((size_t)n * n * 4, 1);
    return px;
}

/* Filled opaque disc on a transparent background. A convex hull of a disc is a
 * many-sided polygon (> 4 vertices at max_vertices=8): the per-sprite margin
 * override swaps in a 4-vertex scratch quad, so the hull count must be resynced
 * to 4 or downstream reads past the quad. */
static uint8_t *make_disc(uint32_t n) {
    uint8_t *px = (uint8_t *)calloc((size_t)n * n * 4, 1);
    double c = (n - 1) / 2.0;
    double r = c - 1.0;
    for (uint32_t y = 0; y < n; y++) {
        for (uint32_t x = 0; x < n; x++) {
            double dx = x - c;
            double dy = y - c;
            if ((dx * dx) + (dy * dy) <= r * r) {
                size_t i = ((size_t)y * n + x) * 4;
                px[i] = 200;
                px[i + 1] = 50;
                px[i + 2] = 100;
                px[i + 3] = 255;
            }
        }
    }
    return px;
}

/* RECT opts used across the boundary cases: at max_size=64, pad=2, margin=2 the
 * exact vector_pack fit boundary is N=57 (N>=58 cannot fit an empty page). */
static nt_atlas_opts_t boundary_opts(void) {
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.max_size = 64;
    opts.margin = 2;
    opts.padding = 2;
    opts.shape = NT_ATLAS_SHAPE_RECT;
    return opts;
}

/* A sprite larger than an empty max_size page → one graceful UNFITTABLE error
 * naming the sprite with post-trim w×h + padding/margin + max_size; non-OK
 * finish and NO .ntpack. */
void test_atlas_unfittable_sprite(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_unfittable.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts();
    NtAtlasBuild *atlas_build_112 = nt_atlas_begin(ctx, "toobig", &opts);

    uint8_t *px = make_opaque(80, 200); /* 80 > 57 boundary → cannot fit a 64 page */
    nt_atlas_add_raw(atlas_build_112, px, 80, 80, &(nt_atlas_sprite_opts_t){.name = "giant.png", .origin_x = 0.5F, .origin_y = 0.5F});

    (void)nt_atlas_commit(atlas_build_112); /* no abort */

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("giant.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_STRING("toobig", errs[0].atlas);
    TEST_ASSERT_EQUAL_UINT32(80, errs[0].w); /* post-trim dims */
    TEST_ASSERT_EQUAL_UINT32(80, errs[0].h);
    TEST_ASSERT_EQUAL_UINT32(2, errs[0].padding);
    TEST_ASSERT_EQUAL_UINT32(2, errs[0].margin);
    TEST_ASSERT_EQUAL_UINT32(64, errs[0].max_size);

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_LIMIT, nt_builder_finish_pack(ctx)); /* UNFITTABLE → coarse LIMIT */
    errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("giant.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_STRING("toobig", errs[0].atlas);
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after an unfittable build");

    nt_builder_free_pack(ctx);
    free(px);
}

/* Boundary equality must match vector_pack's real fit predicate. */
void test_atlas_unfittable_boundary_fits(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_boundary_fits.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts();
    NtAtlasBuild *atlas_build_148 = nt_atlas_begin(ctx, "fits", &opts);

    uint8_t *px = make_opaque(57, 200); /* exactly at the fit boundary */
    nt_atlas_add_raw(atlas_build_148, px, 57, 57, &(nt_atlas_sprite_opts_t){.name = "snug.png", .origin_x = 0.5F, .origin_y = 0.5F});

    (void)nt_atlas_commit(atlas_build_148);

    uint32_t n = 0;
    (void)nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, n, "boundary sprite must not be falsely rejected");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_TRUE_MESSAGE(file_exists(path), ".ntpack must exist after a successful build");

    nt_builder_free_pack(ctx);
    free(px);
    (void)remove(path);
}

/* More than ATLAS_MAX_PAGES sprites that each fit one-per-page → one graceful
 * PAGES_EXHAUSTED error (pool joined, buffers freed), non-OK finish, no file. */
void test_atlas_pages_exhausted_graceful(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_pages_exhausted.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    /* Run the packer multi-threaded so the exhaustion teardown actually exercises
     * the worker broadcast/join path (single-threaded would skip the pool). */
    nt_builder_set_threads(ctx, 4);
    nt_atlas_opts_t opts = boundary_opts();
    NtAtlasBuild *atlas_build_179 = nt_atlas_begin(ctx, "toomany", &opts);

    /* 57×57 fills a 64 page (footprint == max_size) → one sprite per page.
     * 70 > ATLAS_MAX_PAGES (64) forces exhaustion. */
    enum { N_SPRITES = 70 };
    uint8_t *sprites[N_SPRITES];
    for (uint32_t i = 0; i < N_SPRITES; i++) {
        sprites[i] = make_opaque(57, (uint8_t)(i + 1));
        char name[32];
        (void)snprintf(name, sizeof(name), "sp_%u.png", i);
        nt_atlas_add_raw(atlas_build_179, sprites[i], 57, 57, &(nt_atlas_sprite_opts_t){.name = name, .origin_x = 0.5F, .origin_y = 0.5F});
    }

    (void)nt_atlas_commit(atlas_build_179); /* no abort, pool joined, buffers freed */

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_PAGES_EXHAUSTED, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("toomany", errs[0].atlas);

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_LIMIT, nt_builder_finish_pack(ctx)); /* PAGES_EXHAUSTED → coarse LIMIT */
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after a pages-exhausted build");

    nt_builder_free_pack(ctx);
    for (uint32_t i = 0; i < N_SPRITES; i++) {
        free(sprites[i]);
    }
}

/* A concave/convex sprite (hull > 4 verts) with a per-sprite margin override
 * larger than the atlas default packs cleanly. tile_pack replaces the hull with
 * a 4-vertex scratch quad for the override; the hull count must be resynced to 4
 * or the empty-page fit test / vector_pack / payload measure read past the quad
 * (heap OOB under ASan). */
void test_atlas_margin_override_hull_no_oob(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_margin_override.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.margin = 0; /* atlas default margin, so the per-sprite override exceeds it */
    NtAtlasBuild *atlas_build_223 = nt_atlas_begin(ctx, "override", &opts);

    uint8_t *px = make_disc(32);
    nt_atlas_add_raw(atlas_build_223, px, 32, 32,
                     &(nt_atlas_sprite_opts_t){.name = "disc.png", .origin_x = 0.5F, .origin_y = 0.5F, .shape = NT_ATLAS_SPRITE_SHAPE_CONVEX, .max_vertices = 8, .margin = 6});
    (void)nt_atlas_commit(atlas_build_223);

    uint32_t n = 0;
    (void)nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, n, "disc with margin override must pack without error");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_TRUE_MESSAGE(file_exists(path), ".ntpack must exist after a successful build");

    nt_builder_free_pack(ctx);
    free(px);
    (void)remove(path);
}

/* A failed rebuild must remove any pre-existing good pack: no .ntpack may
 * survive a failed build. */
void test_atlas_failed_rebuild_removes_stale_pack(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_stale.ntpack";

    /* Pre-create a sentinel "good" pack from a prior successful build. */
    FILE *sentinel = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(sentinel);
    (void)fwrite("STALE", 1, 5, sentinel);
    (void)fclose(sentinel);
    TEST_ASSERT_TRUE(file_exists(path));

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts();
    NtAtlasBuild *atlas_build_256 = nt_atlas_begin(ctx, "toobig", &opts);
    uint8_t *px = make_opaque(80, 200);
    nt_atlas_add_raw(atlas_build_256, px, 80, 80, &(nt_atlas_sprite_opts_t){.name = "giant.png", .origin_x = 0.5F, .origin_y = 0.5F});
    (void)nt_atlas_commit(atlas_build_256);

    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "stale .ntpack must be removed after a failed rebuild");

    nt_builder_free_pack(ctx);
    free(px);
}

/* A fail-fast caller may free the pack immediately after commit, so stale
 * pack and header outputs must already be gone when the failed commit returns. */
void test_atlas_failed_commit_invalidates_stale_outputs(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_stale_early.ntpack";
    const char *header_path = TMP_DIR "/atlas_stale_early.h";

    FILE *pack_sentinel = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(pack_sentinel);
    (void)fwrite("STALE", 1, 5, pack_sentinel);
    (void)fclose(pack_sentinel);
    FILE *header_sentinel = fopen(header_path, "wb");
    TEST_ASSERT_NOT_NULL(header_sentinel);
    (void)fwrite("STALE", 1, 5, header_sentinel);
    (void)fclose(header_sentinel);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts();
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "toobig", &opts);
    uint8_t *px = make_opaque(80, 200);
    nt_atlas_add_raw(atlas, px, 80, 80, &(nt_atlas_sprite_opts_t){.name = "giant.png", .origin_x = 0.5F, .origin_y = 0.5F});

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_LIMIT, nt_atlas_commit(atlas));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "failed commit must invalidate a stale .ntpack before returning");
    TEST_ASSERT_FALSE_MESSAGE(file_exists(header_path), "failed commit must invalidate a stale generated header before returning");

    nt_builder_free_pack(ctx);
    free(px);
}

/* Failure to remove one stale output must not suppress cleanup of the other. */
void test_atlas_failed_commit_attempts_both_stale_outputs(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_stale_blocked.ntpack";
    const char *blocker_path = TMP_DIR "/atlas_stale_blocked.ntpack/blocker";
    const char *header_path = TMP_DIR "/atlas_stale_blocked.h";
    (void)remove(blocker_path);
    (void)RMDIR(path);
    (void)remove(header_path);

    TEST_ASSERT_EQUAL_INT(0, MKDIR(path));
    FILE *blocker = fopen(blocker_path, "wb");
    TEST_ASSERT_NOT_NULL(blocker);
    (void)fwrite("BLOCK", 1, 5, blocker);
    (void)fclose(blocker);
    FILE *header_sentinel = fopen(header_path, "wb");
    TEST_ASSERT_NOT_NULL(header_sentinel);
    (void)fwrite("STALE", 1, 5, header_sentinel);
    (void)fclose(header_sentinel);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts();
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "toobig", &opts);
    uint8_t *px = make_opaque(80, 200);
    nt_atlas_add_raw(atlas, px, 80, 80, &(nt_atlas_sprite_opts_t){.name = "giant.png", .origin_x = 0.5F, .origin_y = 0.5F});

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_IO, nt_atlas_commit(atlas));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(header_path), "failed pack cleanup must still remove the stale header");
    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("giant.png", errs[0].sprite);
    TEST_ASSERT_EQUAL(NT_BUILD_ERR_IO, nt_builder_finish_pack(ctx));
    errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("giant.png", errs[0].sprite);
    TEST_ASSERT_FALSE_MESSAGE(file_exists(header_path), "finish retry must not recreate the stale header");

    nt_builder_free_pack(ctx);
    free(px);
    (void)remove(blocker_path);
    (void)RMDIR(path);
}

/* One atlas with a corrupt-image sprite, a transparent-after-trim sprite, and a
 * good sprite: the pre-packing validation stages still run on the survivors even
 * after the corrupt image fails, so BOTH the corrupt-image
 * and the transparent error are collected (the good sprite produces none). */
void test_atlas_cross_stage_collect_all(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_cross_stage.ntpack";
    (void)remove(path);

    /* A garbage file stb_image cannot decode → CORRUPT_IMAGE (the file must
     * exist, else atlas_add asserts on the read). */
    const char *bad_png = TMP_DIR "/corrupt.png";
    FILE *bf = fopen(bad_png, "wb");
    TEST_ASSERT_NOT_NULL(bf);
    (void)fwrite("not a real png", 1, 14, bf);
    (void)fclose(bf);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    NtAtlasBuild *atlas_build_287 = nt_atlas_begin(ctx, "mixed", NULL);

    nt_atlas_add(atlas_build_287, bad_png, &(nt_atlas_sprite_opts_t){.name = "corrupt.png", .origin_x = 0.5F, .origin_y = 0.5F});
    uint8_t *clear = make_transparent(16);
    nt_atlas_add_raw(atlas_build_287, clear, 16, 16, &(nt_atlas_sprite_opts_t){.name = "clear.png", .origin_x = 0.5F, .origin_y = 0.5F});
    uint8_t *good = make_opaque(16, 200);
    nt_atlas_add_raw(atlas_build_287, good, 16, 16, &(nt_atlas_sprite_opts_t){.name = "good.png", .origin_x = 0.5F, .origin_y = 0.5F});

    (void)nt_atlas_commit(atlas_build_287);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, n, "corrupt + transparent both reported, good produces none");
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_CORRUPT_IMAGE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("corrupt.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_TRANSPARENT_AFTER_TRIM, errs[1].kind);
    TEST_ASSERT_EQUAL_STRING("clear.png", errs[1].sprite);

    /* Coarse result derives from errs[0] (CORRUPT_IMAGE) → FORMAT. */
    TEST_ASSERT_EQUAL(NT_BUILD_ERR_FORMAT, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after a failed build");

    nt_builder_free_pack(ctx);
    free(clear);
    free(good);
    (void)remove(bad_png);
}

/* Scan an error list for a given kind. */
static bool has_error_kind(const nt_build_error_t *errs, uint32_t n, nt_build_error_kind kind) {
    for (uint32_t i = 0; i < n; i++) {
        if (errs[i].kind == kind) {
            return true;
        }
    }
    return false;
}

/* A fully-transparent sprite, a duplicate-name pair, and an unfittable
 * sprite in one atlas — the pre-pack validation pass reports ALL of
 * TRANSPARENT_AFTER_TRIM, DUPLICATE_NAME and UNFITTABLE, not just the first
 * earlier content error. */
void test_atlas_validate_reports_all_kinds(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_validate_all.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts();
    NtAtlasBuild *atlas_build_337 = nt_atlas_begin(ctx, "mixed", &opts);

    uint8_t *clear = make_transparent(16);
    nt_atlas_add_raw(atlas_build_337, clear, 16, 16, &(nt_atlas_sprite_opts_t){.name = "clear.png", .origin_x = 0.5F, .origin_y = 0.5F});
    /* Duplicate name, distinct pixels (distinct hashes → both unique). */
    uint8_t *dup_a = make_opaque(16, 10);
    nt_atlas_add_raw(atlas_build_337, dup_a, 16, 16, &(nt_atlas_sprite_opts_t){.name = "dup.png", .origin_x = 0.5F, .origin_y = 0.5F});
    uint8_t *dup_b = make_opaque(16, 20);
    nt_atlas_add_raw(atlas_build_337, dup_b, 16, 16, &(nt_atlas_sprite_opts_t){.name = "dup.png", .origin_x = 0.5F, .origin_y = 0.5F});
    uint8_t *giant = make_opaque(80, 200); /* 80 > 57 boundary → unfittable */
    nt_atlas_add_raw(atlas_build_337, giant, 80, 80, &(nt_atlas_sprite_opts_t){.name = "giant.png", .origin_x = 0.5F, .origin_y = 0.5F});

    (void)nt_atlas_commit(atlas_build_337);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_TRUE_MESSAGE(has_error_kind(errs, n, NT_BUILD_ERR_KIND_ATLAS_TRANSPARENT_AFTER_TRIM), "transparent sprite must be reported");
    TEST_ASSERT_TRUE_MESSAGE(has_error_kind(errs, n, NT_BUILD_ERR_KIND_ATLAS_DUPLICATE_REGION_NAME), "duplicate name must be reported");
    TEST_ASSERT_TRUE_MESSAGE(has_error_kind(errs, n, NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE), "unfittable sprite must be reported");

    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after a failed build");

    nt_builder_free_pack(ctx);
    free(clear);
    free(dup_a);
    free(dup_b);
    free(giant);
}

/* A per-sprite margin override BELOW the atlas margin is clamped up to the
 * atlas value (raise-only), so an UNFITTABLE record must report the EFFECTIVE
 * margin the packer used, not the smaller requested override. */
void test_atlas_unfittable_reports_effective_margin(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_effective_margin.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.max_size = 64;
    opts.margin = 8;
    opts.padding = 0;
    opts.shape = NT_ATLAS_SHAPE_RECT;
    NtAtlasBuild *atlas_build_382 = nt_atlas_begin(ctx, "effmargin", &opts);

    /* 50 + 2*8 (effective margin) = 66 > 64 → unfittable. The override of 1 is
     * below the atlas margin, so the packer uses 8; the record must too. */
    uint8_t *px = make_opaque(50, 200);
    nt_atlas_add_raw(atlas_build_382, px, 50, 50, &(nt_atlas_sprite_opts_t){.name = "wide.png", .origin_x = 0.5F, .origin_y = 0.5F, .margin = 1});

    (void)nt_atlas_commit(atlas_build_382);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("wide.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(8, errs[0].margin, "record must report the effective (clamped-up) margin, not the below-atlas override");

    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after an unfittable build");

    nt_builder_free_pack(ctx);
    free(px);
}

/* Two byte-identical unfittable sprites with different names dedup to one
 * unique entry, but BOTH names must get their own UNFITTABLE error. */
void test_atlas_unfittable_dedup_aliases(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_unfittable_alias.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts();
    NtAtlasBuild *atlas_build_415 = nt_atlas_begin(ctx, "twins", &opts);

    /* Identical pixels (same red) → dedup collapses to one unique sprite. */
    uint8_t *a = make_opaque(80, 200);
    nt_atlas_add_raw(atlas_build_415, a, 80, 80, &(nt_atlas_sprite_opts_t){.name = "big_a.png", .origin_x = 0.5F, .origin_y = 0.5F});
    uint8_t *b = make_opaque(80, 200);
    nt_atlas_add_raw(atlas_build_415, b, 80, 80, &(nt_atlas_sprite_opts_t){.name = "big_b.png", .origin_x = 0.5F, .origin_y = 0.5F});

    (void)nt_atlas_commit(atlas_build_415);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, n, "both dedup aliases must report UNFITTABLE");
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE, errs[0].kind);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE, errs[1].kind);
    /* One error per name, in add order. */
    TEST_ASSERT_EQUAL_STRING("big_a.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_STRING("big_b.png", errs[1].sprite);

    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after an unfittable build");

    nt_builder_free_pack(ctx);
    free(a);
    free(b);
}

/* A transparent sprite A added BEFORE a corrupt-file sprite B — errors
 * publish in ADD order [A TRANSPARENT, B CORRUPT] (the reverse of discovery
 * order, since B's decode error is pushed at add-time and A's only in
 * commit), and the coarse finish result derives from A (VALIDATION, not
 * FORMAT). */
void test_atlas_errors_stable_add_order(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_add_order.ntpack";
    (void)remove(path);

    const char *bad_png = TMP_DIR "/corrupt_order.png";
    FILE *bf = fopen(bad_png, "wb");
    TEST_ASSERT_NOT_NULL(bf);
    (void)fwrite("not a real png", 1, 14, bf);
    (void)fclose(bf);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    NtAtlasBuild *atlas_build_460 = nt_atlas_begin(ctx, "ordered", NULL);

    /* A first: survives decode, fails alpha-trim during commit. */
    uint8_t *clear = make_transparent(16);
    nt_atlas_add_raw(atlas_build_460, clear, 16, 16, &(nt_atlas_sprite_opts_t){.name = "first_clear.png", .origin_x = 0.5F, .origin_y = 0.5F});
    /* B second: decode fails at add-time, error pushed immediately. */
    nt_atlas_add(atlas_build_460, bad_png, &(nt_atlas_sprite_opts_t){.name = "second_corrupt.png", .origin_x = 0.5F, .origin_y = 0.5F});

    (void)nt_atlas_commit(atlas_build_460);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_ERR_KIND_ATLAS_TRANSPARENT_AFTER_TRIM, errs[0].kind, "earliest-added offender (A) sorts first");
    TEST_ASSERT_EQUAL_STRING("first_clear.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_CORRUPT_IMAGE, errs[1].kind);
    TEST_ASSERT_EQUAL_STRING("second_corrupt.png", errs[1].sprite);

    /* Coarse result derives from errs[0] (A) → VALIDATION, not B's FORMAT. */
    TEST_ASSERT_EQUAL(NT_BUILD_ERR_VALIDATION, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after a failed build");

    nt_builder_free_pack(ctx);
    free(clear);
    (void)remove(bad_png);
}

/* Filling the error list must preserve the earliest-seq PREFIX. A transparent
 * sprite A is added FIRST (seq 0) but its error only surfaces during commit;
 * meanwhile the list fills with corrupt-decode errors (seq 1..MAX). A's error
 * must EVICT the highest-seq corrupt tail so errs[0] is its VALIDATION error,
 * not a FORMAT corrupt. */
void test_atlas_truncation_preserves_earliest(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_truncate_prefix.ntpack";
    (void)remove(path);

    const char *bad_png = TMP_DIR "/corrupt_fill.png";
    FILE *bf = fopen(bad_png, "wb");
    TEST_ASSERT_NOT_NULL(bf);
    (void)fwrite("not a real png", 1, 14, bf);
    (void)fclose(bf);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    NtAtlasBuild *atlas_build_505 = nt_atlas_begin(ctx, "prefix", NULL);

    /* A first (seq 0): survives decode, fails alpha-trim during commit. */
    uint8_t *clear = make_transparent(16);
    nt_atlas_add_raw(atlas_build_505, clear, 16, 16, &(nt_atlas_sprite_opts_t){.name = "aaa_first_clear.png", .origin_x = 0.5F, .origin_y = 0.5F});

    /* Fill the list to capacity with corrupt-decode errors (seq 1..MAX). The
     * atlas stays open, so each corrupt add keeps collecting errors. */
    for (uint32_t i = 0; i < NT_BUILD_MAX_ERRORS; i++) {
        char nm[64];
        (void)snprintf(nm, sizeof(nm), "corrupt_%04u.png", i);
        nt_atlas_add(atlas_build_505, bad_png, &(nt_atlas_sprite_opts_t){.name = nm, .origin_x = 0.5F, .origin_y = 0.5F});
    }

    (void)nt_atlas_commit(atlas_build_505);

    TEST_ASSERT_TRUE_MESSAGE(nt_builder_errors_truncated(ctx), "list must be flagged truncated");
    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(NT_BUILD_MAX_ERRORS, n);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_ERR_KIND_ATLAS_TRANSPARENT_AFTER_TRIM, errs[0].kind, "earliest-added (seq 0) must head the capped prefix");
    TEST_ASSERT_EQUAL_STRING("aaa_first_clear.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_STRING("corrupt_0254.png", errs[NT_BUILD_MAX_ERRORS - 1].sprite);

    /* Coarse result derives from errs[0] → VALIDATION, not the corrupt FORMAT. */
    TEST_ASSERT_EQUAL(NT_BUILD_ERR_VALIDATION, nt_builder_finish_pack(ctx));

    nt_builder_free_pack(ctx);
    free(clear);
    (void)remove(bad_png);
}

/* Area overflow is classified before allocation. */
void test_atlas_add_raw_oversized_image_too_large(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_oversized.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts();
    NtAtlasBuild *atlas_build_547 = nt_atlas_begin(ctx, "huge", &opts);

    /* Oversized dimensions are classified before non-empty spans require pixels. */
    nt_atlas_add_raw(atlas_build_547, NULL, 70000, 70000, &(nt_atlas_sprite_opts_t){.name = "huge.png", .origin_x = 0.5F, .origin_y = 0.5F});

    (void)nt_atlas_commit(atlas_build_547);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("huge.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_UINT32(70000, errs[0].w);
    TEST_ASSERT_EQUAL_UINT32(70000, errs[0].h);

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_LIMIT, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after an oversized build");

    nt_builder_free_pack(ctx);
}

void test_atlas_add_raw_serialization_dimension_graceful(void) {
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/atlas_raw_wide.ntpack");
    TEST_ASSERT_NOT_NULL(ctx);
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "wide", NULL);
    nt_atlas_add_raw(atlas, NULL, UINT16_MAX + 1U, 1, &(nt_atlas_sprite_opts_t){.name = "wide.png", .origin_x = 0.5F, .origin_y = 0.5F});

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_LIMIT, nt_atlas_commit(atlas));
    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_SPRITE_TOO_LARGE, errs[0].kind);
    TEST_ASSERT_EQUAL_UINT32(UINT16_MAX + 1U, errs[0].w);
    TEST_ASSERT_EQUAL_UINT32(1, errs[0].h);

    nt_builder_free_pack(ctx);
}

void test_atlas_mid_pipeline_errors_lsan(void) {
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/atlas_mid_pipeline_errors.ntpack");
    TEST_ASSERT_NOT_NULL(ctx);
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "mid", NULL);

    uint8_t slice9[8 * 8 * 4] = {0};
    for (size_t i = 0; i < sizeof(slice9) / 4; i++) {
        slice9[(i * 4) + 3] = 255;
    }
    nt_atlas_add_raw(atlas, slice9, 8, 8, &(nt_atlas_sprite_opts_t){.name = "slice9.png", .origin_x = 0.5F, .origin_y = 0.5F, .slice9_left = 4, .slice9_right = 4});

    const uint32_t trim_width = 40000;
    uint8_t *trim = (uint8_t *)calloc((size_t)trim_width * 4, 1);
    TEST_ASSERT_NOT_NULL(trim);
    trim[((size_t)35000 * 4) + 3] = 255;
    nt_atlas_add_raw(atlas, trim, trim_width, 1, &(nt_atlas_sprite_opts_t){.name = "trim.png", .origin_x = 0.5F, .origin_y = 0.5F});

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_VALIDATION, nt_atlas_commit(atlas));
    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_SLICE9_TOO_BIG, errs[0].kind);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_TRIM_OFFSET_OVERFLOW, errs[1].kind);
    TEST_ASSERT_EQUAL_UINT32(35000, errs[1].w);
    TEST_ASSERT_EQUAL_UINT32(0, errs[1].h);
    TEST_ASSERT_EQUAL(NT_BUILD_ERR_VALIDATION, nt_builder_finish_pack(ctx));

    nt_builder_free_pack(ctx);
    free(trim);
}

/* File and glob decoding classify oversized headers as image limits. */
void test_atlas_add_file_and_glob_oversized_image_too_large(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_oversized_files.ntpack";
    const char *direct_path = TMP_DIR "/oversized_direct.png";
    const char *glob_path = TMP_DIR "/oversized_glob_a.png";
    (void)remove(path);
    write_oversized_png_header(direct_path, UINT32_C(0x01000001), 1);
    write_oversized_png_header(glob_path, 1, UINT32_C(0x01000001));

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "hugefiles", NULL);
    nt_atlas_add(atlas, direct_path, &(nt_atlas_sprite_opts_t){.name = "direct.png", .origin_x = 0.5F, .origin_y = 0.5F});
    nt_atlas_add_glob(atlas, TMP_DIR "/oversized_glob_*.png", NULL);

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_LIMIT, nt_atlas_commit(atlas));
    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("direct.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE, errs[1].kind);
    TEST_ASSERT_EQUAL_STRING("oversized_glob_a.png", errs[1].sprite);

    nt_builder_free_pack(ctx);
    (void)remove(direct_path);
    (void)remove(glob_path);
}

void test_atlas_add_file_and_glob_zero_dimension(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_zero_dimension_files.ntpack";
    const char *direct_path = TMP_DIR "/zero_width_direct.png";
    const char *glob_path = TMP_DIR "/zero_dimension_glob_a.png";
    (void)remove(path);
    write_zero_dimension_png_header(direct_path, true);
    write_zero_dimension_png_header(glob_path, false);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "zerodim", NULL);
    nt_atlas_add(atlas, direct_path, &(nt_atlas_sprite_opts_t){.name = "direct.png", .origin_x = 0.5F, .origin_y = 0.5F});
    nt_atlas_add_glob(atlas, TMP_DIR "/zero_dimension_glob_*.png", NULL);

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_VALIDATION, nt_atlas_commit(atlas));
    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ZERO_DIM, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("direct.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ZERO_DIM, errs[1].kind);
    TEST_ASSERT_EQUAL_STRING("zero_dimension_glob_a.png", errs[1].sprite);

    nt_builder_free_pack(ctx);
    (void)remove(direct_path);
    (void)remove(glob_path);
}

static void add_zero_dim_errors(NtAtlasBuild *atlas, const char *prefix, uint32_t count) {
    uint8_t dummy = 0;
    for (uint32_t i = 0; i < count; i++) {
        char name[32];
        (void)snprintf(name, sizeof(name), "%s%03u.png", prefix, i);
        nt_atlas_add_raw(atlas, &dummy, 0, 1, &(nt_atlas_sprite_opts_t){.name = name, .origin_x = 0.5F, .origin_y = 0.5F});
    }
}

/* The pack accumulator preserves the exact 256-error prefix across commits. */
void test_atlas_error_cap_crosses_transaction_boundary(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_cross_transaction_cap.ntpack";
    (void)remove(path);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);

    NtAtlasBuild *first = nt_atlas_begin(ctx, "first", NULL);
    add_zero_dim_errors(first, "A", NT_BUILD_MAX_ERRORS - 1);
    TEST_ASSERT_EQUAL(NT_BUILD_ERR_VALIDATION, nt_atlas_commit(first));

    NtAtlasBuild *second = nt_atlas_begin(ctx, "second", NULL);
    add_zero_dim_errors(second, "B", 2);
    TEST_ASSERT_EQUAL(NT_BUILD_ERR_VALIDATION, nt_atlas_commit(second));

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(NT_BUILD_MAX_ERRORS, n);
    TEST_ASSERT_TRUE(nt_builder_errors_truncated(ctx));
    TEST_ASSERT_EQUAL_STRING("A000.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_STRING("A254.png", errs[NT_BUILD_MAX_ERRORS - 2].sprite);
    TEST_ASSERT_EQUAL_STRING("B000.png", errs[NT_BUILD_MAX_ERRORS - 1].sprite);

    nt_builder_free_pack(ctx);
}

/* An over-cap atlas still reports duplicate names from the same transaction. */
void test_atlas_over_region_cap_still_reports_duplicate_names(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_over_region_cap.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "overcap", NULL);
    const uint8_t pixel[4] = {200, 50, 100, 255};
    for (uint32_t i = 0; i <= UINT16_MAX; i++) {
        char name[32];
        if (i == UINT16_MAX) {
            (void)snprintf(name, sizeof(name), "sprite_0.png");
        } else {
            (void)snprintf(name, sizeof(name), "sprite_%u.png", i);
        }
        nt_atlas_add_raw(atlas, pixel, 1, 1, &(nt_atlas_sprite_opts_t){.name = name, .origin_x = 0.5F, .origin_y = 0.5F});
    }

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_DUPLICATE, nt_atlas_commit(atlas));
    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_TRUE_MESSAGE(has_error_kind(errs, n, NT_BUILD_ERR_KIND_ATLAS_DUPLICATE_REGION_NAME), "duplicate name must be collected above the region cap");
    TEST_ASSERT_TRUE_MESSAGE(has_error_kind(errs, n, NT_BUILD_ERR_KIND_ATLAS_TOO_MANY_REGIONS), "region cap must also be reported");

    nt_builder_free_pack(ctx);
}

/* A valid hull must not be skipped by the defensive NULL-hull guard. */
void test_atlas_valid_hull_override_reports_once(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_valid_hull_override.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts(); /* max_size 64, margin 2 */
    NtAtlasBuild *atlas_build_585 = nt_atlas_begin(ctx, "override", &opts);

    /* A disc has a real many-sided convex hull. margin override 60 (>> atlas 2)
     * builds a scratch quad wider than 64 → unfittable. The guard must keep this
     * legitimate override sprite and report it once. */
    uint8_t *disc = make_disc(40);
    nt_atlas_add_raw(atlas_build_585, disc, 40, 40,
                     &(nt_atlas_sprite_opts_t){.name = "disc.png", .origin_x = 0.5F, .origin_y = 0.5F, .shape = NT_ATLAS_SPRITE_SHAPE_CONVEX, .max_vertices = 8, .margin = 60});

    (void)nt_atlas_commit(atlas_build_585);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, n, "valid-hull override sprite reported once, not skipped, not doubled");
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("disc.png", errs[0].sprite);

    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after a failed build");

    nt_builder_free_pack(ctx);
    free(disc);
}

/* When the error list is FULL, an error with a seq >= the current tail's seq is
 * DROPPED (it belongs outside the earliest-seq prefix) — the sibling of the
 * evict-tail branch. Fill to capacity with monotonically-increasing-seq corrupt
 * adds, then add more: the later ones drop and errs[0]/errs[last] stay the
 * earliest-added prefix. */
void test_atlas_truncation_drops_later_seqs(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_truncate_drop.ntpack";
    (void)remove(path);

    const char *bad_png = TMP_DIR "/corrupt_drop.png";
    FILE *bf = fopen(bad_png, "wb");
    TEST_ASSERT_NOT_NULL(bf);
    (void)fwrite("not a real png", 1, 14, bf);
    (void)fclose(bf);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    NtAtlasBuild *atlas_build_627 = nt_atlas_begin(ctx, "drop", NULL);

    /* Fill exactly to capacity: seq 0..MAX-1, names corrupt_0000..corrupt_(MAX-1). */
    for (uint32_t i = 0; i < NT_BUILD_MAX_ERRORS; i++) {
        char nm[64];
        (void)snprintf(nm, sizeof(nm), "corrupt_%04u.png", i);
        nt_atlas_add(atlas_build_627, bad_png, &(nt_atlas_sprite_opts_t){.name = nm, .origin_x = 0.5F, .origin_y = 0.5F});
    }
    /* Additional corrupt adds carry higher seqs than the tail → each is DROPPED. */
    for (uint32_t i = 0; i < 8; i++) {
        char nm[64];
        (void)snprintf(nm, sizeof(nm), "zzz_late_%04u.png", i);
        nt_atlas_add(atlas_build_627, bad_png, &(nt_atlas_sprite_opts_t){.name = nm, .origin_x = 0.5F, .origin_y = 0.5F});
    }

    (void)nt_atlas_commit(atlas_build_627);

    TEST_ASSERT_TRUE_MESSAGE(nt_builder_errors_truncated(ctx), "list must be flagged truncated");
    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(NT_BUILD_MAX_ERRORS, n);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("corrupt_0000.png", errs[0].sprite, "earliest-added heads the prefix");
    /* The tail slot keeps the last in-prefix add, not any dropped later error. */
    TEST_ASSERT_EQUAL_STRING_MESSAGE("corrupt_0255.png", errs[NT_BUILD_MAX_ERRORS - 1].sprite, "later-seq adds must be dropped, not overwrite the tail");

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_FORMAT, nt_builder_finish_pack(ctx));

    nt_builder_free_pack(ctx);
    (void)remove(bad_png);
}

/* A per-sprite margin AND extrude override ABOVE the atlas baseline must carry
 * through to the UNFITTABLE record verbatim (both are the effective max the
 * packer reserved). The complement of the below-atlas clamp-up case. */
void test_atlas_unfittable_reports_above_atlas_override(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_above_override.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.max_size = 64;
    opts.margin = 2;
    opts.padding = 0;
    opts.extrude = 2;
    opts.shape = NT_ATLAS_SHAPE_RECT;
    NtAtlasBuild *atlas_build_674 = nt_atlas_begin(ctx, "aboveovr", &opts);

    /* margin override 20 (> atlas 2): 50 + 2*20 = 90 > 64 → unfittable. extrude
     * override 10 (> atlas 2) is the effective bleed the record must report. The
     * sprite uses the atlas RECT default (no shape override) — the effective shape
     * is RECT, so the per-sprite extrude must not trap the cross-field assert. */
    uint8_t *px = make_opaque(50, 200);
    nt_atlas_add_raw(atlas_build_674, px, 50, 50, &(nt_atlas_sprite_opts_t){.name = "wide.png", .origin_x = 0.5F, .origin_y = 0.5F, .margin = 20, .extrude = 10});

    (void)nt_atlas_commit(atlas_build_674);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("wide.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(20, errs[0].margin, "above-atlas margin override carries through");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(10, errs[0].detail_a, "above-atlas extrude override carries through");

    TEST_ASSERT_EQUAL(NT_BUILD_ERR_LIMIT, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after an unfittable build");

    nt_builder_free_pack(ctx);
    free(px);
}

void test_build_error_format_matrix(void) {
    typedef struct {
        nt_build_error_kind kind;
        const char *expected;
    } FormatCase;
    static const FormatCase cases[] = {
        {NT_BUILD_ERR_KIND_CORRUPT_IMAGE, "atlas 'atlas', sprite 'sprite.png': corrupt or undecodable image"},
        {NT_BUILD_ERR_KIND_ZERO_DIM, "atlas 'atlas', sprite 'sprite.png': zero width or height"},
        {NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE, "atlas 'atlas', sprite 'sprite.png': image 123x456 exceeds decode limit"},
        {NT_BUILD_ERR_KIND_ATLAS_TRANSPARENT_AFTER_TRIM, "atlas 'atlas', sprite 'sprite.png': fully transparent after trim"},
        {NT_BUILD_ERR_KIND_ATLAS_SLICE9_TOO_BIG, "atlas 'atlas', sprite 'sprite.png': slice9 borders (7) >= source extent (9)"},
        {NT_BUILD_ERR_KIND_ATLAS_DEGENERATE_HULL, "atlas 'atlas', sprite 'sprite.png': degenerate hull (no usable outline)"},
        {NT_BUILD_ERR_KIND_ATLAS_CONTOUR_VERTEX_OVERFLOW, "atlas 'atlas', sprite 'sprite.png': contour exceeds vertex budget"},
        {NT_BUILD_ERR_KIND_ATLAS_DUPLICATE_REGION_NAME, "atlas 'atlas': duplicate region name 'sprite.png'"},
        {NT_BUILD_ERR_KIND_ATLAS_TOO_MANY_REGIONS, "atlas 'atlas': region count exceeds 65535"},
        {NT_BUILD_ERR_KIND_ATLAS_SPRITE_TOO_LARGE, "atlas 'atlas', sprite 'sprite.png': 123x456 exceeds 65535px"},
        {NT_BUILD_ERR_KIND_ATLAS_TRIM_OFFSET_OVERFLOW, "atlas 'atlas', sprite 'sprite.png': trim offset exceeds int16 range (x=123, y=456)"},
        {NT_BUILD_ERR_KIND_ATLAS_PAGES_EXHAUSTED, "atlas 'atlas': ran out of pages (max_size=2048)"},
        {NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE, "atlas 'atlas', sprite 'sprite.png': 123x456 + padding 3 + margin 5 + extrude 7 does not fit an empty page of max_size 2048"},
        {NT_BUILD_ERR_KIND_NONE, "no error"},
    };

    char buf[256];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        nt_build_error_t err = {.kind = cases[i].kind, .w = 123, .h = 456, .padding = 3, .margin = 5, .max_size = 2048, .detail_a = 7, .detail_b = 9};
        error_copy_test_name(err.atlas, "atlas");
        error_copy_test_name(err.sprite, "sprite.png");
        nt_build_error_format(&err, buf, sizeof(buf));
        TEST_ASSERT_EQUAL_STRING(cases[i].expected, buf);
    }
}

/* nt_build_error_format IMAGE_TOO_LARGE arm: an oversized-header reject leaves
 * dims unwritten (0x0) — the message must omit the bogus "0x0", while a known
 * area-overflow (real dims) keeps the WxH form. */
void test_build_error_format_image_too_large(void) {
    char buf[256];

    /* Unknown dims (stbi_info reject): no "0x0". */
    nt_build_error_t unknown = {.kind = NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE, .w = 0, .h = 0};
    error_copy_test_name(unknown.sprite, "huge.png");
    nt_build_error_format(&unknown, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "huge.png"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "exceeds decode limit"));
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "0x0"), "must not report bogus 0x0 dims");

    /* Known dims (area overflow): keep the WxH form. */
    nt_build_error_t known = {.kind = NT_BUILD_ERR_KIND_IMAGE_TOO_LARGE, .w = 40000, .h = 40000};
    error_copy_test_name(known.sprite, "big.png");
    nt_build_error_format(&known, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "40000x40000"));
}

/* Freeing an unfinished transaction releases all atlas-owned allocations. */
void test_atlas_free_pack_cleans_open_transaction(void) {
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/atlas_open_free.ntpack");
    TEST_ASSERT_NOT_NULL(ctx);
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "open", NULL);
    const uint8_t pixel[4] = {1, 2, 3, 255};
    nt_atlas_add_raw(atlas, pixel, 1, 1, &(nt_atlas_sprite_opts_t){.name = "pixel.png", .origin_x = 0.5F, .origin_y = 0.5F});

    nt_builder_free_pack(ctx);
}

void test_atlas_frontier_lifecycle_lsan(void) { TEST_ASSERT_TRUE(nt_atlas_test_frontier_lifecycle_stress()); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_atlas_unfittable_sprite);
    RUN_TEST(test_atlas_unfittable_boundary_fits);
    RUN_TEST(test_atlas_pages_exhausted_graceful);
    RUN_TEST(test_atlas_margin_override_hull_no_oob);
    RUN_TEST(test_atlas_failed_rebuild_removes_stale_pack);
    RUN_TEST(test_atlas_failed_commit_invalidates_stale_outputs);
    RUN_TEST(test_atlas_failed_commit_attempts_both_stale_outputs);
    RUN_TEST(test_atlas_cross_stage_collect_all);
    RUN_TEST(test_atlas_validate_reports_all_kinds);
    RUN_TEST(test_atlas_unfittable_reports_effective_margin);
    RUN_TEST(test_atlas_unfittable_dedup_aliases);
    RUN_TEST(test_atlas_errors_stable_add_order);
    RUN_TEST(test_atlas_truncation_preserves_earliest);
    RUN_TEST(test_atlas_add_raw_oversized_image_too_large);
    RUN_TEST(test_atlas_add_raw_serialization_dimension_graceful);
    RUN_TEST(test_atlas_mid_pipeline_errors_lsan);
    RUN_TEST(test_atlas_add_file_and_glob_oversized_image_too_large);
    RUN_TEST(test_atlas_add_file_and_glob_zero_dimension);
    RUN_TEST(test_atlas_error_cap_crosses_transaction_boundary);
    RUN_TEST(test_atlas_over_region_cap_still_reports_duplicate_names);
    RUN_TEST(test_atlas_valid_hull_override_reports_once);
    RUN_TEST(test_atlas_truncation_drops_later_seqs);
    RUN_TEST(test_atlas_unfittable_reports_above_atlas_override);
    RUN_TEST(test_build_error_format_matrix);
    RUN_TEST(test_build_error_format_image_too_large);
    RUN_TEST(test_atlas_free_pack_cleans_open_transaction);
    RUN_TEST(test_atlas_frontier_lifecycle_lsan);
    return UNITY_END();
}
