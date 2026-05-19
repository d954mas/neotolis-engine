/* tests/unit/test_nt_ui_measure_cb.c — Plan 52-00 stub
 *
 * Covers CLAY-03 (Clay_SetMeasureTextFunction → nt_font_measure_n). Wave 0
 * ships TEST_IGNORE bodies; Plan 52-03 fills with real assertions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* CLAY-03: measure callback wired at module init; verified via direct invocation
 * or through Clay's measure-function-registered state. */
static void test_measure_callback_wired(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-03"); }

/* CLAY-03: callback forwards Clay_StringSlice (chars+length) to nt_font_measure_n
 * with proper font_id → nt_font_t resolution. */
static void test_measure_callback_forwards_to_font_measure_n(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-03"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_measure_callback_wired);
    RUN_TEST(test_measure_callback_forwards_to_font_measure_n);
    return UNITY_END();
}
