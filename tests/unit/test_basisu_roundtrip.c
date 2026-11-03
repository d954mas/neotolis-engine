#include "nt_basisu_encoder.h"
#include "nt_basisu_transcoder.h"
#include "nt_builder.h"
#include "nt_pack_format.h"
#include "test_helpers/basis_test_chain.h"
#include "test_helpers/build_assert_trap.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"
#include <math.h>
#include <string.h>

#define MAX_W 96U
#define MAX_H 64U
void setUp(void) {}
void tearDown(void) {}

static void fill_pixels(uint8_t *src, uint32_t width, uint32_t height, bool alpha) {
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint8_t *px = &src[(((size_t)y * width) + x) * 4];
            px[0] = (uint8_t)(32U + (x * 160U / width));
            px[1] = (uint8_t)(24U + (y * 120U / height));
            px[2] = 64;
            px[3] = alpha ? (uint8_t)(40U + (x * 180U / width)) : 255;
        }
    }
}

static void check_pixels(const uint8_t *src, const uint8_t *out, uint32_t bytes) {
    for (uint32_t c = 0; c < 4; c++) {
        uint32_t error = 0;
        for (uint32_t i = c; i < bytes; i += 4) {
            int delta = (int)out[i] - (int)src[i];
            error += (uint32_t)(delta < 0 ? -delta : delta);
        }
        TEST_ASSERT_LESS_THAN_UINT32(24U * (bytes / 4U), error);
    }
}

static const nt_texture_format_t s_targets[] = {NT_TEXTURE_FORMAT_ETC2_RGB8, NT_TEXTURE_FORMAT_ETC2_RGBA8, NT_TEXTURE_FORMAT_BC7_RGBA, NT_TEXTURE_FORMAT_ASTC_4x4_RGBA, NT_TEXTURE_FORMAT_RGBA8};
/* NT_BASISU_HAS_ETC2/BC7/ASTC of this configure; RGBA8 is always admitted. */
static const bool s_target_enabled[] = {NT_BASISU_HAS_ETC2 != 0, NT_BASISU_HAS_ETC2 != 0, NT_BASISU_HAS_BC7 != 0, NT_BASISU_HAS_ASTC != 0, true};

/* Presets of the admitted codecs; the encoder itself is a superset. */
static uint32_t enabled_codec_presets(nt_basisu_encode_opts_t out[2]) {
    uint32_t count = 0;
#if NT_BASISU_HAS_ETC1S
    out[count++] = nt_tex_compress_etc1s_default();
#endif
#if NT_BASISU_HAS_UASTC
    out[count++] = nt_tex_compress_uastc_default();
#endif
    return count;
}

/* Bytes of the whole chain, levels back to back with no padding. */
static uint32_t chain_bytes(nt_texture_format_t format, uint32_t width, uint32_t height, uint32_t levels) {
    uint32_t total = 0;
    for (uint32_t level = 0; level < levels; level++) {
        total += (uint32_t)nt_texture_level_bytes(format, nt_texture_level_extent(width, level), nt_texture_level_extent(height, level));
    }
    return total;
}

/* RGBA8 is the widest output: its chain is 4/3 of its base level. */
#define MAX_CHAIN_BYTES (((MAX_W * MAX_H * 4U) * 4U / 3U) + 64U)
static uint8_t s_out[MAX_CHAIN_BYTES + 16U];

static void check_outputs(const nt_basisu_encode_result_t *enc, const nt_basisu_info_t *info, const uint8_t *src) {
    for (uint32_t f = 0; f < sizeof(s_targets) / sizeof(s_targets[0]); f++) {
        const uint32_t bytes = chain_bytes(s_targets[f], info->width, info->height, info->level_count);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(s_out) - 16U, bytes);
        memset(s_out, 0xCD, sizeof(s_out));
        if (!s_target_enabled[f]) {
            continue; /* target option OFF: not a legal argument in this build */
        }
        nt_basisu_transcode_chain(enc->data, enc->size, info, s_targets[f], s_out);
        /* The chain ends exactly where the per-level sizes say it does. */
        for (uint32_t i = bytes; i < bytes + 16; i++) {
            TEST_ASSERT_EQUAL_HEX8(0xCD, s_out[i]);
        }
        if (s_targets[f] == NT_TEXTURE_FORMAT_RGBA8) {
            check_pixels(src, s_out, (uint32_t)nt_texture_level_bytes(NT_TEXTURE_FORMAT_RGBA8, info->width, info->height));
        }
    }
}

