/* Baseline dedup probe: every assertion here holds both before and after the
 * post-trim/D4 dedup change. Requirement-level folds land with the behaviour. */

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
#include "nt_pack_format.h"
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

/* Unity is built with UNITY_EXCLUDE_FLOAT; bit equality is also the stronger
 * claim here — the builder derives the pivot from the same float we passed in. */
static void assert_float_bits_equal(float expect, float actual, const char *msg) {
    uint32_t e = 0;
    uint32_t a = 0;
    memcpy(&e, &expect, sizeof(e));
    memcpy(&a, &actual, sizeof(a));
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(e, a, msg);
}

/* Pack the 10-frame fixture single-threaded so the run is byte-deterministic. */
static bool build_dedup_pack(const char *path, const char *atlas_name) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, atlas_name, &opts);
    atlas_dedup_fixture_add(atlas);
    nt_build_result_t commit = nt_atlas_commit(atlas);
    nt_build_result_t finish = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return commit == NT_BUILD_OK && finish == NT_BUILD_OK;
}

/* Collect the fixture's regions from a freshly built pack. */
static void collect_fixture(const char *path, const char *atlas_name, nt_atlas_dedup_region_t *out, uint32_t *out_count) {
    TEST_ASSERT_TRUE_MESSAGE(build_dedup_pack(path, atlas_name), "dedup fixture pack failed");
    size_t len = 0;
    uint8_t *bytes = read_bin_file(path, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(bytes, "read produced pack");
    bool ok = atlas_dedup_collect_regions(bytes, len, out, NT_ATLAS_DEDUP_FRAME_COUNT, out_count);
    free(bytes);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from produced pack");
}

void test_dedup_fixture_builds(void) {
    nt_atlas_dedup_region_t regions[NT_ATLAS_DEDUP_FRAME_COUNT] = {0};
    uint32_t count = 0;
    collect_fixture(TMP_DIR "/dedup_fixture.ntpack", "dedup_fixture", regions, &count);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_FRAME_COUNT, count, "one region per frame");
    /* Every frame carries the full art box after trim, so a fold can only come
     * from content identity — not from an accidentally empty sprite. */
    for (uint32_t i = 0; i < count; ++i) {
        TEST_ASSERT_GREATER_THAN_UINT8_MESSAGE(0, regions[i].vertex_count, "region has no geometry");
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, count), "no placements measured");
}

void test_dedup_region_metadata_is_per_sprite(void) {
    nt_atlas_dedup_region_t regions[NT_ATLAS_DEDUP_FRAME_COUNT] = {0};
    uint32_t count = 0;
    collect_fixture(TMP_DIR "/dedup_metadata.ntpack", "dedup_metadata", regions, &count);
    TEST_ASSERT_EQUAL_UINT32(NT_ATLAS_DEDUP_FRAME_COUNT, count);
    for (uint32_t i = 0; i < count; ++i) {
        const nt_atlas_dedup_frame_t *f = &k_atlas_dedup_frames[i];
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(f->canvas_w, regions[i].source_w, "source_w must be this frame's canvas width");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(f->canvas_h, regions[i].source_h, "source_h must be this frame's canvas height");
        assert_float_bits_equal(f->origin_x, regions[i].origin_x, "origin_x must be this frame's pivot");
        /* The builder writes pivots y-up. */
        assert_float_bits_equal(1.0F - f->origin_y, regions[i].origin_y, "origin_y must be this frame's pivot, y-up");
        TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)f->art_x, regions[i].trim_offset_x, "trim_offset_x must be this frame's art offset");
        /* trim_offset_y counts from the bottom edge in y-up source space. */
        const int16_t expect_y = (int16_t)(f->canvas_h - f->art_y - NT_ATLAS_DEDUP_ART_H);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(expect_y, regions[i].trim_offset_y, "trim_offset_y must be this frame's y-up art offset");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, regions[i].slice9_lrtb[0], "fixture has no slice9 borders");
    }
}

/* Block sharing is a serialization property, so these cases use their own solid
 * rects: size and colour alone decide whether two blocks come out byte-equal. */
#define BLOCK_SPRITE_MAX_DIM 32
#define BLOCK_SPRITE_MAX_PX (BLOCK_SPRITE_MAX_DIM * BLOCK_SPRITE_MAX_DIM * 4)
#define BLOCK_SPRITE_MAX_COUNT 4

typedef struct {
    const char *name;
    uint16_t w;
    uint16_t h;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} block_sprite_t;

