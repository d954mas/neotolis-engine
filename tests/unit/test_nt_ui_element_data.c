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
    nt_ui_element_data_t *d = NT_UI_DATA_LAYER(5);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_UINT8(5U, d->layer);
    TEST_ASSERT_NULL(d->user_data);
}

/* NT_UI_DATA_FULL sets both layer and user_data. */
static void test_full_macro(void) {
    int payload = 42;
    nt_ui_element_data_t *d = NT_UI_DATA_FULL(7, &payload);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_UINT8(7U, d->layer);
    TEST_ASSERT_EQUAL_PTR(&payload, d->user_data);
}

/* Layer-only path uses static table (no scratch); FULL with user_data scratches. */
static void test_layer_only_no_scratch(void) {
    const size_t before = nt_mem_scratch_test_used();
    (void)NT_UI_DATA_LAYER(0);
    (void)NT_UI_DATA_LAYER(1);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)before, (uint64_t)nt_mem_scratch_test_used());
}

static void test_full_with_user_data_uses_scratch(void) {
    int payload = 1;
    const size_t before = nt_mem_scratch_test_used();
    (void)NT_UI_DATA_FULL(2, &payload);
    TEST_ASSERT_TRUE(nt_mem_scratch_test_used() > before);
}

/* Layer-only: same layer -> same pointer (static table); different layer -> distinct. */
static void test_layer_only_same_layer_same_ptr(void) {
    nt_ui_element_data_t *a = NT_UI_DATA_LAYER(3);
    nt_ui_element_data_t *b = NT_UI_DATA_LAYER(3);
    nt_ui_element_data_t *c = NT_UI_DATA_LAYER(4);
    TEST_ASSERT_EQUAL_PTR(a, b);
    TEST_ASSERT_TRUE(a != c);
    TEST_ASSERT_EQUAL_UINT8(3U, a->layer);
    TEST_ASSERT_EQUAL_UINT8(4U, c->layer);
}

/* FULL allocations are distinct (each gets its own scratch slot). */
static void test_full_distinct_allocations(void) {
    int p1 = 1;
    int p2 = 2;
    nt_ui_element_data_t *a = NT_UI_DATA_FULL(0, &p1);
    nt_ui_element_data_t *b = NT_UI_DATA_FULL(0, &p2);
    TEST_ASSERT_TRUE(a != b);
    TEST_ASSERT_EQUAL_PTR(&p1, a->user_data);
    TEST_ASSERT_EQUAL_PTR(&p2, b->user_data);
}

/* scratch_reset invalidates only FULL pointers; layer-only table survives. */
static void test_reset_releases_macro_storage(void) {
    int payload = 0;
    (void)NT_UI_DATA_FULL(1, &payload);
    const size_t after_alloc = nt_mem_scratch_test_used();
    TEST_ASSERT_TRUE(after_alloc > 0U);
    nt_mem_scratch_reset();
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)nt_mem_scratch_test_used());
    nt_ui_element_data_t *d = NT_UI_DATA_LAYER(2);
    TEST_ASSERT_EQUAL_UINT8(2U, d->layer);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_layer_only_macro);
    RUN_TEST(test_full_macro);
    RUN_TEST(test_layer_only_no_scratch);
    RUN_TEST(test_full_with_user_data_uses_scratch);
    RUN_TEST(test_layer_only_same_layer_same_ptr);
    RUN_TEST(test_full_distinct_allocations);
    RUN_TEST(test_reset_releases_macro_storage);
    return UNITY_END();
}