static void roundtrip(uint32_t width, uint32_t height, nt_basisu_codec_t codec, bool alpha) {
    uint8_t src[MAX_W * MAX_H * 4];
    fill_pixels(src, width, height, alpha);
    nt_basisu_encode_opts_t opts = codec == NT_BASISU_CODEC_ETC1S ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
    uint32_t levels = 1;
    for (uint32_t size = width > height ? width : height; size > 1; size >>= 1U) {
        levels++;
    }
    basis_test_chain_t chain = basis_test_chain(src, width, height, levels);
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, chain.data, chain.count, width, height, alpha ? NT_TEXTURE_FORMAT_RGBA8 : NT_TEXTURE_FORMAT_RGB8, &opts, true);
    basis_test_chain_free(&chain);
    TEST_ASSERT_NOT_NULL(enc.data);

    const nt_basisu_info_t info = nt_basisu_info(enc.data, enc.size);
    TEST_ASSERT_EQUAL_INT(codec, info.codec);
    /* Unpadded dimensions: 13x7 stays 13x7, not the 16x8 block padding. */
    TEST_ASSERT_EQUAL_UINT32(width, info.width);
    TEST_ASSERT_EQUAL_UINT32(height, info.height);
    TEST_ASSERT_EQUAL_UINT32(levels, info.level_count);
    TEST_ASSERT_EQUAL(alpha, info.has_alpha);

    check_outputs(&enc, &info, src);
    nt_basisu_encode_free(&enc);
}

static void codec_cases(nt_basisu_codec_t codec, bool alpha) {
    roundtrip(16, 8, codec, alpha);
    roundtrip(13, 7, codec, alpha);
    roundtrip(1, 1, codec, alpha);
    roundtrip(96, 64, codec, alpha);
}
#if NT_BASISU_HAS_ETC1S
void test_etc1s_rgb(void) { codec_cases(NT_BASISU_CODEC_ETC1S, false); }
void test_etc1s_alpha(void) { codec_cases(NT_BASISU_CODEC_ETC1S, true); }
#endif
#if NT_BASISU_HAS_UASTC
void test_uastc_rgb(void) { codec_cases(NT_BASISU_CODEC_UASTC_LDR, false); }
void test_uastc_alpha(void) { codec_cases(NT_BASISU_CODEC_UASTC_LDR, true); }
#endif

void test_non_basis_header_asserts(void) {
    uint8_t pixels[16 * 8 * 4];
    fill_pixels(pixels, 16, 8, false);
    NT_TEST_EXPECT_ASSERT((void)nt_basisu_info(pixels, sizeof(pixels)));
}

void test_encode_without_threads_asserts(void) {
    uint8_t pixels[16 * 8 * 4];
    fill_pixels(pixels, 16, 8, false);
    const nt_basisu_encode_opts_t opts = nt_tex_compress_etc1s_high();
    EXPECT_BUILD_ASSERT_MATCH((void)nt_basisu_encode(0, pixels, 1, 16, 8, NT_TEXTURE_FORMAT_RGB8, &opts, true), "basis_threads");
    /* 16x8 has 5 levels; a sixth has no size. */
    EXPECT_BUILD_ASSERT_MATCH((void)nt_basisu_encode(1, pixels, 6, 16, 8, NT_TEXTURE_FORMAT_RGB8, &opts, true), "level_count outside");
    EXPECT_BUILD_ASSERT_MATCH((void)nt_basisu_encode(1, pixels, 0, 16, 8, NT_TEXTURE_FORMAT_RGB8, &opts, true), "level_count outside");
}

/* An RGBA source the encoder finds fully opaque loses its alpha slices, so the
 * blob -- not the asset header -- decides whether a target needs alpha. */
void test_opaque_rgba_source_reports_no_alpha(void) {
    uint8_t pixels[16 * 8 * 4];
    fill_pixels(pixels, 16, 8, false);
    nt_basisu_encode_opts_t codecs[2];
    const uint32_t codec_count = enabled_codec_presets(codecs);
    basis_test_chain_t chain = basis_test_chain(pixels, 16, 8, nt_texture_full_chain_levels(16, 8));
    for (uint32_t i = 0; i < codec_count; i++) {
        nt_basisu_encode_result_t enc = nt_basisu_encode(1, chain.data, chain.count, 16, 8, NT_TEXTURE_FORMAT_RGBA8, &codecs[i], true);
        TEST_ASSERT_NOT_NULL(enc.data);
        const nt_basisu_info_t info = nt_basisu_info(enc.data, enc.size);
        nt_basisu_encode_free(&enc);
        TEST_ASSERT_FALSE(info.has_alpha);
    }
    basis_test_chain_free(&chain);
}

