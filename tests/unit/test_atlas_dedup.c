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

/* Unity is built with UNITY_EXCLUDE_FLOAT; bit equality is also the stronger
 * claim here — the builder derives the pivot from the same float we passed in. */
static void assert_float_bits_equal(float expect, float actual, const char *msg) {
    uint32_t e = 0;
    uint32_t a = 0;
    memcpy(&e, &expect, sizeof(e));
    memcpy(&a, &actual, sizeof(a));
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(e, a, msg);
}

/* The statistics view is owned by the ctx and dangles after nt_builder_free_pack,
 * so every case reads it through this helper while the ctx is still alive. */
static void read_last_atlas_stats(const NtBuilderContext *ctx, nt_atlas_stats_t *out) {
    if (out == NULL) {
        return;
    }
    const nt_atlas_stats_t empty = {0};
    uint32_t n = 0;
    const nt_atlas_stats_t *stats = nt_builder_get_atlas_stats(ctx, &n);
    *out = (n > 0) ? stats[n - 1] : empty;
}

/* Pack the 10-frame fixture single-threaded so the run is byte-deterministic.
 * cache may be NULL; per_frame_dedup may be NULL (every frame inherits);
 * out_stats may be NULL. */
static bool build_dedup_pack_opts(const char *path, const char *atlas_name, const char *cache, bool dedup, const uint8_t *per_frame_dedup, nt_atlas_stats_t *out_stats) {
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
    read_last_atlas_stats(ctx, out_stats);
    nt_builder_free_pack(ctx);
    return commit == NT_BUILD_OK && finish == NT_BUILD_OK;
}

static bool build_dedup_pack(const char *path, const char *atlas_name) { return build_dedup_pack_opts(path, atlas_name, NULL, true, NULL, NULL); }

/* Collect the fixture's regions from a pack built with the given dedup settings. */
static void collect_fixture_opts(const char *path, const char *atlas_name, bool dedup, const uint8_t *per_frame_dedup, nt_atlas_dedup_region_t *out, uint32_t *out_count) {
    TEST_ASSERT_TRUE_MESSAGE(build_dedup_pack_opts(path, atlas_name, NULL, dedup, per_frame_dedup, NULL), "dedup fixture pack failed");
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
    while ((ent = readdir(d)) != NULL) { // NOLINT(concurrency-mt-unsafe)
        /* Suffix too — a future sidecar (.tmp/.meta) must not skew the POSIX count
         * while the Windows glob ignores it. */
        const size_t name_len = strlen(ent->d_name);
        if (strncmp(ent->d_name, "atlas_", 6) == 0 && name_len > 4 && strcmp(ent->d_name + name_len - 4, ".bin") == 0) {
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
    while ((ent = readdir(d)) != NULL) { // NOLINT(concurrency-mt-unsafe)
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
    /* Smoke bounds only — the exact fold pin lives in the headline test below. */
    const uint32_t placements = atlas_dedup_distinct_placements(regions, count);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_STATE_COUNT, placements, "fewer placements than distinct art states");
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_FRAME_COUNT, placements, "more placements than frames");
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
        for (uint32_t k = 0; k < 4; ++k) {
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, regions[i].slice9_lrtb[k], "fixture has no slice9 borders");
        }
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

/* The runtime bakes trim_offset into cached_pos[], indexed by vertex_start, so two
 * regions on one byte range would silently overwrite each other's positions — the
 * later one wins and the earlier sprite renders displaced. Post-trim dedup is what
 * made differing trim offsets reachable inside a group, so pin it here. */
void test_shared_vertex_block_implies_equal_trim_offset(void) {
    nt_atlas_dedup_region_t regions[NT_ATLAS_DEDUP_FRAME_COUNT] = {0};
    uint32_t count = 0;
    collect_fixture_opts(TMP_DIR "/dedup_block_trim.ntpack", "dedup_block_trim", true, NULL, regions, &count);
    TEST_ASSERT_EQUAL_UINT32(NT_ATLAS_DEDUP_FRAME_COUNT, count);
    /* Vacuous unless the fixture really folds — it shares 4 placements over 10 frames. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_STATE_COUNT, atlas_dedup_distinct_placements(regions, count), "the fixture must fold before this proves anything");
    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t j = i + 1; j < count; ++j) {
            if (regions[i].vertex_start != regions[j].vertex_start) {
                continue;
            }
            TEST_ASSERT_EQUAL_INT16_MESSAGE(regions[i].trim_offset_x, regions[j].trim_offset_x, "regions sharing a vertex block must agree on trim_offset_x");
            TEST_ASSERT_EQUAL_INT16_MESSAGE(regions[i].trim_offset_y, regions[j].trim_offset_y, "regions sharing a vertex block must agree on trim_offset_y");
        }
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

/* Frame 3 is added after frames 0 and 6 of its state, so the case above can only
 * prove the member side. Frame 0 is the state's lowest add index — the root the
 * others would alias onto — so marking it off tests the target side. */
void test_sprite_dedup_off_first_is_not_a_target(void) {
    static const uint8_t per_frame[NT_ATLAS_DEDUP_FRAME_COUNT] = {NT_ATLAS_SPRITE_DEDUP_OFF, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    nt_atlas_dedup_region_t regions[NT_ATLAS_DEDUP_FRAME_COUNT] = {0};
    uint32_t count = 0;
    collect_fixture_opts(TMP_DIR "/dedup_off_first.ntpack", "dedup_off_first", true, per_frame, regions, &count);
    for (uint32_t i = 1; i < count; ++i) {
        const bool same_place = regions[i].page_index == regions[0].page_index && regions[i].u_min == regions[0].u_min && regions[i].v_min == regions[0].v_min;
        TEST_ASSERT_FALSE_MESSAGE(same_place, "a dedup-off sprite must not become an alias target");
    }
    /* The state re-roots at the next eligible member instead of splitting. */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[3].page_index, regions[6].page_index, "the remaining members must re-root onto one placement");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[3].u_min, regions[6].u_min, "the remaining members must re-root onto one placement");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[3].v_min, regions[6].v_min, "the remaining members must re-root onto one placement");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_STATE_COUNT + 1, atlas_dedup_distinct_placements(regions, count), "exactly one placement is added by the off sprite");
}

