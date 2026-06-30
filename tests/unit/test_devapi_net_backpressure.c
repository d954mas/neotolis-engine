/* Loopback transport backpressure: a large response survives a full send buffer (EWOULDBLOCK ->
 * select-wait -> delivered intact) and a wedged peer is dropped on a bounded timeout, never busy-spun.
 * All socket plumbing lives in nt_devapi_net.c (the platform boundary); this driver just asserts the
 * selftest's verdict so the test stays platform-agnostic. */

#include "devapi/nt_devapi_net.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_devapi_net_backpressure(void) {
    /* 0 = pass; any non-zero is the step number that failed (see nt_devapi_net_backpressure_selftest). */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_devapi_net_backpressure_selftest(), "backpressure selftest failed (value = failing step)");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_devapi_net_backpressure);
    return UNITY_END();
}
