/* Progress bar widget tests (RED Wave-0 scaffold).
 *
 * 59-04 GREENs these: fill ratio (value 0..1 -> fill rect), STRETCH (slice9,
 * shared slider fill-emit path) vs CROP (UV-clipped, slice9 ignored) fill modes,
 * all 4 fill directions (LTR / RTL / bottom-up / top-down). Until then this
 * binary is intentionally RED. */

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* fill ratio scales the fill rect; STRETCH vs CROP pick distinct emit paths; the
 * 4 direction enum values mirror the fill origin correctly. */
static void test_progress_red_wave0(void) { TEST_FAIL_MESSAGE("Wave 4 (59-04 progress-and-scrollbar) — RED scaffold, not yet GREEN"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_progress_red_wave0);
    return UNITY_END();
}
