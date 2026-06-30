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
#include "memory/nt_mem_scratch.h"
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

/* ---- (c) degenerate inputs (bounds-safety, no-assert path) ---- */
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

/* ---- (c2) invalid stride (item_extent+gap <= 0 or NaN): safe window -> zero spacers ----
 * vlist_begin clamps the full stride (item_extent + gap) to safe_extent before deriving the window and
 * the leading/trailing spacers. The pure probe stands in for that window math: a non-positive / NaN
 * stride collapses to {0,0}, so leading (first*safe_extent) and trailing ((count-1-last)*safe_extent)
 * are 0 -- never a negative or NaN FIXED spacer. (begin also asserts the bad stride in debug; see the
 * death tests below. The clamp is the real guard in NT_ASSERT_MODE=OFF.) */
static void test_vlist_window_bad_stride_safe(void) {
    /* item_extent=10, gap=-20 -> stride -10: safe single in-range item, never negative. */
    nt_ui_vlist_range_t r = nt_ui_vlist_test_window(-100.0F, 200.0F, 10.0F + (-20.0F), 100U, 2);
    TEST_ASSERT_EQUAL_UINT32(0U, r.first);
    TEST_ASSERT_EQUAL_UINT32(0U, r.last);
    /* NaN stride -> same safe collapse (the !(x > 0) guard also rejects NaN). */
    r = nt_ui_vlist_test_window(-100.0F, 200.0F, NAN, 100U, 2);
    TEST_ASSERT_EQUAL_UINT32(0U, r.first);
    TEST_ASSERT_EQUAL_UINT32(0U, r.last);
}

/* ---- (c3) huge count: int64 window math keeps first/last in [0, count-1] (no long overflow) ----
 * `long` is 32-bit on Windows (LLP64) and WASM (ILP32), so a count near UINT32_MAX with a deep scroll
 * or a large overscan would wrap a `long` intermediate and break the "last never exceeds count-1"
 * contract. The int64 locals keep every clamp honest. */
static void test_vlist_window_huge_count(void) {
    const uint32_t count = 0xFFFFFFF0U; /* ~4.29e9, well past INT32_MAX */
    /* Deep scroll + large overscan: the window must clamp into range with last >= first. */
    nt_ui_vlist_range_t r = nt_ui_vlist_test_window(-1.0e9F, 200.0F, 40.0F, count, 1000000);
    TEST_ASSERT_TRUE(r.first <= r.last);
    TEST_ASSERT_TRUE(r.last <= count - 1U);
    /* Fully scrolled past the end (scrolled/extent overflows a 32-bit long): last clamps to count-1. */
    r = nt_ui_vlist_test_window(-((float)count * 40.0F), 200.0F, 40.0F, count, 8);
    TEST_ASSERT_TRUE(r.first <= r.last);
    TEST_ASSERT_EQUAL_UINT32(count - 1U, r.last);
}

/* ---- (d) per-id stability / distinctness / no adjacent-base collision (window <= ring) ---- */
static void test_vlist_item_id_recycle(void) {
    const uint32_t base = 0x7711U;
    const uint32_t ring = 512U; /* > 256 so the 0..255 indices below map to distinct slots */
    /* Stable across calls (== across frames, since it is pure). */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_vlist_item_id_of(base, 42U, ring), nt_ui_vlist_item_id_of(base, 42U, ring));

    /* Distinct per index + always nonzero, over a wide window (all within one ring). */
    for (uint32_t i = 0U; i < 256U; ++i) {
        const uint32_t id_i = nt_ui_vlist_item_id_of(base, i, ring);
        TEST_ASSERT_NOT_EQUAL_UINT32(0U, id_i);
        for (uint32_t j = i + 1U; j < 256U; ++j) {
            TEST_ASSERT_NOT_EQUAL_UINT32(id_i, nt_ui_vlist_item_id_of(base, j, ring));
        }
    }

    /* The additive-collision case the fmix exists to avoid: id_of(b, k+1) != id_of(b+1, k)
     * for adjacent bases/indices (additive base+index would alias these). */
    for (uint32_t b = base; b < base + 32U; ++b) {
        for (uint32_t k = 0U; k < 32U; ++k) {
            TEST_ASSERT_NOT_EQUAL_UINT32(nt_ui_vlist_item_id_of(b, k + 1U, ring), nt_ui_vlist_item_id_of(b + 1U, k, ring));
        }
    }
}

