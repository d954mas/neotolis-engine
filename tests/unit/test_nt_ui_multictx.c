/* tests/unit/test_nt_ui_multictx.c — Plan 52-00 stub
 *
 * Covers UI-03 (3-context interleave + Clay_SetCurrentContext invariant). Wave
 * 0 ships TEST_IGNORE bodies; Plan 52-02 fills with real assertions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* UI-03: 3-ctx sequential interleave — Clay_GetCurrentContext switches per begin/end */
static void test_three_ctx_interleave(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-02"); }

/* UI-03: per-ctx in_frame flag isolated (begin c1 / end c1 / begin c2 / end c2) */
static void test_per_ctx_in_frame_isolation(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-02"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_three_ctx_interleave);
    RUN_TEST(test_per_ctx_in_frame_isolation);
    return UNITY_END();
}
