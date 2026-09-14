#include "nt_texture_format.h"
#include "unity.h"

#include <stdbool.h>
#include <stdint.h>

void setUp(void) {}

void tearDown(void) {}

/* --- Predicates over the whole enum --- */

/* One row per representable value, including INVALID and the first value past
 * the last member: a classification that widens with an appended member is the
 * bug the predicates exist to prevent. */
typedef struct {
    uint32_t fmt;
    bool valid;
    bool depth;
    bool compressed;
    bool integer;
    bool sampled_color;
    bool pixel_valid;
    uint32_t bpp; /* nt_texture_bpp: 0 for everything outside the packed-asset subset */
} format_row_t;

/* clang-format off */
static const format_row_t k_format_rows[] = {
    /* format                          valid  depth  compressed  integer  sampled_color  pixel_valid  bpp */
    {NT_TEXTURE_FORMAT_INVALID,        false, false, false,      false,   false,         false,       0},
    {NT_TEXTURE_FORMAT_RGBA8,          true,  false, false,      false,   true,          true,        4},
    {NT_TEXTURE_FORMAT_RGB8,           true,  false, false,      false,   true,          true,        3},
    {NT_TEXTURE_FORMAT_RG8,            true,  false, false,      false,   true,          true,        2},
    {NT_TEXTURE_FORMAT_R8,             true,  false, false,      false,   true,          true,        1},
    {NT_TEXTURE_FORMAT_RGBA16F,        true,  false, false,      false,   true,          false,       0},
    {NT_TEXTURE_FORMAT_RG16UI,         true,  false, false,      true,    false,         false,       0},
    {NT_TEXTURE_FORMAT_RGBA32F,        true,  false, false,      false,   true,          false,       0},
    {NT_TEXTURE_FORMAT_DEPTH16,        true,  true,  false,      false,   false,         false,       0},
    {NT_TEXTURE_FORMAT_DEPTH24,        true,  true,  false,      false,   false,         false,       0},
    {NT_TEXTURE_FORMAT_DEPTH32F,       true,  true,  false,      false,   false,         false,       0},
    {NT_TEXTURE_FORMAT_ETC2_RGB8,      true,  false, true,       false,   true,          false,       0},
    {NT_TEXTURE_FORMAT_ETC2_RGBA8,     true,  false, true,       false,   true,          false,       0},
    {NT_TEXTURE_FORMAT_BC7_RGBA,       true,  false, true,       false,   true,          false,       0},
    {NT_TEXTURE_FORMAT_ASTC_4x4_RGBA,  true,  false, true,       false,   true,          false,       0},
    {15,                               false, false, false,      false,   false,         false,       0},
};
/* clang-format on */

void test_format_predicates_cover_every_value(void) {
    for (uint32_t i = 0; i < sizeof(k_format_rows) / sizeof(k_format_rows[0]); ++i) {
        const format_row_t *row = &k_format_rows[i];
        nt_texture_format_t fmt = (nt_texture_format_t)row->fmt;
        TEST_ASSERT_EQUAL_UINT(i, row->fmt);
        TEST_ASSERT_EQUAL_INT(row->valid, nt_texture_format_valid(fmt));
        TEST_ASSERT_EQUAL_INT(row->depth, nt_texture_format_is_depth(fmt));
        TEST_ASSERT_EQUAL_INT(row->compressed, nt_texture_format_is_compressed(fmt));
        TEST_ASSERT_EQUAL_INT(row->integer, nt_texture_format_is_integer(fmt));
        TEST_ASSERT_EQUAL_INT(row->sampled_color, nt_texture_format_is_sampled_color(fmt));
        TEST_ASSERT_EQUAL_INT(row->pixel_valid, nt_texture_pixel_format_valid(fmt));
        TEST_ASSERT_EQUAL_UINT32(row->bpp, nt_texture_bpp(fmt));
    }
}

/* --- nt_texture_level_bytes: pinned sizes --- */