static bool build_block_pack(const char *path, const char *atlas_name, const block_sprite_t *sprites, uint32_t count) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, atlas_name, &opts);
    uint8_t px[BLOCK_SPRITE_MAX_PX];
    for (uint32_t i = 0; i < count; ++i) {
        const block_sprite_t *s = &sprites[i];
        const uint32_t texels = (uint32_t)s->w * (uint32_t)s->h;
        for (uint32_t t = 0; t < texels; ++t) {
            px[(t * 4U) + 0U] = s->r;
            px[(t * 4U) + 1U] = s->g;
            px[(t * 4U) + 2U] = s->b;
            px[(t * 4U) + 3U] = 255U;
        }
        nt_atlas_sprite_opts_t sopts = nt_atlas_sprite_opts_defaults();
        sopts.name = s->name;
        nt_atlas_add_raw(atlas, px, s->w, s->h, &sopts);
    }
    nt_build_result_t commit = nt_atlas_commit(atlas);
    nt_build_result_t finish = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return commit == NT_BUILD_OK && finish == NT_BUILD_OK;
}

static void collect_block_pack(const char *path, const char *atlas_name, const block_sprite_t *sprites, uint32_t count, nt_atlas_dedup_region_t *out) {
    TEST_ASSERT_TRUE_MESSAGE(build_block_pack(path, atlas_name, sprites, count), "block-sharing pack failed");
    size_t len = 0;
    uint8_t *bytes = read_bin_file(path, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(bytes, "read produced pack");
    uint32_t got = 0;
    const bool ok = atlas_dedup_collect_regions(bytes, len, out, count, &got);
    free(bytes);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(count, got, "one region per sprite");
}

void test_identity_aliases_share_one_vertex_block(void) {
    static const block_sprite_t sprites[3] = {
        {"block_a0", 16, 12, 220, 60, 60},
        {"block_a1", 16, 12, 220, 60, 60}, /* byte-identical to block_a0 */
        {"block_b", 20, 8, 70, 90, 230},
    };
    nt_atlas_dedup_region_t regions[BLOCK_SPRITE_MAX_COUNT] = {0};
    collect_block_pack(TMP_DIR "/dedup_block_share.ntpack", "dedup_block_share", sprites, 3, regions);

    /* One placement: the alias occupies its original's rectangle. */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[0].page_index, regions[1].page_index, "identical sprites must share a page");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[0].u_min, regions[1].u_min, "identical sprites must share a placement U");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[0].v_min, regions[1].v_min, "identical sprites must share a placement V");
    /* One block: the emitted bytes are equal, so they share one byte range. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(regions[0].vertex_start, regions[1].vertex_start, "identical sprites must share one vertex block");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(regions[0].index_start, regions[1].index_start, "identical sprites must share one index block");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[0].flags, regions[1].flags, "per-sprite flag recomputation must agree for an identity alias");
    /* A different sprite keeps its own block. */
    TEST_ASSERT_NOT_EQUAL_MESSAGE(regions[0].vertex_start, regions[2].vertex_start, "a different sprite must not share the block");
}

void test_distinct_sprites_do_not_share_blocks(void) {
    static const block_sprite_t sprites[3] = {
        {"block_c0", 14, 10, 235, 200, 40},
        {"block_c1", 18, 10, 60, 210, 80},
        {"block_c2", 14, 16, 70, 90, 230},
    };
    nt_atlas_dedup_region_t regions[BLOCK_SPRITE_MAX_COUNT] = {0};
    collect_block_pack(TMP_DIR "/dedup_block_distinct.ntpack", "dedup_block_distinct", sprites, 3, regions);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(regions[0].vertex_start, regions[1].vertex_start, "distinct sprites must not share a block");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(regions[0].vertex_start, regions[2].vertex_start, "distinct sprites must not share a block");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(regions[1].vertex_start, regions[2].vertex_start, "distinct sprites must not share a block");
}

void test_dedup_pack_is_deterministic(void) {
    const char *path_a = TMP_DIR "/dedup_determinism_a.ntpack";
    const char *path_b = TMP_DIR "/dedup_determinism_b.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_dedup_pack(path_a, "dedup_determinism"), "first determinism pack failed");
    TEST_ASSERT_TRUE_MESSAGE(build_dedup_pack(path_b, "dedup_determinism"), "second determinism pack failed");
    size_t len_a = 0;
    size_t len_b = 0;
    uint8_t *a = read_bin_file(path_a, &len_a);
    uint8_t *b = read_bin_file(path_b, &len_b);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(len_a, len_b, "two runs produced different pack sizes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, memcmp(a, b, len_a), "two runs produced different pack bytes");
    free(a);
    free(b);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dedup_fixture_builds);
    RUN_TEST(test_dedup_region_metadata_is_per_sprite);
    RUN_TEST(test_identity_aliases_share_one_vertex_block);
    RUN_TEST(test_distinct_sprites_do_not_share_blocks);
    RUN_TEST(test_dedup_pack_is_deterministic);
    return UNITY_END();
}
