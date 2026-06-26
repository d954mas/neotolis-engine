/* Virtualized-list clipper tests.
 *
 * Pure window math (mid-scroll range, edge clamp, degenerate/bounds-safety) drives the
 * NT_TEST_ACCESS probe — no Clay frame needed. Recycle-id stability/distinctness uses the
 * pure fmix. Full-frame cases declare a real vlist and read back the layout: spacers size
 * the content to count*extent, exactly one scroll/clip is registered, and the axis selects
 * the layout direction. UNITY_EXCLUDE_FLOAT: compare via eps. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_state.h"
#include "ui/nt_ui_vlist.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* ---- (a) windowing range mid-scroll: floor/ceil math over a representative offset ---- */
static void test_vlist_window_midscroll(void) {
    /* pos is Clay's negative-down childOffset: scrolled 200px down over 40px rows. */
    nt_ui_vlist_range_t r = nt_ui_vlist_test_window(-200.0F, 200.0F, 40.0F, 100U, 0);
    /* first = floor(200/40) = 5; visible = ceil(200/40)+1 = 6; last = 5+6-1 = 10. */
    TEST_ASSERT_EQUAL_UINT32(5U, r.first);
    TEST_ASSERT_EQUAL_UINT32(10U, r.last);

    /* Overscan widens both sides by `overscan` rows. */
    r = nt_ui_vlist_test_window(-200.0F, 200.0F, 40.0F, 100U, 2);
    TEST_ASSERT_EQUAL_UINT32(3U, r.first); /* 5 - 2 */
    TEST_ASSERT_EQUAL_UINT32(12U, r.last); /* 3 + (6+4) - 1 */
}

/* ---- (b) edge clamp: top, fully-scrolled, sub-viewport ---- */
static void test_vlist_window_edge_clamp(void) {
    /* Top: pos 0 -> first 0. */
    nt_ui_vlist_range_t r = nt_ui_vlist_test_window(0.0F, 200.0F, 40.0F, 100U, 0);
    TEST_ASSERT_EQUAL_UINT32(0U, r.first);

    /* Fully scrolled (content 4000, viewport 200 -> max offset -3800): last clamps to count-1. */
    r = nt_ui_vlist_test_window(-3800.0F, 200.0F, 40.0F, 100U, 0);
    TEST_ASSERT_EQUAL_UINT32(99U, r.last);

    /* Sub-viewport: content (3*40=120) < viewport 200 -> whole list visible. */
    r = nt_ui_vlist_test_window(0.0F, 200.0F, 40.0F, 3U, 0);
    TEST_ASSERT_EQUAL_UINT32(0U, r.first);
    TEST_ASSERT_EQUAL_UINT32(2U, r.last);
}

/* ---- (c) degenerate inputs (bounds-safety, threat T-70-03, no-assert path) ---- */
static void test_vlist_window_degenerate_safe(void) {
    /* count == 0 -> empty window (first > last); a `for (i<=last)` loop never runs. */
    nt_ui_vlist_range_t r = nt_ui_vlist_test_window(-100.0F, 200.0F, 40.0F, 0U, 0);
    TEST_ASSERT_TRUE(r.first > r.last);

    /* item_extent <= 0 -> safe single in-range item, no div-by-zero. */
    r = nt_ui_vlist_test_window(-100.0F, 200.0F, 0.0F, 100U, 0);
    TEST_ASSERT_EQUAL_UINT32(0U, r.first);
    TEST_ASSERT_EQUAL_UINT32(0U, r.last);
    r = nt_ui_vlist_test_window(-100.0F, 200.0F, -5.0F, 100U, 0);
    TEST_ASSERT_EQUAL_UINT32(0U, r.first);
    TEST_ASSERT_EQUAL_UINT32(0U, r.last);

    /* viewport <= 0 -> safe single item. */
    r = nt_ui_vlist_test_window(0.0F, 0.0F, 40.0F, 100U, 0);
    TEST_ASSERT_EQUAL_UINT32(0U, r.first);
    TEST_ASSERT_EQUAL_UINT32(0U, r.last);

    /* NaN pos -> coerced to top, last stays in range. */
    r = nt_ui_vlist_test_window(NAN, 200.0F, 40.0F, 100U, 0);
    TEST_ASSERT_EQUAL_UINT32(0U, r.first);
    TEST_ASSERT_TRUE(r.last <= 99U);

    /* Absurd overscan never produces first > last (non-empty) nor last > count-1. */
    r = nt_ui_vlist_test_window(-3800.0F, 200.0F, 40.0F, 100U, 1000000);
    TEST_ASSERT_EQUAL_UINT32(0U, r.first);
    TEST_ASSERT_EQUAL_UINT32(99U, r.last);
    TEST_ASSERT_TRUE(r.first <= r.last);
}

