#include "render/nt_render_defs.h"
#include "sort/nt_sort.h"
#include "unity.h"

/* Instantiate sort locally — test does not depend on nt_render */
NT_SORT_DEFINE(test_sort_fn, nt_render_item_t)

/* ---- setUp / tearDown (sort has no global state) ---- */

void setUp(void) {}
void tearDown(void) {}

/* ---- test_sort_ascending ---- */

void test_sort_ascending(void) {
    nt_render_item_t items[5] = {
        {.sort_key = 50, .entity = 1, .batch_key = 0}, {.sort_key = 10, .entity = 2, .batch_key = 0}, {.sort_key = 40, .entity = 3, .batch_key = 0},
        {.sort_key = 20, .entity = 4, .batch_key = 0}, {.sort_key = 30, .entity = 5, .batch_key = 0},
    };
    nt_render_item_t scratch[5];

    test_sort_fn(items, 5, scratch);

    TEST_ASSERT_EQUAL_UINT64(10, items[0].sort_key);
    TEST_ASSERT_EQUAL_UINT64(20, items[1].sort_key);
    TEST_ASSERT_EQUAL_UINT64(30, items[2].sort_key);
    TEST_ASSERT_EQUAL_UINT64(40, items[3].sort_key);
    TEST_ASSERT_EQUAL_UINT64(50, items[4].sort_key);
}

/* ---- test_sort_preserves_payload ---- */

void test_sort_preserves_payload(void) {
    nt_render_item_t items[3] = {
        {.sort_key = 30, .entity = 100, .batch_key = 10},
        {.sort_key = 10, .entity = 200, .batch_key = 20},
        {.sort_key = 20, .entity = 300, .batch_key = 30},
    };
    nt_render_item_t scratch[3];

    test_sort_fn(items, 3, scratch);

    /* After sort: key 10 -> entity 200, key 20 -> entity 300, key 30 -> entity 100 */
    TEST_ASSERT_EQUAL_UINT64(10, items[0].sort_key);
    TEST_ASSERT_EQUAL_UINT32(200, items[0].entity);
    TEST_ASSERT_EQUAL_UINT32(20, items[0].batch_key);

    TEST_ASSERT_EQUAL_UINT64(20, items[1].sort_key);
    TEST_ASSERT_EQUAL_UINT32(300, items[1].entity);
    TEST_ASSERT_EQUAL_UINT32(30, items[1].batch_key);

    TEST_ASSERT_EQUAL_UINT64(30, items[2].sort_key);
    TEST_ASSERT_EQUAL_UINT32(100, items[2].entity);
    TEST_ASSERT_EQUAL_UINT32(10, items[2].batch_key);
}

/* ---- test_sort_empty ---- */

void test_sort_empty(void) {
    /* count=0, NULL items -- must not crash */
    test_sort_fn(NULL, 0, NULL);
}

/* ---- test_sort_single ---- */

void test_sort_single(void) {
    nt_render_item_t item = {.sort_key = 42, .entity = 1, .batch_key = 7};
    nt_render_item_t scratch[1];

    test_sort_fn(&item, 1, scratch);

    TEST_ASSERT_EQUAL_UINT64(42, item.sort_key);
    TEST_ASSERT_EQUAL_UINT32(1, item.entity);
    TEST_ASSERT_EQUAL_UINT32(7, item.batch_key);
}

/* ---- test_sort_already_sorted ---- */

void test_sort_already_sorted(void) {
    nt_render_item_t items[4] = {
        {.sort_key = 1, .entity = 1, .batch_key = 0},
        {.sort_key = 2, .entity = 2, .batch_key = 0},
        {.sort_key = 3, .entity = 3, .batch_key = 0},
        {.sort_key = 4, .entity = 4, .batch_key = 0},
    };
    nt_render_item_t scratch[4];

    test_sort_fn(items, 4, scratch);

    TEST_ASSERT_EQUAL_UINT64(1, items[0].sort_key);
    TEST_ASSERT_EQUAL_UINT64(2, items[1].sort_key);
    TEST_ASSERT_EQUAL_UINT64(3, items[2].sort_key);
    TEST_ASSERT_EQUAL_UINT64(4, items[3].sort_key);
}

/* ---- test_sort_reverse ---- */

void test_sort_reverse(void) {
    nt_render_item_t items[4] = {
        {.sort_key = 4, .entity = 1, .batch_key = 0},
        {.sort_key = 3, .entity = 2, .batch_key = 0},
        {.sort_key = 2, .entity = 3, .batch_key = 0},
        {.sort_key = 1, .entity = 4, .batch_key = 0},
    };
    nt_render_item_t scratch[4];

    test_sort_fn(items, 4, scratch);

    TEST_ASSERT_EQUAL_UINT64(1, items[0].sort_key);
    TEST_ASSERT_EQUAL_UINT64(2, items[1].sort_key);
    TEST_ASSERT_EQUAL_UINT64(3, items[2].sort_key);
    TEST_ASSERT_EQUAL_UINT64(4, items[3].sort_key);
}

