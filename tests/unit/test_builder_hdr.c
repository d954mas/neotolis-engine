/* HDR texture sources and RAW RGB9E5 packs: decode, range contract, box chain, cache. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

/* clang-format off */
#include "nt_builder.h"
#include "nt_builder_internal.h"
#include "nt_pack_format.h"
#include "nt_texture_format.h"
#include "stb_image_write.h"
#include "test_helpers/build_assert_trap.h"
#include "unity.h"
/* clang-format on */

#define TMP_DIR "build/tests/tmp_hdr"

void setUp(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
}

void tearDown(void) {}

// #region helpers

static nt_tex_opts_t hdr_opts(bool gen_mipmaps) {
    nt_tex_opts_t opts = nt_tex_opts_defaults();
    opts.format = NT_TEXTURE_FORMAT_RGB9E5;
    opts.gen_mipmaps = gen_mipmaps;
    if (!gen_mipmaps) {
        opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    }
    return opts;
}

static uint8_t *read_all(const char *path, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    (void)fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc((size_t)size);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL((size_t)size, fread(data, 1, (size_t)size, f));
    (void)fclose(f);
    *out_size = (uint32_t)size;
    return data;
}

/* The single texture asset of a finished pack: its header and payload words. */
typedef struct {
    uint8_t *pack;
    uint32_t pack_size;
    NtTextureAssetHeader hdr;
    uint32_t words[64]; /* payload copy, so the test reads aligned words */
} texture_asset_t;

static texture_asset_t read_texture(const char *pack_path) {
    texture_asset_t t = {0};
    t.pack = read_all(pack_path, &t.pack_size);
    NtPackHeader pack;
    memcpy(&pack, t.pack, sizeof(pack));
    TEST_ASSERT_EQUAL_UINT16(1, pack.asset_count);
    NtAssetEntry entry;
    memcpy(&entry, t.pack + sizeof(pack), sizeof(entry));
    TEST_ASSERT_EQUAL_UINT8(NT_ASSET_TEXTURE, entry.asset_type);
    memcpy(&t.hdr, t.pack + entry.offset, sizeof(t.hdr));
    TEST_ASSERT_EQUAL_UINT32(entry.size, sizeof(t.hdr) + t.hdr.data_size);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(t.words), t.hdr.data_size);
    memcpy(t.words, t.pack + entry.offset + sizeof(t.hdr), t.hdr.data_size);
    return t;
}

static void finish(NtBuilderContext *ctx) {
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);
}

static texture_asset_t build_raw(const char *pack_path, const float *rgb, uint32_t w, uint32_t h, const nt_tex_opts_t *opts) {
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    nt_builder_add_texture_raw_f32(ctx, rgb, w, h, "hdr", opts);
    finish(ctx);
    return read_texture(pack_path);
}

static void unpack_rgb9e5(uint32_t v, float out[3]) {
    const int e = (int)(v >> 27U) - 24;
    for (uint32_t k = 0; k < 3; k++) {
        out[k] = ldexpf((float)((v >> (9U * k)) & 511U), e);
    }
}

static uint32_t pack_one(float r, float g, float b) {
    const float rgb[3] = {r, g, b};
    nt_tex_opts_t opts = hdr_opts(false);
    texture_asset_t t = build_raw(TMP_DIR "/one.ntpack", rgb, 1, 1, &opts);
    const uint32_t v = t.words[0];
    free(t.pack);
    return v;
}

/* Unity is built without float support. */
#define EXPECT_NEAR(tolerance, want, got) TEST_ASSERT_TRUE_MESSAGE(fabsf((float)(want) - (got)) <= (tolerance), #got " differs from " #want)

#define E5(r, g, b, e) ((uint32_t)(r) | ((uint32_t)(g) << 9U) | ((uint32_t)(b) << 18U) | ((uint32_t)(e) << 27U))

// #endregion

// #region RGB9E5 encoding

