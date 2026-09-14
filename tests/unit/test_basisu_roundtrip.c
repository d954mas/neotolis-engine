#include "nt_basisu_encoder.h"
#include "nt_basisu_transcoder.h"
#include "nt_builder.h"
#include "nt_pack_format.h"
#include "unity.h"
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
/* NT_BASISU_TARGETS of this configure; RGBA8 is always admitted. */
static const bool s_target_enabled[] = {NT_BASISU_HAS_ETC2 != 0, NT_BASISU_HAS_ETC2 != 0, NT_BASISU_HAS_BC7 != 0, NT_BASISU_HAS_ASTC != 0, true};

/* Presets of the codecs in NT_BASISU_CODECS; the encoder itself is a superset. */
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
            /* Outside NT_BASISU_TARGETS: refused, nothing written. */
            TEST_ASSERT_FALSE(nt_basisu_transcode_chain(enc->data, enc->size, info, s_targets[f], s_out, bytes));
            for (uint32_t i = 0; i < sizeof(s_out); i++) {
                TEST_ASSERT_EQUAL_HEX8(0xCD, s_out[i]);
            }
            continue;
        }
        TEST_ASSERT_TRUE(nt_basisu_transcode_chain(enc->data, enc->size, info, s_targets[f], s_out, bytes));
        /* The chain ends exactly where the per-level sizes say it does. */
        for (uint32_t i = bytes; i < bytes + 16; i++) {
            TEST_ASSERT_EQUAL_HEX8(0xCD, s_out[i]);
        }
        if (s_targets[f] == NT_TEXTURE_FORMAT_RGBA8) {
            check_pixels(src, s_out, (uint32_t)nt_texture_level_bytes(NT_TEXTURE_FORMAT_RGBA8, info->width, info->height));
        }
        /* One byte short must be refused with no level written at all. */
        memset(s_out, 0xCD, sizeof(s_out));
        TEST_ASSERT_FALSE(nt_basisu_transcode_chain(enc->data, enc->size, info, s_targets[f], s_out, bytes - 1U));
        for (uint32_t i = 0; i < sizeof(s_out); i++) {
            TEST_ASSERT_EQUAL_HEX8(0xCD, s_out[i]);
        }
    }
}

static void roundtrip(uint32_t width, uint32_t height, nt_basisu_codec_t codec, bool alpha) {
    uint8_t src[MAX_W * MAX_H * 4];
    fill_pixels(src, width, height, alpha);
    nt_basisu_encode_opts_t opts = codec == NT_BASISU_CODEC_ETC1S ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, src, width, height, alpha, &opts);
    TEST_ASSERT_NOT_NULL(enc.data);
    uint32_t levels = 1;
    for (uint32_t size = width > height ? width : height; size > 1; size >>= 1U) {
        levels++;
    }
    TEST_ASSERT_EQUAL_UINT32(levels, enc.mip_count);

    nt_basisu_info_t info = {0};
    TEST_ASSERT_TRUE(nt_basisu_info(enc.data, enc.size, &info));
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

#if !NT_BASISU_HAS_ETC1S || !NT_BASISU_HAS_UASTC
/* The native encoder still emits the codec outside NT_BASISU_CODECS; the
 * wrapper must refuse it at info and again for a caller-built info. */
void test_codec_outside_the_set_is_refused(void) {
    uint8_t src[16 * 8 * 4];
    fill_pixels(src, 16, 8, true);
    const nt_basisu_codec_t codec = NT_BASISU_HAS_ETC1S ? NT_BASISU_CODEC_UASTC_LDR : NT_BASISU_CODEC_ETC1S;
    nt_basisu_encode_opts_t opts = codec == NT_BASISU_CODEC_ETC1S ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, src, 16, 8, true, &opts);
    TEST_ASSERT_NOT_NULL(enc.data);
    nt_basisu_info_t info = {0};
    TEST_ASSERT_FALSE(nt_basisu_info(enc.data, enc.size, &info));
    info.codec = codec;
    info.width = 16;
    info.height = 8;
    info.level_count = 5;
    info.has_alpha = true;
    memset(s_out, 0xCD, sizeof(s_out));
    TEST_ASSERT_FALSE(nt_basisu_transcode_chain(enc.data, enc.size, &info, NT_TEXTURE_FORMAT_RGBA8, s_out, chain_bytes(NT_TEXTURE_FORMAT_RGBA8, 16, 8, 5)));
    for (uint32_t i = 0; i < sizeof(s_out); i++) {
        TEST_ASSERT_EQUAL_HEX8(0xCD, s_out[i]);
    }
    nt_basisu_encode_free(&enc);
}
#endif

void test_reject_non_basis_header(void) {
    uint8_t pixels[16 * 8 * 4];
    fill_pixels(pixels, 16, 8, false);
    nt_basisu_info_t info = {0};
    TEST_ASSERT_FALSE(nt_basisu_info(pixels, sizeof(pixels), &info));
}

/* An RGBA source the encoder finds fully opaque loses its alpha slices, so the
 * blob -- not the asset header -- decides whether a target needs alpha. */