#if NT_BASISU_HAS_UASTC
/* Byte offsets into basist::basis_file_header / basis_slice_desc
 * (deps/basisu/transcoder/basisu_file_headers.h; pack(1), little-endian). */
#define BASIS_HEADER_TOTAL_SLICES_OFS 14U
#define BASIS_HEADER_SLICE_DESC_OFS_OFS 65U
#define BASIS_SLICE_DESC_BYTES 23U
#define BASIS_SLICE_LEVEL_INDEX_OFS 3U
#define BASIS_SLICE_ORIG_WIDTH_OFS 5U

static uint32_t read_le(const uint8_t *p, uint32_t bytes) {
    uint32_t value = 0;
    for (uint32_t i = 0; i < bytes; i++) {
        value |= (uint32_t)p[i] << (8U * i);
    }
    return value;
}

/* A level whose stored width shrank inside the same block count: upstream
 * accepts it (m_num_blocks_x still matches) and would then write that level
 * with its own row stride, so nt_basisu_info has to assert on the chain itself.
 * validate_header re-checks no CRC over the slice array, so the patch survives. */
void test_mip_level_with_a_shrunken_stored_width_asserts(void) {
    uint8_t pixels[13 * 7 * 4];
    fill_pixels(pixels, 13, 7, false);
    nt_basisu_encode_opts_t opts = nt_tex_compress_uastc_default();
    /* Opaque UASTC: exactly one slice per level, so level 1 has its own desc. */
    basis_test_chain_t chain = basis_test_chain(pixels, 13, 7, nt_texture_full_chain_levels(13, 7));
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, chain.data, chain.count, 13, 7, NT_TEXTURE_FORMAT_RGB8, &opts, true);
    basis_test_chain_free(&chain);
    TEST_ASSERT_NOT_NULL(enc.data);

    const nt_basisu_info_t info = nt_basisu_info(enc.data, enc.size);
    TEST_ASSERT_EQUAL_UINT32(4, info.level_count);

    uint8_t *blob = enc.data;
    const uint32_t total_slices = read_le(blob + BASIS_HEADER_TOTAL_SLICES_OFS, 3);
    TEST_ASSERT_EQUAL_UINT32(info.level_count, total_slices);
    uint8_t *slices = blob + read_le(blob + BASIS_HEADER_SLICE_DESC_OFS_OFS, 4);

    uint8_t *level1 = NULL;
    for (uint32_t i = 0; i < total_slices; i++) {
        uint8_t *slice = slices + ((size_t)i * BASIS_SLICE_DESC_BYTES);
        if (slice[BASIS_SLICE_LEVEL_INDEX_OFS] == 1) {
            level1 = slice;
        }
    }
    TEST_ASSERT_NOT_NULL(level1);
    /* Level 1 of 13x7 is 6x3 -- two 4x4 blocks wide, exactly as 5x3 would be. */
    TEST_ASSERT_EQUAL_UINT32(6, read_le(level1 + BASIS_SLICE_ORIG_WIDTH_OFS, 2));
    level1[BASIS_SLICE_ORIG_WIDTH_OFS] = 5;

    NT_TEST_EXPECT_ASSERT((void)nt_basisu_info(enc.data, enc.size));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "halved chain"));
    nt_basisu_encode_free(&enc);
}
#endif

/* Unity terminates failed reads with longjmp before the stream is reused. */
// NOLINTBEGIN(clang-analyzer-unix.Stream)
/* The Basis payload of one raw texture built as a pack through the public
 * builder, so its format reaches the encoder. Returns the payload size. */
