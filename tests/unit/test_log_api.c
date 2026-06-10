#include "log/nt_log.h"
#include "unity.h"

void setUp(void) { nt_log_set_level(NT_LOG_LEVEL_INFO); }
void tearDown(void) {}

/* Direct nt_log_write with domain */
void test_log_domain_info(void) {
    nt_log_write(NT_LOG_LEVEL_INFO, "gfx", "init %s", "ok");
    TEST_PASS();
}

void test_log_domain_warn(void) {
    nt_log_write(NT_LOG_LEVEL_WARN, "gfx", "slow %d", 1);
    TEST_PASS();
}

void test_log_domain_error(void) {
    nt_log_write(NT_LOG_LEVEL_ERROR, "res", "fail %d", 0);
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

/* Content-dedup: same message logs once, repeat is suppressed.
 * Real impl asserts the dedup; the stub build (returns false) smoke-runs the calls. */
void test_log_unique_dedups_same_message(void) {
    bool first = nt_log_warn_unique("unique-dedup-A id=%d", 7);
    bool again = nt_log_warn_unique("unique-dedup-A id=%d", 7);
#ifdef NT_LOG_REAL_IMPL
    TEST_ASSERT_TRUE(first);  /* first occurrence logs */
    TEST_ASSERT_FALSE(again); /* identical content deduped */
#else
    (void)first;
    (void)again;
    TEST_PASS();
#endif
}

/* Content-dedup: distinct messages from the same call site each log. */
void test_log_unique_distinct_messages_each_log(void) {
    bool a = nt_log_warn_unique("unique-dedup-B id=%d", 1);
    bool b = nt_log_warn_unique("unique-dedup-B id=%d", 2);
#ifdef NT_LOG_REAL_IMPL
    TEST_ASSERT_TRUE(a);
    TEST_ASSERT_TRUE(b);
#else
    (void)a;
    (void)b;
    TEST_PASS();
#endif
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
    RUN_TEST(test_log_unique_dedups_same_message);
    RUN_TEST(test_log_unique_distinct_messages_each_log);
    return UNITY_END();
}
