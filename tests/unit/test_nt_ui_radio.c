/* Radio widget tests (WIDGET-11 + SC#2 exclusivity). Wave-0 RED stub.
 *
 * Plan 03 fills the real exclusivity / no-flicker coverage once nt_ui_radio has
 * a body. This placeholder registers + builds against the header so the target
 * exists; it intentionally fails until Plan 03 lands the implementation. */

#include "ui/nt_ui_checkbox.h" /* shared nt_ui_checkbox_style_t + nt_ui_radio signature */
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_radio_implemented_in_plan_03(void) { TEST_FAIL_MESSAGE("Wave-0 RED: nt_ui_radio is implemented in Plan 58-03"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_radio_implemented_in_plan_03);
    return UNITY_END();
}
