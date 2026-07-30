/* nt_window_add_pre_swap_hook's bool return lets nt_devapi_capture_install_seam
 * arm only after registration. Links the shared stub registry, no GL/GLFW. */

#include "test_helpers/nt_assert_trap.h"
#include "window/nt_window.h"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Distinct bodies so identical-code-folding can't merge them into one address (which would defeat the
   per-fn dedup and the table-fill below). */
static int s_hits[5];
static void hook0(void) { s_hits[0]++; }
static void hook1(void) { s_hits[1]++; }
static void hook2(void) { s_hits[2]++; }
static void hook3(void) { s_hits[3]++; }
static void hook4(void) { s_hits[4]++; }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_pre_swap_hook_registration(void) {
    TEST_ASSERT_FALSE(nt_window_add_pre_swap_hook(NULL)); /* NULL fn -> not registered. */
    TEST_ASSERT_TRUE(nt_window_add_pre_swap_hook(hook0)); /* fresh -> registered. */
    TEST_ASSERT_TRUE(nt_window_add_pre_swap_hook(hook0)); /* idempotent re-register -> true, no new slot. */
    TEST_ASSERT_TRUE(nt_window_add_pre_swap_hook(hook1));
    TEST_ASSERT_TRUE(nt_window_add_pre_swap_hook(hook2));
    TEST_ASSERT_TRUE(nt_window_add_pre_swap_hook(hook3)); /* table now full (default cap 4: hook0..3). */
    /* Overflow is a programmer invariant and must assert in supported modes. */
    NT_TEST_EXPECT_ASSERT(nt_window_add_pre_swap_hook(hook4));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pre_swap_hook_registration);
    return UNITY_END();
}
