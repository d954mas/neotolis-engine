/* ROBUST-01/02 acceptance: an unfittable sprite and an ATLAS_MAX_PAGES-exhausting
 * set both produce graceful errors (no abort), write no .ntpack, and leave no
 * leaked buffer / hung worker pool. This target runs under LIVE LSan (no
 * detect_leaks=0 masking) — its passing is the concrete ROBUST-02 proof for the
 * pack-time failure paths. A boundary-fit case proves the UNFITTABLE pre-check
 * is no stricter than the real vector_pack fit test. */

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_atlas_unfittable_sprite);
    RUN_TEST(test_atlas_unfittable_boundary_fits);
    RUN_TEST(test_atlas_pages_exhausted_graceful);
    return UNITY_END();
}