static uint32_t encode_raw_pack(nt_basisu_codec_t codec, nt_texture_pixel_format_t format, nt_tex_process_t process, const uint8_t *pixels, uint32_t width, uint32_t height, uint8_t basis[4096]) {
    nt_tex_opts_t opts = nt_tex_opts_defaults();
    opts.format = format;
    opts.process = process;
    opts.compress = codec == NT_BASISU_CODEC_ETC1S ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
    opts.mips = NT_TEX_MIPS_BAKED;
    NtBuilderContext *ctx = nt_builder_start_pack("basis_raw.ntpack");
    nt_builder_add_texture_raw(ctx, pixels, width, height, "raw", &opts);
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    FILE *file = fopen("basis_raw.ntpack", "rb");
    TEST_ASSERT_NOT_NULL(file);
    NtPackHeader pack = {0};
    NtAssetEntry entry = {0};
    NtTextureAssetHeader header = {0};
    TEST_ASSERT_EQUAL(1, fread(&pack, sizeof(pack), 1, file));
    TEST_ASSERT_EQUAL(1, fread(&entry, sizeof(entry), 1, file));
    (void)fseek(file, (long)entry.offset, SEEK_SET);
    TEST_ASSERT_EQUAL(1, fread(&header, sizeof(header), 1, file));
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(4096, header.data_size);
    TEST_ASSERT_EQUAL(header.data_size, fread(basis, 1, header.data_size, file));
    (void)fclose(file);
    TEST_ASSERT_EQUAL_UINT16(format, header.format);
    return header.data_size;
}
// NOLINTEND(clang-analyzer-unix.Stream)

/* The decoded 1x1 tail of an 8x8 checker of `even` and `odd` texels. */
static void encode_checker_tail(nt_basisu_codec_t codec, nt_texture_pixel_format_t format, nt_tex_alpha_t alpha, const uint8_t even[4], const uint8_t odd[4], uint8_t out_tail[4]) {
    uint8_t pixels[8 * 8 * 4];
    for (uint32_t y = 0; y < 8; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            memcpy(&pixels[(((size_t)y * 8) + x) * 4], ((x + y) & 1U) ? odd : even, 4);
        }
    }
    uint8_t basis[4096] = {0};
    const uint32_t size = encode_raw_pack(codec, format, (nt_tex_process_t){.alpha = alpha}, pixels, 8, 8, basis);
    const nt_basisu_info_t info = nt_basisu_info(basis, size);
    /* 8x8 -> 4x4 -> 2x2 -> 1x1: the 4 RGBA8 bytes at the end are the tail level. */
    uint8_t chain[((8 * 8) + (4 * 4) + (2 * 2) + 1) * 4] = {0};
    TEST_ASSERT_EQUAL_UINT32(4, info.level_count);
    nt_basisu_transcode_chain(basis, size, &info, NT_TEXTURE_FORMAT_RGBA8, chain);
    memcpy(out_tail, chain + sizeof(chain) - 4, 4);
}

/* Level l is solid colour l, which no downsample of level 0 can produce: the
 * blob carries the caller's levels at max(1, extent >> l), not a chain of its own. */
void test_encoder_stores_the_caller_levels(void) {
    static const uint8_t colours[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}};
    enum { W = 12, H = 5, LEVELS = 4 }; /* 12x5 -> 6x2 -> 3x1 -> 1x1 */
    uint8_t texels[((W * H) + (6 * 2) + (3 * 1) + 1) * 4];
    uint8_t *dst = texels;
    for (uint32_t l = 0; l < LEVELS; l++) {
        const uint32_t count = nt_texture_level_extent(W, l) * nt_texture_level_extent(H, l);
        for (uint32_t i = 0; i < count; i++) {
            memcpy(dst + ((size_t)i * 4), colours[l], 4);
        }
        dst += (size_t)count * 4;
    }
    nt_basisu_encode_opts_t codecs[2];
    const uint32_t codec_count = enabled_codec_presets(codecs);
    for (uint32_t c = 0; c < codec_count; c++) {
        nt_basisu_encode_result_t enc = nt_basisu_encode(1, texels, LEVELS, W, H, NT_TEXTURE_FORMAT_RGB8, &codecs[c], true);
        const nt_basisu_info_t info = nt_basisu_info(enc.data, enc.size);
        TEST_ASSERT_EQUAL_UINT32(W, info.width);
        TEST_ASSERT_EQUAL_UINT32(H, info.height);
        TEST_ASSERT_EQUAL_UINT32(LEVELS, info.level_count);
        TEST_ASSERT_EQUAL_UINT32(sizeof(texels), chain_bytes(NT_TEXTURE_FORMAT_RGBA8, W, H, LEVELS));
        nt_basisu_transcode_chain(enc.data, enc.size, &info, NT_TEXTURE_FORMAT_RGBA8, s_out);
        nt_basisu_encode_free(&enc);
        for (uint32_t i = 0; i < sizeof(texels); i++) {
            TEST_ASSERT_INT_WITHIN(12, texels[i], s_out[i]);
        }
    }
}

