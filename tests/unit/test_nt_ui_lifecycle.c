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
static const nt_ui_create_desc_t s_ui_desc = {.max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT, .max_scissor_depth = NT_UI_WALKER_MAX_SCISSOR_DEPTH};

void setUp(void) {
    nt_test_assert_install();
    nt_ui_module_init();
}
void tearDown(void) { nt_ui_module_shutdown(); }

static void test_create_destroy(void) {
    void *arena = (void *)s_arena_u64;
    nt_ui_context_t *ctx = nt_ui_create_context(arena, sizeof s_arena_u64, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(ctx);
    // NOLINTNEXTLINE(bugprone-casting-through-void)
    TEST_ASSERT_EQUAL_PTR(arena, (void *)ctx);
    nt_ui_destroy_context(ctx);
}

static void test_min_arena_size(void) {
    size_t min = nt_ui_min_arena_size(&s_ui_desc);
    TEST_ASSERT_GREATER_THAN(0, min);
    /* min must cover ctx struct + Clay_MinMemorySize. */
    TEST_ASSERT_GREATER_OR_EQUAL(sizeof(struct nt_ui_context) /* approx */, min);
}

/* Death-test: arena pointer misaligned by 1 byte must assert. */
static void test_misaligned_assert(void) {
    /* Offset the aligned arena by 1 to make it 1-byte-aligned. */
    void *bad_arena = (void *)((char *)s_arena_u64 + 1);
    NT_TEST_EXPECT_ASSERT(nt_ui_create_context(bad_arena, sizeof s_arena_u64 - 1, &s_ui_desc));
}

/* destroy zeros the ctx struct but does NOT touch the rest of the arena
 * (caller owns memory). Verify bytes past sizeof(ctx) are preserved. */
static void test_destroy_preserves_arena(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena_u64, sizeof s_arena_u64, &s_ui_desc);
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
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena_u64, sizeof s_arena_u64, &s_ui_desc);
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

/* destroy_context must null Clay's global current_ptr when it dangles at
 * the just-destroyed ctx -- nt_ui_begin parks Clay's current pointer inside
 * the arena, and memset zeroes the Clay_Context behind that pointer. */
static void test_destroy_clears_dangling_clay_current(void) {
    nt_ui_context_t *ctx = nt_ui_create_context(s_arena_u64, sizeof s_arena_u64, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(ctx);

    /* Drive a begin/end pair so Clay's current_ptr points at ctx->clay. */
    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);
    nt_ui_begin(ctx, 800.0F, 600.0F, &mouse);
    nt_ui_end(ctx);
    TEST_ASSERT_NOT_NULL(Clay_GetCurrentContext());

    nt_ui_destroy_context(ctx);
    /* Without the fix, Clay still holds a pointer into the now-zeroed arena. */
    TEST_ASSERT_NULL(Clay_GetCurrentContext());
}

/* nt_ui_min_arena_size must be a pure query: even though it has to set
 * Clay's global default to compute Clay_MinMemorySize(), it must restore
 * the prior value so external Clay consumers don't see our scratch state. */
static void test_min_arena_size_restores_clay_default(void) {
    const int32_t before = nt_ui_test_clay_default_max_element_count();
    nt_ui_create_desc_t custom = nt_ui_create_desc_defaults();
    custom.max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT * 2U; /* != before */
    (void)nt_ui_min_arena_size(&custom);
    TEST_ASSERT_EQUAL_INT32(before, nt_ui_test_clay_default_max_element_count());
}

/* create_context likewise must restore Clay's global default after staging
 * desc->max_elements into it for Clay_Initialize to inherit. */
static void test_create_context_restores_clay_default(void) {
    const int32_t before = nt_ui_test_clay_default_max_element_count();
    nt_ui_create_desc_t custom = nt_ui_create_desc_defaults();
    custom.max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT * 2U; /* != before */
    /* Need a bigger arena because larger max_elements grows Clay's arena. */
    alignas(NT_UI_ARENA_ALIGN) static uint8_t big_arena[NT_UI_DEFAULT_ARENA_SIZE * 4U];
    nt_ui_context_t *ctx = nt_ui_create_context(big_arena, sizeof big_arena, &custom);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL_INT32(before, nt_ui_test_clay_default_max_element_count());
    nt_ui_destroy_context(ctx);
}

/* create_context must not leak active-ctx selection: Clay_Initialize
 * unconditionally sets current=new, so create must save/restore. Caller's
 * pre-create current (NULL if none) must survive. */
static void test_create_context_restores_clay_current(void) {
    /* No active Clay context at this point (module_init from setUp doesn't
     * create any). Clay_GetCurrentContext returns NULL. */
    TEST_ASSERT_NULL(Clay_GetCurrentContext());

    nt_ui_context_t *ctx = nt_ui_create_context(s_arena_u64, sizeof s_arena_u64, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(ctx);

    /* After create, current must still be NULL -- user owns active-ctx
     * selection via nt_ui_begin. */
    TEST_ASSERT_NULL(Clay_GetCurrentContext());

    nt_ui_destroy_context(ctx);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_destroy);
    RUN_TEST(test_min_arena_size);
    RUN_TEST(test_misaligned_assert);
    RUN_TEST(test_destroy_preserves_arena);
    RUN_TEST(test_destroy_in_frame_asserts);
    RUN_TEST(test_create_context_restores_clay_current);
    RUN_TEST(test_destroy_clears_dangling_clay_current);
    RUN_TEST(test_min_arena_size_restores_clay_default);
    RUN_TEST(test_create_context_restores_clay_default);
    return UNITY_END();
}