/* The override is two-sided: an atlas-level off must not veto a per-sprite on.
 * Frames 0 and 6 are the same art state, so exactly they may fold. */
void test_sprite_dedup_on_overrides_atlas_off(void) {
    static const uint8_t per_frame[NT_ATLAS_DEDUP_FRAME_COUNT] = {NT_ATLAS_SPRITE_DEDUP_ON, 0, 0, 0, 0, 0, NT_ATLAS_SPRITE_DEDUP_ON, 0, 0, 0};
    nt_atlas_dedup_region_t regions[NT_ATLAS_DEDUP_FRAME_COUNT] = {0};
    uint32_t count = 0;
    collect_fixture_opts(TMP_DIR "/dedup_on_sprite.ntpack", "dedup_on_sprite", false, per_frame, regions, &count);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[0].page_index, regions[6].page_index, "two dedup-on sprites must share a page under an atlas-level off");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[0].u_min, regions[6].u_min, "two dedup-on sprites must share a placement U under an atlas-level off");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[0].v_min, regions[6].v_min, "two dedup-on sprites must share a placement V under an atlas-level off");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_FRAME_COUNT - 1, atlas_dedup_distinct_placements(regions, count), "only the two overridden sprites may fold");
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
    TEST_ASSERT_TRUE(build_dedup_pack_opts(on, "dedup_flag", cache, true, NULL, NULL));
    TEST_ASSERT_TRUE(build_dedup_pack_opts(off, "dedup_flag", cache, false, NULL, NULL));
    TEST_ASSERT_TRUE(build_dedup_pack_opts(sprite_off, "dedup_flag", cache, true, per_frame, NULL));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, count_atlas_cache_files(cache), "each dedup spelling must occupy its own cache key");
    TEST_ASSERT_TRUE_MESSAGE(packs_differ(on, off), "the atlas dedup flag must change the packed output");
    TEST_ASSERT_TRUE_MESSAGE(packs_differ(on, sprite_off), "a per-sprite dedup override must change the packed output");
}

/* The other half of the cache contract: the same key twice must be a real hit
 * that replays the aliased placements, not just a second miss under one name. */
void test_atlas_cache_hit_replays_the_aliases(void) {
    const char *cache = TMP_DIR "/dedup_hit_cache";
    const char *first = TMP_DIR "/dedup_hit_a.ntpack";
    const char *second = TMP_DIR "/dedup_hit_b.ntpack";
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    (void)MKDIR(cache);
    clear_atlas_cache_files(cache);
    nt_atlas_stats_t miss = {0};
    nt_atlas_stats_t hit = {0};
    TEST_ASSERT_TRUE(build_dedup_pack_opts(first, "dedup_hit", cache, true, NULL, &miss));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, count_atlas_cache_files(cache), "the first run must write one key");
    TEST_ASSERT_TRUE(build_dedup_pack_opts(second, "dedup_hit", cache, true, NULL, &hit));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, count_atlas_cache_files(cache), "the second run must reuse the key, not add one");
    TEST_ASSERT_FALSE_MESSAGE(packs_differ(first, second), "a cache hit must reproduce the pack byte for byte");

    /* Without this pair every assertion below holds by construction: dedup,
     * geometry and serialize re-run on both builds and the packer is
     * deterministic, so a build that never READ the cache would look identical. */
    TEST_ASSERT_FALSE_MESSAGE(miss.cache_hit, "the first run must miss the cache");
    TEST_ASSERT_TRUE_MESSAGE(hit.cache_hit, "the second run must replay from the cache, not re-pack");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_STATE_COUNT, hit.placements, "a cache hit must still report the folded placement count");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(miss.placements, hit.placements, "a cache hit must report the same placements");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(miss.folds_exact, hit.folds_exact, "a cache hit must report the same exact folds");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(miss.folds_d4, hit.folds_d4, "a cache hit must report the same D4 folds");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(miss.area_saved_px, hit.area_saved_px, "a cache hit must report the same saved area");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(miss.vertex_blocks_shared, hit.vertex_blocks_shared, "a cache hit must report the same shared blocks");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_FRAME_COUNT, hit.folds_exact + hit.folds_d4 + hit.placements, "every sprite must be a placement or a fold on a cache hit");
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
    uint8_t shape; /* 0 = atlas default; RECT is what makes a QUAD_* hint reachable */
} block_sprite_t;

/* out_stats may be NULL. */
static bool build_block_pack(const char *path, const char *atlas_name, const block_sprite_t *sprites, uint32_t count, nt_atlas_stats_t *out_stats) {
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
        /* The table is data, not a type — a larger entry must fail loudly, not smash. */
        TEST_ASSERT_TRUE_MESSAGE((size_t)texels * 4U <= sizeof(px), "block sprite exceeds BLOCK_SPRITE_MAX_DIM");
        for (uint32_t t = 0; t < texels; ++t) {
            px[(t * 4U) + 0U] = s->r;
            px[(t * 4U) + 1U] = s->g;
            px[(t * 4U) + 2U] = s->b;
            px[(t * 4U) + 3U] = 255U;
        }
        nt_atlas_sprite_opts_t sopts = nt_atlas_sprite_opts_defaults();
        sopts.name = s->name;
        sopts.shape = s->shape;
        nt_atlas_add_raw(atlas, px, s->w, s->h, &sopts);
    }
    nt_build_result_t commit = nt_atlas_commit(atlas);
    nt_build_result_t finish = nt_builder_finish_pack(ctx);
    read_last_atlas_stats(ctx, out_stats);
    nt_builder_free_pack(ctx);
    return commit == NT_BUILD_OK && finish == NT_BUILD_OK;
}