void test_rgb9e5_packs_reference_values(void) {
    TEST_ASSERT_EQUAL_HEX32(0, pack_one(0.0F, 0.0F, 0.0F));
    TEST_ASSERT_EQUAL_HEX32(E5(256, 0, 0, 14), pack_one(0.25F, 0.0F, 0.0F));
    TEST_ASSERT_EQUAL_HEX32(E5(256, 256, 256, 16), pack_one(1.0F, 1.0F, 1.0F));
    TEST_ASSERT_EQUAL_HEX32(E5(0, 256, 0, 17), pack_one(0.0F, 2.0F, 0.0F));
    TEST_ASSERT_EQUAL_HEX32(E5(0, 0, 256, 19), pack_one(0.0F, 0.0F, 8.0F));
    /* Unequal channels share the largest one's exponent. */
    TEST_ASSERT_EQUAL_HEX32(E5(8, 64, 256, 19), pack_one(0.25F, 2.0F, 8.0F));
    /* The smallest quantum, 2^-24, survives at exponent 0. */
    TEST_ASSERT_EQUAL_HEX32(E5(1, 0, 0, 0), pack_one(ldexpf(1.0F, -24), 0.0F, 0.0F));
}

void test_rgb9e5_rounding_edges(void) {
    /* 2 - 2^-9 rounds its mantissa to 512: the exponent carries and the value becomes 2. */
    TEST_ASSERT_EQUAL_HEX32(E5(256, 0, 0, 17), pack_one(1.998046875F, 0.0F, 0.0F));
    /* A half-quantum tie rounds up: 256.5 -> 257. */
    TEST_ASSERT_EQUAL_HEX32(E5(257, 0, 0, 16), pack_one(1.0F + ldexpf(1.0F, -9), 0.0F, 0.0F));
    /* The source limit is not representable: 65216 / 128 = 509.5 rounds to 510, so 65280. */
    TEST_ASSERT_EQUAL_HEX32(E5(510, 0, 0, 31), pack_one(65216.0F, 0.0F, 0.0F));
    /* Just below half a quantum rounds down; float x + 0.5 would round it up to 1. */
    TEST_ASSERT_EQUAL_HEX32(E5(256, 0, 0, 16), pack_one(1.0F, nextafterf(ldexpf(1.0F, -9), 0.0F), 0.0F));
}

// #endregion

// #region box chain

void test_rgb9e5_chain_is_the_full_halved_chain(void) {
    /* 5x3 -> 2x1 -> 1x1: odd sizes take the floor of the halving. */
    float rgb[5 * 3 * 3];
    for (uint32_t i = 0; i < 5 * 3; i++) {
        rgb[(i * 3) + 0] = 0.25F;
        rgb[(i * 3) + 1] = 2.0F;
        rgb[(i * 3) + 2] = 8.0F;
    }
    nt_tex_opts_t opts = hdr_opts(true);
    texture_asset_t t = build_raw(TMP_DIR "/chain.ntpack", rgb, 5, 3, &opts);
    TEST_ASSERT_EQUAL_UINT16(NT_TEXTURE_FORMAT_RGB9E5, t.hdr.format);
    TEST_ASSERT_EQUAL_UINT8(NT_TEXTURE_COMPRESSION_RAW, t.hdr.compression);
    TEST_ASSERT_EQUAL_UINT16(3, t.hdr.mip_count);
    TEST_ASSERT_EQUAL_UINT32((15 + 2 + 1) * 4, t.hdr.data_size);
    /* The runtime cannot generate RGB9E5 mips; the flag would make it try. */
    TEST_ASSERT_EQUAL_UINT8(0, t.hdr.flags & NT_TEXTURE_FLAG_GEN_MIPMAPS);
    /* A box filter keeps a constant image constant at every level, above 1 included. */
    for (uint32_t i = 0; i < 18; i++) {
        TEST_ASSERT_EQUAL_HEX32(E5(8, 64, 256, 19), t.words[i]);
    }
    free(t.pack);
}

