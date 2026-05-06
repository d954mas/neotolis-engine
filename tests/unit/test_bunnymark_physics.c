/* Deterministic physics tests for examples/bunnymark/bunny_physics.h.
 * Mirrors britzl/defold-bunnymark update_native_position_velocity. */

#include "bunny_physics.h"
#include "unity.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

static bool float_near(float a, float b, float epsilon) { return fabsf(a - b) <= epsilon; }

void setUp(void) {}
void tearDown(void) {}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — 1000-iter sweep over many invariants is the test's purpose
void test_bunny_init_defold_ranges(void) {
    nt_bunny_rng_t rng = {.state = 12345ULL};
    for (int i = 0; i < 1000; i++) {
        nt_bunny_t b;
        nt_bunny_init_defold(&b, 800.0F, &rng);
        TEST_ASSERT_TRUE_MESSAGE(b.x >= 0.0F && b.x < 800.0F, "x out of viewport range");
        TEST_ASSERT_TRUE_MESSAGE(b.y >= BUNNY_DEFOLD_SPAWN_Y_MIN && b.y < BUNNY_DEFOLD_SPAWN_Y_MAX, "y out of Defold spawn range");
        TEST_ASSERT_TRUE_MESSAGE(b.vy <= 0.0F && b.vy > -BUNNY_DEFOLD_VELOCITY_MAX, "vy out of Defold initial range");
        TEST_ASSERT_TRUE_MESSAGE(b.variant < 5U, "variant must be 0..4");
    }
}

void test_bunny_init_spawn_wrapper_sets_position(void) {
    nt_bunny_rng_t rng = {.state = 42ULL};
    nt_bunny_t b;
    nt_bunny_init(&b, 10.0F, 20.0F, &rng);
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.x, 10.0F, 1e-5F), "x must equal spawn_x");
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.y, 20.0F, 1e-5F), "y must equal spawn_y");
    TEST_ASSERT_TRUE_MESSAGE(b.vy <= 0.0F && b.vy > -BUNNY_DEFOLD_VELOCITY_MAX, "vy out of Defold initial range");
}

void test_bunny_step_applies_acceleration_before_position(void) {
    nt_bunny_t b = {.x = 100.0F, .y = 1000.0F, .vy = -100.0F, .variant = 0};
    nt_bunny_step(&b, 0.5F);
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.vy, -700.0F, 1e-5F), "vy must subtract 1200*dt");
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.y, 650.0F, 1e-5F), "y must advance by updated velocity");
}

void test_bunny_step_bounces_at_floor(void) {
    nt_bunny_t b = {.x = 100.0F, .y = 60.0F, .vy = -100.0F, .variant = 0};
    nt_bunny_step(&b, 0.1F);
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.y, 62.0F, 1e-5F), "y must reflect at floor to preserve energy");
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.vy, 220.0F, 1e-5F), "vy must invert after acceleration");
}

void test_bunny_step_no_horizontal_motion(void) {
    nt_bunny_t b = {.x = 123.0F, .y = 1000.0F, .vy = -100.0F, .variant = 0};
    nt_bunny_step(&b, 0.5F);
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.x, 123.0F, 1e-5F), "x must stay fixed like Defold native velocity path");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bunny_init_defold_ranges);
    RUN_TEST(test_bunny_init_spawn_wrapper_sets_position);
    RUN_TEST(test_bunny_step_applies_acceleration_before_position);
    RUN_TEST(test_bunny_step_bounces_at_floor);
    RUN_TEST(test_bunny_step_no_horizontal_motion);
    return UNITY_END();
}
