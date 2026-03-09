#include "unity.h"
#include "log/nt_log.h"

void setUp(void) { }
void tearDown(void) { }

void test_log_init_does_not_crash(void) {
    nt_log_init();
    TEST_PASS();
}

void test_log_info_does_not_crash(void) {
    nt_log_info("test message");
    TEST_PASS();
}

void test_log_error_does_not_crash(void) {
    nt_log_error("test error");
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_log_init_does_not_crash);
    RUN_TEST(test_log_info_does_not_crash);
    RUN_TEST(test_log_error_does_not_crash);
    return UNITY_END();
}
