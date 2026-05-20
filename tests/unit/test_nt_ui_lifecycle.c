/* tests/unit/test_nt_ui_lifecycle.c -- Plan 52-02
 *
 * Covers UI-01 (create_context + min_arena_size + misalign assert) and
 * UI-02 (destroy preserves arena bytes -- caller owns memory).
 *
 * Revision Issue 3: death-tests use NT_TEST_EXPECT_ASSERT; no Unity-
 * ignore fallback (the assert-trap macro replaces all prior stubs).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test_helpers/nt_assert_trap.h"
#include "ui/nt_ui.h"
#include "unity.h"

/* 8-byte aligned static arena via uint64_t backing array.
 * NT_UI_DEFAULT_ARENA_SIZE is 8 MiB -- divide by 8 to size in uint64_t.
 * Re-used across tests; setUp memsets to 0xAB so the "destroy preserves
 * bytes" case can verify caller-owned memory is untouched. */
static uint64_t s_arena_u64[NT_UI_DEFAULT_ARENA_SIZE / 8U];

void setUp(void) { nt_test_assert_install(); }
void tearDown(void) {}

/* UI-01: create returns non-NULL, destroy does not blow up. */
static void test_create_destroy(void) {
    void *arena = (void *)s_arena_u64;
    nt_ui_context_t *ctx = nt_ui_create_context(arena, sizeof s_arena_u64);
    TEST_ASSERT_NOT_NULL(ctx);
    /* ctx is placed in the first ~256 bytes of the arena (D-52-10).
     * Unity's TEST_ASSERT_EQUAL_PTR macro casts both args through void*, which
     * tidy flags as "casting through void"; the cast IS the macro contract so
     * the cleanest suppression is a NOLINT. */
    // NOLINTNEXTLINE(bugprone-casting-through-void)
    TEST_ASSERT_EQUAL_PTR(arena, (void *)ctx);
    nt_ui_destroy_context(ctx);
}

/* UI-01: min_arena_size > 0 and includes Clay_MinMemorySize headroom. */
static void test_min_arena_size(void) {
    size_t min = nt_ui_min_arena_size();
    /* Clay's minimum + ctx struct + alignment slack must clearly exceed
     * a trivial threshold. 4 KiB is well below the real value (Clay v0.14
     * minimum is typically ~tens of KiB). */
    TEST_ASSERT_GREATER_THAN_size_t(0U, min);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(4096U, min);
    /* And smaller than the default arena (8 MiB) -- create must succeed
     * with the default. */
    TEST_ASSERT_LESS_OR_EQUAL_size_t((size_t)NT_UI_DEFAULT_ARENA_SIZE, min);
}

/* UI-01 death-test: misaligned arena pointer asserts.
 * Construct a deliberately mis-aligned pointer by offsetting by 1 byte. */
static void test_misaligned_assert(void) {
    uint8_t *base = (uint8_t *)s_arena_u64;
    /* base is 8-aligned; base+1 is guaranteed misaligned. The arena is
     * still oversized -- the misalign should trip before any size check. */
    void *misaligned = (void *)(base + 1);
    NT_TEST_EXPECT_ASSERT(nt_ui_create_context(misaligned, sizeof s_arena_u64 - 1U));
}

/* UI-02: destroy preserves arena bytes beyond the ctx struct (caller
 * owns the memory). Fill the tail with a sentinel before create, then
 * verify it survives create + destroy. */
static void test_destroy_preserves_arena(void) {
    uint8_t *bytes = (uint8_t *)s_arena_u64;
    /* Bytes [4096..8192) are well past the ctx struct (~256 B) and the
     * Clay arena will USE some range starting right after the ctx. We
     * pick a region that overlaps Clay memory only conceptually: the
     * point of UI-02 is that destroy_context does NOT memset / free the
     * caller's bytes. After destroy, the sentinel should still be there
     * because destroy only touches sizeof(nt_ui_context_t) at the head. */
    const size_t tail_start = (size_t)NT_UI_DEFAULT_ARENA_SIZE - 4096U;
    memset(bytes + tail_start, 0xAB, 4096U);

    nt_ui_context_t *ctx = nt_ui_create_context((void *)s_arena_u64, sizeof s_arena_u64);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_ui_destroy_context(ctx);

    /* Tail bytes survive destroy. */
    for (size_t i = 0; i < 4096U; i += 64U) {
        TEST_ASSERT_EQUAL_UINT8(0xABU, bytes[tail_start + i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_destroy);
    RUN_TEST(test_min_arena_size);
    RUN_TEST(test_misaligned_assert);
    RUN_TEST(test_destroy_preserves_arena);
    return UNITY_END();
}