void test_level_bytes_compressed_blocks(void) {
    /* 13x7 rounds up to 4x2 blocks, so a partial block still costs a full one. */
    TEST_ASSERT_EQUAL_UINT64(128, nt_texture_level_bytes(NT_TEXTURE_FORMAT_BC7_RGBA, 13, 7));
    TEST_ASSERT_EQUAL_UINT64(16, nt_texture_level_bytes(NT_TEXTURE_FORMAT_BC7_RGBA, 1, 1));
    TEST_ASSERT_EQUAL_UINT64(64, nt_texture_level_bytes(NT_TEXTURE_FORMAT_ETC2_RGB8, 13, 7));
    TEST_ASSERT_EQUAL_UINT64(8, nt_texture_level_bytes(NT_TEXTURE_FORMAT_ETC2_RGB8, 3, 1));
    TEST_ASSERT_EQUAL_UINT64(32, nt_texture_level_bytes(NT_TEXTURE_FORMAT_ETC2_RGBA8, 6, 3));
    TEST_ASSERT_EQUAL_UINT64(32, nt_texture_level_bytes(NT_TEXTURE_FORMAT_ASTC_4x4_RGBA, 6, 3));
}

void test_level_bytes_uncompressed(void) {
    TEST_ASSERT_EQUAL_UINT64(273, nt_texture_level_bytes(NT_TEXTURE_FORMAT_RGB8, 13, 7));
    TEST_ASSERT_EQUAL_UINT64(6, nt_texture_level_bytes(NT_TEXTURE_FORMAT_RGB8, 2, 1));
    TEST_ASSERT_EQUAL_UINT64(364, nt_texture_level_bytes(NT_TEXTURE_FORMAT_RGBA8, 13, 7));
    TEST_ASSERT_EQUAL_UINT64(182, nt_texture_level_bytes(NT_TEXTURE_FORMAT_RG8, 13, 7));
    TEST_ASSERT_EQUAL_UINT64(91, nt_texture_level_bytes(NT_TEXTURE_FORMAT_R8, 13, 7));
    TEST_ASSERT_EQUAL_UINT64(32, nt_texture_level_bytes(NT_TEXTURE_FORMAT_RGBA16F, 2, 2));
    TEST_ASSERT_EQUAL_UINT64(16, nt_texture_level_bytes(NT_TEXTURE_FORMAT_RG16UI, 2, 2));
    TEST_ASSERT_EQUAL_UINT64(16, nt_texture_level_bytes(NT_TEXTURE_FORMAT_RGBA32F, 1, 1));
}

void test_level_bytes_max_size_does_not_wrap(void) {
    /* Exactly 2^32: a 32-bit accumulator would report 0 here. */
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(4294967296), nt_texture_level_bytes(NT_TEXTURE_FORMAT_RGBA8, 32768, 32768));
}

void test_level_bytes_unsized_formats_are_zero(void) {
    TEST_ASSERT_EQUAL_UINT64(0, nt_texture_level_bytes(NT_TEXTURE_FORMAT_INVALID, 13, 7));
    TEST_ASSERT_EQUAL_UINT64(0, nt_texture_level_bytes(NT_TEXTURE_FORMAT_DEPTH16, 13, 7));
    TEST_ASSERT_EQUAL_UINT64(0, nt_texture_level_bytes(NT_TEXTURE_FORMAT_DEPTH24, 13, 7));
    TEST_ASSERT_EQUAL_UINT64(0, nt_texture_level_bytes(NT_TEXTURE_FORMAT_DEPTH32F, 13, 7));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_format_predicates_cover_every_value);
    RUN_TEST(test_level_bytes_compressed_blocks);
    RUN_TEST(test_level_bytes_uncompressed);
    RUN_TEST(test_level_bytes_max_size_does_not_wrap);
    RUN_TEST(test_level_bytes_unsized_formats_are_zero);

    return UNITY_END();
}
