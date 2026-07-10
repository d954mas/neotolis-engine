#include "nt_basisu_encoder.h"
#include "nt_basisu_transcoder.h"
#include "unity.h"

#include <string.h>

/* Encoder (nt_builder side) and runtime transcoder linked into ONE executable —
 * the in-process pack -> preview shape external tools use. Linking is half the test:
 * both libs must share a single basisu transcoder TU or the link duplicates symbols. */

#define IMG_W 16U
#define IMG_H 8U

void setUp(void) {}
void tearDown(void) {}

/* Asymmetric horizontal red gradient: survives lossy ETC1S, breaks on axis swap */
static void fill_gradient(uint8_t *rgba) {
    for (uint32_t y = 0; y < IMG_H; y++) {
        for (uint32_t x = 0; x < IMG_W; x++) {
            uint8_t *px = &rgba[(((size_t)y * IMG_W) + x) * 4];
            px[0] = (uint8_t)(x * 17U);
            px[1] = 0;
            px[2] = 0;
            px[3] = 255;
        }
    }
}

static uint32_t avg_red(const uint8_t *rgba, uint32_t x_begin, uint32_t x_end) {
    uint32_t sum = 0;
    for (uint32_t y = 0; y < IMG_H; y++) {
        for (uint32_t x = x_begin; x < x_end; x++) {
            sum += rgba[(((size_t)y * IMG_W) + x) * 4];
        }
    }
    return sum / ((x_end - x_begin) * IMG_H);
}

void test_encode_then_transcode_roundtrip(void) {
    uint8_t src[IMG_W * IMG_H * 4];
    fill_gradient(src);

    nt_basisu_encoder_init();
    nt_basisu_encode_result_t enc = nt_basisu_encode(1, src, IMG_W, IMG_H, false, false, 128, 0.0F, 0.0F, false);
    TEST_ASSERT_NOT_NULL(enc.data);
    TEST_ASSERT_GREATER_THAN_UINT32(0, enc.size);

    nt_basisu_transcoder_global_init();
    TEST_ASSERT_TRUE(nt_basisu_validate_header(enc.data, enc.size));
    TEST_ASSERT_EQUAL_UINT32(1, nt_basisu_get_level_count(enc.data, enc.size));

    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t blocks = 0;
    TEST_ASSERT_TRUE(nt_basisu_get_level_desc(enc.data, enc.size, 0, &w, &h, &blocks));
    TEST_ASSERT_EQUAL_UINT32(IMG_W, w);
    TEST_ASSERT_EQUAL_UINT32(IMG_H, h);
    TEST_ASSERT_EQUAL_UINT32((IMG_W / 4) * (IMG_H / 4), blocks);

    uint8_t out[IMG_W * IMG_H * 4];
    memset(out, 0, sizeof(out));
    TEST_ASSERT_TRUE(nt_basisu_start_transcoding(enc.data, enc.size));
    /* RGBA32: output_blocks = pixel count */
    TEST_ASSERT_TRUE(nt_basisu_transcode_level(enc.data, enc.size, 0, out, IMG_W * IMG_H, NT_BASISU_FORMAT_RGBA32));
    nt_basisu_stop_transcoding();

    /* Gradient direction survives lossy encode: right half clearly redder than left */
    uint32_t left_red = avg_red(out, 0, IMG_W / 2);
    uint32_t right_red = avg_red(out, IMG_W / 2, IMG_W);
    TEST_ASSERT_GREATER_THAN_UINT32(left_red + 64, right_red);

    nt_basisu_encode_free(&enc);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_encode_then_transcode_roundtrip);
    return UNITY_END();
}
