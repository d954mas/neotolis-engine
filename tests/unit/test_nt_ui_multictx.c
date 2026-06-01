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
alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena_c[NT_UI_TEST_ARENA_SIZE];
static const nt_ui_create_desc_t s_ui_desc = {.max_elements = NT_UI_DEFAULT_MAX_ELEMENT_COUNT};

void setUp(void) {
    nt_test_assert_install();
    nt_ui_module_init();
}
void tearDown(void) { nt_ui_module_shutdown(); }

/* Three contexts coexist; begin/end pairs interleave correctly with
 * Clay_GetCurrentContext switching per ctx and the in-frame tracker
 * clearing on each end. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_three_ctx_interleave(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a, &s_ui_desc);
    nt_ui_context_t *b = nt_ui_create_context(s_arena_b, sizeof s_arena_b, &s_ui_desc);
    nt_ui_context_t *c = nt_ui_create_context(s_arena_c, sizeof s_arena_c, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    /* Each ctx has its own Clay context pointer. */
    TEST_ASSERT_NOT_EQUAL(a->clay, b->clay);
    TEST_ASSERT_NOT_EQUAL(b->clay, c->clay);
    TEST_ASSERT_NOT_EQUAL(a->clay, c->clay);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    /* a: begin sets Clay current to a->clay; in-frame ctx is a. */
    nt_ui_begin(a, 100.0F, 100.0F, 0.0F, &mouse, 1);
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());
    TEST_ASSERT_EQUAL_PTR(a, nt_ui_test_inframe_ctx());
    nt_ui_end(a);
    TEST_ASSERT_NULL(nt_ui_test_inframe_ctx());

    /* b: same shape, different ctx. */
    nt_ui_begin(b, 200.0F, 200.0F, 0.0F, &mouse, 1);
    TEST_ASSERT_EQUAL_PTR(b->clay, Clay_GetCurrentContext());
    TEST_ASSERT_EQUAL_PTR(b, nt_ui_test_inframe_ctx());
    nt_ui_end(b);
    TEST_ASSERT_NULL(nt_ui_test_inframe_ctx());

    /* c: same shape, different ctx. */
    nt_ui_begin(c, 300.0F, 300.0F, 0.0F, &mouse, 1);
    TEST_ASSERT_EQUAL_PTR(c->clay, Clay_GetCurrentContext());
    TEST_ASSERT_EQUAL_PTR(c, nt_ui_test_inframe_ctx());
    nt_ui_end(c);
    TEST_ASSERT_NULL(nt_ui_test_inframe_ctx());

    nt_ui_destroy_context(a);
    nt_ui_destroy_context(b);
    nt_ui_destroy_context(c);
}

/* Per-ctx in_frame isolation across sequential begin/end. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_per_ctx_in_frame_isolation(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a, &s_ui_desc);
    nt_ui_context_t *b = nt_ui_create_context(s_arena_b, sizeof s_arena_b, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);

    /* Before any begin: no in-frame ctx, neither ctx is in_frame. */
    TEST_ASSERT_NULL(nt_ui_test_inframe_ctx());
    TEST_ASSERT_FALSE(a->in_frame);
    TEST_ASSERT_FALSE(b->in_frame);

    nt_ui_begin(a, 100.0F, 100.0F, 0.0F, &mouse, 1);
    TEST_ASSERT_TRUE(a->in_frame);
    TEST_ASSERT_FALSE(b->in_frame); /* B is NOT marked in-frame */
    TEST_ASSERT_EQUAL_PTR(a, nt_ui_test_inframe_ctx());
    nt_ui_end(a);
    TEST_ASSERT_FALSE(a->in_frame);

    nt_ui_begin(b, 100.0F, 100.0F, 0.0F, &mouse, 1);
    TEST_ASSERT_FALSE(a->in_frame);
    TEST_ASSERT_TRUE(b->in_frame);
    TEST_ASSERT_EQUAL_PTR(b, nt_ui_test_inframe_ctx());
    nt_ui_end(b);

    TEST_ASSERT_NULL(nt_ui_test_inframe_ctx());

    nt_ui_destroy_context(a);
    nt_ui_destroy_context(b);
}

