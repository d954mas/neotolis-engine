/* tests/unit/test_nt_ui_walker_readonly.c — Plan 52-00 stub
 *
 * Covers UI-06 (multi-walk read-only invariant) and UI-07 (viewport from
 * target applied at walker entry) plus three death-tests for pre-walk
 * asserts (D-52-06 atlas, D-52-19 sprite material, D-52-19 text material).
 * Wave 0 ships TEST_IGNORE bodies; Plan 52-04 fills with real assertions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* UI-06: two walks against same ctx + target produce identical probe state */
static void test_second_walk_identical(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* UI-07: walker entry applies target->viewport via nt_gfx_set_viewport */
static void test_viewport_applied(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* D-52-06: walk without atlas set → assert (death-test) */
static void test_walk_without_atlas_asserts(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* D-52-19 (sprite material variant — Revision Issue 1): walk without sprite material asserts */
static void test_walk_without_sprite_material_asserts(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

/* D-52-19: walk without text material set → assert (death-test) */
static void test_walk_without_text_material_asserts(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-04"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_second_walk_identical);
    RUN_TEST(test_viewport_applied);
    RUN_TEST(test_walk_without_atlas_asserts);
    RUN_TEST(test_walk_without_sprite_material_asserts);
    RUN_TEST(test_walk_without_text_material_asserts);
    return UNITY_END();
}