/* ---- (d2) ring recycle: index and index+ring share a slot/id; one window stays distinct ---- */
static void test_vlist_item_id_ring(void) {
    const uint32_t base = 0x1234U;
    const uint32_t ring = 64U;
    /* index and index+ring (and +2*ring) fold onto the same slot -> identical id (frame-stable recycle). */
    for (uint32_t i = 0U; i < 200U; ++i) {
        const uint32_t id_i = nt_ui_vlist_item_id_of(base, i, ring);
        TEST_ASSERT_EQUAL_UINT32(id_i, nt_ui_vlist_item_id_of(base, i + ring, ring));
        TEST_ASSERT_EQUAL_UINT32(id_i, nt_ui_vlist_item_id_of(base, i + (2U * ring), ring));
    }
    /* Within a single ring (one window) every slot is distinct -> no DUPLICATE_ID across visible rows. */
    for (uint32_t i = 0U; i < ring; ++i) {
        for (uint32_t j = i + 1U; j < ring; ++j) {
            TEST_ASSERT_NOT_EQUAL_UINT32(nt_ui_vlist_item_id_of(base, i, ring), nt_ui_vlist_item_id_of(base, j, ring));
        }
    }
    /* ring<=1 disables recycling: slot == index (every index distinct, never folds). */
    TEST_ASSERT_NOT_EQUAL_UINT32(nt_ui_vlist_item_id_of(base, 0U, 1U), nt_ui_vlist_item_id_of(base, 1U, 1U));
    TEST_ASSERT_NOT_EQUAL_UINT32(nt_ui_vlist_item_id_of(base, 5U, 0U), nt_ui_vlist_item_id_of(base, 5U + 64U, 0U));
}

/* ---- Full-frame fixture: a 200x200 vlist over `count` rows of `extent`. ---- */
#define VL_ID 0x5111C17U
#define VL_VIEW 200.0F
#define VL_RING (nt_ui_vlist_style_defaults().id_ring) /* default-style recycle modulus (matches vlist_frame) */
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
    const nt_ui_bbox_t b0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 0U, VL_RING));
    const nt_ui_bbox_t b1 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 1U, VL_RING));
    TEST_ASSERT_TRUE(b0.found);
    TEST_ASSERT_TRUE(b1.found);
    TEST_ASSERT_TRUE(b1.y > b0.y);
    TEST_ASSERT_TRUE(fabsf(b1.x - b0.x) < 0.5F);
}

static void test_vlist_axis_x_layout(void) {
    /* X list: consecutive rows run along X (same Y). */
    vlist_frame(NT_UI_AXIS_X, 100U, 40.0F);
    vlist_frame(NT_UI_AXIS_X, 100U, 40.0F);
    const nt_ui_bbox_t b0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 0U, VL_RING));
    const nt_ui_bbox_t b1 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 1U, VL_RING));
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
    const nt_ui_bbox_t row0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 0U, VL_RING));
    const nt_ui_bbox_t cont = nt_ui_get_bbox(s_fx.ctx, VL_ID);
    TEST_ASSERT_TRUE(row0.found && cont.found);
    TEST_ASSERT_TRUE(fabsf(row0.y - cont.y) < 0.5F);
    /* Each row advances by exactly `extent` (childGap 0, item_extent stride). */
    const nt_ui_bbox_t row1 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 1U, VL_RING));
    TEST_ASSERT_TRUE(row1.found);
    TEST_ASSERT_TRUE(fabsf((row1.y - row0.y) - extent) < 0.5F);
}

/* ---- (f-x) X-axis parity: content measures count*extent on WIDTH, the clip scrolls on X (not Y),
 * and rows advance along X — the "horizontal strip + scrollbar tracks the giant list" contract. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — inflated by the TEST_ASSERT macro expansion
static void test_vlist_spacer_content_size_x(void) {
    const uint32_t count = 100U;
    const float extent = 40.0F;
    vlist_frame(NT_UI_AXIS_X, count, extent);
    vlist_frame(NT_UI_AXIS_X, count, extent);

    Clay_Context *saved = Clay_GetCurrentContext();
    Clay_SetCurrentContext(s_fx.ctx->clay);
    const Clay_ScrollContainerData scd = Clay_GetScrollContainerData((Clay_ElementId){.id = VL_ID});
    Clay_SetCurrentContext(saved);
    TEST_ASSERT_TRUE(scd.found);
    /* Content WIDTH = leading + visible + trailing = count*extent (X is the scroll axis here). */
    TEST_ASSERT_TRUE(fabsf(scd.contentDimensions.width - ((float)count * extent)) < 1.0F);
    /* The clip + its scrollbar live on X only (horizontal strip), never Y. */
    TEST_ASSERT_TRUE(scd.config.horizontal);
    TEST_ASSERT_FALSE(scd.config.vertical);

    /* At rest (left) first == 0: row 0 sits at the container's LEFT edge (leading spacer = 0). */
    const nt_ui_bbox_t row0 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 0U, VL_RING));
    const nt_ui_bbox_t cont = nt_ui_get_bbox(s_fx.ctx, VL_ID);
    TEST_ASSERT_TRUE(row0.found && cont.found);
    TEST_ASSERT_TRUE(fabsf(row0.x - cont.x) < 0.5F);
    /* Each row advances by exactly `extent` along X. */
    const nt_ui_bbox_t row1 = nt_ui_get_bbox(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 1U, VL_RING));
    TEST_ASSERT_TRUE(row1.found);
    TEST_ASSERT_TRUE(fabsf((row1.x - row0.x) - extent) < 0.5F);
}

