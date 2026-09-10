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

static void check_outputs(const nt_basisu_encode_result_t *enc, uint32_t level, uint32_t w, uint32_t h, uint32_t blocks, const uint8_t *src) {
    const nt_basisu_format_t formats[] = {NT_BASISU_FORMAT_ETC1_RGB, NT_BASISU_FORMAT_ETC2_RGBA, NT_BASISU_FORMAT_BC7_RGBA, NT_BASISU_FORMAT_ASTC_4x4_RGBA, NT_BASISU_FORMAT_RGBA32};
    for (uint32_t f = 0; f < sizeof(formats) / sizeof(formats[0]); f++) {
        uint8_t out[(MAX_W * MAX_H * 4) + 16];
        memset(out, 0xCD, sizeof(out));
        const uint32_t count = formats[f] == NT_BASISU_FORMAT_RGBA32 ? w * h : blocks;
        const uint32_t bytes = count * nt_basisu_bytes_per_block(formats[f]);
        TEST_ASSERT_TRUE(nt_basisu_transcode_level(enc->data, enc->size, level, out, count, formats[f]));
        for (uint32_t i = bytes; i < bytes + 16; i++) {
            TEST_ASSERT_EQUAL_HEX8(0xCD, out[i]);
        }
        if (formats[f] == NT_BASISU_FORMAT_RGBA32 && level == 0) {
            check_pixels(src, out, bytes);
        }
    }
}

static void roundtrip(uint32_t width, uint32_t height, nt_basisu_codec_t codec, bool alpha) {
    uint8_t src[MAX_W * MAX_H * 4];
    fill_pixels(src, width, height, alpha);
    nt_basisu_encode_opts_t opts = codec == NT_BASISU_CODEC_ETC1S ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, src, width, height, alpha, &opts, true);
    TEST_ASSERT_NOT_NULL(enc.data);
    TEST_ASSERT_TRUE(nt_basisu_validate_header(enc.data, enc.size));
    uint32_t levels = 1;
    for (uint32_t size = width > height ? width : height; size > 1; size >>= 1U) {
        levels++;
    }
    TEST_ASSERT_EQUAL_UINT32(levels, enc.mip_count);
    TEST_ASSERT_EQUAL_UINT32(levels, nt_basisu_get_level_count(enc.data, enc.size));
    TEST_ASSERT_TRUE(nt_basisu_start_transcoding(enc.data, enc.size));
    for (uint32_t level = 0; level < levels; level++) {
        uint32_t w = 0;
        uint32_t h = 0;
        uint32_t blocks = 0;
        TEST_ASSERT_TRUE(nt_basisu_get_level_desc(enc.data, enc.size, level, &w, &h, &blocks));
        TEST_ASSERT_EQUAL_UINT32((width >> level) ? (width >> level) : 1U, w);
        TEST_ASSERT_EQUAL_UINT32((height >> level) ? (height >> level) : 1U, h);
        TEST_ASSERT_EQUAL_UINT32(((w + 3U) / 4U) * ((h + 3U) / 4U), blocks);
        check_outputs(&enc, level, w, h, blocks, src);
    }
    nt_basisu_stop_transcoding();
    nt_basisu_encode_free(&enc);
}

static void codec_cases(nt_basisu_codec_t codec, bool alpha) {
    roundtrip(16, 8, codec, alpha);
    roundtrip(13, 7, codec, alpha);
    roundtrip(1, 1, codec, alpha);
    roundtrip(96, 64, codec, alpha);
}
void test_etc1s_rgb(void) { codec_cases(NT_BASISU_CODEC_ETC1S, false); }
void test_etc1s_alpha(void) { codec_cases(NT_BASISU_CODEC_ETC1S, true); }
void test_uastc_rgb(void) { codec_cases(NT_BASISU_CODEC_UASTC_LDR, false); }
void test_uastc_alpha(void) { codec_cases(NT_BASISU_CODEC_UASTC_LDR, true); }

void test_encode_without_mipmaps(void) {
    uint8_t pixels[16 * 8 * 4];
    fill_pixels(pixels, 16, 8, false);
    for (uint32_t i = 0; i < 2; i++) {
        nt_basisu_codec_t codec = i == 0 ? NT_BASISU_CODEC_ETC1S : NT_BASISU_CODEC_UASTC_LDR;
        nt_basisu_encode_opts_t opts = codec == NT_BASISU_CODEC_ETC1S ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
        nt_basisu_encode_result_t enc = nt_basisu_encode(1, pixels, 16, 8, false, &opts, false);
        TEST_ASSERT_NOT_NULL(enc.data);
        uint32_t mip_count = enc.mip_count;
        uint32_t levels = nt_basisu_get_level_count(enc.data, enc.size);
        nt_basisu_encode_free(&enc);
        TEST_ASSERT_EQUAL_UINT32(1, mip_count);
        TEST_ASSERT_EQUAL_UINT32(1, levels);
    }
}

void test_reject_non_basis_header(void) {
    uint8_t pixels[16 * 8 * 4];
    fill_pixels(pixels, 16, 8, false);
    TEST_ASSERT_FALSE(nt_basisu_validate_header(pixels, sizeof(pixels)));
}

