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
    nt_basisu_info_t info = {0};
    TEST_ASSERT_FALSE(nt_basisu_info(fake_basis, sizeof(fake_basis), &info));
    const nt_basisu_info_t chain_info = {.codec = NT_BASISU_CODEC_UASTC_LDR, .width = 2, .height = 2, .level_count = 2, .has_alpha = false};
    TEST_ASSERT_FALSE(nt_basisu_transcode_chain(fake_basis, sizeof(fake_basis), &chain_info, NT_TEXTURE_FORMAT_RGBA8, out_buf, sizeof(out_buf)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stub_off_returns_failure);
    return UNITY_END();
}