/* ---- (g) one-clip-only: exactly one scroll container, never one per row ---- */
static void test_vlist_one_clip(void) {
    vlist_frame(NT_UI_AXIS_Y, 100U, 40.0F);
    vlist_frame(NT_UI_AXIS_Y, 100U, 40.0F);

    /* The vlist owns a single scroll keyed by VL_ID (the last/only scroll begun this frame). */
    TEST_ASSERT_EQUAL_UINT32(VL_ID, nt_ui_scroll_test_last_scroll_id());
    TEST_ASSERT_TRUE(nt_ui_state_has_tag(s_fx.ctx, VL_ID, VL_SCRL_TAG));
    /* Rows are NOT scroll containers (no clip-per-row). */
    TEST_ASSERT_FALSE(nt_ui_state_has_tag(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 0U, VL_RING), VL_SCRL_TAG));
    TEST_ASSERT_FALSE(nt_ui_state_has_tag(s_fx.ctx, nt_ui_vlist_item_id_of(VL_ID, 3U, VL_RING), VL_SCRL_TAG));
}

/* ---- (h) oversized window vs id_ring (a too-small id_ring would alias two visible rows onto one
 * recycle slot -> DUPLICATE_ID). FULL asserts on the unclamped count; OFF clamps to id_ring-1. Each
 * build tests the path it takes. ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL
static void test_vlist_window_exceeds_ring_asserts(void) {
    nt_ui_vlist_style_t st = nt_ui_vlist_style_defaults();
    st.id_ring = 8U; /* tiny ring; the 200px viewport over 10px rows wants ~29 rows >> ring-1 */
    st.overscan = 4;
    const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(VL_VIEW), CLAY_SIZING_FIXED(VL_VIEW)}}};
    nt_pointer_t p = {0};

    /* Frame 1 (clean): lay out VL_ID so frame 2 reads its real 200px viewport. Viewport is 0 on
     * frame 1 -> a tiny single-row window, so no assert here. */
    nt_mem_scratch_reset();
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    nt_ui_vlist_range_t r = nt_ui_vlist_begin(s_fx.ctx, NULL, VL_ID, 1000U, 10.0F, NT_UI_AXIS_Y, &st, &decl);
    for (uint32_t i = r.first; i <= r.last && i < 1000U; ++i) {
        CLAY({.id = (Clay_ElementId){.id = nt_ui_vlist_item_id(s_fx.ctx, i)}, .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(10.0F)}}}) {}
    }
    nt_ui_vlist_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);

    /* Frame 2: the oversized window now fires the assert. It fires AFTER the owned scroll clip opens,
     * so the frame is abandoned mid-tree; tearDown closes it defensively (ui_walker_fixture_shutdown). */
    nt_mem_scratch_reset();
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    NT_TEST_EXPECT_ASSERT((void)nt_ui_vlist_begin(s_fx.ctx, NULL, VL_ID, 1000U, 10.0F, NT_UI_AXIS_Y, &st, &decl));
}
#else
static void test_vlist_window_ring_clamp_off(void) {
    const uint32_t ring = 8U;
    nt_ui_vlist_range_t r = {1U, 0U};
    for (int f = 0; f < 2; ++f) { /* frame 1 establishes the bbox; frame 2 sees the real viewport */
        nt_mem_scratch_reset();
        nt_pointer_t p = {0};
        nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
        CLAY({.id = CLAY_ID("rclamp_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(VL_VIEW), CLAY_SIZING_FIXED(VL_VIEW)}}}) {
            nt_ui_vlist_style_t st = nt_ui_vlist_style_defaults();
            st.id_ring = ring; /* tiny ring; the 200px viewport over 10px rows would otherwise show ~29 rows */
            st.overscan = 4;
            const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(VL_VIEW), CLAY_SIZING_FIXED(VL_VIEW)}}};
            r = nt_ui_vlist_begin(s_fx.ctx, NULL, VL_ID, 1000U, 10.0F, NT_UI_AXIS_Y, &st, &decl);
            for (uint32_t i = r.first; i <= r.last && i < 1000U; ++i) {
                CLAY({.id = (Clay_ElementId){.id = nt_ui_vlist_item_id(s_fx.ctx, i)}, .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(10.0F)}}}) {}
            }
            nt_ui_vlist_end(s_fx.ctx);
        }
        nt_ui_end(s_fx.ctx);
    }
    const uint32_t window = (r.last >= r.first) ? (r.last - r.first + 1U) : 0U;
    TEST_ASSERT_TRUE(window > 0U);   /* still renders rows */
    TEST_ASSERT_TRUE(window < ring); /* (last-first+1) <= ring-1: no two visible rows alias a slot */
}
#endif