/* Transparent source texels in an RGB8 or SRGB8 texture must not reach Basis:
 * it scans user levels for alpha even with alpha checks off. */
void test_alpha_less_formats_encode_opaque_levels(void) {
    uint8_t pixels[8 * 8 * 4];
    for (uint32_t i = 0; i < 8 * 8; i++) {
        const uint8_t rgba[4] = {(uint8_t)(i * 4), 90, 200, (i & 1U) ? 255 : 0};
        memcpy(pixels + ((size_t)i * 4), rgba, 4);
    }
    static const nt_texture_pixel_format_t formats[] = {NT_TEXTURE_FORMAT_RGB8, NT_TEXTURE_FORMAT_SRGB8};
    nt_basisu_encode_opts_t codecs[2];
    const uint32_t codec_count = enabled_codec_presets(codecs);
    for (uint32_t c = 0; c < codec_count; c++) {
        for (uint32_t f = 0; f < 2; f++) {
            uint8_t basis[4096] = {0};
            const uint32_t size = encode_raw_pack(codecs[c].codec, formats[f], (nt_tex_process_t){0}, pixels, 8, 8, basis);
            const nt_basisu_info_t info = nt_basisu_info(basis, size);
            TEST_ASSERT_FALSE(info.has_alpha);
            TEST_ASSERT_EQUAL_UINT32(4, info.level_count);
            const uint32_t bytes = chain_bytes(NT_TEXTURE_FORMAT_RGBA8, 8, 8, 4);
            nt_basisu_transcode_chain(basis, size, &info, NT_TEXTURE_FORMAT_RGBA8, s_out);
            for (uint32_t i = 3; i < bytes; i += 4) {
                TEST_ASSERT_EQUAL_UINT8(255, s_out[i]);
            }
        }
    }
}