static void check_premultiplied_mip(nt_basisu_codec_t codec) {
    uint8_t pixels[8 * 8 * 4];
    for (uint32_t y = 0; y < 8; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            uint8_t value = ((x + y) & 1U) ? 255 : 0;
            memset(&pixels[(((size_t)y * 8) + x) * 4], value, 4);
        }
    }
    nt_basisu_encode_opts_t opts = codec == NT_BASISU_CODEC_ETC1S ? nt_tex_compress_etc1s_high() : nt_tex_compress_uastc_default();
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, pixels, 8, 8, true, &opts, true);
    TEST_ASSERT_NOT_NULL(enc.data);
    uint8_t mip[4] = {0};
    bool started = nt_basisu_start_transcoding(enc.data, enc.size);
    bool decoded = started && nt_basisu_transcode_level(enc.data, enc.size, 3, mip, 1, NT_BASISU_FORMAT_RGBA32);
    if (started) {
        nt_basisu_stop_transcoding();
    }
    nt_basisu_encode_free(&enc);
    TEST_ASSERT_TRUE(decoded);
    /* Equal opaque-white and transparent-black coverage averages every channel to 128. */
    for (uint32_t c = 0; c < 4; c++) {
        TEST_ASSERT_INT_WITHIN(8, 128, mip[c]);
    }
}

void test_etc1s_premultiplied_mip(void) { check_premultiplied_mip(NT_BASISU_CODEC_ETC1S); }
void test_uastc_premultiplied_mip(void) { check_premultiplied_mip(NT_BASISU_CODEC_UASTC_LDR); }

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
        /* basis_file_header.m_tex_format is byte 20: ETC1S=0, UASTC LDR=1. */
        TEST_ASSERT_GREATER_THAN_UINT32(20, header.data_size);
        TEST_ASSERT_EQUAL_UINT8(codec == NT_BASISU_CODEC_ETC1S ? 0 : 1, basis[20]);
        TEST_ASSERT_FALSE(header.flags & NT_TEXTURE_FLAG_GEN_MIPMAPS);
        TEST_ASSERT_EQUAL(NT_TEXTURE_DEFAULT_FILTER_LINEAR_MIPMAP_LINEAR, header.default_min_filter);
        uint32_t levels = 1;
        for (uint32_t size = header.width > header.height ? header.width : header.height; size > 1; size >>= 1U) {
            levels++;
        }
        TEST_ASSERT_EQUAL_UINT32(levels, header.mip_count);
        TEST_ASSERT_TRUE(nt_basisu_start_transcoding(basis, header.data_size));
        for (uint32_t level = 0; level < levels; level++) {
            uint32_t w = 0;
            uint32_t h = 0;
            uint32_t blocks = 0;
            TEST_ASSERT_TRUE(nt_basisu_get_level_desc(basis, header.data_size, level, &w, &h, &blocks));
            TEST_ASSERT_EQUAL_UINT32((header.width >> level) ? (header.width >> level) : 1, w);
            TEST_ASSERT_EQUAL_UINT32((header.height >> level) ? (header.height >> level) : 1, h);
            uint8_t rgba[32 * 32 * 4];
            TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(rgba) / 4, w * h);
            TEST_ASSERT_TRUE(nt_basisu_transcode_level(basis, header.data_size, level, rgba, w * h, NT_BASISU_FORMAT_RGBA32));
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
        nt_basisu_stop_transcoding();
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
    nt_basisu_encode_opts_t codecs[] = {nt_tex_compress_etc1s_default(), nt_tex_compress_uastc_default()};
    for (uint32_t i = 0; i < 2; i++) {
        nt_basisu_codec_t codec = codecs[i].codec;
        const char *path = i == 0 ? "basis_public_etc1s.ntpack" : "basis_public_uastc.ntpack";
        NtBuilderContext *ctx = nt_builder_start_pack(path);
        nt_builder_set_threads(ctx, 2);
        nt_tex_opts_t opts = nt_tex_opts_defaults();
        opts.compress = &codecs[i];
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
        atlas_opts.compress = &codecs[i];
        atlas_opts.max_size = 32;
        atlas_opts.padding = 0;
        atlas_opts.shape = NT_ATLAS_SHAPE_RECT;
        atlas_opts.allowed_transforms = NT_ATLAS_TRANSFORMS_IDENTITY;
        NtAtlasBuild *atlas = nt_atlas_begin(ctx, "atlas", &atlas_opts);
        memset(&codecs[i], 0, sizeof(codecs[i]));
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
    nt_basisu_encode_opts_t codecs[] = {nt_tex_compress_etc1s_default(), nt_tex_compress_uastc_default()};
    for (uint32_t i = 0; i < 2; i++) {
        nt_tex_opts_t opts = nt_tex_opts_defaults();
        opts.compress = &codecs[i];
        const char *path = i == 0 ? "basis_single_etc1s.ntpack" : "basis_single_uastc.ntpack";
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
    RUN_TEST(test_etc1s_rgb);
    RUN_TEST(test_etc1s_alpha);
    RUN_TEST(test_uastc_rgb);
    RUN_TEST(test_uastc_alpha);
    RUN_TEST(test_encode_without_mipmaps);
    RUN_TEST(test_reject_non_basis_header);
    RUN_TEST(test_etc1s_premultiplied_mip);
    RUN_TEST(test_uastc_premultiplied_mip);
    RUN_TEST(test_public_basis_single_pixel_full_mip_chain);
    RUN_TEST(test_public_basis_file_memory_atlas);
    return UNITY_END();
}
