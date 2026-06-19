/* Wave 0 scaffold for the radial-feedback phase (Phase 66).
 *
 * This is the test home for the radial WIDGET math/ABI (nt_ui_radial,
 * nt_ui_radial_image) which land in later plans (66-03 / 66-04). Plan 66-01
 * only delivers the renderer capability (extended vertex layout + custom-attr
 * emit) which is verified in test_sprite_renderer.c — so this file ships with a
 * single placeholder PASS so the CTest target exists and downstream plans have
 * a registered home to grow into. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Placeholder — widget math/ABI tests land in plans 66-03 / 66-04. */
void test_ui_radial_scaffold_present(void) { TEST_ASSERT_TRUE(true); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ui_radial_scaffold_present);
    return UNITY_END();
}