/* ---- (i) nested vlists swept back and forth ---- Without recycling, a 10k-row sweep's distinct
 * per-row ids saturate Clay's persistent element hashmap -> stale layoutElement -> build_tree degrade.
 * id_ring bounds the ids (degrade stays 0); id_ring==0 reproduces the saturation backstop. */
#define VL_OUTER_ID 0x0C0FFEE1U
#define VL_Y_ID 0x0C0FFEE2U
#define VL_X_ID 0x0C0FFEE3U
#define VL_BIG_COUNT 10000U
#define VL_BIG_ROW_H 34.0F
#define VL_BIG_COL_W 80.0F

static void vlist_nested_sweep_frame(float pos_y, uint32_t id_ring) {
    nt_mem_scratch_reset(); /* per-frame UI scratch (element_data/payloads), exactly like the app loop */
    nt_pointer_t p = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);

    /* Pin vlist_y's offset so the window is deterministic (the state cell exists from frame 2 on).
     * pos==target==raw, vel 0, no gesture flags -> the integrator's in-bounds clamp holds it. */
    nt_ui_scroll_state_t *vs = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, VL_Y_ID);
    if (vs != NULL) {
        vs->pos[1] = pos_y;
        vs->target[1] = pos_y;
        vs->raw[1] = pos_y;
        vs->vel[0] = 0.0F;
        vs->vel[1] = 0.0F;
        vs->flags = 0U;
    }

    nt_ui_scroll_style_t outer = nt_ui_scroll_style_defaults(); /* ALWAYS bar => a floating tree root */
    nt_ui_vlist_style_t vst = nt_ui_vlist_style_defaults();     /* owned scroll: ALWAYS bar */
    vst.id_ring = id_ring;                                      /* 0 = no recycle (saturates), default = bounded */
    const Clay_ElementDeclaration ydecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(VL_VIEW), CLAY_SIZING_FIXED(VL_VIEW)}}};
    const Clay_ElementDeclaration xdecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(VL_VIEW), CLAY_SIZING_FIXED(60.0F)}}};

    CLAY({.id = CLAY_ID("vlsweep_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(VL_VIEW), CLAY_SIZING_FIXED(400.0F)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, VL_OUTER_ID, &outer, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}});
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6}}) {
            const nt_ui_vlist_range_t ry = nt_ui_vlist_begin(s_fx.ctx, NULL, VL_Y_ID, VL_BIG_COUNT, VL_BIG_ROW_H, NT_UI_AXIS_Y, &vst, &ydecl);
            for (uint32_t i = ry.first; i <= ry.last && i < VL_BIG_COUNT; ++i) {
                CLAY({.id = (Clay_ElementId){.id = nt_ui_vlist_item_id(s_fx.ctx, i)}, .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(VL_BIG_ROW_H)}}}) {}
            }
            nt_ui_vlist_end(s_fx.ctx);

            /* vlist_x AFTER vlist_y: its position-dependent row count shifts the container index (the repro). */
            const nt_ui_vlist_range_t rx = nt_ui_vlist_begin(s_fx.ctx, NULL, VL_X_ID, VL_BIG_COUNT, VL_BIG_COL_W, NT_UI_AXIS_X, &vst, &xdecl);
            for (uint32_t i = rx.first; i <= rx.last && i < VL_BIG_COUNT; ++i) {
                CLAY({.id = (Clay_ElementId){.id = nt_ui_vlist_item_id(s_fx.ctx, i)}, .layout = {.sizing = {CLAY_SIZING_FIXED(VL_BIG_COL_W), CLAY_SIZING_GROW(0)}}}) {}
            }
            nt_ui_vlist_end(s_fx.ctx);
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* Sweep vlist_y top<->bottom several times over 10k rows. Returns the build_tree degrade count. */
static uint32_t vlist_nested_sweep(uint32_t id_ring) {
    nt_ui_internal_test_reset_stale_floating_parent_count();

    const float content = (float)VL_BIG_COUNT * VL_BIG_ROW_H; /* 340000 */
    const float maxpos = -(content - VL_VIEW);                /* most-negative offset (fully scrolled) */

    /* Frame 1 establishes dims (the pin is a no-op: the state cell is created during this frame). */
    vlist_nested_sweep_frame(0.0F, id_ring);

    const int steps = 200;
    for (int rev = 0; rev < 4; ++rev) {
        for (int k = 0; k <= steps; ++k) {
            const float t = (float)k / (float)steps;
            const float frac = ((rev & 1) == 0) ? t : (1.0F - t); /* alternate sweep direction */
            vlist_nested_sweep_frame(maxpos * frac, id_ring);
        }
    }
    return nt_ui_internal_test_stale_floating_parent_count();
}

