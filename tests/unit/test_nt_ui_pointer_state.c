/* tests/unit/test_nt_ui_pointer_state.c — Plan 52-00 stub
 *
 * Covers CLAY-04 (Clay_SetPointerState driven by nt_pointer_t in nt_ui_begin).
 * Wave 0 ships TEST_IGNORE bodies; Plan 52-03 fills with real assertions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* CLAY-04: nt_ui_begin pulls mouse x/y/buttons[LEFT].is_down and forwards to
 * Clay_SetPointerState. Verified via Clay-side probes or via direct
 * Clay_GetPointerState observable state after nt_ui_begin returns. */
static void test_pointer_state_set_from_nt_pointer(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-03"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pointer_state_set_from_nt_pointer);
    return UNITY_END();
}
