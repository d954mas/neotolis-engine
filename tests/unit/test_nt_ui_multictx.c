/* System headers before Unity -- avoids __declspec(noreturn) clash on MSVC. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena_a[NT_UI_TEST_ARENA_SIZE];
alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena_b[NT_UI_TEST_ARENA_SIZE];
alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena_c[NT_UI_TEST_ARENA_SIZE];
static const nt_ui_create_desc_t s_ui_desc = {.max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT};

void setUp(void) {
    nt_test_assert_install();
    nt_ui_module_init();
}
void tearDown(void) { nt_ui_module_shutdown(); }

/* Three contexts coexist; begin/end pairs interleave correctly with
 * Clay_GetCurrentContext switching per ctx and the in-frame tracker
 * clearing on each end. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_three_ctx_interleave(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a, &s_ui_desc);
    nt_ui_context_t *b = nt_ui_create_context(s_arena_b, sizeof s_arena_b, &s_ui_desc);
    nt_ui_context_t *c = nt_ui_create_context(s_arena_c, sizeof s_arena_c, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    /* Each ctx has its own Clay context pointer. */
    TEST_ASSERT_NOT_EQUAL(a->clay, b->clay);
    TEST_ASSERT_NOT_EQUAL(b->clay, c->clay);
    TEST_ASSERT_NOT_EQUAL(a->clay, c->clay);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    /* a: begin sets Clay current to a->clay; in-frame ctx is a. */
    nt_ui_begin(a, 100.0F, 100.0F, &mouse);
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());
    TEST_ASSERT_EQUAL_PTR(a, nt_ui_test_inframe_ctx());
    nt_ui_end(a);
    TEST_ASSERT_NULL(nt_ui_test_inframe_ctx());

    /* b: same shape, different ctx. */
    nt_ui_begin(b, 200.0F, 200.0F, &mouse);
    TEST_ASSERT_EQUAL_PTR(b->clay, Clay_GetCurrentContext());
    TEST_ASSERT_EQUAL_PTR(b, nt_ui_test_inframe_ctx());
    nt_ui_end(b);
    TEST_ASSERT_NULL(nt_ui_test_inframe_ctx());

    /* c: same shape, different ctx. */
    nt_ui_begin(c, 300.0F, 300.0F, &mouse);
    TEST_ASSERT_EQUAL_PTR(c->clay, Clay_GetCurrentContext());
    TEST_ASSERT_EQUAL_PTR(c, nt_ui_test_inframe_ctx());
    nt_ui_end(c);
    TEST_ASSERT_NULL(nt_ui_test_inframe_ctx());

    nt_ui_destroy_context(a);
    nt_ui_destroy_context(b);
    nt_ui_destroy_context(c);
}

/* Per-ctx in_frame isolation across sequential begin/end. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_per_ctx_in_frame_isolation(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a, &s_ui_desc);
    nt_ui_context_t *b = nt_ui_create_context(s_arena_b, sizeof s_arena_b, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    /* Before any begin: no in-frame ctx, neither ctx is in_frame. */
    TEST_ASSERT_NULL(nt_ui_test_inframe_ctx());
    TEST_ASSERT_FALSE(a->in_frame);
    TEST_ASSERT_FALSE(b->in_frame);

    nt_ui_begin(a, 100.0F, 100.0F, &mouse);
    TEST_ASSERT_TRUE(a->in_frame);
    TEST_ASSERT_FALSE(b->in_frame); /* B is NOT marked in-frame */
    TEST_ASSERT_EQUAL_PTR(a, nt_ui_test_inframe_ctx());
    nt_ui_end(a);
    TEST_ASSERT_FALSE(a->in_frame);

    nt_ui_begin(b, 100.0F, 100.0F, &mouse);
    TEST_ASSERT_FALSE(a->in_frame);
    TEST_ASSERT_TRUE(b->in_frame);
    TEST_ASSERT_EQUAL_PTR(b, nt_ui_test_inframe_ctx());
    nt_ui_end(b);

    TEST_ASSERT_NULL(nt_ui_test_inframe_ctx());

    nt_ui_destroy_context(a);
    nt_ui_destroy_context(b);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_three_ctx_interleave);
    RUN_TEST(test_per_ctx_in_frame_isolation);
    return UNITY_END();
}
