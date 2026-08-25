/* Drives the basisu stub with NT_ASSERT_MODE=0 to prove the failure-return
 * safety net: under OFF every entry point returns its failure value so
 * nt_gfx's activate error path (log + FAILED asset) still runs. This does not
 * make OFF a supported runtime mode. */
#include "basisu/nt_basisu_transcoder.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const unsigned char fake_basis[16] = {0x73, 0x42, 0x13, 0x00};
static unsigned char out_buf[64];

static void test_stub_off_returns_failure(void) {
    nt_basisu_transcoder_global_init(); /* must not crash */
    TEST_ASSERT_FALSE(nt_basisu_validate_header(fake_basis, sizeof(fake_basis)));
    TEST_ASSERT_EQUAL_UINT32(0, nt_basisu_get_level_count(fake_basis, sizeof(fake_basis)));
    uint32_t w = 1;
    uint32_t h = 1;
    uint32_t blocks = 1;
    TEST_ASSERT_FALSE(nt_basisu_get_level_desc(fake_basis, sizeof(fake_basis), 0, &w, &h, &blocks));
    TEST_ASSERT_FALSE(nt_basisu_start_transcoding(fake_basis, sizeof(fake_basis)));
    nt_basisu_stop_transcoding(); /* must not crash */
    TEST_ASSERT_FALSE(nt_basisu_transcode_level(fake_basis, sizeof(fake_basis), 0, out_buf, 1, NT_BASISU_FORMAT_RGBA32));
    TEST_ASSERT_EQUAL_UINT32(0, nt_basisu_bytes_per_block(NT_BASISU_FORMAT_BC7_RGBA));
    TEST_ASSERT_EQUAL_UINT32(0, nt_basisu_gl_internal_format(NT_BASISU_FORMAT_BC7_RGBA));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stub_off_returns_failure);
    return UNITY_END();
}