/* PROOF the recycle fix works: with default id_ring the distinct ids stay bounded, so the same
 * 10k sweep that used to saturate now never does -> degrade count == 0, and no build_tree trap. */
static void test_vlist_nested_scroll_reversals_no_crash(void) {
    const uint32_t degrades = vlist_nested_sweep(nt_ui_vlist_style_defaults().id_ring);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, degrades, "ring recycling must keep Clay's hashmap bounded (no saturation/degrade)");
}

/* BACKSTOP coverage: with recycling disabled (id_ring==0) the same sweep saturates the hashmap
 * deterministically, exercising build_tree's identity-seed degrade path (no trap, count > 0). */
static void test_vlist_degrade_backstop_on_saturation(void) {
    const uint32_t degrades = vlist_nested_sweep(0U);
    TEST_ASSERT_TRUE_MESSAGE(degrades > 0U, "absolute-id sweep must reproduce Clay hashmap saturation -> build_tree degrade backstop");
}

/* ---- Death tests (NT_ASSERT_FULL only): begin signals a developer error on a bad stride ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL
/* A non-finite / non-positive stride (item_extent + gap) fires the developer-signal assert ON TOP of
 * the silent clamp. The assert fires before the owned scroll opens, so the frame stays balanced. */
static void vlist_begin_with_gap_expect_assert(uint32_t root_id, float item_extent, float gap) {
    nt_ui_vlist_style_t st = nt_ui_vlist_style_defaults();
    st.gap = gap;
    const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(VL_VIEW), CLAY_SIZING_FIXED(VL_VIEW)}}};
    nt_pointer_t p = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    CLAY({.id = (Clay_ElementId){.id = root_id}, .layout = {.sizing = {CLAY_SIZING_FIXED(VL_VIEW), CLAY_SIZING_FIXED(VL_VIEW)}}}) {
        NT_TEST_EXPECT_ASSERT((void)nt_ui_vlist_begin(s_fx.ctx, NULL, VL_ID, 100U, item_extent, NT_UI_AXIS_Y, &st, &decl));
    }
    nt_ui_end(s_fx.ctx);
}

static void test_vlist_begin_negative_gap_asserts(void) { vlist_begin_with_gap_expect_assert(nt_ui_id("badgap_neg"), 10.0F, -20.0F); }
static void test_vlist_begin_nan_gap_asserts(void) { vlist_begin_with_gap_expect_assert(nt_ui_id("badgap_nan"), 10.0F, NAN); }
#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_vlist_window_midscroll);
    RUN_TEST(test_vlist_window_edge_clamp);
    RUN_TEST(test_vlist_window_degenerate_safe);
    RUN_TEST(test_vlist_window_bad_stride_safe);
    RUN_TEST(test_vlist_window_huge_count);
    RUN_TEST(test_vlist_item_id_recycle);
    RUN_TEST(test_vlist_item_id_ring);
    RUN_TEST(test_vlist_axis_y_layout);
    RUN_TEST(test_vlist_axis_x_layout);
    RUN_TEST(test_vlist_spacer_content_size);
    RUN_TEST(test_vlist_spacer_content_size_x);
    RUN_TEST(test_vlist_one_clip);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_vlist_window_exceeds_ring_asserts);
#else
    RUN_TEST(test_vlist_window_ring_clamp_off);
#endif
    RUN_TEST(test_vlist_nested_scroll_reversals_no_crash);
    RUN_TEST(test_vlist_degrade_backstop_on_saturation);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_vlist_begin_negative_gap_asserts);
    RUN_TEST(test_vlist_begin_nan_gap_asserts);
#endif
    return UNITY_END();
}
