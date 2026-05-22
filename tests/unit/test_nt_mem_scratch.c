#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

void setUp(void) { nt_test_assert_install(); }
void tearDown(void) {}

static void teardown_if_init(void) {
    /* Used after each death-test path where init/shutdown state is uncertain. */
    if (nt_mem_scratch_test_size() > 0U) {
        nt_mem_scratch_shutdown();
    }
}

static void test_init_then_shutdown(void) {
    nt_mem_scratch_init(4096U);
    TEST_ASSERT_EQUAL_UINT64(4096U, (uint64_t)nt_mem_scratch_test_size());
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)nt_mem_scratch_test_used());
    nt_mem_scratch_shutdown();
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)nt_mem_scratch_test_size());
}

static void test_alloc_advances_used(void) {
    nt_mem_scratch_init(4096U);
    void *a = nt_mem_scratch_alloc(16U, 8U);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_UINT64(16U, (uint64_t)nt_mem_scratch_test_used());
    void *b = nt_mem_scratch_alloc(32U, 8U);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT64(48U, (uint64_t)nt_mem_scratch_test_used());
    TEST_ASSERT_TRUE((uint8_t *)b == (uint8_t *)a + 16);
    nt_mem_scratch_shutdown();
}

/* Caller asks for align=8 starting at used=3 -> next alloc must land at 8. */
static void test_alloc_aligns_up(void) {
    nt_mem_scratch_init(4096U);
    (void)nt_mem_scratch_alloc(3U, 1U); /* leave used at 3 */
    TEST_ASSERT_EQUAL_UINT64(3U, (uint64_t)nt_mem_scratch_test_used());
    uint8_t *p = (uint8_t *)nt_mem_scratch_alloc(8U, 8U);
    /* p's address must be 8-aligned. */
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)((uintptr_t)p & 7U));
    /* used jumped from 3 to 8 (padding) then +8 alloc => 16. */
    TEST_ASSERT_EQUAL_UINT64(16U, (uint64_t)nt_mem_scratch_test_used());
    nt_mem_scratch_shutdown();
}

static void test_reset_clears_used(void) {
    nt_mem_scratch_init(4096U);
    (void)nt_mem_scratch_alloc(128U, 8U);
    TEST_ASSERT_EQUAL_UINT64(128U, (uint64_t)nt_mem_scratch_test_used());
    nt_mem_scratch_reset();
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)nt_mem_scratch_test_used());
    /* Next alloc starts from the base again. */
    void *p = nt_mem_scratch_alloc(16U, 8U);
    TEST_ASSERT_EQUAL_UINT64(16U, (uint64_t)nt_mem_scratch_test_used());
    (void)p;
    nt_mem_scratch_shutdown();
}

static void test_overflow_asserts(void) {
    nt_mem_scratch_init(64U);
    (void)nt_mem_scratch_alloc(32U, 8U);
    NT_TEST_EXPECT_ASSERT(nt_mem_scratch_alloc(64U, 8U));
    teardown_if_init();
}

static void test_double_init_asserts(void) {
    nt_mem_scratch_init(1024U);
    NT_TEST_EXPECT_ASSERT(nt_mem_scratch_init(1024U));
    teardown_if_init();
}

static void test_shutdown_without_init_asserts(void) { NT_TEST_EXPECT_ASSERT(nt_mem_scratch_shutdown()); }

static void test_alloc_without_init_asserts(void) { NT_TEST_EXPECT_ASSERT(nt_mem_scratch_alloc(16U, 8U)); }

static void test_reset_without_init_asserts(void) { NT_TEST_EXPECT_ASSERT(nt_mem_scratch_reset()); }

static void test_non_pow2_align_asserts(void) {
    nt_mem_scratch_init(1024U);
    NT_TEST_EXPECT_ASSERT(nt_mem_scratch_alloc(16U, 3U));
    teardown_if_init();
}

/* Typed macro returns a properly aligned pointer. */
static void test_alloc_typed_macro(void) {
    nt_mem_scratch_init(4096U);
    uint64_t *p = NT_MEM_SCRATCH_ALLOC(uint64_t);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)((uintptr_t)p & 7U));
    *p = 0xCAFEBABEDEADBEEFULL;
    TEST_ASSERT_EQUAL_UINT64(0xCAFEBABEDEADBEEFULL, *p);
    nt_mem_scratch_shutdown();
}

static void test_alloc_array_macro(void) {
    nt_mem_scratch_init(4096U);
    uint32_t *arr = NT_MEM_SCRATCH_ALLOC_ARRAY(uint32_t, 16);
    TEST_ASSERT_NOT_NULL(arr);
    for (uint32_t i = 0U; i < 16U; ++i) {
        arr[i] = i;
    }
    for (uint32_t i = 0U; i < 16U; ++i) {
        TEST_ASSERT_EQUAL_UINT32(i, arr[i]);
    }
    nt_mem_scratch_shutdown();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_then_shutdown);
    RUN_TEST(test_alloc_advances_used);
    RUN_TEST(test_alloc_aligns_up);
    RUN_TEST(test_reset_clears_used);
    RUN_TEST(test_overflow_asserts);
    RUN_TEST(test_double_init_asserts);
    RUN_TEST(test_shutdown_without_init_asserts);
    RUN_TEST(test_alloc_without_init_asserts);
    RUN_TEST(test_reset_without_init_asserts);
    RUN_TEST(test_non_pow2_align_asserts);
    RUN_TEST(test_alloc_typed_macro);
    RUN_TEST(test_alloc_array_macro);
    return UNITY_END();
}