void test_opaque_rgba_source_reports_no_alpha(void) {
    uint8_t pixels[16 * 8 * 4];
    fill_pixels(pixels, 16, 8, false);
    nt_basisu_encode_opts_t codecs[2];
    const uint32_t codec_count = enabled_codec_presets(codecs);
    for (uint32_t i = 0; i < codec_count; i++) {
        nt_basisu_encode_result_t enc = nt_basisu_encode(1, pixels, 16, 8, true, &codecs[i]);
        TEST_ASSERT_NOT_NULL(enc.data);
        nt_basisu_info_t info = {0};
        TEST_ASSERT_TRUE(nt_basisu_info(enc.data, enc.size, &info));
        nt_basisu_encode_free(&enc);
        TEST_ASSERT_FALSE(info.has_alpha);
    }
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
 * with its own row stride, so nt_basisu_info has to reject the chain itself.
 * validate_header re-checks no CRC over the slice array, so the patch survives. */
void test_reject_mip_level_with_a_shrunken_stored_width(void) {
    uint8_t pixels[13 * 7 * 4];
    fill_pixels(pixels, 13, 7, false);
    nt_basisu_encode_opts_t opts = nt_tex_compress_uastc_default();
    /* Opaque UASTC: exactly one slice per level, so level 1 has its own desc. */
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, pixels, 13, 7, false, &opts);
    TEST_ASSERT_NOT_NULL(enc.data);

    nt_basisu_info_t info = {0};
    TEST_ASSERT_TRUE(nt_basisu_info(enc.data, enc.size, &info));
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

    TEST_ASSERT_FALSE(nt_basisu_info(enc.data, enc.size, &info));
    nt_basisu_encode_free(&enc);
}
#endif

static void check_premultiplied_mip(nt_basisu_codec_t codec) {
    uint8_t pixels[8 * 8 * 4];
    for (uint32_t y = 0; y < 8; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            uint8_t value = ((x + y) & 1U) ? 255 : 0;
            memset(&pixels[(((size_t)y * 8) + x) * 4], value, 4);
        }
    }
    nt_basisu_encode_opts_t opts = codec == NT_BASISU_CODEC_ETC1S ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, pixels, 8, 8, true, &opts);
    TEST_ASSERT_NOT_NULL(enc.data);
    nt_basisu_info_t info = {0};
    TEST_ASSERT_TRUE(nt_basisu_info(enc.data, enc.size, &info));
    /* 8x8 -> 4x4 -> 2x2 -> 1x1: the 4 RGBA8 bytes at the end are the tail level. */
    uint8_t chain[((8 * 8) + (4 * 4) + (2 * 2) + 1) * 4] = {0};
    bool decoded = nt_basisu_transcode_chain(enc.data, enc.size, &info, NT_TEXTURE_FORMAT_RGBA8, chain, (uint32_t)sizeof(chain));
    nt_basisu_encode_free(&enc);
    TEST_ASSERT_EQUAL_UINT32(4, info.level_count);
    TEST_ASSERT_TRUE(decoded);
    const uint8_t *mip = chain + sizeof(chain) - 4;
    /* Equal opaque-white and transparent-black coverage averages every channel to 128. */
    for (uint32_t c = 0; c < 4; c++) {
        TEST_ASSERT_INT_WITHIN(8, 128, mip[c]);
    }
}

#if NT_BASISU_HAS_ETC1S
void test_etc1s_premultiplied_mip(void) { check_premultiplied_mip(NT_BASISU_CODEC_ETC1S); }
#endif
#if NT_BASISU_HAS_UASTC
void test_uastc_premultiplied_mip(void) { check_premultiplied_mip(NT_BASISU_CODEC_UASTC_LDR); }
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
            TEST_ASSERT_EQUAL_UINT32(texture_count == 1 ? 1 : 8, header.width);
            TEST_ASSERT_EQUAL_UINT32(texture_count == 1 ? 1 : 4, header.height);
        }
        TEST_ASSERT_EQUAL_UINT8(texture_count == 1 ? 0 : NT_TEXTURE_FLAG_PREMULTIPLIED, header.flags);
        uint8_t basis[16384] = {0};
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(basis), header.data_size);
        TEST_ASSERT_EQUAL(header.data_size, fread(basis, 1, header.data_size, file));
        TEST_ASSERT_EQUAL(NT_TEXTURE_COMPRESSION_BASIS, header.compression);
        nt_basisu_info_t info = {0};
        TEST_ASSERT_TRUE(nt_basisu_info(basis, header.data_size, &info));
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
        TEST_ASSERT_TRUE(nt_basisu_transcode_chain(basis, header.data_size, &info, NT_TEXTURE_FORMAT_RGBA8, chain, bytes));
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
        opts.premultiplied = true;
        opts.gen_mipmaps = false;
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

void test_public_basis_single_pixel_full_mip_chain(void) {
    const uint8_t pixel[4] = {70, 120, 190, 180};
    nt_basisu_encode_opts_t codecs[2];
    const uint32_t codec_count = enabled_codec_presets(codecs);
    for (uint32_t i = 0; i < codec_count; i++) {
        nt_tex_opts_t opts = nt_tex_opts_defaults();
        opts.compress = codecs[i];
        const char *path = codecs[i].codec == NT_BASISU_CODEC_ETC1S ? "basis_single_etc1s.ntpack" : "basis_single_uastc.ntpack";
        NtBuilderContext *ctx = nt_builder_start_pack(path);
        nt_builder_add_texture_raw(ctx, pixel, 1, 1, "pixel", &opts);
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
    RUN_TEST(test_etc1s_premultiplied_mip);
#endif
#if NT_BASISU_HAS_UASTC
    RUN_TEST(test_uastc_rgb);
    RUN_TEST(test_uastc_alpha);
    RUN_TEST(test_reject_mip_level_with_a_shrunken_stored_width);
    RUN_TEST(test_uastc_premultiplied_mip);
#endif
#if !NT_BASISU_HAS_ETC1S || !NT_BASISU_HAS_UASTC
    RUN_TEST(test_codec_outside_the_set_is_refused);
#endif
    RUN_TEST(test_reject_non_basis_header);
    RUN_TEST(test_opaque_rgba_source_reports_no_alpha);
    RUN_TEST(test_public_basis_single_pixel_full_mip_chain);
    RUN_TEST(test_public_basis_file_memory_atlas);
    return UNITY_END();
}
