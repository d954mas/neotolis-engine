/* Pack-level dedup assertions: everything is read back from the produced pack,
 * never from builder internals. */

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
#include <dirent.h>
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

/* Pack the 10-frame fixture single-threaded so the run is byte-deterministic.
 * cache may be NULL; per_frame_dedup may be NULL (every frame inherits). */
static bool build_dedup_pack_opts(const char *path, const char *atlas_name, const char *cache, bool dedup, const uint8_t *per_frame_dedup) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    if (cache != NULL) {
        (void)MKDIR(cache);
        nt_builder_set_cache_dir(ctx, cache);
    }
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.dedup = dedup;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, atlas_name, &opts);
    atlas_dedup_fixture_add_opts(atlas, per_frame_dedup);
    nt_build_result_t commit = nt_atlas_commit(atlas);
    nt_build_result_t finish = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return commit == NT_BUILD_OK && finish == NT_BUILD_OK;
}

static bool build_dedup_pack(const char *path, const char *atlas_name) { return build_dedup_pack_opts(path, atlas_name, NULL, true, NULL); }

/* Collect the fixture's regions from a pack built with the given dedup settings. */
static void collect_fixture_opts(const char *path, const char *atlas_name, bool dedup, const uint8_t *per_frame_dedup, nt_atlas_dedup_region_t *out, uint32_t *out_count) {
    TEST_ASSERT_TRUE_MESSAGE(build_dedup_pack_opts(path, atlas_name, NULL, dedup, per_frame_dedup), "dedup fixture pack failed");
    size_t len = 0;
    uint8_t *bytes = read_bin_file(path, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(bytes, "read produced pack");
    const bool ok = atlas_dedup_collect_regions(bytes, len, out, NT_ATLAS_DEDUP_FRAME_COUNT, out_count);
    free(bytes);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_FRAME_COUNT, *out_count, "one region per frame");
}

/* One atlas_<key>.bin per distinct cache key; the texture-encode cache uses a
 * different name shape, so counting the prefix counts atlas keys. */
static uint32_t count_atlas_cache_files(const char *dir) {
    uint32_t n = 0;
#ifdef _WIN32
    char pattern[512];
    (void)snprintf(pattern, sizeof(pattern), "%s\\atlas_*.bin", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        ++n;
    } while (FindNextFileA(h, &fd));
    (void)FindClose(h);
#else
    DIR *d = opendir(dir);
    if (d == NULL) {
        return 0;
    }
    const struct dirent *ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "atlas_", 6) == 0) {
            ++n;
        }
    }
    (void)closedir(d);
#endif
    return n;
}

/* Remove prior atlas cache files so the count reflects this run only. */
static void clear_atlas_cache_files(const char *dir) {
#ifdef _WIN32
    char pattern[512];
    (void)snprintf(pattern, sizeof(pattern), "%s\\atlas_*.bin", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        char p[512];
        (void)snprintf(p, sizeof(p), "%s\\%s", dir, fd.cFileName);
        (void)DeleteFileA(p);
    } while (FindNextFileA(h, &fd));
    (void)FindClose(h);
#else
    DIR *d = opendir(dir);
    if (d == NULL) {
        return;
    }
    const struct dirent *ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "atlas_", 6) == 0) {
            char p[512];
            (void)snprintf(p, sizeof(p), "%s/%s", dir, ent->d_name);
            (void)remove(p);
        }
    }
    (void)closedir(d);
#endif
}

