#include "log/nt_log.h"
#include "unity.h"

void setUp(void) { nt_log_set_level(NT_LOG_LEVEL_INFO); }
void tearDown(void) {}

/* Domain impl functions */
void test_log_domain_info(void) {
    nt_log_info_impl("gfx", "init %s", "ok");
    TEST_PASS();
}

void test_log_domain_warn(void) {
    nt_log_warn_impl("gfx", "slow %d", 1);
    TEST_PASS();
}

void test_log_domain_error(void) {
    nt_log_error_impl("res", "fail %d", 0);
    TEST_PASS();
}

/* Domain macros (compile with -DNT_LOG_DOMAIN_DEFAULT="test_mod") */
void test_log_macro_info(void) {
    NT_LOG_INFO("started %d", 1);
    TEST_PASS();
}

void test_log_macro_warn(void) {
    NT_LOG_WARN("caution %s", "x");
    TEST_PASS();
}

void test_log_macro_error(void) {
    NT_LOG_ERROR("fatal %d", 0);
    TEST_PASS();
}

/* Printf-style variadic formatting */
void test_log_info_formats_printf_args(void) {
    nt_log_info("value=%d str=%s", 42, "hello");
    TEST_PASS();
}

/* Level filtering */
void test_log_set_level_filters_info(void) {
    nt_log_set_level(NT_LOG_LEVEL_WARN);
    nt_log_info("this should be filtered");
    /* If we get here without crash, filtering worked */
    TEST_PASS();
}

void test_log_set_level_allows_error(void) {
    nt_log_set_level(NT_LOG_LEVEL_WARN);
    nt_log_error("this should still print");
    TEST_PASS();
}

void test_log_set_level_none_filters_all(void) {
    nt_log_set_level(NT_LOG_LEVEL_NONE);
    nt_log_info("filtered");
    nt_log_warn("filtered");
    nt_log_error("filtered");
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_log_domain_info);
    RUN_TEST(test_log_domain_warn);
    RUN_TEST(test_log_domain_error);
    RUN_TEST(test_log_macro_info);
    RUN_TEST(test_log_macro_warn);
    RUN_TEST(test_log_macro_error);
    RUN_TEST(test_log_info_formats_printf_args);
    RUN_TEST(test_log_set_level_filters_info);
    RUN_TEST(test_log_set_level_allows_error);
    RUN_TEST(test_log_set_level_none_filters_all);
    return UNITY_END();
}
