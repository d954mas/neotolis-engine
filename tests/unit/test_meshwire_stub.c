/* Loud-fail contract of nt_meshwire_stub: every entry point must fire
 * NT_ASSERT on first contact (a wire-encoded mesh in a stub build is a
 * build-composition bug). The OFF-mode failure-return contract lives in
 * test_meshwire_stub_off.c. */
#include "meshwire/nt_meshwire.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

void setUp(void) { nt_test_assert_install(); }
void tearDown(void) {}

static const uint8_t fake_wire[8] = {0xE1, 0x00, 0x00, 0x00};
static uint8_t out_buf[64];

static void test_stub_decode_indices_asserts(void) { NT_TEST_EXPECT_ASSERT((void)nt_meshwire_decode_indices(out_buf, 3, 2, fake_wire, sizeof(fake_wire))); }

static void test_stub_reinterleave_asserts(void) {
    uint32_t elem_sizes[1] = {4};
    NT_TEST_EXPECT_ASSERT((void)nt_meshwire_reinterleave(out_buf, fake_wire, 2, elem_sizes, 1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stub_decode_indices_asserts);
    RUN_TEST(test_stub_reinterleave_asserts);
    return UNITY_END();
}
