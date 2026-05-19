/* tests/unit/test_nt_ui_walker_scissor.c — Plan 52-00 stub
 *
 * Covers WALK-02 (walker-local scissor stack ≤8 + balanced exit assert) and
 * WALK-03 (Y-flip top-left → GL bottom-left + intersection at push). Wave 0
 * ships TEST_IGNORE bodies; Plan 52-04 fills with real assertions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* WALK-02: 8 nested scissors pushed and popped successfully */
static void test_scissor_depth_8_ok(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-02: 9th nested scissor asserts (overflow) — death-test */
static void test_scissor_depth_9_asserts(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-02: unbalanced stack (more SCISSOR_START than END) asserts at walker exit */
static void test_scissor_unbalanced_asserts_at_exit(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-03: Clay top-left scissor rect translates to GL bottom-left rect */
static void test_scissor_y_flip_top_left_to_gl_bottom_left(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-03: nested SCISSOR_START intersects with stack top, not replaces */
static void test_scissor_intersection_nested(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-03 / D-52-17: walker exit disables scissor (nt_gfx_set_scissor_enabled(false)) */
static void test_walker_exit_disables_scissor(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_scissor_depth_8_ok);
    RUN_TEST(test_scissor_depth_9_asserts);
    RUN_TEST(test_scissor_unbalanced_asserts_at_exit);
    RUN_TEST(test_scissor_y_flip_top_left_to_gl_bottom_left);
    RUN_TEST(test_scissor_intersection_nested);
    RUN_TEST(test_walker_exit_disables_scissor);
    return UNITY_END();
}
