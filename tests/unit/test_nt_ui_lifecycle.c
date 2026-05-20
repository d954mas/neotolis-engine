/* System headers before Unity -- avoids __declspec(noreturn) clash on MSVC. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_helpers/nt_assert_trap.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena_u64[NT_UI_DEFAULT_ARENA_SIZE];

void setUp(void) { nt_test_assert_install(); }
void tearDown(void) {}

static void test_create_destroy(void) {
    void *arena = (void *)s_arena_u64;
    nt_ui_context_t *ctx = nt_ui_create_context(arena, sizeof s_arena_u64);
    TEST_ASSERT_NOT_NULL(ctx);
    // NOLINTNEXTLINE(bugprone-casting-through-void)
    TEST_ASSERT_EQUAL_PTR(arena, (void *)ctx);
    nt_ui_destroy_context(ctx);
}

static void test_min_arena_size(void) {
    size_t min = nt_ui_min_arena_size();
    TEST_ASSERT_GREATER_THAN(0, min);
    /* min must cover ctx struct + Clay_MinMemorySize. */
    TEST_ASSERT_GREATER_OR_EQUAL(sizeof(struct nt_ui_context) /* approx */, min);
}

/* Death-test: arena pointer misaligned by 1 byte must assert. */
static void test_misaligned_assert(void) {
    /* Offset the aligned arena by 1 to make it 1-byte-aligned. */
    void *bad_arena = (void *)((char *)s_arena_u64 + 1);
    NT_TEST_EXPECT_ASSERT(nt_ui_create_context(bad_arena, sizeof s_arena_u64 - 1));
}

/* destroy zeros the ctx struct but does NOT touch the rest of the arena
 * (caller owns memory). Verify bytes past sizeof(ctx) are preserved. */
static void test_destroy_preserves_arena(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena_u64, sizeof s_arena_u64);
    TEST_ASSERT_NOT_NULL(ctx);

    /* Plant a sentinel at a high offset that ctx cannot reach. */
    const size_t sentinel_offset = sizeof s_arena_u64 - 16;
    uint8_t *sentinel_ptr = (uint8_t *)s_arena_u64 + sentinel_offset;
    static const uint8_t kSentinel[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
    memcpy(sentinel_ptr, kSentinel, sizeof kSentinel);

    nt_ui_destroy_context(ctx);

    TEST_ASSERT_EQUAL_MEMORY(kSentinel, sentinel_ptr, sizeof kSentinel);
}

/* destroy_context on a mid-frame ctx must assert. */
static void test_destroy_in_frame_asserts(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena_u64, sizeof s_arena_u64);
    TEST_ASSERT_NOT_NULL(ctx);

    /* begin requires a pointer struct + the arena to be backed by nt_input
     * etc., but we only need ctx->in_frame=true for this assert path. Forge
     * it directly via the internal header. */
    ctx->in_frame = true;
    NT_TEST_EXPECT_ASSERT(nt_ui_destroy_context(ctx));

    /* Clean up: clear in_frame manually so the real destroy succeeds. */
    ctx->in_frame = false;
    nt_ui_destroy_context(ctx);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_destroy);
    RUN_TEST(test_min_arena_size);
    RUN_TEST(test_misaligned_assert);
    RUN_TEST(test_destroy_preserves_arena);
    RUN_TEST(test_destroy_in_frame_asserts);
    return UNITY_END();
}
