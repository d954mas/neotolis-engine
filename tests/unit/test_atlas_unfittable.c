/* Unfittable-sprite acceptance: an unfittable sprite and an ATLAS_MAX_PAGES-
 * exhausting set both produce graceful errors (no abort), write no .ntpack, and
 * leave no leaked buffer / hung worker pool. This target runs under LIVE LSan (no
 * detect_leaks=0 masking) — its passing is the concrete proof for the pack-time
 * failure paths. A boundary-fit case proves the UNFITTABLE pre-check is no
 * stricter than the real vector_pack fit test. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* clang-format off */
#include "nt_builder.h"
#include "unity.h"
/* clang-format on */

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

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) {
        (void)fclose(f);
        return true;
    }
    return false;
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
    nt_builder_begin_atlas(ctx, "toobig", &opts);

    uint8_t *px = make_opaque(80, 200); /* 80 > 57 boundary → cannot fit a 64 page */
    nt_builder_atlas_add_raw(ctx, px, 80, 80, &(nt_atlas_sprite_opts_t){.name = "giant.png", .origin_x = 0.5F, .origin_y = 0.5F});

    nt_builder_end_atlas(ctx); /* no abort */

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_UNFITTABLE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("giant.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_STRING("toobig", errs[0].atlas);
    TEST_ASSERT_EQUAL_UINT32(80, errs[0].w); /* post-trim dims */
    TEST_ASSERT_EQUAL_UINT32(80, errs[0].h);
    TEST_ASSERT_EQUAL_UINT32(2, errs[0].padding);
    TEST_ASSERT_EQUAL_UINT32(2, errs[0].margin);
    TEST_ASSERT_EQUAL_UINT32(64, errs[0].max_size);

    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after an unfittable build");

    nt_builder_free_pack(ctx);
    free(px);
}

/* A sprite sized exactly at the largest footprint that still fits max_size packs
 * successfully — proves the pre-check is no stricter than vector_pack (A1). */
void test_atlas_unfittable_boundary_fits(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_boundary_fits.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts();
    nt_builder_begin_atlas(ctx, "fits", &opts);

    uint8_t *px = make_opaque(57, 200); /* exactly at the fit boundary */
    nt_builder_atlas_add_raw(ctx, px, 57, 57, &(nt_atlas_sprite_opts_t){.name = "snug.png", .origin_x = 0.5F, .origin_y = 0.5F});

    nt_builder_end_atlas(ctx);

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
    nt_builder_begin_atlas(ctx, "toomany", &opts);

    /* 57×57 fills a 64 page (footprint == max_size) → one sprite per page.
     * 70 > ATLAS_MAX_PAGES (64) forces exhaustion. */
    enum { N_SPRITES = 70 };
    uint8_t *sprites[N_SPRITES];
    for (uint32_t i = 0; i < N_SPRITES; i++) {
        sprites[i] = make_opaque(57, (uint8_t)(i + 1));
        char name[32];
        (void)snprintf(name, sizeof(name), "sp_%u.png", i);
        nt_builder_atlas_add_raw(ctx, sprites[i], 57, 57, &(nt_atlas_sprite_opts_t){.name = name, .origin_x = 0.5F, .origin_y = 0.5F});
    }

    nt_builder_end_atlas(ctx); /* no abort, pool joined, buffers freed */

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_PAGES_EXHAUSTED, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("toomany", errs[0].atlas);

    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
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
    nt_builder_begin_atlas(ctx, "override", &opts);

    uint8_t *px = make_disc(32);
    nt_builder_atlas_add_raw(ctx, px, 32, 32, &(nt_atlas_sprite_opts_t){.name = "disc.png", .origin_x = 0.5F, .origin_y = 0.5F, .shape = NT_ATLAS_SPRITE_SHAPE_CONVEX, .max_vertices = 8, .margin = 6});
    nt_builder_end_atlas(ctx);

    uint32_t n = 0;
    (void)nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, n, "disc with margin override must pack without error");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_TRUE_MESSAGE(file_exists(path), ".ntpack must exist after a successful build");

    nt_builder_free_pack(ctx);
    free(px);
    (void)remove(path);
}

/* A poisoned rebuild must remove any pre-existing good pack: no .ntpack may
 * survive a failed build. */