void test_rgb9e5_without_gen_mipmaps_is_one_level(void) {
    const float rgb[2 * 2 * 3] = {0};
    nt_tex_opts_t opts = hdr_opts(false);
    texture_asset_t t = build_raw(TMP_DIR "/one_level.ntpack", rgb, 2, 2, &opts);
    TEST_ASSERT_EQUAL_UINT16(1, t.hdr.mip_count);
    TEST_ASSERT_EQUAL_UINT32(2 * 2 * 4, t.hdr.data_size);
    free(t.pack);
}

/* Asymmetric values per channel and per pixel: a swap, flip or wrong weight fails. */
void test_rgb9e5_chain_box_filters_each_level(void) {
    /* 2x2 -> 1x1 is the exact four-pixel average. */
    const float quad[2 * 2 * 3] = {0.25F, 2.0F, 8.0F, 0.75F, 6.0F, 0.0F, 2.0F, 0.0F, 4.0F, 1.0F, 4.0F, 4.0F};
    nt_tex_opts_t opts = hdr_opts(true);
    texture_asset_t t = build_raw(TMP_DIR "/box.ntpack", quad, 2, 2, &opts);
    TEST_ASSERT_EQUAL_UINT16(2, t.hdr.mip_count);
    float px[3];
    unpack_rgb9e5(t.words[1], px); /* source (row 0, col 1) keeps its place */
    EXPECT_NEAR(0.0F, 0.75F, px[0]);
    EXPECT_NEAR(0.0F, 6.0F, px[1]);
    unpack_rgb9e5(t.words[4], px);
    EXPECT_NEAR(1.0F / 128.0F, 1.0F, px[0]);
    EXPECT_NEAR(1.0F / 128.0F, 3.0F, px[1]);
    EXPECT_NEAR(1.0F / 128.0F, 4.0F, px[2]);
    free(t.pack);

    /* 5 -> 2 wide: area weights 0.4, 0.4, 0.2 | 0.2, 0.4, 0.4, no negative lobe. */
    const float row[5 * 3] = {10.0F, 0.0F, 0.0F, 10.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    t = build_raw(TMP_DIR "/box_odd.ntpack", row, 5, 1, &opts);
    TEST_ASSERT_EQUAL_UINT16(3, t.hdr.mip_count);
    unpack_rgb9e5(t.words[5], px);
    EXPECT_NEAR(1.0F / 32.0F, 8.0F, px[0]);
    unpack_rgb9e5(t.words[6], px);
    EXPECT_NEAR(0.0F, 0.0F, px[0]);
    free(t.pack);
}

void test_rgb9e5_max_size_resizes_with_the_box_filter(void) {
    float rgb[4 * 2 * 3];
    for (uint32_t i = 0; i < 4 * 2; i++) {
        rgb[(i * 3) + 0] = (i % 2 == 0) ? 8.0F : 0.0F;
        rgb[(i * 3) + 1] = 2.0F;
        rgb[(i * 3) + 2] = 0.25F;
    }
    nt_tex_opts_t opts = hdr_opts(false);
    opts.max_size = 2;
    texture_asset_t t = build_raw(TMP_DIR "/resize.ntpack", rgb, 4, 2, &opts);
    TEST_ASSERT_EQUAL_UINT32(2, t.hdr.width);
    TEST_ASSERT_EQUAL_UINT32(1, t.hdr.height);
    float px[3];
    unpack_rgb9e5(t.words[0], px);
    EXPECT_NEAR(1.0F / 64.0F, 4.0F, px[0]);
    EXPECT_NEAR(1.0F / 64.0F, 2.0F, px[1]);
    EXPECT_NEAR(1.0F / 64.0F, 0.25F, px[2]);
    free(t.pack);
}

// #endregion

// #region .hdr sources

/* Radiance keeps an 8-bit mantissa per pixel, scaled to the largest channel: every value here is exact in the file. */
static const float k_hdr_rgb[4 * 2 * 3] = {
    0.25F, 2.0F, 8.0F, 8.0F, 0.25F, 2.0F, 2.0F, 8.0F, 0.25F, 65024.0F, 512.0F, 256.0F, 0.0F, 0.0F, 0.0F, 1.5F, 3.0F, 6.0F, 0.125F, 0.25F, 0.5F, 4.0F, 4.0F, 4.0F,
};

static void write_hdr_file(const char *path) { TEST_ASSERT_NOT_EQUAL_INT(0, stbi_write_hdr(path, 4, 2, 3, k_hdr_rgb)); }

typedef enum { CACHE_NONE, CACHE_COLD, CACHE_WARM } cache_run_t;

static texture_asset_t build_hdr_pack(const char *pack_path, int source, const char *cache_dir, cache_run_t cache_run) {
    const char *hdr_path = TMP_DIR "/source.hdr";
    NtBuilderContext *ctx = nt_builder_start_pack(pack_path);
    if (cache_run != CACHE_NONE) {
        nt_builder_set_cache_dir(ctx, cache_dir);
    }
    nt_tex_opts_t opts = hdr_opts(true);
    if (source == 0) {
        nt_builder_add_texture(ctx, hdr_path, &opts);
    } else if (source == 1) {
        uint32_t size = 0;
        uint8_t *bytes = read_all(hdr_path, &size);
        nt_builder_add_texture_from_memory(ctx, bytes, size, hdr_path, &opts);
        free(bytes);
    } else {
        nt_builder_add_texture_raw_f32(ctx, k_hdr_rgb, 4, 2, hdr_path, &opts);
    }
    if (cache_run == CACHE_COLD) {
        /* An entry left by an earlier run would make the cold build warm. */
        char entry[512];
        nt_builder_build_cache_path(cache_dir, ctx->pending[0].decoded_hash, nt_builder_compute_opts_hash(&ctx->pending[0]), entry, sizeof(entry));
        (void)remove(entry);
    }
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    if (cache_run != CACHE_NONE) {
        TEST_ASSERT_EQUAL_UINT32(cache_run == CACHE_WARM ? 1 : 0, ctx->cache_hit_count);
        TEST_ASSERT_EQUAL_UINT32(cache_run == CACHE_COLD ? 1 : 0, ctx->cache_miss_count);
    }
    nt_builder_free_pack(ctx);
    return read_texture(pack_path);
}

void test_hdr_file_memory_and_float_input_pack_the_same_texture(void) {
    write_hdr_file(TMP_DIR "/source.hdr");
    texture_asset_t from_file = build_hdr_pack(TMP_DIR "/hdr_file.ntpack", 0, NULL, CACHE_NONE);
    texture_asset_t from_memory = build_hdr_pack(TMP_DIR "/hdr_memory.ntpack", 1, NULL, CACHE_NONE);
    texture_asset_t from_floats = build_hdr_pack(TMP_DIR "/hdr_floats.ntpack", 2, NULL, CACHE_NONE);
    TEST_ASSERT_EQUAL_UINT16(3, from_floats.hdr.mip_count);
    TEST_ASSERT_EQUAL_UINT16(3, from_file.hdr.mip_count);
    TEST_ASSERT_EQUAL_UINT16(3, from_memory.hdr.mip_count);
    TEST_ASSERT_EQUAL_UINT32(from_floats.hdr.data_size, from_file.hdr.data_size);
    TEST_ASSERT_EQUAL_UINT32(from_floats.hdr.data_size, from_memory.hdr.data_size);
    TEST_ASSERT_EQUAL_MEMORY(from_floats.words, from_file.words, from_file.hdr.data_size);
    TEST_ASSERT_EQUAL_MEMORY(from_floats.words, from_memory.words, from_memory.hdr.data_size);
    /* Values above 1 survive decode and pack. */
    float px[3];
    unpack_rgb9e5(from_file.words[3], px);
    EXPECT_NEAR(0.0F, 65024.0F, px[0]);
    free(from_file.pack);
    free(from_memory.pack);
    free(from_floats.pack);
}

void test_hdr_cold_and_warm_cache_pack_the_same_bytes(void) {
    write_hdr_file(TMP_DIR "/source.hdr");
    const char *cache_dir = TMP_DIR "/cache";
    (void)MKDIR(cache_dir);
    texture_asset_t cold = build_hdr_pack(TMP_DIR "/hdr_cold.ntpack", 0, cache_dir, CACHE_COLD);
    texture_asset_t warm = build_hdr_pack(TMP_DIR "/hdr_warm.ntpack", 0, cache_dir, CACHE_WARM);
    TEST_ASSERT_EQUAL_UINT32(cold.pack_size, warm.pack_size);
    TEST_ASSERT_EQUAL_MEMORY(cold.pack, warm.pack, cold.pack_size);
    free(cold.pack);
    free(warm.pack);
}

/* For RGB9E5, gen_mipmaps decides the payload (baked chain or one level), so it is part of the key. */
void test_hdr_gen_mipmaps_alone_changes_the_cache_key(void) {
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/hdr_keys.ntpack");
    nt_tex_opts_t chain = hdr_opts(true);
    nt_tex_opts_t one_level = hdr_opts(false);
    one_level.filter_min = chain.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR;
    nt_builder_add_texture_raw_f32(ctx, k_hdr_rgb, 4, 2, "chain", &chain);
    nt_builder_add_texture_raw_f32(ctx, k_hdr_rgb, 4, 2, "one_level", &one_level);
    TEST_ASSERT_EQUAL_UINT64(ctx->pending[0].decoded_hash, ctx->pending[1].decoded_hash);
    TEST_ASSERT_TRUE(nt_builder_compute_opts_hash(&ctx->pending[0]) != nt_builder_compute_opts_hash(&ctx->pending[1]));
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    TEST_ASSERT_EQUAL_UINT32(0, ctx->early_dedup_count);
    nt_builder_free_pack(ctx);
}

// #endregion

// #region contract asserts

static void expect_raw_value_assert(float value) {
    const float rgb[3] = {1.0F, value, 1.0F};
    nt_tex_opts_t opts = hdr_opts(false);
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/bad.ntpack");
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_texture_raw_f32(ctx, rgb, 1, 1, "bad", &opts), "finite and in [0, 65216]");
    nt_builder_free_pack(ctx);
}