/* ---- (d) recycle-id stability / distinctness / no adjacent-base collision ---- */
static void test_vlist_item_id_recycle(void) {
    const uint32_t base = 0x7711U;
    /* Stable across calls (== across frames, since it is pure). */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_vlist_item_id_of(base, 42U), nt_ui_vlist_item_id_of(base, 42U));

    /* Distinct per index + always nonzero, over a wide window. */
    for (uint32_t i = 0U; i < 256U; ++i) {
        const uint32_t id_i = nt_ui_vlist_item_id_of(base, i);
        TEST_ASSERT_NOT_EQUAL_UINT32(0U, id_i);
        for (uint32_t j = i + 1U; j < 256U; ++j) {
            TEST_ASSERT_NOT_EQUAL_UINT32(id_i, nt_ui_vlist_item_id_of(base, j));
        }
    }

    /* The additive-collision case the fmix exists to avoid: id_of(b, k+1) != id_of(b+1, k)
     * for adjacent bases/indices (additive base+index would alias these). */
    for (uint32_t b = base; b < base + 32U; ++b) {
        for (uint32_t k = 0U; k < 32U; ++k) {
            TEST_ASSERT_NOT_EQUAL_UINT32(nt_ui_vlist_item_id_of(b, k + 1U), nt_ui_vlist_item_id_of(b + 1U, k));
        }
    }
}

/* ---- Full-frame fixture: a 200x200 vlist over `count` rows of `extent`. ---- */
#define VL_ID 0x5111C17U
#define VL_VIEW 200.0F
#define VL_SCRL_TAG NT_UI_STATE_TAG('s', 'c', 'r', 'l')

static nt_ui_vlist_range_t vlist_frame(nt_ui_axis_t axis, uint32_t count, float extent) {
    nt_ui_vlist_range_t r = {1U, 0U};
    nt_pointer_t p = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(VL_VIEW), CLAY_SIZING_FIXED(VL_VIEW)}}}) {
        nt_ui_vlist_style_t st = nt_ui_vlist_style_defaults();
        const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(VL_VIEW), CLAY_SIZING_FIXED(VL_VIEW)}}};
        r = nt_ui_vlist_begin(s_fx.ctx, NULL, VL_ID, count, extent, axis, &st, &decl);
        for (uint32_t i = r.first; i <= r.last && i < count; ++i) {
            const uint32_t iid = nt_ui_vlist_item_id(s_fx.ctx, i);
            const Clay_Sizing rs = (axis == NT_UI_AXIS_X) ? (Clay_Sizing){CLAY_SIZING_FIXED(extent), CLAY_SIZING_GROW(0)} : (Clay_Sizing){CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(extent)};
            CLAY({.id = (Clay_ElementId){.id = iid}, .layout = {.sizing = rs}}) {}
        }
        nt_ui_vlist_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    return r;
}

/* ---- (e) both axes: the layout direction follows the axis ---- */
static void test_vlist_axis_y_layout(void) {
    /* Y list: consecutive rows stack on Y (same X). */
    vlist_frame(NT_UI_AXIS_Y, 100U, 40.0F);
    vlist_frame(NT_UI_AXIS_Y, 100U, 40.0F);
    const nt_ui_bbox_t b0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 0U));
    const nt_ui_bbox_t b1 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 1U));
    TEST_ASSERT_TRUE(b0.found);
    TEST_ASSERT_TRUE(b1.found);
    TEST_ASSERT_TRUE(b1.y > b0.y);
    TEST_ASSERT_TRUE(fabsf(b1.x - b0.x) < 0.5F);
}

