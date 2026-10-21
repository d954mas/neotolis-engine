#include "core/nt_platform.h"
#include "time/nt_time.h"
#include "unity.h"

void setUp(void) { /* no fixture: the probe is a pure read */ }

void tearDown(void) { /* no teardown */ }

/* Spin until user time moves: Windows counters tick every ~15.6 ms, so a fixed loop can read no change. */
void test_cpu_usage_grows_under_busy_loop(void) {
    nt_platform_cpu_t a = nt_platform_cpu_usage();
    nt_platform_cpu_t b = a;
    uint64_t deadline = nt_time_nanos() + 2000000000ULL;
    volatile uint64_t sink = 0;
    while (b.user_us == a.user_us && nt_time_nanos() < deadline) {
        for (uint32_t i = 0; i < 1000000U; i++) {
            sink += i;
        }
        b = nt_platform_cpu_usage();
    }
    (void)sink;
    TEST_ASSERT_TRUE(b.user_us > a.user_us);
    TEST_ASSERT_TRUE(b.system_us >= a.system_us);
}

void test_cpu_percent_one_core_quarter(void) {
    nt_platform_cpu_t prev = {1000000U, 500000U};
    nt_platform_cpu_t cur = {1200000U, 550000U};
    TEST_ASSERT_EQUAL_INT(25, (int)nt_platform_cpu_percent(prev, cur, 1000000U));
}

/* Two fully busy threads over the window read as two cores, not a capped 100. */
void test_cpu_percent_exceeds_100_with_threads(void) {
    nt_platform_cpu_t prev = {0U, 0U};
    nt_platform_cpu_t cur = {1500000U, 500000U};
    TEST_ASSERT_EQUAL_INT(200, (int)nt_platform_cpu_percent(prev, cur, 1000000U));
}

void test_cpu_percent_idle_is_zero(void) {
    nt_platform_cpu_t same = {42U, 7U};
    TEST_ASSERT_EQUAL_INT(0, (int)nt_platform_cpu_percent(same, same, 1000000U));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cpu_usage_grows_under_busy_loop);
    RUN_TEST(test_cpu_percent_one_core_quarter);
    RUN_TEST(test_cpu_percent_exceeds_100_with_threads);
    RUN_TEST(test_cpu_percent_idle_is_zero);
    return UNITY_END();
}
