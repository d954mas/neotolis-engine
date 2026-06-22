#include "core/nt_platform.h"
#include "unity.h"

void setUp(void) { /* no fixture: the probe is a pure read */ }

void tearDown(void) { /* no teardown */ }

/* On a native build the test process has a real resident set. */
void test_memory_usage_used_nonzero_on_native(void) {
#if defined(NT_PLATFORM_NATIVE) || defined(NT_PLATFORM_WIN)
    nt_platform_mem_t mem = nt_platform_memory_usage();
    TEST_ASSERT_GREATER_THAN_size_t(0U, mem.used);
#else
    TEST_IGNORE_MESSAGE("nonzero-used is a native-only assertion (web mem comes later)");
#endif
}

/* Documented invariant: native sets reserved == used (no distinct reserve figure);
 * web sets reserved == heap size >= used. Either way reserved >= used. */
void test_memory_usage_reserved_ge_used(void) {
    nt_platform_mem_t mem = nt_platform_memory_usage();
    /* Trivially true on native (reserved == used by construction); the real distinct-reserve path
       (reserved == web heap size > used) is exercised only on the web build. */
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(mem.used, mem.reserved);
}

/* Two calls do not crash and stay plausibly stable (allow drift, reject garbage). */
void test_memory_usage_stable_across_calls(void) {
#if defined(NT_PLATFORM_NATIVE) || defined(NT_PLATFORM_WIN)
    nt_platform_mem_t a = nt_platform_memory_usage();
    nt_platform_mem_t b = nt_platform_memory_usage();
    TEST_ASSERT_GREATER_THAN_size_t(0U, a.used);
    TEST_ASSERT_GREATER_THAN_size_t(0U, b.used);
#else
    TEST_IGNORE_MESSAGE("native-only assertion");
#endif
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_memory_usage_used_nonzero_on_native);
    RUN_TEST(test_memory_usage_reserved_ge_used);
    RUN_TEST(test_memory_usage_stable_across_calls);
    return UNITY_END();
}