static void collect_block_pack(const char *path, const char *atlas_name, const block_sprite_t *sprites, uint32_t count, nt_atlas_dedup_region_t *out) {
    TEST_ASSERT_TRUE_MESSAGE(build_block_pack(path, atlas_name, sprites, count, NULL), "block-sharing pack failed");
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
        {"block_a0", 16, 12, 220, 60, 60, 0},
        {"block_a1", 16, 12, 220, 60, 60, 0}, /* byte-identical to block_a0 */
        {"block_b", 20, 8, 70, 90, 230, 0},
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
        {"block_c0", 14, 10, 235, 200, 40, 0},
        {"block_c1", 18, 10, 60, 210, 80, 0},
        {"block_c2", 14, 16, 70, 90, 230, 0},
    };
    nt_atlas_dedup_region_t regions[BLOCK_SPRITE_MAX_COUNT] = {0};
    collect_block_pack(TMP_DIR "/dedup_block_distinct.ntpack", "dedup_block_distinct", sprites, 3, regions);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(regions[0].vertex_start, regions[1].vertex_start, "distinct sprites must not share a block");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(regions[0].vertex_start, regions[2].vertex_start, "distinct sprites must not share a block");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(regions[1].vertex_start, regions[2].vertex_start, "distinct sprites must not share a block");
}

/* The OFF guarantee is pixels and placement, not blob bytes: serialize-stage block
 * sharing is byte-equality only and does not consult the off-switch. Alone on its
 * own page at the same (x, y), the OFF twin emits byte-identical vertices and
 * legally shares the read-only range — extending OFF to blob bytes, or re-merging
 * the placements, would each break one of the two pins below. */
void test_sprite_dedup_off_shares_bytes_but_not_placement(void) {
    const char *path = TMP_DIR "/dedup_off_block.ntpack";
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "start_pack failed");
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    /* One 40x40 sprite per 64-page: both twins land at their page's origin, so
     * the emitted UVs — and therefore the blocks — come out byte-identical. */
    opts.max_size = 64;
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_IDENTITY;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "dedup_off_block", &opts);
    static uint8_t px[40 * 40 * 4];
    for (uint32_t t = 0; t < 40U * 40U; ++t) {
        px[(t * 4U) + 0U] = 140U;
        px[(t * 4U) + 1U] = 60U;
        px[(t * 4U) + 2U] = 200U;
        px[(t * 4U) + 3U] = 255U;
    }
    nt_atlas_sprite_opts_t on = nt_atlas_sprite_opts_defaults();
    on.name = "off_twin_on";
    nt_atlas_add_raw(atlas, px, 40, 40, &on);
    nt_atlas_sprite_opts_t off = nt_atlas_sprite_opts_defaults();
    off.name = "off_twin_off";
    off.dedup = NT_ATLAS_SPRITE_DEDUP_OFF;
    nt_atlas_add_raw(atlas, px, 40, 40, &off);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(atlas), "commit failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_builder_finish_pack(ctx), "finish_pack failed");
    nt_builder_free_pack(ctx);

    size_t len = 0;
    uint8_t *bytes = read_bin_file(path, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(bytes, "read produced pack");
    nt_atlas_dedup_region_t regions[2] = {0};
    uint32_t got = 0;
    const bool ok = atlas_dedup_collect_regions(bytes, len, regions, 2, &got);
    free(bytes);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from produced pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, got, "one region per sprite");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(regions[0].page_index, regions[1].page_index, "each twin must sit alone on its own page");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, atlas_dedup_distinct_placements(regions, 2), "OFF must keep a private placement");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(regions[0].vertex_start, regions[1].vertex_start, "byte-identical blocks share one vertex range regardless of the off-switch");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(regions[0].index_start, regions[1].index_start, "byte-identical blocks share one index range regardless of the off-switch");
}

/* The root of a group is the member stored at identity, and a transposed member
 * carries the relative transform — so the region transforms read straight back
 * which sprite the group was rooted at. Swapping the add order moves it. */