void test_atlas_poison_removes_stale_pack(void) {
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
    nt_builder_begin_atlas(ctx, "toobig", &opts);
    uint8_t *px = make_opaque(80, 200); /* unfittable → poisons the build */
    nt_builder_atlas_add_raw(ctx, px, 80, 80, &(nt_atlas_sprite_opts_t){.name = "giant.png", .origin_x = 0.5F, .origin_y = 0.5F});
    nt_builder_end_atlas(ctx);

    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "stale .ntpack must be removed after a poisoned rebuild");

    nt_builder_free_pack(ctx);
    free(px);
}

/* One atlas with a corrupt-image sprite, a transparent-after-trim sprite, and a
 * good sprite: the pre-packing validation stages still run on the survivors even
 * though the corrupt image already poisoned the pack, so BOTH the corrupt-image
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
    nt_builder_begin_atlas(ctx, "mixed", NULL);

    nt_builder_atlas_add(ctx, bad_png, &(nt_atlas_sprite_opts_t){.name = "corrupt.png", .origin_x = 0.5F, .origin_y = 0.5F});
    uint8_t *clear = make_transparent(16);
    nt_builder_atlas_add_raw(ctx, clear, 16, 16, &(nt_atlas_sprite_opts_t){.name = "clear.png", .origin_x = 0.5F, .origin_y = 0.5F});
    uint8_t *good = make_opaque(16, 200);
    nt_builder_atlas_add_raw(ctx, good, 16, 16, &(nt_atlas_sprite_opts_t){.name = "good.png", .origin_x = 0.5F, .origin_y = 0.5F});

    nt_builder_end_atlas(ctx); /* no abort; validates survivors even when poisoned */

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, n, "corrupt + transparent both reported, good produces none");
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_CORRUPT_IMAGE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("corrupt.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_TRANSPARENT_AFTER_TRIM, errs[1].kind);
    TEST_ASSERT_EQUAL_STRING("clear.png", errs[1].sprite);

    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after a poisoned build");

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
 * poisoning error. */
void test_atlas_validate_reports_all_kinds(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_validate_all.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts();
    nt_builder_begin_atlas(ctx, "mixed", &opts);

    uint8_t *clear = make_transparent(16);
    nt_builder_atlas_add_raw(ctx, clear, 16, 16, &(nt_atlas_sprite_opts_t){.name = "clear.png", .origin_x = 0.5F, .origin_y = 0.5F});
    /* Duplicate name, distinct pixels (distinct hashes → both unique). */
    uint8_t *dup_a = make_opaque(16, 10);
    nt_builder_atlas_add_raw(ctx, dup_a, 16, 16, &(nt_atlas_sprite_opts_t){.name = "dup.png", .origin_x = 0.5F, .origin_y = 0.5F});
    uint8_t *dup_b = make_opaque(16, 20);
    nt_builder_atlas_add_raw(ctx, dup_b, 16, 16, &(nt_atlas_sprite_opts_t){.name = "dup.png", .origin_x = 0.5F, .origin_y = 0.5F});
    uint8_t *giant = make_opaque(80, 200); /* 80 > 57 boundary → unfittable */
    nt_builder_atlas_add_raw(ctx, giant, 80, 80, &(nt_atlas_sprite_opts_t){.name = "giant.png", .origin_x = 0.5F, .origin_y = 0.5F});

    nt_builder_end_atlas(ctx);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_TRUE_MESSAGE(has_error_kind(errs, n, NT_BUILD_ERR_KIND_TRANSPARENT_AFTER_TRIM), "transparent sprite must be reported");
    TEST_ASSERT_TRUE_MESSAGE(has_error_kind(errs, n, NT_BUILD_ERR_KIND_DUPLICATE_NAME), "duplicate name must be reported");
    TEST_ASSERT_TRUE_MESSAGE(has_error_kind(errs, n, NT_BUILD_ERR_KIND_UNFITTABLE), "unfittable sprite must be reported");

    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after a poisoned build");

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
    nt_builder_begin_atlas(ctx, "effmargin", &opts);

    /* 50 + 2*8 (effective margin) = 66 > 64 → unfittable. The override of 1 is
     * below the atlas margin, so the packer uses 8; the record must too. */
    uint8_t *px = make_opaque(50, 200);
    nt_builder_atlas_add_raw(ctx, px, 50, 50, &(nt_atlas_sprite_opts_t){.name = "wide.png", .origin_x = 0.5F, .origin_y = 0.5F, .margin = 1});

    nt_builder_end_atlas(ctx);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_UNFITTABLE, errs[0].kind);
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
    nt_builder_begin_atlas(ctx, "twins", &opts);

    /* Identical pixels (same red) → dedup collapses to one unique sprite. */
    uint8_t *a = make_opaque(80, 200);
    nt_builder_atlas_add_raw(ctx, a, 80, 80, &(nt_atlas_sprite_opts_t){.name = "big_a.png", .origin_x = 0.5F, .origin_y = 0.5F});
    uint8_t *b = make_opaque(80, 200);
    nt_builder_atlas_add_raw(ctx, b, 80, 80, &(nt_atlas_sprite_opts_t){.name = "big_b.png", .origin_x = 0.5F, .origin_y = 0.5F});

    nt_builder_end_atlas(ctx);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, n, "both dedup aliases must report UNFITTABLE");
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_UNFITTABLE, errs[0].kind);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_UNFITTABLE, errs[1].kind);
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
 * end_atlas), and the coarse finish result derives from A (VALIDATION, not
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
    nt_builder_begin_atlas(ctx, "ordered", NULL);

    /* A first: survives decode, fails alpha-trim only inside end_atlas. */
    uint8_t *clear = make_transparent(16);
    nt_builder_atlas_add_raw(ctx, clear, 16, 16, &(nt_atlas_sprite_opts_t){.name = "first_clear.png", .origin_x = 0.5F, .origin_y = 0.5F});
    /* B second: decode fails at add-time, error pushed immediately. */
    nt_builder_atlas_add(ctx, bad_png, &(nt_atlas_sprite_opts_t){.name = "second_corrupt.png", .origin_x = 0.5F, .origin_y = 0.5F});

    nt_builder_end_atlas(ctx);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(2, n);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_ERR_KIND_TRANSPARENT_AFTER_TRIM, errs[0].kind, "earliest-added offender (A) sorts first");
    TEST_ASSERT_EQUAL_STRING("first_clear.png", errs[0].sprite);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_CORRUPT_IMAGE, errs[1].kind);
    TEST_ASSERT_EQUAL_STRING("second_corrupt.png", errs[1].sprite);

    /* Coarse result derives from errs[0] (A) → VALIDATION, not B's FORMAT. */
    TEST_ASSERT_EQUAL(NT_BUILD_ERR_VALIDATION, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after a poisoned build");

    nt_builder_free_pack(ctx);
    free(clear);
    (void)remove(bad_png);
}