static bool packs_differ(const char *path_a, const char *path_b) {
    size_t len_a = 0;
    size_t len_b = 0;
    uint8_t *a = read_bin_file(path_a, &len_a);
    uint8_t *b = read_bin_file(path_b, &len_b);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    const bool differ = (len_a != len_b) || memcmp(a, b, len_a) != 0;
    free(a);
    free(b);
    return differ;
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

/* The DEDUP-01 headline: the 10 frames carry 4 art states on 10 different
 * canvases, so nothing but a post-trim key can bring them together. */
void test_post_trim_identical_frames_share_four_placements(void) {
    nt_atlas_dedup_region_t regions[NT_ATLAS_DEDUP_FRAME_COUNT] = {0};
    uint32_t count = 0;
    collect_fixture_opts(TMP_DIR "/dedup_post_trim.ntpack", "dedup_post_trim", true, NULL, regions, &count);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_STATE_COUNT, atlas_dedup_distinct_placements(regions, count), "10 post-trim-identical frames must share 4 placements");
    /* Frames of one art state land on one rectangle; different states do not. */
    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t j = i + 1; j < count; ++j) {
            const bool same_place = regions[i].page_index == regions[j].page_index && regions[i].u_min == regions[j].u_min && regions[i].v_min == regions[j].v_min;
            const bool same_state = k_atlas_dedup_frames[i].state == k_atlas_dedup_frames[j].state;
            TEST_ASSERT_EQUAL_MESSAGE(same_state, same_place, "a placement must be shared by exactly the frames of one art state");
        }
        /* Each alias still carries its own trim offset and pivot. */
        const nt_atlas_dedup_frame_t *f = &k_atlas_dedup_frames[i];
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(f->canvas_w, regions[i].source_w, "an alias keeps its own source width");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(f->canvas_h, regions[i].source_h, "an alias keeps its own source height");
        TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)f->art_x, regions[i].trim_offset_x, "an alias keeps its own trim offset");
        assert_float_bits_equal(f->origin_x, regions[i].origin_x, "an alias keeps its own pivot");
    }
}

/* Also pins the default: the previous case relies on dedup being on by default. */
void test_dedup_disabled_at_atlas_level_gives_ten_placements(void) {
    TEST_ASSERT_TRUE_MESSAGE(nt_atlas_opts_defaults().dedup, "dedup must default to on");
    nt_atlas_dedup_region_t regions[NT_ATLAS_DEDUP_FRAME_COUNT] = {0};
    uint32_t count = 0;
    collect_fixture_opts(TMP_DIR "/dedup_off_atlas.ntpack", "dedup_off_atlas", false, NULL, regions, &count);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_FRAME_COUNT, atlas_dedup_distinct_placements(regions, count), "dedup off must give every frame its own placement");
}

/* Frame 3 shares art state 0 with frames 0 and 6, so marking it off proves both
 * directions at once: it is neither an alias nor an alias target. */
void test_sprite_dedup_off_is_bidirectional(void) {
    static const uint8_t per_frame[NT_ATLAS_DEDUP_FRAME_COUNT] = {0, 0, 0, NT_ATLAS_SPRITE_DEDUP_OFF, 0, 0, 0, 0, 0, 0};
    nt_atlas_dedup_region_t regions[NT_ATLAS_DEDUP_FRAME_COUNT] = {0};
    uint32_t count = 0;
    collect_fixture_opts(TMP_DIR "/dedup_off_sprite.ntpack", "dedup_off_sprite", true, per_frame, regions, &count);
    const uint32_t off = 3;
    for (uint32_t i = 0; i < count; ++i) {
        if (i == off) {
            continue;
        }
        const bool same_place = regions[i].page_index == regions[off].page_index && regions[i].u_min == regions[off].u_min && regions[i].v_min == regions[off].v_min;
        TEST_ASSERT_FALSE_MESSAGE(same_place, "a dedup-off sprite must keep a private placement");
    }
    /* The rest of its state is unaffected. */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[0].page_index, regions[6].page_index, "the remaining members of the state must still share a page");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[0].u_min, regions[6].u_min, "the remaining members of the state must still share a placement U");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[0].v_min, regions[6].v_min, "the remaining members of the state must still share a placement V");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_STATE_COUNT + 1, atlas_dedup_distinct_placements(regions, count), "exactly one placement is added by the off sprite");
}

/* A shared cache that ignored the flags would replay the first run's placements
 * and hand back an identical pack. */
