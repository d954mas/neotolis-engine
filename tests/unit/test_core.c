#include "unity.h"
#include "core/nt_core.h"

#include <string.h>

void setUp(void) {
    /* Called before each test */
}

void tearDown(void) {
    /* Ensure engine is shut down between tests to reset state */
    nt_engine_shutdown();
}

void test_engine_init_succeeds(void) {
    nt_engine_config_t config = {.app_name = "test", .version = 1};
    TEST_ASSERT_EQUAL(NT_OK, nt_engine_init(&config));
}

void test_engine_init_null_config_fails(void) {
    TEST_ASSERT_EQUAL(NT_ERR_INVALID_ARG, nt_engine_init(NULL));
}

void test_engine_double_init_fails(void) {
    nt_engine_config_t config = {.app_name = "test", .version = 1};
    TEST_ASSERT_EQUAL(NT_OK, nt_engine_init(&config));
    TEST_ASSERT_EQUAL(NT_ERR_INIT_FAILED, nt_engine_init(&config));
}

void test_engine_version_string_not_empty(void) {
    const char *ver = nt_engine_version_string();
    TEST_ASSERT_NOT_NULL(ver);
    TEST_ASSERT_GREATER_THAN(0, strlen(ver));
}

void test_engine_shutdown_idempotent(void) {
    nt_engine_config_t config = {.app_name = "test", .version = 1};
    TEST_ASSERT_EQUAL(NT_OK, nt_engine_init(&config));
    nt_engine_shutdown();
    /* Second shutdown should not crash */
    nt_engine_shutdown();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_engine_init_succeeds);
    RUN_TEST(test_engine_init_null_config_fails);
    RUN_TEST(test_engine_double_init_fails);
    RUN_TEST(test_engine_version_string_not_empty);
    RUN_TEST(test_engine_shutdown_idempotent);
    return UNITY_END();
}
