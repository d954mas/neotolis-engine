/* tests/unit/test_nt_ui_stats.c — Plan 52-00 stub
 *
 * Covers WALK-09 (ui_draw_calls + ui_element_count user counters wired at
 * nt_ui_walk exit). Wave 0 ships TEST_IGNORE bodies; Plan 52-05 fills with
 * real assertions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* WALK-09 / D-52-20: ui_draw_calls user counter updated by per-walk delta */
static void test_ui_draw_calls_counter_set(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-05"); }

/* WALK-09 / D-52-20: ui_element_count user counter equals iterated command count */
static void test_ui_element_count_counter_set(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-05"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ui_draw_calls_counter_set);
    RUN_TEST(test_ui_element_count_counter_set);
    return UNITY_END();
}