void test_dedup_flags_change_the_atlas_cache_key(void) {
    const char *cache = TMP_DIR "/dedup_flag_cache";
    const char *on = TMP_DIR "/dedup_flag_on.ntpack";
    const char *off = TMP_DIR "/dedup_flag_off.ntpack";
    const char *sprite_off = TMP_DIR "/dedup_flag_sprite_off.ntpack";
    static const uint8_t per_frame[NT_ATLAS_DEDUP_FRAME_COUNT] = {0, 0, 0, NT_ATLAS_SPRITE_DEDUP_OFF, 0, 0, 0, 0, 0, 0};
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    (void)MKDIR(cache);
    clear_atlas_cache_files(cache);
    TEST_ASSERT_EQUAL_UINT32(0, count_atlas_cache_files(cache));
    TEST_ASSERT_TRUE(build_dedup_pack_opts(on, "dedup_flag", cache, true, NULL));
    TEST_ASSERT_TRUE(build_dedup_pack_opts(off, "dedup_flag", cache, false, NULL));
    TEST_ASSERT_TRUE(build_dedup_pack_opts(sprite_off, "dedup_flag", cache, true, per_frame));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, count_atlas_cache_files(cache), "each dedup spelling must occupy its own cache key");
    TEST_ASSERT_TRUE_MESSAGE(packs_differ(on, off), "the atlas dedup flag must change the packed output");
    TEST_ASSERT_TRUE_MESSAGE(packs_differ(on, sprite_off), "a per-sprite dedup override must change the packed output");
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

/* The root of a group is the member stored at identity, and a transposed member
 * carries the relative transform — so the region transforms read straight back
 * which sprite the group was rooted at. Swapping the add order moves it. */
void test_alias_root_is_lowest_add_index(void) {
    static const block_sprite_t wide_first[3] = {
        {"root_w0", 20, 14, 120, 180, 60},
        {"root_t1", 14, 20, 120, 180, 60},
        {"root_w2", 20, 14, 120, 180, 60},
    };
    static const block_sprite_t tall_first[3] = {
        {"root2_t0", 14, 20, 120, 180, 60},
        {"root2_w1", 20, 14, 120, 180, 60},
        {"root2_w2", 20, 14, 120, 180, 60},
    };
    nt_atlas_dedup_region_t regions[BLOCK_SPRITE_MAX_COUNT] = {0};
    collect_block_pack(TMP_DIR "/dedup_root_wide.ntpack", "dedup_root_wide", wide_first, 3, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 3), "a transposed twin must share the placement");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, regions[0].transform, "the first-added member is the root and is stored at identity");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_TRANSPOSE, regions[1].transform, "the transposed member records its relative transform");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, regions[2].transform, "a same-orientation member aliases at identity");

    nt_atlas_dedup_region_t swapped[BLOCK_SPRITE_MAX_COUNT] = {0};
    collect_block_pack(TMP_DIR "/dedup_root_tall.ntpack", "dedup_root_tall", tall_first, 3, swapped);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(swapped, 3), "a transposed twin must share the placement");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, swapped[0].transform, "the root follows the add order, not the sprite dims");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_TRANSPOSE, swapped[1].transform, "both later members now need the relative transform");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_TRANSPOSE, swapped[2].transform, "both later members now need the relative transform");
}

/* A mirrored relative is the only case that separates the texel mapping
 * (w-1-x) from the corner mapping (w-x), so the pair is an asymmetric L and its
 * explicit horizontal mirror — built with w-1-x here rather than through the
 * builder helper the match itself uses. */
#define MIRROR_ART_W 16U
#define MIRROR_ART_H 12U

static void fill_mirror_art(uint8_t *px, bool mirrored) {
    for (uint32_t y = 0; y < MIRROR_ART_H; ++y) {
        for (uint32_t x = 0; x < MIRROR_ART_W; ++x) {
            const uint32_t sx = mirrored ? (MIRROR_ART_W - 1U - x) : x;
            nt_atlas_dedup_art_pixel(3U, sx, y, px + ((size_t)((y * MIRROR_ART_W) + x) * 4U));
        }
    }
}