void test_hdr_values_outside_the_source_range_assert(void) {
    expect_raw_value_assert(65216.01F);
    expect_raw_value_assert(-0.001F);
    expect_raw_value_assert(NAN);
    expect_raw_value_assert(INFINITY);
}

void test_hdr_file_values_outside_the_source_range_assert(void) {
    /* Radiance stores 2^17 exactly; the file path checks the decoded floats too. */
    const float rgb[3] = {131072.0F, 0.0F, 0.0F};
    TEST_ASSERT_NOT_EQUAL_INT(0, stbi_write_hdr(TMP_DIR "/too_bright.hdr", 1, 1, 3, rgb));
    nt_tex_opts_t opts = hdr_opts(false);
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/bad.ntpack");
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_texture(ctx, TMP_DIR "/too_bright.hdr", &opts), "finite and in [0, 65216]");
    nt_builder_free_pack(ctx);
}

void test_hdr_range_is_checked_before_resize(void) {
    /* Averaged with its neighbours, -1 would vanish into a valid value. */
    const float rgb[2 * 3] = {-1.0F, 0.0F, 0.0F, 3.0F, 0.0F, 0.0F};
    nt_tex_opts_t opts = hdr_opts(false);
    opts.max_size = 1;
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/bad.ntpack");
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_texture_raw_f32(ctx, rgb, 2, 1, "bad", &opts), "finite and in [0, 65216]");
    nt_builder_free_pack(ctx);
}

