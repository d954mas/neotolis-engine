/**
 * Sanitizer proof test.
 *
 * This test intentionally triggers UB to prove sanitizers are working.
 * It should FAIL (abort) under sanitizer builds, and CTest marks it as
 * PASSED because WILL_FAIL=TRUE is set on this test.
 *
 * Run separately: ctest --preset native-debug -R sanitizer_proof
 */
/* stdlib.h must come before limits.h on Windows+Clang to avoid
   __declspec(noreturn) conflict with C17 stdnoreturn.h macro. */
#include <stdlib.h>
#include <limits.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_buffer_overflow_detected(void) {
    /* Intentional heap buffer overflow -- ASan should catch this */
    volatile char *buf = (volatile char *)malloc(10);
    buf[10] = 'x'; /* One byte past end */
    free((void *)buf);
    TEST_FAIL_MESSAGE("ASan should have aborted before reaching this line");
}

void test_undefined_behavior_detected(void) {
    /* Intentional signed integer overflow -- UBSan should catch this */
    volatile int x = INT_MAX;
    x += 1; /* Signed overflow is UB */
    (void)x;
    TEST_FAIL_MESSAGE("UBSan should have aborted before reaching this line");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_buffer_overflow_detected);
    RUN_TEST(test_undefined_behavior_detected);
    return UNITY_END();
}
