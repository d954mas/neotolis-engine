#include "nt_basisu_encoder.h"
#include "nt_basisu_transcoder.h"
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

static void roundtrip(uint32_t width, uint32_t height, bool uastc, bool alpha) {
    uint8_t src[MAX_W * MAX_H * 4];
    fill_pixels(src, width, height, alpha);
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, src, width, height, alpha, uastc, uastc ? 2U : 200U, 0.0F, 0.0F, true);
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

static void codec_cases(bool uastc, bool alpha) {
    roundtrip(16, 8, uastc, alpha);
    roundtrip(13, 7, uastc, alpha);
    roundtrip(1, 1, uastc, alpha);
    roundtrip(96, 64, uastc, alpha);
}
void test_etc1s_rgb(void) { codec_cases(false, false); }
void test_etc1s_alpha(void) { codec_cases(false, true); }
void test_uastc_rgb(void) { codec_cases(true, false); }
void test_uastc_alpha(void) { codec_cases(true, true); }

void test_encode_without_mipmaps(void) {
    uint8_t pixels[16 * 8 * 4];
    fill_pixels(pixels, 16, 8, false);
    for (uint32_t codec = 0; codec < 2; codec++) {
        bool uastc = codec != 0;
        nt_basisu_encode_result_t enc = nt_basisu_encode(1, pixels, 16, 8, false, uastc, uastc ? 2U : 200U, 0.0F, 0.0F, false);
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

static void check_premultiplied_mip(bool uastc) {
    uint8_t pixels[8 * 8 * 4];
    for (uint32_t y = 0; y < 8; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            uint8_t value = ((x + y) & 1U) ? 255 : 0;
            memset(&pixels[(((size_t)y * 8) + x) * 4], value, 4);
        }
    }
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, pixels, 8, 8, true, uastc, uastc ? 2U : 200U, 0.0F, 0.0F, true);
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

void test_etc1s_premultiplied_mip(void) { check_premultiplied_mip(false); }
void test_uastc_premultiplied_mip(void) { check_premultiplied_mip(true); }

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
    return UNITY_END();
}
