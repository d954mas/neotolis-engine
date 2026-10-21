#include "core/nt_platform.h"
#include "time/nt_time.h"
#include "unity.h"

void setUp(void) { /* no fixture: the probe is a pure read */ }

void tearDown(void) { /* no teardown */ }

/* Spin until user time moves: Windows counters tick every ~15.6 ms, so a fixed loop can read no change.
 * The deadline only catches a dead probe; a loaded CI runner may get little CPU per wall second. */
void test_cpu_usage_grows_under_busy_loop(void) {
    nt_platform_cpu_t a = nt_platform_cpu_usage();
    nt_platform_cpu_t b = a;
    uint64_t deadline = nt_time_nanos() + 10000000000ULL;
    volatile uint64_t sink = 0;
    while (b.user_ns == a.user_ns && nt_time_nanos() < deadline) {
        for (uint32_t i = 0; i < 1000000U; i++) {
            sink += i;
        }
        b = nt_platform_cpu_usage();
    }
    (void)sink;
    TEST_ASSERT_TRUE(b.user_ns > a.user_ns);
    TEST_ASSERT_TRUE(b.user_ns + b.system_ns > a.user_ns + a.system_ns);
}

/* Exact float compares: Unity float asserts compile to nothing, and 25/200/0 are exact in float. */
void test_cpu_percent_one_core_quarter(void) {
    nt_platform_cpu_t prev = {1000000000U, 500000000U};
    nt_platform_cpu_t cur = {1200000000U, 550000000U};
    TEST_ASSERT_TRUE(nt_platform_cpu_percent(prev, cur, 1000000000U) == 25.0F);
}

/* Two fully busy threads over the window read as two cores, not a capped 100. */
void test_cpu_percent_exceeds_100_with_threads(void) {
    nt_platform_cpu_t prev = {0U, 0U};
    nt_platform_cpu_t cur = {1500000000U, 500000000U};
    TEST_ASSERT_TRUE(nt_platform_cpu_percent(prev, cur, 1000000000U) == 200.0F);
}

/* Time moving from user to system with a growing total is valid input. */
void test_cpu_percent_accepts_field_shift(void) {
    nt_platform_cpu_t prev = {1000000000U, 0U};
    nt_platform_cpu_t cur = {900000000U, 200000000U};
    TEST_ASSERT_TRUE(nt_platform_cpu_percent(prev, cur, 1000000000U) == 10.0F);
}

void test_cpu_percent_idle_is_zero(void) {
    nt_platform_cpu_t same = {42U, 7U};
    TEST_ASSERT_TRUE(nt_platform_cpu_percent(same, same, 1000000000U) == 0.0F);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cpu_usage_grows_under_busy_loop);
    RUN_TEST(test_cpu_percent_one_core_quarter);
    RUN_TEST(test_cpu_percent_exceeds_100_with_threads);
    RUN_TEST(test_cpu_percent_accepts_field_shift);
    RUN_TEST(test_cpu_percent_idle_is_zero);
    return UNITY_END();
}