/* REVIEW-2 P2-2: public read APIs must snapshot+restore Clay current context.
 * Without this, calling a B-read API while A is the caller's "active" Clay ctx
 * silently leaks B's clay onto the global slot -- the next Clay op on caller's
 * side hits the wrong ctx. The pattern fix is the 3-line
 *   saved = Clay_GetCurrentContext(); set(B->clay); read; set(saved);
 * which restores whatever WAS set on entry. This test covers every public read
 * API that touches Clay private accessors. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_read_apis_restore_clay_current_context(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a, &s_ui_desc);
    nt_ui_context_t *b = nt_ui_create_context(s_arena_b, sizeof s_arena_b, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    /* Force a NON-NULL "caller" Clay current ctx (A) before each B-read. The
     * restore must put A back, never leak B's clay onto the global slot. */
    Clay_SetCurrentContext(a->clay);
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());

    /* nt_ui_get_bbox(B, id): id != 0 is asserted; the id has no layout entry
     * (we never ran a frame on B), so Clay returns found=false -- fine, what
     * we verify is the global ctx slot AFTER the call. */
    nt_ui_bbox_t bb = nt_ui_get_bbox(b, 0xDEADBEEFU);
    TEST_ASSERT_FALSE(bb.found);
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());

    /* nt_ui_internal_get_layout_element_count: read-only count, no ctx swap
     * needed (does not call any Clay accessor that requires current ctx). Kept
     * here as a baseline -- it must NOT change current either. */
    (void)nt_ui_internal_get_layout_element_count(b);
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());

    /* nt_ui_internal_get_layout_element_view: swaps + restores internally. */
    nt_ui_inspector_element_view_t v = nt_ui_internal_get_layout_element_view(b, 0);
    (void)v;
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());

    /* nt_ui_internal_collect_tree_rows: swaps + restores internally. */
    nt_ui_inspector_tree_row_t rows[4];
    int32_t written = nt_ui_internal_collect_tree_rows(b, rows, 4);
    TEST_ASSERT_EQUAL_INT32(0, written); /* B never ran a frame -> 0 roots */
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());

    /* nt_ui_internal_get_element_info: swaps + restores internally. The id
     * lookup misses (no frame on B), returning info.found=false -- still the
     * code path through Clay__GetHashMapItem with the restore at exit. */
    nt_ui_inspector_element_info_t info = nt_ui_internal_get_element_info(b, 0xCAFEBABEU);
    TEST_ASSERT_FALSE(info.found);
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());

    /* Restore to NULL before destroy so destroy's "current==ctx->clay -> NULL"
     * branch is not the one resetting state (keeps the test invariant clean). */
    Clay_SetCurrentContext(NULL);

    nt_ui_destroy_context(a);
    nt_ui_destroy_context(b);
}

/* REVIEW-2 P2-2 (correctness arm): nt_ui_get_bbox(B, id) must read from
 * B's clay regardless of what's globally current. Pre-fix, the function called
 * Clay_GetElementData WITHOUT setting current to ctx->clay -- it silently
 * returned the CURRENT ctx's data instead. This test runs a frame on A with a
 * known id, runs no frame on B, then queries B for that id while A is current.
 * Correct behavior: B.found == false (no such id in B). Pre-fix would have
 * returned A's bbox (false positive). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_get_bbox_reads_from_passed_ctx_not_current(void) {
    nt_ui_context_t *a = nt_ui_create_context(s_arena_a, sizeof s_arena_a, &s_ui_desc);
    nt_ui_context_t *b = nt_ui_create_context(s_arena_b, sizeof s_arena_b, &s_ui_desc);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    /* Frame on A declares "abox" with a known floating bbox so Clay's hashmap
     * has it. B never runs a frame, so its hashmap stays empty. */
    nt_pointer_t mouse;
    memset(&mouse, 0, sizeof mouse);
    nt_ui_begin(a, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("abox"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 50.0F, .y = 60.0F}}, .layout = {.sizing = {CLAY_SIZING_FIXED(120.0F), CLAY_SIZING_FIXED(40.0F)}}}) {}
    nt_ui_end(a);

    const uint32_t abox_id = nt_ui_id("abox");

    /* Sanity: A finds abox. Current ctx is NULL after nt_ui_end -- set it back
     * to A explicitly so we know precisely what "leaks" would look like. */
    Clay_SetCurrentContext(a->clay);
    nt_ui_bbox_t bb_a = nt_ui_get_bbox(a, abox_id);
    TEST_ASSERT_TRUE(bb_a.found);

    /* Now the meat: ask B for the same id while A is current. With the
     * snapshot/set/restore pattern in place, Clay_GetElementData runs against
     * B's clay (empty hashmap) -> found=false. Without the pattern, it would
     * have run against A's clay and returned found=true (the bug). */
    Clay_SetCurrentContext(a->clay);
    nt_ui_bbox_t bb_b = nt_ui_get_bbox(b, abox_id);
    TEST_ASSERT_FALSE_MESSAGE(bb_b.found, "nt_ui_get_bbox(B, id) returned data from A's clay -- snapshot/restore missing");

    /* And the restore: A is still current after the call. */
    TEST_ASSERT_EQUAL_PTR(a->clay, Clay_GetCurrentContext());

    Clay_SetCurrentContext(NULL);
    nt_ui_destroy_context(a);
    nt_ui_destroy_context(b);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_three_ctx_interleave);
    RUN_TEST(test_per_ctx_in_frame_isolation);
    RUN_TEST(test_read_apis_restore_clay_current_context);
    RUN_TEST(test_get_bbox_reads_from_passed_ctx_not_current);
    return UNITY_END();
}
