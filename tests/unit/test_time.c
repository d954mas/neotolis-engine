#include "unity.h"
#include "time/nt_time.h"

#include <math.h>

/* Helper: float approximately equal (avoids UNITY_EXCLUDE_FLOAT issue) */
static bool float_near(float a, float b, float epsilon) {
    return fabsf(a - b) <= epsilon;
}

void setUp(void) {
    /* Called before each test */
}

void tearDown(void) {
    /* Called after each test */
}

/* 1. nt_accumulator_init sets all fields correctly */
void test_accumulator_init_sets_fields(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 1.0f / 60.0f, 4);

    TEST_ASSERT_TRUE(float_near(1.0f / 60.0f, acc.fixed_dt, 1e-6f));
    TEST_ASSERT_TRUE(float_near(0.0f, acc.accumulator, 1e-6f));
    TEST_ASSERT_EQUAL_INT(4, acc.max_steps);
    TEST_ASSERT_EQUAL_INT(0, acc.steps_this_frame);
}

/* 2. Add exactly 1 fixed_dt, step returns true once then false */
void test_accumulator_single_step(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 1.0f / 60.0f, 4);
    nt_accumulator_add(&acc, 1.0f / 60.0f);

    TEST_ASSERT_TRUE(nt_accumulator_step(&acc));
    TEST_ASSERT_FALSE(nt_accumulator_step(&acc));
}

/* 3. Add 3*fixed_dt, step returns true 3 times then false */
void test_accumulator_multiple_steps(void) {
    nt_accumulator_t acc;
    /* Use 0.25f (exact in IEEE 754) to avoid float-subtraction drift */
    nt_accumulator_init(&acc, 0.25f, 10);
    nt_accumulator_add(&acc, 0.75f);

    TEST_ASSERT_TRUE(nt_accumulator_step(&acc));
    TEST_ASSERT_TRUE(nt_accumulator_step(&acc));
    TEST_ASSERT_TRUE(nt_accumulator_step(&acc));
    TEST_ASSERT_FALSE(nt_accumulator_step(&acc));
}

/* 4. Max steps clamp: add 10*fixed_dt with max_steps=4, only 4 steps */
void test_accumulator_max_steps_clamp(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 1.0f / 60.0f, 4);
    nt_accumulator_add(&acc, 10.0f / 60.0f);

    int count = 0;
    while (nt_accumulator_step(&acc)) {
        count++;
    }
    TEST_ASSERT_EQUAL_INT(4, count);
    TEST_ASSERT_EQUAL_INT(4, acc.steps_this_frame);
}

/* 5. Add less than fixed_dt, step returns false immediately */
void test_accumulator_partial_step(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 1.0f / 60.0f, 4);
    nt_accumulator_add(&acc, 0.5f / 60.0f);

    TEST_ASSERT_FALSE(nt_accumulator_step(&acc));
}

/* 6. steps_this_frame resets to 0 on nt_accumulator_add */
void test_accumulator_steps_this_frame_resets_on_add(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 1.0f / 60.0f, 4);

    /* First frame: do some steps */
    nt_accumulator_add(&acc, 2.0f / 60.0f);
    nt_accumulator_step(&acc);
    nt_accumulator_step(&acc);
    TEST_ASSERT_EQUAL_INT(2, acc.steps_this_frame);

    /* Second frame: add resets steps_this_frame */
    nt_accumulator_add(&acc, 1.0f / 60.0f);
    TEST_ASSERT_EQUAL_INT(0, acc.steps_this_frame);
}

/* 7. Alpha returns correct interpolation factor (~0.5 for half fixed_dt) */
void test_accumulator_alpha_range(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 1.0f / 60.0f, 4);
    nt_accumulator_add(&acc, 0.5f / 60.0f);

    float alpha = nt_accumulator_alpha(&acc);
    TEST_ASSERT_TRUE_MESSAGE(
        float_near(0.5f, alpha, 0.01f),
        "Alpha should be approximately 0.5 for half fixed_dt");
}

/* 8. Alpha returns 0.0 when fixed_dt is zero (safety) */
void test_accumulator_alpha_zero_fixed_dt(void) {
    nt_accumulator_t acc;
    nt_accumulator_init(&acc, 0.0f, 4);
    nt_accumulator_add(&acc, 0.1f);

    float alpha = nt_accumulator_alpha(&acc);
    TEST_ASSERT_TRUE_MESSAGE(
        float_near(0.0f, alpha, 1e-6f),
        "Alpha should be 0.0 when fixed_dt is zero");
}

/* 9. nt_time_now returns a positive value */
void test_time_now_positive(void) {
    double now = nt_time_now();
    TEST_ASSERT_TRUE_MESSAGE(now > 0.0, "nt_time_now() must be positive");
}

/* 10. Two sequential nt_time_now calls return non-decreasing values */
void test_time_now_monotonic(void) {
    double first = nt_time_now();
    double second = nt_time_now();
    TEST_ASSERT_TRUE_MESSAGE(second >= first,
                             "nt_time_now() must be monotonic");
}

/* 11. nt_time_nanos returns a positive value */
void test_time_nanos_positive(void) {
    uint64_t nanos = nt_time_nanos();
    TEST_ASSERT_TRUE_MESSAGE(nanos > 0,
                             "nt_time_nanos() must be positive");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_accumulator_init_sets_fields);
    RUN_TEST(test_accumulator_single_step);
    RUN_TEST(test_accumulator_multiple_steps);
    RUN_TEST(test_accumulator_max_steps_clamp);
    RUN_TEST(test_accumulator_partial_step);
    RUN_TEST(test_accumulator_steps_this_frame_resets_on_add);
    RUN_TEST(test_accumulator_alpha_range);
    RUN_TEST(test_accumulator_alpha_zero_fixed_dt);
    RUN_TEST(test_time_now_positive);
    RUN_TEST(test_time_now_monotonic);
    RUN_TEST(test_time_nanos_positive);
    return UNITY_END();
}
