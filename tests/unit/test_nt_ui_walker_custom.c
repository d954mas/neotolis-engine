/* tests/unit/test_nt_ui_walker_custom.c — Plan 52-00 stub
 *
 * Covers WALK-05 (CUSTOM → registered handler with opaque clay_cmd ptr).
 * Wave 0 ships TEST_IGNORE bodies; Plan 52-04 fills with real assertions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* WALK-05 / D-52-09: registered custom handler is invoked with (cmd, userdata) */
static void test_custom_handler_invoked(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* WALK-05 / D-52-09: NULL custom handler → silent skip (no warning, no assert) */
static void test_null_custom_handler_silent_skip(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_custom_handler_invoked);
    RUN_TEST(test_null_custom_handler_silent_skip);
    return UNITY_END();
}