void test_hdr_source_and_format_must_agree(void) {
    write_hdr_file(TMP_DIR "/source.hdr");
    const uint8_t rgba[4] = {255, 255, 255, 255};
    TEST_ASSERT_NOT_EQUAL_INT(0, stbi_write_png(TMP_DIR "/source.png", 1, 1, 4, rgba, 4));
    nt_tex_opts_t opts = hdr_opts(false);

    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/bad.ntpack");
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_texture(ctx, TMP_DIR "/source.png", &opts), "RGB9E5 requires an HDR (.hdr) source");
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_texture(ctx, TMP_DIR "/source.hdr", NULL), "an HDR source requires format RGB9E5");
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_texture_raw(ctx, rgba, 1, 1, "bad", &opts), "use nt_builder_add_texture_raw_f32");
    const float rgb[3] = {1.0F, 1.0F, 1.0F};
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_add_texture_raw_f32(ctx, rgb, 1, 1, "bad", NULL), "float pixels require format RGB9E5");
    nt_builder_free_pack(ctx);
}

void test_hdr_opts_contract(void) {
    nt_tex_opts_t opts = hdr_opts(true);
    opts.compress = nt_tex_compress_uastc_default();
    EXPECT_BUILD_ASSERT_MATCH((void)nt_builder_assert_texture_opts(&opts), "Basis compression requires");
    /* Premultiplying would treat the float source as RGBA8 bytes. */
    opts = hdr_opts(true);
    opts.premultiplied = true;
    EXPECT_BUILD_ASSERT_MATCH((void)nt_builder_assert_texture_opts(&opts), "premultiplied=true requires RGBA8");
    /* A mip filter without a baked chain has no levels to sample. */
    opts = hdr_opts(false);
    opts.filter_min = NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_LINEAR;
    EXPECT_BUILD_ASSERT_MATCH((void)nt_builder_assert_texture_opts(&opts), "RAW mipmap filter requires gen_mipmaps=true");
}