void test_mirrored_twin_folds_with_its_relative_transform(void) {
    const char *path = TMP_DIR "/dedup_mirror.ntpack";
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "dedup_mirror", &opts);
    uint8_t px[MIRROR_ART_W * MIRROR_ART_H * 4U];
    static const char *names[2] = {"L_base", "L_mirror"};
    for (uint32_t i = 0; i < 2; ++i) {
        fill_mirror_art(px, i == 1);
        nt_atlas_sprite_opts_t sopts = nt_atlas_sprite_opts_defaults();
        sopts.name = names[i];
        nt_atlas_add_raw(atlas, px, (uint16_t)MIRROR_ART_W, (uint16_t)MIRROR_ART_H, &sopts);
    }
    TEST_ASSERT_EQUAL_INT(NT_BUILD_OK, nt_atlas_commit(atlas));
    TEST_ASSERT_EQUAL_INT(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    size_t len = 0;
    uint8_t *bytes = read_bin_file(path, &len);
    TEST_ASSERT_NOT_NULL(bytes);
    nt_atlas_dedup_region_t regions[2] = {0};
    uint32_t count = 0;
    const bool ok = atlas_dedup_collect_regions(bytes, len, regions, 2, &count);
    free(bytes);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT32(2, count);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 2), "a mirrored twin must share the placement");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_IDENTITY, regions[0].transform, "the root is stored at identity");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_FLIP_H, regions[1].transform, "the mirrored twin records flipH as its relative transform");
}

/* Resolved-value grouping and slice9 safety are decided by the atlas options and
 * the per-sprite overrides, so these cases drive one solid rect per sprite and
 * vary only the spelling under test. */
#define RESOLVED_SPRITE_MAX_DIM 48
#define RESOLVED_SPRITE_MAX_PX (RESOLVED_SPRITE_MAX_DIM * RESOLVED_SPRITE_MAX_DIM * 4)
#define RESOLVED_SPRITE_MAX_COUNT 6

typedef struct {
    const char *name;
    uint16_t w;
    uint16_t h;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t margin;          /* per-sprite margin override (0 = inherit) */
    uint16_t slice9_lrtb[4]; /* all zero = no slice9 */
} resolved_sprite_t;

static bool build_resolved_pack(const char *path, const char *atlas_name, const nt_atlas_opts_t *opts, const resolved_sprite_t *sprites, uint32_t count) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, atlas_name, opts);
    uint8_t px[RESOLVED_SPRITE_MAX_PX];
    for (uint32_t i = 0; i < count; ++i) {
        const resolved_sprite_t *s = &sprites[i];
        const uint32_t texels = (uint32_t)s->w * (uint32_t)s->h;
        for (uint32_t t = 0; t < texels; ++t) {
            px[(t * 4U) + 0U] = s->r;
            px[(t * 4U) + 1U] = s->g;
            px[(t * 4U) + 2U] = s->b;
            px[(t * 4U) + 3U] = 255U;
        }
        nt_atlas_sprite_opts_t sopts = nt_atlas_sprite_opts_defaults();
        sopts.name = s->name;
        sopts.margin = s->margin;
        sopts.slice9_left = s->slice9_lrtb[0];
        sopts.slice9_right = s->slice9_lrtb[1];
        sopts.slice9_top = s->slice9_lrtb[2];
        sopts.slice9_bottom = s->slice9_lrtb[3];
        nt_atlas_add_raw(atlas, px, s->w, s->h, &sopts);
    }
    nt_build_result_t commit = nt_atlas_commit(atlas);
    nt_build_result_t finish = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return commit == NT_BUILD_OK && finish == NT_BUILD_OK;
}

static void collect_resolved_pack(const char *path, const char *atlas_name, const nt_atlas_opts_t *opts, const resolved_sprite_t *sprites, uint32_t count, nt_atlas_dedup_region_t *out) {
    TEST_ASSERT_TRUE_MESSAGE(build_resolved_pack(path, atlas_name, opts, sprites, count), "resolved-value pack failed");
    size_t len = 0;
    uint8_t *bytes = read_bin_file(path, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(bytes, "read produced pack");
    uint32_t got = 0;
    const bool ok = atlas_dedup_collect_regions(bytes, len, out, count, &got);
    free(bytes);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(count, got, "one region per sprite");
}

/* margin 0 (inherit) and margin 4 (explicit) resolve to the same footprint, so
 * they group; margin 9 resolves higher and must keep its own placement. */
void test_resolved_margin_groups_equivalent_spellings(void) {
    static const resolved_sprite_t sprites[3] = {
        {"margin_inherit", 20, 14, 210, 120, 40, 0, {0, 0, 0, 0}},
        {"margin_explicit", 20, 14, 210, 120, 40, 4, {0, 0, 0, 0}},
        {"margin_raised", 20, 14, 210, 120, 40, 9, {0, 0, 0, 0}},
    };
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.margin = 4;
    nt_atlas_dedup_region_t regions[RESOLVED_SPRITE_MAX_COUNT] = {0};
    collect_resolved_pack(TMP_DIR "/dedup_resolved_margin.ntpack", "dedup_resolved_margin", &opts, sprites, 3, regions);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[0].page_index, regions[1].page_index, "equivalent margin spellings must share a page");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[0].u_min, regions[1].u_min, "equivalent margin spellings must share a placement U");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[0].v_min, regions[1].v_min, "equivalent margin spellings must share a placement V");
    /* A higher resolved margin is a different packing footprint, not a spelling. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, atlas_dedup_distinct_placements(regions, 3), "a raised margin must keep its own placement");
}

/* Borders are per-region metadata written from each sprite's own data, so the
 * same art with different borders is one placement and two border sets. */
