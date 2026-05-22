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
static const nt_ui_create_desc_t s_ui_desc = {.max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT, .max_scissor_depth = NT_UI_WALKER_MAX_SCISSOR_DEPTH};

void setUp(void) {
    nt_test_assert_install();
    nt_ui_module_init();
}
void tearDown(void) { nt_ui_module_shutdown(); }

static void test_begin_sets_current_ctx(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(a);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    nt_ui_begin(a, 800.0F, 600.0F, &mouse);
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());
    nt_ui_end(a);

    nt_ui_destroy_context(a);
}

static void test_stray_nested_begin_asserts(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a, &s_ui_desc);
    nt_ui_context_t *b = nt_ui_create_context(s_arena_b, sizeof s_arena_b, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    nt_ui_begin(a, 800.0F, 600.0F, &mouse);
    NT_TEST_EXPECT_ASSERT(nt_ui_begin(b, 800.0F, 600.0F, &mouse));
    /* Assert fires before begin mutates state, so a is still in-frame. */
    nt_ui_end(a);

    nt_ui_destroy_context(a);
    nt_ui_destroy_context(b);
}

static void test_end_clears_in_frame(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a, &s_ui_desc);
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

/* Symmetric with begin: end nulls Clay's current so stray CLAY_* between
 * frames fails loudly instead of corrupting the just-frozen layout. */
static void test_end_clears_clay_current(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(a);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    nt_ui_begin(a, 800.0F, 600.0F, &mouse);
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());
    nt_ui_end(a);
    TEST_ASSERT_NULL(Clay_GetCurrentContext());

    nt_ui_destroy_context(a);
}

static void test_end_without_begin_asserts(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(a);

    NT_TEST_EXPECT_ASSERT(nt_ui_end(a));

    nt_ui_destroy_context(a);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_begin_sets_current_ctx);
    RUN_TEST(test_stray_nested_begin_asserts);
    RUN_TEST(test_end_clears_in_frame);
    RUN_TEST(test_end_clears_clay_current);
    RUN_TEST(test_end_without_begin_asserts);
    return UNITY_END();
}