// #endregion

// #region atlases stay LDR

void test_atlas_rgb9e5_page_asserts(void) {
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.format = NT_TEXTURE_FORMAT_RGB9E5;
    opts.premultiplied = false; /* premultiplication already rejects every format but RGBA8 */
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/atlas.ntpack");
    EXPECT_BUILD_ASSERT_MATCH((void)nt_atlas_begin(ctx, "page", &opts), "atlas pages are LDR");
    nt_builder_free_pack(ctx);
}

void test_atlas_hdr_sprite_is_a_content_error(void) {
    write_hdr_file(TMP_DIR "/source.hdr");
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/atlas.ntpack");
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "page", NULL);
    nt_atlas_add(atlas, TMP_DIR "/source.hdr", NULL);
    TEST_ASSERT_EQUAL(NT_BUILD_ERR_FORMAT, nt_atlas_commit(atlas));
    uint32_t count = 0;
    const nt_build_error_t *errors = nt_builder_get_errors(ctx, &count);
    TEST_ASSERT_EQUAL_UINT32(1, count);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_HDR_IMAGE, errors[0].kind);
    char line[512];
    nt_build_error_format(&errors[0], line, sizeof(line));
    TEST_ASSERT_NOT_NULL(strstr(line, "HDR image; atlas pages are LDR"));
    nt_builder_free_pack(ctx);
}

// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rgb9e5_packs_reference_values);
    RUN_TEST(test_rgb9e5_rounding_edges);
    RUN_TEST(test_rgb9e5_chain_is_the_full_halved_chain);
    RUN_TEST(test_rgb9e5_without_gen_mipmaps_is_one_level);
    RUN_TEST(test_rgb9e5_chain_box_filters_each_level);
    RUN_TEST(test_rgb9e5_max_size_resizes_with_the_box_filter);
    RUN_TEST(test_hdr_file_memory_and_float_input_pack_the_same_texture);
    RUN_TEST(test_hdr_cold_and_warm_cache_pack_the_same_bytes);
    RUN_TEST(test_hdr_gen_mipmaps_alone_changes_the_cache_key);
    RUN_TEST(test_hdr_values_outside_the_source_range_assert);
    RUN_TEST(test_hdr_file_values_outside_the_source_range_assert);
    RUN_TEST(test_hdr_range_is_checked_before_resize);
    RUN_TEST(test_hdr_source_and_format_must_agree);
    RUN_TEST(test_hdr_opts_contract);
    RUN_TEST(test_atlas_rgb9e5_page_asserts);
    RUN_TEST(test_atlas_hdr_sprite_is_a_content_error);
    return UNITY_END();
}
