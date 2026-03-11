#include "time/nt_time.h"
#include "unity.h"

#include <math.h>

/* Helper: float approximately equal (avoids UNITY_EXCLUDE_FLOAT issue) */
static bool float_near(float a, float b, float epsilon) { return fabsf(a - b) <= epsilon; }

void setUp(void) { /* Called before each test */ }

void tearDown(void) { /* Called after each test */ }

/* 1. nt_accumulator_init sets all fields correctly */
void test_accumulator_init_sets_fields(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 1.0F / 60.0F, 4);

    TEST_ASSERT_TRUE(float_near(1.0F / 60.0F, acc.fixed_dt, 1e-6F));
    TEST_ASSERT_TRUE(float_near(0.0F, acc.accumulator, 1e-6F));
    TEST_ASSERT_EQUAL_INT(4, acc.max_steps);
}

/* 2. Add exactly 1 fixed_dt, get 1 step */
void test_accumulator_single_step(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 1.0F / 60.0F, 4);
    TEST_ASSERT_EQUAL_INT(1, nt_accumulator_update(&acc, 1.0F / 60.0F));
}

/* 3. Add 3*fixed_dt, get 3 steps */
void test_accumulator_multiple_steps(void) {
    nt_accumulator_t acc;
    /* Use 0.25F (exact in IEEE 754) to avoid float-subtraction drift */
    nt_accumulator_init(&acc, 0.25F, 10);
    TEST_ASSERT_EQUAL_INT(3, nt_accumulator_update(&acc, 0.75F));
}

/* 4. Max steps clamp: add 10*fixed_dt with max_steps=4, only 4 steps */
void test_accumulator_max_steps_clamp(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 1.0F / 60.0F, 4);
    TEST_ASSERT_EQUAL_INT(4, nt_accumulator_update(&acc, 10.0F / 60.0F));
}

/* 5. Add less than fixed_dt, get 0 steps */
void test_accumulator_partial_step(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 1.0F / 60.0F, 4);
    TEST_ASSERT_EQUAL_INT(0, nt_accumulator_update(&acc, 0.5F / 60.0F));
}

/* 6. Leftover accumulates across frames */
void test_accumulator_leftover_carries(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 0.25F, 10);

    /* Add 0.3: 1 step, 0.05 leftover */
    TEST_ASSERT_EQUAL_INT(1, nt_accumulator_update(&acc, 0.3F));
    /* Add 0.2: leftover 0.05 + 0.2 = 0.25, 1 step */
    TEST_ASSERT_EQUAL_INT(1, nt_accumulator_update(&acc, 0.2F));
}

/* 7. nt_time_now returns a positive value */
void test_time_now_positive(void) {
    double now = nt_time_now();
    TEST_ASSERT_TRUE_MESSAGE(now > 0.0, "nt_time_now() must be positive");
}

/* 8. Two sequential nt_time_now calls return non-decreasing values */
void test_time_now_monotonic(void) {
    double first = nt_time_now();
    double second = nt_time_now();
    TEST_ASSERT_TRUE_MESSAGE(second >= first, "nt_time_now() must be monotonic");
}

/* 9. nt_time_nanos returns a positive value */
void test_time_nanos_positive(void) {
    uint64_t nanos = nt_time_nanos();
    TEST_ASSERT_TRUE_MESSAGE(nanos > 0, "nt_time_nanos() must be positive");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_accumulator_init_sets_fields);
    RUN_TEST(test_accumulator_single_step);
    RUN_TEST(test_accumulator_multiple_steps);
    RUN_TEST(test_accumulator_max_steps_clamp);
    RUN_TEST(test_accumulator_partial_step);
    RUN_TEST(test_accumulator_leftover_carries);
    RUN_TEST(test_time_now_positive);
    RUN_TEST(test_time_now_monotonic);
    RUN_TEST(test_time_nanos_positive);
    return UNITY_END();
}