void test_alias_root_is_lowest_add_index(void) {
    /* RECT so every region is a 4-vertex quad — the only shape that can carry a
     * QUAD_* hint, and therefore the only one that can prove an alias keeps it. */
    static const block_sprite_t wide_first[3] = {
        {"root_w0", 20, 14, 120, 180, 60, NT_ATLAS_SPRITE_SHAPE_RECT},
        {"root_t1", 14, 20, 120, 180, 60, NT_ATLAS_SPRITE_SHAPE_RECT},
        {"root_w2", 20, 14, 120, 180, 60, NT_ATLAS_SPRITE_SHAPE_RECT},
    };
    static const block_sprite_t tall_first[3] = {
        {"root2_t0", 14, 20, 120, 180, 60, NT_ATLAS_SPRITE_SHAPE_RECT},
        {"root2_w1", 20, 14, 120, 180, 60, NT_ATLAS_SPRITE_SHAPE_RECT},
        {"root2_w2", 20, 14, 120, 180, 60, NT_ATLAS_SPRITE_SHAPE_RECT},
    };
    nt_atlas_dedup_region_t regions[BLOCK_SPRITE_MAX_COUNT] = {0};
    collect_block_pack(TMP_DIR "/dedup_root_wide.ntpack", "dedup_root_wide", wide_first, 3, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 3), "a transposed twin must share the placement");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, regions[0].transform, "the first-added member is the root and is stored at identity");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_TRANSPOSE, regions[1].transform, "the transposed member records its relative transform");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, regions[2].transform, "a same-orientation member aliases at identity");
    /* The canonical ring rotation is what makes a derived alias indistinguishable from
     * a standalone pack, so the QUAD_* hint cannot depend on a member's orientation. */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, regions[1].vertex_count, "a transposed RECT alias stays a quad");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(6, regions[1].index_count, "a transposed RECT alias keeps six indices");
    TEST_ASSERT_TRUE_MESSAGE((regions[0].flags & NT_ATLAS_REGION_FLAG_QUAD_MASK) != 0, "a RECT root must carry a QUAD_* hint");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[0].flags, regions[1].flags, "a transposed alias must carry the root's render flags");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[0].flags, regions[2].flags, "an identity alias must carry the root's render flags");

    nt_atlas_dedup_region_t swapped[BLOCK_SPRITE_MAX_COUNT] = {0};
    collect_block_pack(TMP_DIR "/dedup_root_tall.ntpack", "dedup_root_tall", tall_first, 3, swapped);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(swapped, 3), "a transposed twin must share the placement");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, swapped[0].transform, "the root follows the add order, not the sprite dims");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_TRANSPOSE, swapped[1].transform, "both later members now need the relative transform");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_TRANSPOSE, swapped[2].transform, "both later members now need the relative transform");
    TEST_ASSERT_TRUE_MESSAGE((swapped[0].flags & NT_ATLAS_REGION_FLAG_QUAD_MASK) != 0, "a RECT root must carry a QUAD_* hint");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(swapped[0].flags, swapped[1].flags, "a transposed alias must carry the root's render flags");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(swapped[0].flags, swapped[2].flags, "a transposed alias must carry the root's render flags");
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
    /* Appended so the positional initializers above keep their field mapping. */
    uint8_t extrude;         /* 0 = inherit */
    uint8_t max_vertices;    /* 0 = inherit */
    uint8_t alpha_threshold; /* used only when has_alpha_threshold */
    bool has_alpha_threshold;
    uint8_t shape;                /* 0 = inherit */
    float max_added_area_percent; /* used only when has_max_added_area_percent */
    bool has_max_added_area_percent;
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
        /* The table is data, not a type — a larger entry must fail loudly, not smash. */
        TEST_ASSERT_TRUE_MESSAGE((size_t)texels * 4U <= sizeof(px), "resolved sprite exceeds RESOLVED_SPRITE_MAX_DIM");
        for (uint32_t t = 0; t < texels; ++t) {
            px[(t * 4U) + 0U] = s->r;
            px[(t * 4U) + 1U] = s->g;
            px[(t * 4U) + 2U] = s->b;
            px[(t * 4U) + 3U] = 255U;
        }
        nt_atlas_sprite_opts_t sopts = nt_atlas_sprite_opts_defaults();
        sopts.name = s->name;
        sopts.margin = s->margin;
        sopts.extrude = s->extrude;
        sopts.max_vertices = s->max_vertices;
        sopts.alpha_threshold = s->alpha_threshold;
        sopts.has_alpha_threshold = s->has_alpha_threshold;
        sopts.shape = s->shape;
        sopts.max_added_area_percent = s->max_added_area_percent;
        sopts.has_max_added_area_percent = s->has_max_added_area_percent;
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
        {"margin_inherit", 20, 14, 210, 120, 40, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"margin_explicit", 20, 14, 210, 120, 40, 4, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"margin_raised", 20, 14, 210, 120, 40, 9, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
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

/* Each remaining term of the resolved-value gate gets its own refusal. The art is a
 * solid rect, so all three spellings below select the identical quad — only the gate
 * itself can keep them apart, and dropping any one term silently merges sprites whose
 * geometry was cut to a different budget. */
void test_resolved_geometry_controls_each_refuse_a_fold(void) {
    static const resolved_sprite_t extrude_pair[2] = {
        {"extrude_base", 20, 14, 90, 160, 200, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"extrude_wide", 20, 14, 90, 160, 200, 0, {0, 0, 0, 0}, 3, 0, 0, false, 0, 0.0F, false},
    };
    static const resolved_sprite_t verts_pair[2] = {
        {"verts_base", 20, 14, 90, 160, 200, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"verts_eight", 20, 14, 90, 160, 200, 0, {0, 0, 0, 0}, 0, 8, 0, false, 0, 0.0F, false},
    };
    static const resolved_sprite_t alpha_pair[2] = {
        {"alpha_base", 20, 14, 90, 160, 200, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"alpha_high", 20, 14, 90, 160, 200, 0, {0, 0, 0, 0}, 0, 0, 200, true, 0, 0.0F, false},
    };
    static const resolved_sprite_t shape_pair[2] = {
        {"shape_rect", 20, 14, 90, 160, 200, 0, {0, 0, 0, 0}, 0, 0, 0, false, NT_ATLAS_SPRITE_SHAPE_RECT, 0.0F, false},
        {"shape_convex", 20, 14, 90, 160, 200, 0, {0, 0, 0, 0}, 0, 0, 0, false, NT_ATLAS_SPRITE_SHAPE_CONVEX, 0.0F, false},
    };
    static const resolved_sprite_t area_pair[2] = {
        {"area_zero", 20, 14, 90, 160, 200, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, true},
        {"area_wide", 20, 14, 90, 160, 200, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 25.0F, true},
    };
    /* RECT keeps extrude legal and makes the selected quad independent of the
     * budgets, so a fold here could only come from the gate being too loose. */
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.shape = NT_ATLAS_SHAPE_RECT;
    opts.max_vertices = 4;
    opts.alpha_threshold = 1;

    nt_atlas_dedup_region_t regions[RESOLVED_SPRITE_MAX_COUNT] = {0};
    collect_resolved_pack(TMP_DIR "/dedup_resolved_extrude.ntpack", "dedup_resolved_extrude", &opts, extrude_pair, 2, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, atlas_dedup_distinct_placements(regions, 2), "a different resolved extrude must not fold");

    collect_resolved_pack(TMP_DIR "/dedup_resolved_verts.ntpack", "dedup_resolved_verts", &opts, verts_pair, 2, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, atlas_dedup_distinct_placements(regions, 2), "a different resolved max_vertices must not fold");

    collect_resolved_pack(TMP_DIR "/dedup_resolved_alpha.ntpack", "dedup_resolved_alpha", &opts, alpha_pair, 2, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, atlas_dedup_distinct_placements(regions, 2), "a different resolved alpha threshold must not fold");

    collect_resolved_pack(TMP_DIR "/dedup_resolved_shape.ntpack", "dedup_resolved_shape", &opts, shape_pair, 2, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, atlas_dedup_distinct_placements(regions, 2), "a different resolved shape must not fold");

    collect_resolved_pack(TMP_DIR "/dedup_resolved_area.ntpack", "dedup_resolved_area", &opts, area_pair, 2, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, atlas_dedup_distinct_placements(regions, 2), "a different resolved area budget must not fold");
}

/* The other direction of the resolved-value gate: an inherited knob and an explicit
 * spelling equal to the atlas value are one packing decision and must still fold.
 * A "simplification" back to raw-override comparison keeps every refusal pair red
 * above but silently stops these from folding — a pure density regression. */
void test_resolved_spellings_still_fold(void) {
    static const resolved_sprite_t extrude_pair[2] = {
        {"spell_ex_inherit", 20, 14, 30, 200, 90, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"spell_ex_explicit", 20, 14, 30, 200, 90, 0, {0, 0, 0, 0}, 2, 0, 0, false, 0, 0.0F, false},
    };
    static const resolved_sprite_t verts_pair[2] = {
        {"spell_mv_inherit", 20, 14, 40, 200, 90, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"spell_mv_explicit", 20, 14, 40, 200, 90, 0, {0, 0, 0, 0}, 0, 8, 0, false, 0, 0.0F, false},
    };
    static const resolved_sprite_t alpha_pair[2] = {
        {"spell_at_inherit", 20, 14, 50, 200, 90, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"spell_at_explicit", 20, 14, 50, 200, 90, 0, {0, 0, 0, 0}, 0, 0, 5, true, 0, 0.0F, false},
    };
    static const resolved_sprite_t shape_pair[2] = {
        {"spell_sh_inherit", 20, 14, 60, 200, 90, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"spell_sh_explicit", 20, 14, 60, 200, 90, 0, {0, 0, 0, 0}, 0, 0, 0, false, NT_ATLAS_SPRITE_SHAPE_RECT, 0.0F, false},
    };
    static const resolved_sprite_t area_pair[2] = {
        {"spell_ar_inherit", 20, 14, 70, 200, 90, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"spell_ar_explicit", 20, 14, 70, 200, 90, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 25.0F, true},
    };
    /* Atlas values the explicit spellings repeat verbatim. */
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.shape = NT_ATLAS_SHAPE_RECT;
    opts.max_vertices = 8;
    opts.extrude = 2;
    opts.alpha_threshold = 5;
    opts.max_added_area_percent = 25.0F;

    nt_atlas_dedup_region_t regions[RESOLVED_SPRITE_MAX_COUNT] = {0};
    collect_resolved_pack(TMP_DIR "/dedup_spell_extrude.ntpack", "dedup_spell_extrude", &opts, extrude_pair, 2, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 2), "an explicit extrude equal to the atlas value must still fold");

    collect_resolved_pack(TMP_DIR "/dedup_spell_verts.ntpack", "dedup_spell_verts", &opts, verts_pair, 2, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 2), "an explicit max_vertices equal to the atlas value must still fold");

    collect_resolved_pack(TMP_DIR "/dedup_spell_alpha.ntpack", "dedup_spell_alpha", &opts, alpha_pair, 2, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 2), "an explicit alpha threshold equal to the atlas value must still fold");

    collect_resolved_pack(TMP_DIR "/dedup_spell_shape.ntpack", "dedup_spell_shape", &opts, shape_pair, 2, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 2), "an explicit shape equal to the atlas value must still fold");

    collect_resolved_pack(TMP_DIR "/dedup_spell_area.ntpack", "dedup_spell_area", &opts, area_pair, 2, regions);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, atlas_dedup_distinct_placements(regions, 2), "an explicit area budget equal to the atlas value must still fold");
}

