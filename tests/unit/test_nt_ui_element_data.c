#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "ui/nt_ui.h"
#include "unity.h"

void setUp(void) {
    nt_test_assert_install();
    nt_mem_scratch_init(4096U);
}

void tearDown(void) { nt_mem_scratch_shutdown(); }

/* NT_UI_DATA_LAYER returns a valid ptr with layer set and user_data=NULL. */
static void test_layer_only_macro(void) {
    nt_ui_element_data_t *d = (nt_ui_element_data_t *)NT_UI_DATA_LAYER(5);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_UINT8(5U, d->layer);
    TEST_ASSERT_NULL(d->user_data);
}

/* NT_UI_DATA_FULL sets both layer and user_data. */
static void test_full_macro(void) {
    int payload = 42;
    nt_ui_element_data_t *d = (nt_ui_element_data_t *)NT_UI_DATA_FULL(7, &payload);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_UINT8(7U, d->layer);
    TEST_ASSERT_EQUAL_PTR(&payload, d->user_data);
}

/* Each macro call advances scratch usage (proves allocation actually happens). */
static void test_macro_uses_scratch(void) {
    const size_t before = nt_mem_scratch_test_used();
    (void)NT_UI_DATA_LAYER(0);
    const size_t after_first = nt_mem_scratch_test_used();
    TEST_ASSERT_TRUE(after_first > before);
    (void)NT_UI_DATA_FULL(1, NULL);
    const size_t after_second = nt_mem_scratch_test_used();
    TEST_ASSERT_TRUE(after_second > after_first);
}

/* Two macro calls return distinct pointers. */
static void test_macro_distinct_allocations(void) {
    nt_ui_element_data_t *a = (nt_ui_element_data_t *)NT_UI_DATA_LAYER(3);
    nt_ui_element_data_t *b = (nt_ui_element_data_t *)NT_UI_DATA_LAYER(4);
    TEST_ASSERT_TRUE(a != b);
    TEST_ASSERT_EQUAL_UINT8(3U, a->layer);
    TEST_ASSERT_EQUAL_UINT8(4U, b->layer);
}

/* scratch_reset invalidates prior pointers; next macro call starts from base. */
static void test_reset_releases_macro_storage(void) {
    (void)NT_UI_DATA_LAYER(1);
    const size_t after_alloc = nt_mem_scratch_test_used();
    TEST_ASSERT_TRUE(after_alloc > 0U);
    nt_mem_scratch_reset();
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)nt_mem_scratch_test_used());
    /* Macro still works after reset. */
    nt_ui_element_data_t *d = (nt_ui_element_data_t *)NT_UI_DATA_LAYER(2);
    TEST_ASSERT_EQUAL_UINT8(2U, d->layer);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_layer_only_macro);
    RUN_TEST(test_full_macro);
    RUN_TEST(test_macro_uses_scratch);
    RUN_TEST(test_macro_distinct_allocations);
    RUN_TEST(test_reset_releases_macro_storage);
    return UNITY_END();
}