/* alpha_cutoff survives Basis: decoded levels of 64 texels or more stay nearer level
 * 0's coverage at the threshold, within a codec tolerance (ETC1S also nearer than the
 * uncut chain; UASTC's uncut large levels are already close). The
 * UASTC 4x4 level keeps over half of it where the uncut chain lost every texel; the
 * ETC1S alpha slice is too coarse at 4x4 and loses it either way. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- one fixture, decoded uncut and cut
static void check_alpha_cutoff_coverage(nt_basisu_codec_t codec) {
    enum { N = 32 };
    uint8_t pixels[N * N * 4];
    for (uint32_t i = 0; i < N * N; i++) {
        const uint32_t row = i / N;
        const double d = fabs((double)(i % N) - 8.0 - (0.35 * row));
        const double d2 = fabs((double)(i % N) - 21.0 - (0.35 * row));
        const double coverage = fmax(fmin(1.0, 1.6 - d), fmin(1.0, 1.6 - d2));
        const uint8_t texel[4] = {60, 140, 40, (uint8_t)lround(fmax(coverage, 0.0) * 255.0)};
        memcpy(pixels + ((size_t)i * 4), texel, 4);
    }
    double worst[2] = {0.0, 0.0}; /* drift of levels of 64 texels or more, uncut and cut */
    double small[2] = {0.0, 0.0}; /* coverage of the 4x4 level, uncut and cut */
    for (uint32_t cut = 0; cut < 2; cut++) {
        uint8_t basis[4096] = {0};
        const uint32_t size = encode_raw_pack(codec, NT_TEXTURE_FORMAT_RGBA8, (nt_tex_process_t){.alpha_cutoff = cut ? 0.5F : 0.0F}, pixels, N, N, basis);
        const nt_basisu_info_t info = nt_basisu_info(basis, size);
        nt_basisu_transcode_chain(basis, size, &info, NT_TEXTURE_FORMAT_RGBA8, s_out);
        const uint8_t *level = s_out;
        double c0 = 0.0;
        for (uint32_t l = 0; l < info.level_count; l++) {
            const uint32_t texels = nt_texture_level_extent(N, l) * nt_texture_level_extent(N, l);
            uint32_t count = 0;
            for (uint32_t i = 0; i < texels; i++) {
                count += level[((size_t)i * 4) + 3] >= 128 ? 1U : 0U;
            }
            const double coverage = (double)count / texels;
            c0 = l == 0 ? coverage : c0;
            if (texels >= 64) {
                worst[cut] = fmax(worst[cut], fabs(coverage - c0));
            }
            small[cut] = texels == 16 ? coverage / c0 : small[cut];
            level += (size_t)texels * 4;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(worst[1] < (codec == NT_BASISU_CODEC_UASTC_LDR ? 0.03 : 0.06), "decoded coverage drifts from level 0");
    if (codec == NT_BASISU_CODEC_ETC1S) {
        TEST_ASSERT_TRUE_MESSAGE(worst[1] < worst[0], "the cutoff does not improve decoded coverage");
    }
    TEST_ASSERT_TRUE(small[0] < 0.5);
    if (codec == NT_BASISU_CODEC_UASTC_LDR) {
        TEST_ASSERT_TRUE(small[1] > 0.5);
    }
}

/* sRGB mips average light, not encoded numbers: black/white gives 188 (0.5 linear), not 128.
 * Alpha stays linear. */
static void check_srgb_mip(nt_basisu_codec_t codec) {
    static const uint8_t opaque_black[4] = {0, 0, 0, 255};
    static const uint8_t opaque_white[4] = {255, 255, 255, 255};
    static const uint8_t clear_white[4] = {255, 255, 255, 0};
    uint8_t mip[4];
    encode_checker_tail(codec, NT_TEXTURE_FORMAT_SRGB8_ALPHA8, NT_TEX_ALPHA_STRAIGHT, opaque_black, opaque_white, mip);
    for (uint32_t c = 0; c < 3; c++) {
        TEST_ASSERT_INT_WITHIN(8, 188, mip[c]);
    }
    TEST_ASSERT_INT_WITHIN(8, 255, mip[3]);
    encode_checker_tail(codec, NT_TEXTURE_FORMAT_SRGB8_ALPHA8, NT_TEX_ALPHA_STRAIGHT, clear_white, opaque_white, mip);
    TEST_ASSERT_INT_WITHIN(8, 128, mip[3]);
}

#if NT_BASISU_HAS_UASTC
void test_uastc_alpha_cutoff_coverage(void) { check_alpha_cutoff_coverage(NT_BASISU_CODEC_UASTC_LDR); }
#endif
#if NT_BASISU_HAS_ETC1S
void test_etc1s_alpha_cutoff_coverage(void) { check_alpha_cutoff_coverage(NT_BASISU_CODEC_ETC1S); }
#endif
#if NT_BASISU_HAS_ETC1S
void test_etc1s_srgb_mip(void) { check_srgb_mip(NT_BASISU_CODEC_ETC1S); }
#endif
#if NT_BASISU_HAS_UASTC
void test_uastc_srgb_mip(void) { check_srgb_mip(NT_BASISU_CODEC_UASTC_LDR); }
#endif

/* Unity terminates failed reads with longjmp before the stream is reused. */
// NOLINTBEGIN(clang-analyzer-unix.Stream)
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- compare header, mip dimensions and pixels independently
static void check_public_pack(const char *path, uint32_t texture_count, const uint8_t expected[4], nt_basisu_codec_t codec) {
    FILE *file = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(file);
    NtPackHeader pack = {0};
    TEST_ASSERT_EQUAL(1, fread(&pack, sizeof(pack), 1, file));
    NtAssetEntry entries[8] = {0};
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(8, pack.asset_count);
    TEST_ASSERT_EQUAL(pack.asset_count, fread(entries, sizeof(entries[0]), pack.asset_count, file));
    uint32_t textures = 0;
    for (uint32_t i = 0; i < pack.asset_count; i++) {
        if (entries[i].asset_type != NT_ASSET_TEXTURE) {
            continue;
        }
        textures++;
        bool atlas_page = entries[i].resource_id == nt_hash64_str("atlas/tex0").value;
        (void)fseek(file, (long)entries[i].offset, SEEK_SET);
        NtTextureAssetHeader header = {0};
        TEST_ASSERT_EQUAL(1, fread(&header, sizeof(header), 1, file));
        if (!atlas_page) {
            TEST_ASSERT_EQUAL_UINT32(texture_count == 1 ? 4 : 8, header.width);
            TEST_ASSERT_EQUAL_UINT32(4, header.height);
        }
        TEST_ASSERT_EQUAL_UINT8(texture_count == 1 ? 0 : NT_TEXTURE_FLAG_PREMULTIPLIED, header.flags);
        uint8_t basis[16384] = {0};
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(basis), header.data_size);
        TEST_ASSERT_EQUAL(header.data_size, fread(basis, 1, header.data_size, file));
        TEST_ASSERT_EQUAL(NT_TEXTURE_COMPRESSION_BASIS, header.compression);
        const nt_basisu_info_t info = nt_basisu_info(basis, header.data_size);
        TEST_ASSERT_EQUAL_INT(codec, info.codec);
        TEST_ASSERT_FALSE(header.flags & NT_TEXTURE_FLAG_GEN_MIPMAPS);
        TEST_ASSERT_EQUAL(NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_LINEAR, header.default_min_filter);
        uint32_t levels = 1;
        for (uint32_t size = header.width > header.height ? header.width : header.height; size > 1; size >>= 1U) {
            levels++;
        }
        TEST_ASSERT_EQUAL_UINT32(levels, header.mip_count);
        TEST_ASSERT_EQUAL_UINT32(levels, info.level_count);
        uint8_t chain[32 * 32 * 4 * 2];
        const uint32_t bytes = chain_bytes(NT_TEXTURE_FORMAT_RGBA8, header.width, header.height, levels);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(chain), bytes);
        nt_basisu_transcode_chain(basis, header.data_size, &info, NT_TEXTURE_FORMAT_RGBA8, chain);
        uint32_t offset = 0;
        for (uint32_t level = 0; level < levels; level++) {
            uint32_t w = (header.width >> level) ? (header.width >> level) : 1U;
            uint32_t h = (header.height >> level) ? (header.height >> level) : 1U;
            const uint8_t *rgba = chain + offset;
            offset += w * h * 4;
            if (atlas_page && level == 0) {
                uint32_t alpha_sum = 0;
                for (uint32_t pixel = 0; pixel < w * h; pixel++) {
                    alpha_sum += rgba[(pixel * 4) + 3];
                }
                TEST_ASSERT_UINT32_WITHIN(12 * w * h, 16 * 8 * 128, alpha_sum);
            }
            for (uint32_t pixel = 0; pixel < w * h; pixel++) {
                for (uint32_t c = 0; c < 4; c++) {
                    uint32_t value = atlas_page ? (uint32_t)expected[c] * rgba[(pixel * 4) + 3] / expected[3] : expected[c];
                    TEST_ASSERT_INT_WITHIN(12, value, rgba[(pixel * 4) + c]);
                }
            }
        }
    }
    (void)fclose(file);
    TEST_ASSERT_EQUAL_UINT32(texture_count, textures);
}