/* ---- test_sort_duplicates ---- */

void test_sort_duplicates(void) {
    nt_render_item_t items[5] = {
        {.sort_key = 3, .entity = 1, .batch_key = 0}, {.sort_key = 1, .entity = 2, .batch_key = 0}, {.sort_key = 3, .entity = 3, .batch_key = 0},
        {.sort_key = 1, .entity = 4, .batch_key = 0}, {.sort_key = 2, .entity = 5, .batch_key = 0},
    };
    nt_render_item_t scratch[5];

    test_sort_fn(items, 5, scratch);

    TEST_ASSERT_EQUAL_UINT64(1, items[0].sort_key);
    TEST_ASSERT_EQUAL_UINT64(1, items[1].sort_key);
    TEST_ASSERT_EQUAL_UINT64(2, items[2].sort_key);
    TEST_ASSERT_EQUAL_UINT64(3, items[3].sort_key);
    TEST_ASSERT_EQUAL_UINT64(3, items[4].sort_key);
}

/* ---- test_sort_stability ---- */

void test_sort_stability(void) {
    /* Items with identical sort_key must preserve relative input order (LSD radix is stable) */
    nt_render_item_t items[3] = {
        {.sort_key = 100, .entity = 1, .batch_key = 0},
        {.sort_key = 100, .entity = 2, .batch_key = 0},
        {.sort_key = 100, .entity = 3, .batch_key = 0},
    };
    nt_render_item_t scratch[3];

    test_sort_fn(items, 3, scratch);

    TEST_ASSERT_EQUAL_UINT32(1, items[0].entity);
    TEST_ASSERT_EQUAL_UINT32(2, items[1].entity);
    TEST_ASSERT_EQUAL_UINT32(3, items[2].entity);
}

/* ---- test_sort_large_keys ---- */

void test_sort_large_keys(void) {
    /* Keys using full 64-bit range (upper 32 bits differ) */
    nt_render_item_t items[4] = {
        {.sort_key = 0xFFFFFFFF00000001ULL, .entity = 1, .batch_key = 0},
        {.sort_key = 0x0000000100000000ULL, .entity = 2, .batch_key = 0},
        {.sort_key = 0x8000000000000000ULL, .entity = 3, .batch_key = 0},
        {.sort_key = 0x0000000000000001ULL, .entity = 4, .batch_key = 0},
    };
    nt_render_item_t scratch[4];

    test_sort_fn(items, 4, scratch);

    TEST_ASSERT_EQUAL_UINT64(0x0000000000000001ULL, items[0].sort_key);
    TEST_ASSERT_EQUAL_UINT64(0x0000000100000000ULL, items[1].sort_key);
    TEST_ASSERT_EQUAL_UINT64(0x8000000000000000ULL, items[2].sort_key);
    TEST_ASSERT_EQUAL_UINT64(0xFFFFFFFF00000001ULL, items[3].sort_key);
}

/* ---- test_sort_pass_skip ---- */

void test_sort_pass_skip(void) {
    /* Keys that only differ in lower 8 bits -- upper 7 passes should all be skipped */
    nt_render_item_t items[4] = {
        {.sort_key = 0xAAAAAAAAAAAA0003ULL, .entity = 1, .batch_key = 0},
        {.sort_key = 0xAAAAAAAAAAAA0001ULL, .entity = 2, .batch_key = 0},
        {.sort_key = 0xAAAAAAAAAAAA0004ULL, .entity = 3, .batch_key = 0},
        {.sort_key = 0xAAAAAAAAAAAA0002ULL, .entity = 4, .batch_key = 0},
    };
    nt_render_item_t scratch[4];

    test_sort_fn(items, 4, scratch);

    TEST_ASSERT_EQUAL_UINT64(0xAAAAAAAAAAAA0001ULL, items[0].sort_key);
    TEST_ASSERT_EQUAL_UINT64(0xAAAAAAAAAAAA0002ULL, items[1].sort_key);
    TEST_ASSERT_EQUAL_UINT64(0xAAAAAAAAAAAA0003ULL, items[2].sort_key);
    TEST_ASSERT_EQUAL_UINT64(0xAAAAAAAAAAAA0004ULL, items[3].sort_key);
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sort_ascending);
    RUN_TEST(test_sort_preserves_payload);
    RUN_TEST(test_sort_empty);
    RUN_TEST(test_sort_single);
    RUN_TEST(test_sort_already_sorted);
    RUN_TEST(test_sort_reverse);
    RUN_TEST(test_sort_duplicates);
    RUN_TEST(test_sort_stability);
    RUN_TEST(test_sort_large_keys);
    RUN_TEST(test_sort_pass_skip);
    return UNITY_END();
}