/* Borders are per-region metadata written from each sprite's own data, so the
 * same art with different borders is one placement and two border sets. */
void test_slice9_same_art_different_borders_share_placement(void) {
    static const resolved_sprite_t sprites[2] = {
        {"panel_even", 24, 40, 180, 90, 60, 0, {4, 4, 4, 4}, 0, 0, 0, false, 0, 0.0F, false},
        {"panel_odd", 24, 40, 180, 90, 60, 0, {6, 2, 8, 3}, 0, 0, 0, false, 0, 0.0F, false},
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
        {"tall_a", 8, 44, 180, 90, 60, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},     {"tall_b", 8, 44, 190, 90, 60, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"wide_plain", 44, 8, 200, 90, 60, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false}, {"wide_panel", 44, 8, 200, 90, 60, 0, {4, 4, 2, 2}, 0, 0, 0, false, 0, 0.0F, false},
        {"tall_c", 8, 40, 210, 90, 60, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
    };
    /* A/B control: the same layout with the borders zeroed. The packer must
     * transpose the folded wide pair here, or the identity assert below would
     * pass vacuously on a packer that never rotates. */
    static const resolved_sprite_t control[5] = {
        {"ctl_tall_a", 8, 44, 180, 90, 60, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},     {"ctl_tall_b", 8, 44, 190, 90, 60, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"ctl_wide_plain", 44, 8, 200, 90, 60, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false}, {"ctl_wide_twin", 44, 8, 200, 90, 60, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
        {"ctl_tall_c", 8, 40, 210, 90, 60, 0, {0, 0, 0, 0}, 0, 0, 0, false, 0, 0.0F, false},
    };
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    /* RECT so the nine-patch and its plain twin resolve to the same shape. */
    opts.shape = NT_ATLAS_SHAPE_RECT;
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ALL;
    nt_atlas_dedup_region_t regions[RESOLVED_SPRITE_MAX_COUNT] = {0};
    collect_resolved_pack(TMP_DIR "/dedup_slice9_ctrl.ntpack", "dedup_slice9_ctrl", &opts, control, 5, regions);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, regions[2].transform, "control: the packer must transpose the unbordered wide pair");

    collect_resolved_pack(TMP_DIR "/dedup_slice9_group.ntpack", "dedup_slice9_group", &opts, sprites, 5, regions);

    /* UVs are normalized per page, so equal u/v on different pages is not a fold. */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(regions[2].page_index, regions[3].page_index, "the nine-patch must alias onto its plain twin");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[2].u_min, regions[3].u_min, "the nine-patch must alias onto its plain twin");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(regions[2].v_min, regions[3].v_min, "the nine-patch must alias onto its plain twin");
    /* Only the group is pinned. The three singletons keep the atlas-wide ALL mask,
     * so the packer may legally orient them however it likes. */
    for (uint32_t i = 2; i < 4; ++i) {
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

/* --- Dedup statistics ---
 * The stage split and the saved area exist nowhere but these counters, so each
 * case pins the published value against an independently known input shape. */

static void assert_fold_identity(const nt_atlas_stats_t *s) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(s->sprites, s->folds_exact + s->folds_d4 + s->placements, "every sprite must be either a placement or a fold");
}

void test_stats_fold_identity_holds(void) {
    nt_atlas_stats_t s = {0};
    TEST_ASSERT_TRUE_MESSAGE(build_dedup_pack_opts(TMP_DIR "/dedup_stats.ntpack", "dedup_stats", NULL, true, NULL, &s), "stats pack failed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_FRAME_COUNT, s.sprites, "the fixture adds ten frames");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_STATE_COUNT, s.placements, "four art states must give four placements");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6, s.folds_exact, "six frames fold at relative transform identity");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.folds_d4, "no frame needs a D4 transform to fold");
    /* Every fold saves exactly one art box. */
    TEST_ASSERT_EQUAL_UINT64_MESSAGE((uint64_t)6U * NT_ATLAS_DEDUP_ART_W * NT_ATLAS_DEDUP_ART_H, s.area_saved_px, "saved area must be six art boxes");
    assert_fold_identity(&s);

    /* With dedup off the same input must report the degenerate end of the range. */
    nt_atlas_stats_t off = {0};
    TEST_ASSERT_TRUE_MESSAGE(build_dedup_pack_opts(TMP_DIR "/dedup_stats_off.ntpack", "dedup_stats_off", NULL, false, NULL, &off), "dedup-off stats pack failed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_FRAME_COUNT, off.placements, "dedup off must report one placement per frame");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, off.folds_exact, "dedup off must report no folds");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, off.area_saved_px, "dedup off must save no area");
    assert_fold_identity(&off);
}

/* out_stats may be NULL. The mask covers both the atlas and every sprite, so a
 * member's effective mask is exactly the atlas mask. */
static bool build_f_stats_pack(const char *path, const char *atlas_name, const char *cache, uint8_t mask, const uint8_t *transforms, uint32_t count, nt_atlas_stats_t *out_stats) {
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
    opts.allowed_transforms = mask;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, atlas_name, &opts);
    atlas_dedup_f_fixture_add(atlas, transforms, count, mask, 0U);
    nt_build_result_t commit = nt_atlas_commit(atlas);
    nt_build_result_t finish = nt_builder_finish_pack(ctx);
    read_last_atlas_stats(ctx, out_stats);
    nt_builder_free_pack(ctx);
    return commit == NT_BUILD_OK && finish == NT_BUILD_OK;
}

void test_stats_report_d4_folds(void) {
    static const uint8_t rotations[4] = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_ROT90, NT_ATLAS_XFORM_ROT180, NT_ATLAS_XFORM_ROT270};
    TEST_ASSERT_TRUE_MESSAGE(atlas_dedup_f_images_pairwise_distinct(), "the F probe must stay asymmetric under D4");
    nt_atlas_stats_t s = {0};
    TEST_ASSERT_TRUE_MESSAGE(build_f_stats_pack(TMP_DIR "/dedup_stats_d4.ntpack", "dedup_stats_d4", NULL, NT_ATLAS_TRANSFORMS_ROTATIONS, rotations, 4, &s), "F rotation stats pack failed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, s.sprites, "four rotated copies were added");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, s.placements, "four rotated copies must share one placement");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.folds_exact, "no rotation folds at identity");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, s.folds_d4, "three rotations fold through a D4 transform");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE((uint64_t)3U * NT_ATLAS_F_W * NT_ATLAS_F_H, s.area_saved_px, "saved area must be three F boxes");
    assert_fold_identity(&s);

    /* Same four images, identity-only mask: the counters must follow the mask. */
    nt_atlas_stats_t masked = {0};
    TEST_ASSERT_TRUE_MESSAGE(build_f_stats_pack(TMP_DIR "/dedup_stats_d4_off.ntpack", "dedup_stats_d4_off", NULL, NT_ATLAS_TRANSFORMS_IDENTITY, rotations, 4, &masked),
                             "F identity-mask stats pack failed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, masked.placements, "an identity-only mask must not fold rotations");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, masked.folds_d4, "an identity-only mask must report no D4 folds");
    assert_fold_identity(&masked);
}

void test_d4_cache_hit_replays_nonidentity_aliases(void) {
    static const uint8_t rotations[4] = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_ROT90, NT_ATLAS_XFORM_ROT180, NT_ATLAS_XFORM_ROT270};
    const char *cache = TMP_DIR "/dedup_d4_hit_cache";
    const char *first = TMP_DIR "/dedup_d4_hit_a.ntpack";
    const char *second = TMP_DIR "/dedup_d4_hit_b.ntpack";
    (void)MKDIR(cache);
    clear_atlas_cache_files(cache);

    nt_atlas_stats_t miss = {0};
    nt_atlas_stats_t hit = {0};
    TEST_ASSERT_TRUE(build_f_stats_pack(first, "dedup_d4_hit", cache, NT_ATLAS_TRANSFORMS_ROTATIONS, rotations, 4, &miss));
    TEST_ASSERT_TRUE(build_f_stats_pack(second, "dedup_d4_hit", cache, NT_ATLAS_TRANSFORMS_ROTATIONS, rotations, 4, &hit));
    TEST_ASSERT_FALSE_MESSAGE(miss.cache_hit, "the first D4 build must miss");
    TEST_ASSERT_TRUE_MESSAGE(hit.cache_hit, "the second D4 build must replay cached placements");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, hit.folds_d4, "cache replay must preserve nonidentity aliases");
    TEST_ASSERT_FALSE_MESSAGE(packs_differ(first, second), "D4 cache replay must reproduce the pack byte for byte");
}

/* An in-memory mirror of the shipped anim_heavy corpus: three sequences of 10, 8
 * and 12 frames over 4, 8 and 6 states = 30 frames / 18 states / 12 duplicates,
 * a ratio known from the corpus generator rather than from the builder. */
#define ANIM_CANVAS 64
#define ANIM_FRAME_COUNT 30

/* clang-format off */
static const uint8_t k_anim_states[ANIM_FRAME_COUNT] = {
    0,  1,  2,  3,  3,  2,  1,  0,  1,  2,          /* orb:   10 frames, 4 states */
    10, 11, 12, 13, 14, 15, 16, 17,                 /* spark:  8 frames, 8 states */
    20, 21, 22, 23, 24, 25, 25, 24, 23, 22, 21, 20, /* ring:  12 frames, 6 states */
};
/* clang-format on */

/* A disc whose radius and canvas position vary per state, so a fold has to come
 * through the post-trim key. The red channel is injective in the state, which is
 * what keeps two different states from folding into each other. */
static void fill_anim_frame(uint8_t *px, uint32_t state) {
    memset(px, 0, (size_t)ANIM_CANVAS * ANIM_CANVAS * 4U);
    const int32_t radius = (int32_t)(8U + ((state % 5U) * 3U));
    const int32_t cx = (int32_t)(22U + ((state * 7U) % 20U));
    const int32_t cy = (int32_t)(22U + ((state * 11U) % 20U));
    for (int32_t y = 0; y < ANIM_CANVAS; ++y) {
        for (int32_t x = 0; x < ANIM_CANVAS; ++x) {
            const int32_t dx = x - cx;
            const int32_t dy = y - cy;
            if ((dx * dx) + (dy * dy) > (radius * radius)) {
                continue;
            }
            uint8_t *p = px + ((size_t)((y * ANIM_CANVAS) + x) * 4U);
            p[0] = (uint8_t)(20U + (state * 8U));
            p[1] = 90U;
            p[2] = 140U;
            p[3] = 255U;
        }
    }
}

void test_stats_match_anim_heavy_shaped_corpus(void) {
    const char *path = TMP_DIR "/dedup_stats_anim.ntpack";
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "dedup_stats_anim", &opts);
    static uint8_t px[ANIM_CANVAS * ANIM_CANVAS * 4];
    for (uint32_t i = 0; i < (uint32_t)ANIM_FRAME_COUNT; ++i) {
        fill_anim_frame(px, k_anim_states[i]);
        char name[16];
        (void)snprintf(name, sizeof(name), "anim_%02u", (unsigned)i);
        nt_atlas_sprite_opts_t sopts = nt_atlas_sprite_opts_defaults();
        sopts.name = name;
        nt_atlas_add_raw(atlas, px, (uint16_t)ANIM_CANVAS, (uint16_t)ANIM_CANVAS, &sopts);
    }
    TEST_ASSERT_EQUAL_INT(NT_BUILD_OK, nt_atlas_commit(atlas));
    TEST_ASSERT_EQUAL_INT(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_atlas_stats_t s = {0};
    read_last_atlas_stats(ctx, &s);
    nt_builder_free_pack(ctx);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(30, s.sprites, "the mirror adds thirty frames");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(18, s.placements, "thirty frames over eighteen states must give eighteen placements");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(12, s.folds_exact, "the repeat pattern contributes twelve exact folds");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.folds_d4, "no state is a D4 image of another");
    assert_fold_identity(&s);
}

