#include "log/nt_log.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_log_init_does_not_crash(void) {
    nt_log_init();
    TEST_PASS();
}

void test_log_info_does_not_crash(void) {
    nt_log_info("test message");
    TEST_PASS();
}

void test_log_info_formats_args(void) {
    nt_log_info("value=%d str=%s", 42, "hello");
    TEST_PASS();
}

void test_log_warn_does_not_crash(void) {
    nt_log_warn("warning %s", "test");
    TEST_PASS();
}

void test_log_error_does_not_crash(void) {
    nt_log_error("test error");
    TEST_PASS();
}

void test_log_error_formats_args(void) {
    nt_log_error("code=%d", 404);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_log_init_does_not_crash);
    RUN_TEST(test_log_info_does_not_crash);
    RUN_TEST(test_log_info_formats_args);
    RUN_TEST(test_log_warn_does_not_crash);
    RUN_TEST(test_log_error_does_not_crash);
    RUN_TEST(test_log_error_formats_args);
    return UNITY_END();
}
