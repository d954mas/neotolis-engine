/* Drives the meshwire stub with NT_ASSERT_MODE=0 to prove the failure-return
 * safety net: under OFF both entry points return false so nt_gfx's activate
 * error path (log + FAILED asset) still runs. This does not make OFF a
 * supported runtime mode. */
#include "meshwire/nt_meshwire.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t fake_wire[8] = {0xE1, 0x00, 0x00, 0x00};
static uint8_t out_buf[64];

static void test_stub_off_returns_failure(void) {
    TEST_ASSERT_FALSE(nt_meshwire_decode_indices(out_buf, 3, 2, fake_wire, sizeof(fake_wire)));
    uint32_t elem_sizes[1] = {4};
    TEST_ASSERT_FALSE(nt_meshwire_reinterleave(out_buf, fake_wire, 2, elem_sizes, 1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stub_off_returns_failure);
    return UNITY_END();
}