void test_slice9_same_art_different_borders_share_placement(void) {
    static const resolved_sprite_t sprites[2] = {
        {"panel_even", 24, 40, 180, 90, 60, 0, {4, 4, 4, 4}},
        {"panel_odd", 24, 40, 180, 90, 60, 0, {6, 2, 8, 3}},
    };
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    nt_atlas_dedup_region_t regions[RESOLVED_SPRITE_MAX_COUNT] = {0};
    collect_resolved_pack(TMP_DIR "/dedup_slice9_borders.ntpack", "dedup_slice9_borders", &opts, sprites, 2, regions);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 2), "same art must share one placement");
    for (uint32_t i = 0; i < 2; ++i) {
        for (uint32_t k = 0; k < 4; ++k) {
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(sprites[i].slice9_lrtb[k], regions[i].slice9_lrtb[k], "each region must keep its own borders");
        }
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, regions[i].transform, "slice9 regions must be stored at identity");
    }
}

/* The case the group intersection exists for: a nine-patch resolves to
 * identity-only, so a plain twin under an unrestricted atlas mask cannot drag
 * the shared placement into a rotation the runtime would reject. */
void test_slice9_group_with_plain_sprite_stays_identity(void) {
    static const resolved_sprite_t sprites[5] = {
        {"tall_a", 8, 44, 180, 90, 60, 0, {0, 0, 0, 0}},     {"tall_b", 8, 44, 190, 90, 60, 0, {0, 0, 0, 0}}, {"wide_plain", 44, 8, 200, 90, 60, 0, {0, 0, 0, 0}},
        {"wide_panel", 44, 8, 200, 90, 60, 0, {4, 4, 2, 2}}, {"tall_c", 8, 40, 210, 90, 60, 0, {0, 0, 0, 0}},
    };
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    /* RECT so the nine-patch and its plain twin resolve to the same shape. */
    opts.shape = NT_ATLAS_SHAPE_RECT;
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ALL;
    nt_atlas_dedup_region_t regions[RESOLVED_SPRITE_MAX_COUNT] = {0};
    collect_resolved_pack(TMP_DIR "/dedup_slice9_group.ntpack", "dedup_slice9_group", &opts, sprites, 5, regions);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[2].u_min, regions[3].u_min, "the nine-patch must alias onto its plain twin");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[2].v_min, regions[3].v_min, "the nine-patch must alias onto its plain twin");
    for (uint32_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, regions[i].transform, "a group containing a nine-patch must pack at identity");
    }
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
    RUN_TEST(test_post_trim_identical_frames_share_four_placements);
    RUN_TEST(test_dedup_disabled_at_atlas_level_gives_ten_placements);
    RUN_TEST(test_sprite_dedup_off_is_bidirectional);
    RUN_TEST(test_dedup_flags_change_the_atlas_cache_key);
    RUN_TEST(test_identity_aliases_share_one_vertex_block);
    RUN_TEST(test_distinct_sprites_do_not_share_blocks);
    RUN_TEST(test_alias_root_is_lowest_add_index);
    RUN_TEST(test_mirrored_twin_folds_with_its_relative_transform);
    RUN_TEST(test_resolved_margin_groups_equivalent_spellings);
    RUN_TEST(test_slice9_same_art_different_borders_share_placement);
    RUN_TEST(test_slice9_group_with_plain_sprite_stays_identity);
    RUN_TEST(test_dedup_pack_is_deterministic);
    return UNITY_END();
}