/* Reading stats[n-1] everywhere would also pass for an implementation that always
 * wrote index 0, or that published a record on a failed commit — so index the array
 * by commit order instead, across a failing commit. The same failure also pins that
 * an UNFITTABLE record describes the sprite it names, not that sprite's group root. */
void test_atlas_stats_and_errors_are_per_commit(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/dedup_stats_multi.ntpack");
    TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "start pack");
    nt_builder_set_threads(ctx, 1);

    static uint8_t px[BLOCK_SPRITE_MAX_PX];
    memset(px, 0xFF, sizeof(px));
    nt_atlas_sprite_opts_t s = nt_atlas_sprite_opts_defaults();

    nt_atlas_opts_t o1 = nt_atlas_opts_defaults();
    NtAtlasBuild *a1 = nt_atlas_begin(ctx, "stats_multi_1", &o1);
    s.name = "m1_a";
    nt_atlas_add_raw(a1, px, 16, 12, &s);
    s.name = "m1_b"; /* byte-identical to m1_a */
    nt_atlas_add_raw(a1, px, 16, 12, &s);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(a1), "the first atlas must commit");

    /* A transposed pair that folds into one group, on a page too small for it. */
    nt_atlas_opts_t o2 = nt_atlas_opts_defaults();
    o2.max_size = 16;
    NtAtlasBuild *a2 = nt_atlas_begin(ctx, "stats_multi_fail", &o2);
    s.name = "m2_wide";
    nt_atlas_add_raw(a2, px, 24, 18, &s);
    s.name = "m2_tall";
    nt_atlas_add_raw(a2, px, 18, 24, &s);
    TEST_ASSERT_TRUE_MESSAGE(nt_atlas_commit(a2) != NT_BUILD_OK, "an unfittable group must fail its commit");

    nt_atlas_opts_t o3 = nt_atlas_opts_defaults();
    NtAtlasBuild *a3 = nt_atlas_begin(ctx, "stats_multi_3", &o3);
    s.name = "m3_a";
    nt_atlas_add_raw(a3, px, 14, 10, &s);
    s.name = "m3_b"; /* different dimensions, so nothing folds */
    nt_atlas_add_raw(a3, px, 18, 10, &s);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(a3), "the third atlas must commit");

    /* Same pair at a page size that fits: proves the failing atlas above really did
     * form a D4 group, so its per-sprite error records are not vacuously correct. */
    nt_atlas_opts_t o4 = nt_atlas_opts_defaults();
    NtAtlasBuild *a4 = nt_atlas_begin(ctx, "stats_multi_group", &o4);
    s.name = "m4_wide";
    nt_atlas_add_raw(a4, px, 24, 18, &s);
    s.name = "m4_tall";
    nt_atlas_add_raw(a4, px, 18, 24, &s);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(a4), "the fitting group must commit");

    uint32_t n = 0;
    const nt_atlas_stats_t *stats = nt_builder_get_atlas_stats(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, n, "a failed commit must publish no stats record");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stats[2].folds_d4, "the transposed pair folds through a D4 transform");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stats[2].placements, "the transposed pair shares one placement");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, stats[0].sprites, "record 0 is the first committed atlas");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stats[0].folds_exact, "the identical pair folds once");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, stats[0].placements, "the identical pair shares one placement");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, stats[1].sprites, "record 1 is the third commit, not the failed one");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, stats[1].folds_exact, "differently sized sprites cannot fold");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, stats[1].placements, "differently sized sprites need two placements");
    /* out_count is documented as optional. */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(stats, nt_builder_get_atlas_stats(ctx, NULL), "a NULL out_count must still return the array");

    uint32_t err_count = 0;
    const nt_build_error_t *errors = nt_builder_get_errors(ctx, &err_count);
    TEST_ASSERT_TRUE_MESSAGE(err_count >= 2, "both members of the unfittable group must be reported");
    bool saw_wide = false;
    bool saw_tall = false;
    for (uint32_t i = 0; i < err_count; ++i) {
        const nt_build_error_t *e = &errors[i];
        if (e->kind != NT_BUILD_ERR_KIND_ATLAS_UNFITTABLE) {
            continue;
        }
        if (strcmp(e->sprite, "m2_wide") == 0) {
            saw_wide = true;
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(24, e->w, "the record must carry the named sprite's own width");
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(18, e->h, "the record must carry the named sprite's own height");
        } else if (strcmp(e->sprite, "m2_tall") == 0) {
            saw_tall = true;
            /* The alias is transposed: its root's dims would be the swapped pair. */
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(18, e->w, "an alias must not be told its root's swapped width");
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(24, e->h, "an alias must not be told its root's swapped height");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_wide && saw_tall, "both group members must appear in the error channel");

    nt_builder_free_pack(ctx);
}