// NOLINTEND(clang-analyzer-unix.Stream)

void test_public_basis_file_memory_atlas(void) {
    uint8_t tga[18 + (16 * 8 * 4)] = {0};
    tga[2] = 2;
    tga[12] = 16;
    tga[14] = 8;
    tga[16] = 32;
    tga[17] = 0x28;
    uint8_t pixels[16 * 8 * 4];
    for (uint32_t i = 0; i < 16 * 8; i++) {
        const uint8_t rgba[] = {100, 180, 60, 128};
        const uint8_t bgra[] = {60, 180, 100, 128};
        memcpy(pixels + ((size_t)i * 4), rgba, 4);
        memcpy(tga + 18 + ((size_t)i * 4), bgra, 4);
    }
    FILE *file = fopen("basis_source.tga", "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL(sizeof(tga), fwrite(tga, 1, sizeof(tga), file));
    (void)fclose(file);
    nt_basisu_encode_opts_t codecs[2];
    const uint32_t codec_count = enabled_codec_presets(codecs);
    for (uint32_t i = 0; i < codec_count; i++) {
        nt_basisu_codec_t codec = codecs[i].codec;
        const char *path = codec == NT_BASISU_CODEC_ETC1S ? "basis_public_etc1s.ntpack" : "basis_public_uastc.ntpack";
        NtBuilderContext *ctx = nt_builder_start_pack(path);
        nt_builder_set_threads(ctx, 2);
        nt_tex_opts_t opts = nt_tex_opts_defaults();
        opts.compress = codecs[i];
        opts.max_size = 8;
        opts.process.alpha = NT_TEX_ALPHA_PREMULTIPLIED;
        opts.mips = NT_TEX_MIPS_BAKED;
        nt_builder_add_texture(ctx, "basis_source.tga", &opts);
        opts.wrap_u = NT_TEXTURE_DEFAULT_WRAP_CLAMP_TO_EDGE;
        nt_builder_add_texture_from_memory(ctx, tga, sizeof(tga), "memory", &opts);
        uint8_t saved_tga[sizeof(tga)];
        memcpy(saved_tga, tga, sizeof(tga));
        memset(tga, 0, sizeof(tga));
        opts.wrap_u = NT_TEXTURE_DEFAULT_WRAP_MIRRORED_REPEAT;
        nt_builder_add_texture_raw(ctx, pixels, 16, 8, "raw_pixels", &opts);
        nt_atlas_opts_t atlas_opts = nt_atlas_opts_defaults();
        atlas_opts.compress = codecs[i];
        atlas_opts.mips = NT_TEX_MIPS_BAKED;
        atlas_opts.max_size = 32;
        atlas_opts.padding = 0;
        atlas_opts.shape = NT_ATLAS_SHAPE_RECT;
        atlas_opts.allowed_transforms = NT_ATLAS_TRANSFORMS_IDENTITY;
        NtAtlasBuild *atlas = nt_atlas_begin(ctx, "atlas", &atlas_opts);
        memset(&opts.compress, 0, sizeof(opts.compress));
        memset(&atlas_opts.compress, 0, sizeof(atlas_opts.compress));
        nt_atlas_sprite_opts_t sprite = nt_atlas_sprite_opts_defaults();
        sprite.name = "sprite";
        nt_atlas_add_raw(atlas, pixels, 16, 8, &sprite);
        uint8_t saved_pixels[sizeof(pixels)];
        memcpy(saved_pixels, pixels, sizeof(pixels));
        memset(pixels, 0, sizeof(pixels));
        TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_atlas_commit(atlas));
        TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
        nt_builder_free_pack(ctx);
        const uint8_t expected[] = {50, 90, 30, 128};
        check_public_pack(path, 4, expected, codec);
        memcpy(tga, saved_tga, sizeof(tga));
        memcpy(pixels, saved_pixels, sizeof(pixels));
    }
}

