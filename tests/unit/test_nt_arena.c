#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/nt_arena.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

alignas(max_align_t) static uint8_t s_buf[256];
static nt_arena_t s_arena;

void setUp(void) {
    nt_test_assert_install();
    nt_arena_init(&s_arena, s_buf, sizeof(s_buf));
}
void tearDown(void) {}

static void test_init_uses_owner_memory(void) {
    TEST_ASSERT_TRUE(s_arena.base == s_buf);
    TEST_ASSERT_EQUAL_UINT64(sizeof(s_buf), (uint64_t)s_arena.size);
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)s_arena.used);
    TEST_ASSERT_TRUE(nt_arena_alloc(&s_arena, 16U, 8U) == s_buf);
}

/* Offsets align relative to base: used 3 -> align 8 lands at 8, used becomes 16. */
static void test_alloc_aligns_up(void) {
    (void)nt_arena_alloc(&s_arena, 3U, 1U);
    uint8_t *p = (uint8_t *)nt_arena_alloc(&s_arena, 8U, 8U);
    TEST_ASSERT_TRUE(p == s_buf + 8);
    TEST_ASSERT_EQUAL_UINT64(16U, (uint64_t)s_arena.used);
}

static void test_exact_capacity_then_overflow_asserts(void) {
    (void)nt_arena_alloc(&s_arena, sizeof(s_buf), 1U);
    TEST_ASSERT_EQUAL_UINT64(sizeof(s_buf), (uint64_t)s_arena.used);
    NT_TEST_EXPECT_ASSERT(nt_arena_alloc(&s_arena, 1U, 1U));
}

/* Alignment padding counts against capacity. */
static void test_padding_overflow_asserts(void) {
    (void)nt_arena_alloc(&s_arena, sizeof(s_buf) - 10U, 1U); /* 9 bytes fit unaligned, not after padding to 8 */
    NT_TEST_EXPECT_ASSERT(nt_arena_alloc(&s_arena, 9U, 8U));
}

static void test_reset_keeps_high_water(void) {
    (void)nt_arena_alloc(&s_arena, 100U, 4U);
    nt_arena_reset(&s_arena);
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)s_arena.used);
    TEST_ASSERT_EQUAL_UINT64(100U, (uint64_t)s_arena.high_water);
    TEST_ASSERT_TRUE(nt_arena_alloc(&s_arena, 4U, 4U) == s_buf);
}

static void test_rewind_restores_used_and_keeps_high_water(void) {
    (void)nt_arena_alloc(&s_arena, 3U, 1U);
    const size_t mark = s_arena.used;
    uint8_t *tmp = (uint8_t *)nt_arena_alloc(&s_arena, 40U, 8U);
    nt_arena_rewind(&s_arena, mark);
    TEST_ASSERT_EQUAL_UINT64(3U, (uint64_t)s_arena.used);
    TEST_ASSERT_EQUAL_UINT64(48U, (uint64_t)s_arena.high_water);
    TEST_ASSERT_TRUE(nt_arena_alloc(&s_arena, 40U, 8U) == tmp);
}

static void test_rewind_forward_asserts(void) {
    (void)nt_arena_alloc(&s_arena, 16U, 8U);
    NT_TEST_EXPECT_ASSERT(nt_arena_rewind(&s_arena, 17U));
}

/* A mark taken before a reset cannot move the head forward again. */
static void test_stale_mark_after_reset_asserts(void) {
    (void)nt_arena_alloc(&s_arena, 64U, 8U);
    const size_t mark = s_arena.used;
    nt_arena_reset(&s_arena);
    NT_TEST_EXPECT_ASSERT(nt_arena_rewind(&s_arena, mark));
    TEST_ASSERT_EQUAL_UINT64(0U, (uint64_t)s_arena.used);
}

/* The typed macros pass _Alignof(T): after 1 byte, uint64_t pads to 8. */
static void test_typed_macros_align(void) {
    (void)nt_arena_alloc(&s_arena, 1U, 1U);
    TEST_ASSERT_TRUE((uint8_t *)NT_ARENA_ALLOC(&s_arena, uint64_t) == s_buf + 8);
    TEST_ASSERT_TRUE((uint8_t *)NT_ARENA_ALLOC_ARRAY(&s_arena, uint32_t, 3) == s_buf + 16);
    TEST_ASSERT_EQUAL_UINT64(28U, (uint64_t)s_arena.used);
}

static void test_alloc_array_overflow_asserts(void) { NT_TEST_EXPECT_ASSERT(nt_arena_alloc_array(&s_arena, sizeof(uint64_t), (size_t)-1, 8U)); }

static void test_bad_align_asserts(void) {
    NT_TEST_EXPECT_ASSERT(nt_arena_alloc(&s_arena, 16U, 3U));
    NT_TEST_EXPECT_ASSERT(nt_arena_alloc(&s_arena, 16U, 2U * _Alignof(max_align_t)));
}

static void test_bad_init_asserts(void) {
    nt_arena_t a;
    NT_TEST_EXPECT_ASSERT(nt_arena_init(&a, NULL, 16U));
    NT_TEST_EXPECT_ASSERT(nt_arena_init(&a, s_buf, 0U));
    NT_TEST_EXPECT_ASSERT(nt_arena_init(&a, s_buf + 1, 16U));
}

/* A zeroed (never initialized) arena has no capacity. */
static void test_alloc_on_zeroed_arena_asserts(void) {
    nt_arena_t a = {0};
    NT_TEST_EXPECT_ASSERT(nt_arena_alloc(&a, 1U, 1U));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_uses_owner_memory);
    RUN_TEST(test_alloc_aligns_up);
    RUN_TEST(test_exact_capacity_then_overflow_asserts);
    RUN_TEST(test_padding_overflow_asserts);
    RUN_TEST(test_reset_keeps_high_water);
    RUN_TEST(test_rewind_restores_used_and_keeps_high_water);
    RUN_TEST(test_rewind_forward_asserts);
    RUN_TEST(test_stale_mark_after_reset_asserts);
    RUN_TEST(test_typed_macros_align);
    RUN_TEST(test_alloc_array_overflow_asserts);
    RUN_TEST(test_bad_align_asserts);
    RUN_TEST(test_bad_init_asserts);
    RUN_TEST(test_alloc_on_zeroed_arena_asserts);
    return UNITY_END();
}
