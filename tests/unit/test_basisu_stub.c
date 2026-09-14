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

static void test_stub_info_asserts(void) {
    nt_basisu_info_t info = {0};
    NT_TEST_EXPECT_ASSERT((void)nt_basisu_info(fake_basis, sizeof(fake_basis), &info));
}

static void test_stub_transcode_chain_asserts(void) {
    const nt_basisu_info_t info = {.codec = NT_BASISU_CODEC_UASTC_LDR, .width = 2, .height = 2, .level_count = 2, .has_alpha = false};
    NT_TEST_EXPECT_ASSERT((void)nt_basisu_transcode_chain(fake_basis, sizeof(fake_basis), &info, NT_TEXTURE_FORMAT_RGBA8, out_buf, sizeof(out_buf)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stub_global_init_asserts);
    RUN_TEST(test_stub_info_asserts);
    RUN_TEST(test_stub_transcode_chain_asserts);
    return UNITY_END();
}