/* Filling the error list must preserve the earliest-seq PREFIX. A transparent
 * sprite A is added FIRST (seq 0) but its error only surfaces in end_atlas;
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
    nt_builder_begin_atlas(ctx, "prefix", NULL);

    /* A first (seq 0): survives decode, fails alpha-trim only inside end_atlas. */
    uint8_t *clear = make_transparent(16);
    nt_builder_atlas_add_raw(ctx, clear, 16, 16, &(nt_atlas_sprite_opts_t){.name = "aaa_first_clear.png", .origin_x = 0.5F, .origin_y = 0.5F});

    /* Fill the list to capacity with corrupt-decode errors (seq 1..MAX). The
     * atlas stays open, so each corrupt add keeps pushing despite poison. */
    for (uint32_t i = 0; i < NT_BUILD_MAX_ERRORS; i++) {
        char nm[64];
        (void)snprintf(nm, sizeof(nm), "corrupt_%04u.png", i);
        nt_builder_atlas_add(ctx, bad_png, &(nt_atlas_sprite_opts_t){.name = nm, .origin_x = 0.5F, .origin_y = 0.5F});
    }

    nt_builder_end_atlas(ctx);

    TEST_ASSERT_TRUE_MESSAGE(nt_builder_errors_truncated(ctx), "list must be flagged truncated");
    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(NT_BUILD_MAX_ERRORS, n);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_ERR_KIND_TRANSPARENT_AFTER_TRIM, errs[0].kind, "earliest-added (seq 0) must head the capped prefix");
    TEST_ASSERT_EQUAL_STRING("aaa_first_clear.png", errs[0].sprite);

    /* Coarse result derives from errs[0] → VALIDATION, not the corrupt FORMAT. */
    TEST_ASSERT_EQUAL(NT_BUILD_ERR_VALIDATION, nt_builder_finish_pack(ctx));

    nt_builder_free_pack(ctx);
    free(clear);
    (void)remove(bad_png);
}

