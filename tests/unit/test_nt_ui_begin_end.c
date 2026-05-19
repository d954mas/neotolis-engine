/* tests/unit/test_nt_ui_begin_end.c — Plan 52-00 stub
 *
 * Covers UI-04 / UI-05 / UI-08 (begin/end lifecycle + stray-nested-begin
 * assert). Wave 0 ships TEST_IGNORE bodies; Plan 52-02 fills with real
 * assertions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* UI-04: nt_ui_begin sets Clay_GetCurrentContext == ctx->clay */
static void test_begin_sets_current_ctx(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-02"); }

/* UI-08 / D-52-12: stray nested begin (begin c1 → begin c2 with c1 in_frame) asserts */
static void test_stray_nested_begin_asserts(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-02"); }

/* UI-05: nt_ui_end clears g_nt_ui_inframe_ctx and ctx->in_frame */
static void test_end_clears_in_frame(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-02"); }

/* UI-05 / D-52-12: end called without begin asserts (death-test) */
static void test_end_without_begin_asserts(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-02"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_sets_current_ctx);
    RUN_TEST(test_stray_nested_begin_asserts);
    RUN_TEST(test_end_clears_in_frame);
    RUN_TEST(test_end_without_begin_asserts);
    return UNITY_END();
}
