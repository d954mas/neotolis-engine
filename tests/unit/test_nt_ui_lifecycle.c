/* tests/unit/test_nt_ui_lifecycle.c — Plan 52-00 stub
 *
 * Covers UI-01 (create_context + min_arena_size + misalign assert) and UI-02
 * (destroy preserves arena bytes). Wave 0 ships TEST_IGNORE bodies; Plan 52-02
 * fills with real assertions.
 */

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* UI-01: create_context returns non-NULL given valid arena */
static void test_create_destroy(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-02"); }

/* UI-01: min_arena_size matches Clay_MinMemorySize + sizeof(ctx) + padding */
static void test_min_arena_size(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-02"); }

/* UI-01: misaligned arena asserts (death-test — uses NT_TEST_EXPECT_ASSERT in Plan 02) */
static void test_misaligned_assert(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-02"); }

/* UI-02: destroy preserves arena bytes (caller owns memory) */
static void test_destroy_preserves_arena(void) { TEST_IGNORE_MESSAGE("Wave 0 stub — filled by plan 52-02"); }

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_destroy);
    RUN_TEST(test_min_arena_size);
    RUN_TEST(test_misaligned_assert);
    RUN_TEST(test_destroy_preserves_arena);
    return UNITY_END();
}
