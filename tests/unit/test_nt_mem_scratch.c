#include <stddef.h>
#include <stdint.h>

#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

/* Arena behavior (alignment, capacity, reset, rewind) is covered by test_nt_arena;
 * these pin what scratch owns: its memory and the typed macros. */

void setUp(void) { nt_test_assert_install(); }
void tearDown(void) {}

static void test_init_then_shutdown(void) {
    nt_mem_scratch_init(4096U);
    TEST_ASSERT_NOT_NULL(g_nt_mem_scratch.base);
    TEST_ASSERT_EQUAL_UINT64(4096U, (uint64_t)g_nt_mem_scratch.size);
    (void)nt_arena_alloc(&g_nt_mem_scratch, 4096U, 1U); /* exact capacity fits */
    nt_mem_scratch_shutdown();
    TEST_ASSERT_NULL(g_nt_mem_scratch.base);
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)g_nt_mem_scratch.high_water);
}

static void test_double_init_asserts(void) {
    nt_mem_scratch_init(1024U);
    NT_TEST_EXPECT_ASSERT(nt_mem_scratch_init(1024U));
    nt_mem_scratch_shutdown();
}

static void test_shutdown_without_init_asserts(void) { NT_TEST_EXPECT_ASSERT(nt_mem_scratch_shutdown()); }

static void test_alloc_without_init_asserts(void) { NT_TEST_EXPECT_ASSERT((void)NT_MEM_SCRATCH_ALLOC(uint64_t)); }

static void test_alloc_typed_macro(void) {
    nt_mem_scratch_init(4096U);
    uint64_t *p = NT_MEM_SCRATCH_ALLOC(uint64_t);
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)((uintptr_t)p & 7U));
    *p = 0xCAFEBABEDEADBEEFULL;
    TEST_ASSERT_EQUAL_UINT64(0xCAFEBABEDEADBEEFULL, *p);
    nt_mem_scratch_shutdown();
}

static void test_alloc_array_macro(void) {
    nt_mem_scratch_init(4096U);
    uint32_t *arr = NT_MEM_SCRATCH_ALLOC_ARRAY(uint32_t, 16);
    TEST_ASSERT_TRUE(arr == (uint32_t *)g_nt_mem_scratch.base);
    TEST_ASSERT_EQUAL_UINT64(16U * sizeof(uint32_t), (uint64_t)g_nt_mem_scratch.used);
    nt_mem_scratch_shutdown();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_then_shutdown);
    RUN_TEST(test_double_init_asserts);
    RUN_TEST(test_shutdown_without_init_asserts);
    RUN_TEST(test_alloc_without_init_asserts);
    RUN_TEST(test_alloc_typed_macro);
    RUN_TEST(test_alloc_array_macro);
    return UNITY_END();
}