void test_stats_report_shared_vertex_blocks(void) {
    /* Every post-trim frame sits on its own canvas at its own offset, so no two
     * regions may share a byte range: the runtime bakes trim_offset into
     * cached_pos[], which is indexed by vertex_start. Folding the placement is
     * still correct and still happens — only the block sharing is withheld. */
    nt_atlas_stats_t s = {0};
    TEST_ASSERT_TRUE_MESSAGE(build_dedup_pack_opts(TMP_DIR "/dedup_stats_blocks.ntpack", "dedup_stats_blocks", NULL, true, NULL, &s), "block stats pack failed");
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, s.folds_exact, "the fixture must fold at least one placement");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.vertex_blocks_shared, "frames with distinct trim offsets must not share a vertex block");

    /* Same art on the same canvas at the same offset — now sharing is allowed. */
    static const block_sprite_t twins[2] = {
        {"stats_t0", 16, 12, 220, 60, 60, 0},
        {"stats_t1", 16, 12, 220, 60, 60, 0},
    };
    nt_atlas_stats_t shared = {0};
    TEST_ASSERT_TRUE_MESSAGE(build_block_pack(TMP_DIR "/dedup_stats_twin_blocks.ntpack", "dedup_stats_twin_blocks", twins, 2, &shared), "twin stats pack failed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, shared.folds_exact, "the twins must fold onto one placement");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(shared.folds_exact, shared.vertex_blocks_shared, "an identity fold at equal trim offsets shares its root's block");
    assert_fold_identity(&shared);

    static const block_sprite_t distinct[3] = {
        {"stats_c0", 14, 10, 235, 200, 40, 0},
        {"stats_c1", 18, 10, 60, 210, 80, 0},
        {"stats_c2", 14, 16, 70, 90, 230, 0},
    };
    nt_atlas_stats_t none = {0};
    TEST_ASSERT_TRUE_MESSAGE(build_block_pack(TMP_DIR "/dedup_stats_no_blocks.ntpack", "dedup_stats_no_blocks", distinct, 3, &none), "distinct-sprite stats pack failed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, none.placements, "three distinct sprites must not fold");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, none.vertex_blocks_shared, "visually distinct sprites must not share a block");
    assert_fold_identity(&none);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dedup_fixture_builds);
    RUN_TEST(test_dedup_region_metadata_is_per_sprite);
    RUN_TEST(test_post_trim_identical_frames_share_four_placements);
    RUN_TEST(test_shared_vertex_block_implies_equal_trim_offset);
    RUN_TEST(test_dedup_disabled_at_atlas_level_gives_ten_placements);
    RUN_TEST(test_sprite_dedup_off_is_bidirectional);
    RUN_TEST(test_sprite_dedup_off_first_is_not_a_target);
    RUN_TEST(test_sprite_dedup_on_overrides_atlas_off);
    RUN_TEST(test_dedup_flags_change_the_atlas_cache_key);
    RUN_TEST(test_atlas_cache_hit_replays_the_aliases);
    RUN_TEST(test_identity_aliases_share_one_vertex_block);
    RUN_TEST(test_distinct_sprites_do_not_share_blocks);
    RUN_TEST(test_sprite_dedup_off_shares_bytes_but_not_placement);
    RUN_TEST(test_alias_root_is_lowest_add_index);
    RUN_TEST(test_mirrored_twin_folds_with_its_relative_transform);
    RUN_TEST(test_resolved_margin_groups_equivalent_spellings);
    RUN_TEST(test_resolved_geometry_controls_each_refuse_a_fold);
    RUN_TEST(test_resolved_spellings_still_fold);
    RUN_TEST(test_slice9_same_art_different_borders_share_placement);
    RUN_TEST(test_slice9_group_with_plain_sprite_stays_identity);
    RUN_TEST(test_dedup_pack_is_deterministic);
    RUN_TEST(test_stats_fold_identity_holds);
    RUN_TEST(test_stats_report_d4_folds);
    RUN_TEST(test_d4_cache_hit_replays_nonidentity_aliases);
    RUN_TEST(test_stats_match_anim_heavy_shaped_corpus);
    RUN_TEST(test_stats_report_shared_vertex_blocks);
    RUN_TEST(test_atlas_stats_and_errors_are_per_commit);
    return UNITY_END();
}