static void test_vlist_axis_x_layout(void) {
    /* X list: consecutive rows run along X (same Y). */
    vlist_frame(NT_UI_AXIS_X, 100U, 40.0F);
    vlist_frame(NT_UI_AXIS_X, 100U, 40.0F);
    const nt_ui_bbox_t b0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 0U));
    const nt_ui_bbox_t b1 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 1U));
    TEST_ASSERT_TRUE(b0.found);
    TEST_ASSERT_TRUE(b1.found);
    TEST_ASSERT_TRUE(b1.x > b0.x);
    TEST_ASSERT_TRUE(fabsf(b1.y - b0.y) < 0.5F);
}

/* ---- (f) spacer sizing: leading+trailing make content measure count*extent ---- */
static void test_vlist_spacer_content_size(void) {
    const uint32_t count = 100U;
    const float extent = 40.0F;
    vlist_frame(NT_UI_AXIS_Y, count, extent);
    vlist_frame(NT_UI_AXIS_Y, count, extent);

    /* Raw Clay queries need the current context (nt_ui_end parks it to NULL). */
    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(s_fx.ctx->clay);
    const Clay_ScrollContainerData scd = Clay_GetScrollContainerData((Clay_ElementId){.id = VL_ID});
    Clay_SetCurrentContext(saved);
    /* Content height = leading(first*extent) + visible*extent + trailing((count-1-last)*extent) = count*extent. */
    TEST_ASSERT_TRUE(fabsf(scd.contentDimensions.height - ((float)count * extent)) < 1.0F);

    /* At rest (top) first == 0: the first row sits at the container's top edge (leading spacer = 0). */
    const nt_ui_bbox_t row0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 0U));
    const nt_ui_bbox_t cont = nt_ui_get_bbox(s_fx.ctx, VL_ID);
    TEST_ASSERT_TRUE(row0.found && cont.found);
    TEST_ASSERT_TRUE(fabsf(row0.y - cont.y) < 0.5F);
    /* Each row advances by exactly `extent` (childGap 0, item_extent stride). */
    const nt_ui_bbox_t row1 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 1U));
    TEST_ASSERT_TRUE(row1.found);
    TEST_ASSERT_TRUE(fabsf((row1.y - row0.y) - extent) < 0.5F);
}

/* ---- (g) one-clip-only: exactly one scroll container, never one per row ---- */
static void test_vlist_one_clip(void) {
    vlist_frame(NT_UI_AXIS_Y, 100U, 40.0F);
    vlist_frame(NT_UI_AXIS_Y, 100U, 40.0F);

    /* The vlist owns a single scroll keyed by VL_ID (the last/only scroll begun this frame). */
    TEST_ASSERT_EQUAL_UINT32(VL_ID, nt_ui_scroll_test_last_scroll_id());
    TEST_ASSERT_TRUE(nt_ui_state_has_tag(s_fx.ctx, VL_ID, VL_SCRL_TAG));
    /* Rows are NOT scroll containers (no clip-per-row). */
    TEST_ASSERT_FALSE(nt_ui_state_has_tag(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 0U), VL_SCRL_TAG));
    TEST_ASSERT_FALSE(nt_ui_state_has_tag(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 3U), VL_SCRL_TAG));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_vlist_window_midscroll);
    RUN_TEST(test_vlist_window_edge_clamp);
    RUN_TEST(test_vlist_window_degenerate_safe);
    RUN_TEST(test_vlist_item_id_recycle);
    RUN_TEST(test_vlist_axis_y_layout);
    RUN_TEST(test_vlist_axis_x_layout);
    RUN_TEST(test_vlist_spacer_content_size);
    RUN_TEST(test_vlist_one_clip);
    return UNITY_END();
}