/* One uniform 4x4 block, the smallest level 0 Basis accepts; its chain still ends at 1x1. */
void test_public_basis_one_block_full_mip_chain(void) {
    const uint8_t pixel[4] = {70, 120, 190, 180};
    uint8_t block[4 * 4 * 4];
    for (size_t i = 0; i < sizeof(block); i += 4) {
        memcpy(block + i, pixel, sizeof(pixel));
    }
    nt_basisu_encode_opts_t codecs[2];
    const uint32_t codec_count = enabled_codec_presets(codecs);
    for (uint32_t i = 0; i < codec_count; i++) {
        nt_tex_opts_t opts = nt_tex_opts_defaults();
        opts.compress = codecs[i];
        opts.mips = NT_TEX_MIPS_BAKED;
        const char *path = codecs[i].codec == NT_BASISU_CODEC_ETC1S ? "basis_block_etc1s.ntpack" : "basis_block_uastc.ntpack";
        NtBuilderContext *ctx = nt_builder_start_pack(path);
        nt_builder_add_texture_raw(ctx, block, 4, 4, "block", &opts);
        nt_build_result_t result = nt_builder_finish_pack(ctx);
        nt_builder_free_pack(ctx);
        TEST_ASSERT_EQUAL(NT_BUILD_OK, result);
        check_public_pack(path, 1, pixel, codecs[i].codec);
    }
}

int main(void) {
    UNITY_BEGIN();
    nt_basisu_transcoder_global_init();
    nt_basisu_encoder_init();
#if NT_BASISU_HAS_ETC1S
    RUN_TEST(test_etc1s_rgb);
    RUN_TEST(test_etc1s_alpha);
    RUN_TEST(test_etc1s_alpha_cutoff_coverage);
    RUN_TEST(test_etc1s_srgb_mip);
#endif
#if NT_BASISU_HAS_UASTC
    RUN_TEST(test_uastc_rgb);
    RUN_TEST(test_uastc_alpha);
    RUN_TEST(test_mip_level_with_a_shrunken_stored_width_asserts);
    RUN_TEST(test_uastc_alpha_cutoff_coverage);
    RUN_TEST(test_uastc_srgb_mip);
#endif
    RUN_TEST(test_non_basis_header_asserts);
    RUN_TEST(test_encode_without_threads_asserts);
    RUN_TEST(test_opaque_rgba_source_reports_no_alpha);
    RUN_TEST(test_encoder_stores_the_caller_levels);
    RUN_TEST(test_alpha_less_formats_encode_opaque_levels);
    RUN_TEST(test_public_basis_one_block_full_mip_chain);
    RUN_TEST(test_public_basis_file_memory_atlas);
    return UNITY_END();
}
