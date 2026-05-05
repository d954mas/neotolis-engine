/* DEMO-02 unit coverage: deterministic per-frame physics integration tests
 * for examples/bunnymark/bunny_physics.h. Uses fixed RNG seed so results are
 * fully reproducible across platforms.
 *
 * Constants under test (PixiJS canonical — Pitfall 2 corrects CONTEXT D-45):
 *   BUNNY_GRAVITY       0.75
 *   BUNNY_VX_MAX        10.0
 *   BUNNY_VY_RANGE      5.0
 *   BUNNY_BOUNCE_BOT   -0.85
 *   BUNNY_BOUNCE_KICK   6.0
 *
 * Coordinate convention: y-up (D-25). gravity DECREMENTS vy, bottom edge y<0. */

#include "bunny_physics.h"
#include "unity.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Float-near helper — Unity is built with UNITY_EXCLUDE_FLOAT in this project,
 * so TEST_ASSERT_FLOAT_WITHIN expands to UNITY_TEST_FAIL. Use bool comparisons
 * via TEST_ASSERT_TRUE_MESSAGE instead (same pattern as test_time / test_stats). */
static bool float_near(float a, float b, float epsilon) { return fabsf(a - b) <= epsilon; }

void setUp(void) {}
void tearDown(void) {}

/* 1. nt_bunny_init produces vx in [0, 10) and vy in [-5, +5) over many trials. */
void test_bunny_init_velocity_ranges(void) {
    nt_bunny_rng_t rng = {.state = 12345ULL};
    for (int i = 0; i < 1000; i++) {
        nt_bunny_t b;
        nt_bunny_init(&b, 0.0F, 0.0F, &rng);
        TEST_ASSERT_TRUE_MESSAGE(b.vx >= 0.0F && b.vx < BUNNY_VX_MAX, "vx out of [0, 10)");
        TEST_ASSERT_TRUE_MESSAGE(b.vy >= -BUNNY_VY_RANGE && b.vy < BUNNY_VY_RANGE, "vy out of [-5, +5)");
        TEST_ASSERT_TRUE_MESSAGE(b.variant < 5U, "variant must be 0..4");
    }
}

/* 2. One step with vy=0 decrements vy by exactly BUNNY_GRAVITY. */
void test_bunny_gravity_decrements_vy(void) {
    nt_bunny_rng_t rng = {.state = 42ULL};
    nt_bunny_t b = {.x = 100.0F, .y = 100.0F, .vx = 0.0F, .vy = 0.0F, .variant = 0};
    nt_bunny_step(&b, 800.0F, 600.0F, &rng);
    /* After step: y = 100 + 0 = 100 (still in middle), vy = 0 - 0.75 = -0.75. */
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.vy, -BUNNY_GRAVITY, 1e-5F), "gravity must decrement vy by 0.75");
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.y, 100.0F, 1e-5F), "y unchanged when starting vy=0");
}

/* 3. Side invert at left edge: bunny at x=0.5 with vx=-2 hits left wall. */
void test_bunny_side_invert_left(void) {
    nt_bunny_rng_t rng = {.state = 77ULL};
    nt_bunny_t b = {.x = 0.5F, .y = 50.0F, .vx = -2.0F, .vy = 0.0F, .variant = 0};
    nt_bunny_step(&b, 100.0F, 100.0F, &rng);
    /* x = 0.5 + (-2) = -1.5 < 0 → vx negates, x clamps to 0. */
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.x, 0.0F, 1e-5F), "x must clamp to 0 at left edge");
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.vx, 2.0F, 1e-5F), "vx must invert to +2");
}

/* 4. Side invert at right edge: bunny at x=w-0.5 with vx=+2 hits right wall. */
void test_bunny_side_invert_right(void) {
    nt_bunny_rng_t rng = {.state = 99ULL};
    nt_bunny_t b = {.x = 99.5F, .y = 50.0F, .vx = 2.0F, .vy = 0.0F, .variant = 0};
    nt_bunny_step(&b, 100.0F, 100.0F, &rng);
    /* x = 99.5 + 2 = 101.5 > 100 → vx negates, x clamps to w. */
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.x, 100.0F, 1e-5F), "x must clamp to w at right edge");
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.vx, -2.0F, 1e-5F), "vx must invert to -2");
}

