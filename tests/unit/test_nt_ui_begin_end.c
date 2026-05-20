/* tests/unit/test_nt_ui_begin_end.c -- Plan 52-02
 *
 * Covers UI-04 (begin sets Clay current to ctx->clay), UI-05 (end
 * clears in-frame state), UI-08 / D-52-12 (stray-nested begin asserts,
 * end-without-begin asserts).
 *
 * Revision Issue 3: death-tests use NT_TEST_EXPECT_ASSERT; no Unity-
 * ignore fallback (the assert-trap macro replaces all prior stubs).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

static uint64_t s_arena_a[NT_UI_DEFAULT_ARENA_SIZE / 8U];
static uint64_t s_arena_b[NT_UI_DEFAULT_ARENA_SIZE / 8U];

void setUp(void) { nt_test_assert_install(); }
void tearDown(void) {
    /* If a previous test left the global in-frame state dirty (e.g. a
     * death-test fired before nt_ui_end could clear it), make sure
     * subsequent tests start from a clean slate. The death-test handler
     * longjmps out of nt_ui_begin AFTER the assert fires but BEFORE the
     * in_frame=true line -- so this should already be clean. Still
     * defensive. */
}

/* UI-04: nt_ui_begin sets Clay current context to ctx->clay (FIRST
 * executable action in begin; CP-03 prevention). */
static void test_begin_sets_current_ctx(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a);
    TEST_ASSERT_NOT_NULL(a);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    nt_ui_begin(a, 800.0F, 600.0F, &mouse);
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());
    nt_ui_end(a);

    nt_ui_destroy_context(a);
}

/* UI-08 / D-52-12 death-test: stray nested begin (a is in-frame, then
 * begin b) asserts on g_nt_ui_inframe_ctx == NULL. */
static void test_stray_nested_begin_asserts(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a);
    nt_ui_context_t *b = nt_ui_create_context(s_arena_b, sizeof s_arena_b);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    nt_ui_begin(a, 800.0F, 600.0F, &mouse);
    /* The second begin must fire NT_ASSERT(g_nt_ui_inframe_ctx == NULL). */
    NT_TEST_EXPECT_ASSERT(nt_ui_begin(b, 800.0F, 600.0F, &mouse));
    /* Recover: a is still in-frame (assert fired before begin mutated
     * any state), so end it cleanly. */
    nt_ui_end(a);

    nt_ui_destroy_context(a);
    nt_ui_destroy_context(b);
}

/* UI-05: nt_ui_end clears module in-frame ctx and ctx->in_frame flag. */
static void test_end_clears_in_frame(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a);
    TEST_ASSERT_NOT_NULL(a);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    nt_ui_begin(a, 800.0F, 600.0F, &mouse);
    TEST_ASSERT_TRUE(a->in_frame);
    TEST_ASSERT_EQUAL_PTR(a, nt_ui_test_inframe_ctx());

    nt_ui_end(a);
    TEST_ASSERT_FALSE(a->in_frame);
    TEST_ASSERT_NULL(nt_ui_test_inframe_ctx());

    nt_ui_destroy_context(a);
}

/* UI-05 / D-52-12 death-test: end without a prior begin asserts on
 * ctx->in_frame. */
static void test_end_without_begin_asserts(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a);
    TEST_ASSERT_NOT_NULL(a);

    /* No begin -- end should assert on ctx->in_frame. */
    NT_TEST_EXPECT_ASSERT(nt_ui_end(a));

    nt_ui_destroy_context(a);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_sets_current_ctx);
    RUN_TEST(test_stray_nested_begin_asserts);
    RUN_TEST(test_end_clears_in_frame);
    RUN_TEST(test_end_without_begin_asserts);
    return UNITY_END();
}