/* A raw sprite whose width*height overflows the 4-byte RGBA size guard surfaces
 * as one graceful IMAGE_TOO_LARGE (the guard fires before any allocation), not a
 * corrupt/oom trap. Pins the oversized classification + coarse LIMIT result. */
void test_atlas_add_raw_oversized_image_too_large(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_oversized.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts();
    nt_builder_begin_atlas(ctx, "huge", &opts);

    /* 70000*70000*4 overflows UINT32_MAX/4 → guarded before alloc, so the single
     * dummy byte is never dereferenced. */
    uint8_t dummy = 0;
    nt_builder_atlas_add_raw(ctx, &dummy, 70000, 70000, &(nt_atlas_sprite_opts_t){.name = "huge.png", .origin_x = 0.5F, .origin_y = 0.5F});

    nt_builder_end_atlas(ctx);

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

/* The fit-loop NULL-hull guard (A6) must not over-skip: a CONVEX-shape sprite
 * with a valid hull and a raise-margin override that pushes its scratch quad past
 * max_size must still report EXACTLY ONE UNFITTABLE (not zero, not a duplicate).
 * A genuine NULL-hull survivor (DEGENERATE_HULL / CONTOUR_VERTEX_OVERFLOW) is not
 * constructible from sprite pixel content — pixel-corner convex hulls are always
 * a >=4-vertex box for any non-empty mask — so the memcpy(NULL,0) / double-report
 * half of the guard is defensive; this pins the reachable half. */
void test_atlas_valid_hull_override_reports_once(void) {
    (void)MKDIR(TMP_DIR);
    const char *path = TMP_DIR "/atlas_valid_hull_override.ntpack";
    (void)remove(path);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_atlas_opts_t opts = boundary_opts(); /* max_size 64, margin 2 */
    nt_builder_begin_atlas(ctx, "override", &opts);

    /* A disc has a real many-sided convex hull. margin override 60 (>> atlas 2)
     * builds a scratch quad wider than 64 → unfittable. The guard must keep this
     * legitimate override sprite and report it once. */
    uint8_t *disc = make_disc(40);
    nt_builder_atlas_add_raw(ctx, disc, 40, 40,
                             &(nt_atlas_sprite_opts_t){.name = "disc.png", .origin_x = 0.5F, .origin_y = 0.5F, .shape = NT_ATLAS_SPRITE_SHAPE_CONVEX, .max_vertices = 8, .margin = 60});

    nt_builder_end_atlas(ctx);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, n, "valid-hull override sprite reported once, not skipped, not doubled");
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_UNFITTABLE, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("disc.png", errs[0].sprite);

    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_FALSE_MESSAGE(file_exists(path), "no .ntpack must exist after a poisoned build");

    nt_builder_free_pack(ctx);
    free(disc);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_atlas_unfittable_sprite);
    RUN_TEST(test_atlas_unfittable_boundary_fits);
    RUN_TEST(test_atlas_pages_exhausted_graceful);
    RUN_TEST(test_atlas_margin_override_hull_no_oob);
    RUN_TEST(test_atlas_poison_removes_stale_pack);
    RUN_TEST(test_atlas_cross_stage_collect_all);
    RUN_TEST(test_atlas_validate_reports_all_kinds);
    RUN_TEST(test_atlas_unfittable_reports_effective_margin);
    RUN_TEST(test_atlas_unfittable_dedup_aliases);
    RUN_TEST(test_atlas_errors_stable_add_order);
    RUN_TEST(test_atlas_truncation_preserves_earliest);
    RUN_TEST(test_atlas_add_raw_oversized_image_too_large);
    RUN_TEST(test_atlas_valid_hull_override_reports_once);
    return UNITY_END();
}