/* 5. Bottom bounce + 50% kick: 1000 trials with bunny at (50, 0.5), vy=-2.
 *    After step:
 *      y = 0.5 + (-2) = -1.5 < 0 → vy *= -0.85 (now 1.7), y clamps to 0.
 *      vy -= 0.75 (gravity already applied earlier in step) → 1.7 - 0.75 = 0.95? NO
 *    Re-read step: x+=vx; y+=vy; vy-=gravity; THEN side/bottom checks.
 *    So vy after gravity: -2 - 0.75 = -2.75. Then bottom bounce: -2.75 * -0.85 = 2.3375.
 *    Plus 50% chance kick of [0, 6) → mean adds +1.5.
 *    Expected mean vy ≈ 2.3375 + 1.5 = 3.8375 (range [2.3375, 8.3375)). */
void test_bunny_bottom_bounce_kick(void) {
    nt_bunny_rng_t rng = {.state = 12345ULL};
    const int N = 1000;
    double sum_vy = 0.0;
    int below_min = 0;
    int above_max = 0;
    for (int i = 0; i < N; i++) {
        nt_bunny_t b = {.x = 50.0F, .y = 0.5F, .vx = 0.0F, .vy = -2.0F, .variant = 0};
        nt_bunny_step(&b, 100.0F, 100.0F, &rng);
        TEST_ASSERT_TRUE_MESSAGE(float_near(b.y, 0.0F, 1e-5F), "y must clamp to 0 at bottom");
        sum_vy += (double)b.vy;
        /* No kick: vy = (-2 - 0.75) * -0.85 = 2.3375. With kick: vy in [2.3375, 8.3375). */
        if (b.vy < 2.3375F - 1e-3F) {
            below_min++;
        }
        if (b.vy >= 2.3375F + BUNNY_BOUNCE_KICK + 1e-3F) {
            above_max++;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(below_min == 0, "no sample should fall below baseline 2.3375");
    TEST_ASSERT_TRUE_MESSAGE(above_max == 0, "no sample should exceed baseline + kick range");
    /* Mean vy should land in (2.3375, 8.3375) — roughly 2.3375 + 0.5*3.0 = 3.8375.
     * Loose tolerance to keep determinism but still prove the kick works. */
    double mean_vy = sum_vy / (double)N;
    TEST_ASSERT_TRUE_MESSAGE(mean_vy > 2.5 && mean_vy < 5.5, "mean post-bounce vy in (2.5, 5.5) — kick must mix in");
}

/* 6. Top clamp (no bounce): bunny at y=h-0.5 with vy=+2 hits top edge. */
void test_bunny_top_clamp_no_bounce(void) {
    nt_bunny_rng_t rng = {.state = 17ULL};
    nt_bunny_t b = {.x = 50.0F, .y = 99.5F, .vx = 0.0F, .vy = 2.0F, .variant = 0};
    nt_bunny_step(&b, 100.0F, 100.0F, &rng);
    /* y = 99.5 + 2 = 101.5; vy = 2 - 0.75 = 1.25; then y > h → vy clamped to 0, y to h. */
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.y, 100.0F, 1e-5F), "y must clamp to h at top edge");
    TEST_ASSERT_TRUE_MESSAGE(float_near(b.vy, 0.0F, 1e-5F), "vy must clamp to 0 at top edge");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bunny_init_velocity_ranges);
    RUN_TEST(test_bunny_gravity_decrements_vy);
    RUN_TEST(test_bunny_side_invert_left);
    RUN_TEST(test_bunny_side_invert_right);
    RUN_TEST(test_bunny_bottom_bounce_kick);
    RUN_TEST(test_bunny_top_clamp_no_bounce);
    return UNITY_END();
}
