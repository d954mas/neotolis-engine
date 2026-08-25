/* Loud-fail contract of nt_basisu_transcoder_stub: every entry point must fire
 * NT_ASSERT on first contact (a BASIS texture in a stub build is a
 * build-composition bug). The OFF-mode failure-return contract lives in
 * test_basisu_stub_off.c. */
#include "basisu/nt_basisu_transcoder.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

void setUp(void) { nt_test_assert_install(); }
void tearDown(void) {}

static const unsigned char fake_basis[16] = {0x73, 0x42, 0x13, 0x00};
static unsigned char out_buf[64];

static void test_stub_global_init_asserts(void) { NT_TEST_EXPECT_ASSERT(nt_basisu_transcoder_global_init()); }

static void test_stub_validate_header_asserts(void) { NT_TEST_EXPECT_ASSERT((void)nt_basisu_validate_header(fake_basis, sizeof(fake_basis))); }

static void test_stub_get_level_count_asserts(void) { NT_TEST_EXPECT_ASSERT((void)nt_basisu_get_level_count(fake_basis, sizeof(fake_basis))); }

static void test_stub_get_level_desc_asserts(void) {
    uint32_t w = 0;
    uint32_t h = 0;
    uint32_t blocks = 0;
    NT_TEST_EXPECT_ASSERT((void)nt_basisu_get_level_desc(fake_basis, sizeof(fake_basis), 0, &w, &h, &blocks));
}

static void test_stub_start_transcoding_asserts(void) { NT_TEST_EXPECT_ASSERT((void)nt_basisu_start_transcoding(fake_basis, sizeof(fake_basis))); }

static void test_stub_stop_transcoding_asserts(void) { NT_TEST_EXPECT_ASSERT(nt_basisu_stop_transcoding()); }

static void test_stub_transcode_level_asserts(void) { NT_TEST_EXPECT_ASSERT((void)nt_basisu_transcode_level(fake_basis, sizeof(fake_basis), 0, out_buf, 1, NT_BASISU_FORMAT_RGBA32)); }

static void test_stub_bytes_per_block_asserts(void) { NT_TEST_EXPECT_ASSERT((void)nt_basisu_bytes_per_block(NT_BASISU_FORMAT_BC7_RGBA)); }

static void test_stub_gl_internal_format_asserts(void) { NT_TEST_EXPECT_ASSERT((void)nt_basisu_gl_internal_format(NT_BASISU_FORMAT_BC7_RGBA)); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stub_global_init_asserts);
    RUN_TEST(test_stub_validate_header_asserts);
    RUN_TEST(test_stub_get_level_count_asserts);
    RUN_TEST(test_stub_get_level_desc_asserts);
    RUN_TEST(test_stub_start_transcoding_asserts);
    RUN_TEST(test_stub_stop_transcoding_asserts);
    RUN_TEST(test_stub_transcode_level_asserts);
    RUN_TEST(test_stub_bytes_per_block_asserts);
    RUN_TEST(test_stub_gl_internal_format_asserts);
    return UNITY_END();
}
