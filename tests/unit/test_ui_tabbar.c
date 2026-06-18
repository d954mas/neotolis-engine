/* Reusable tab-bar tests. Driven through the walker fixture (no GL surface). A click on tab i
 * sets the game-owned int* active to i; clicking another tab updates it. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "input/nt_input_internal.h"
#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_state.h"
#include "ui/nt_ui_tabbar.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define VIEW_W 800.0F
#define VIEW_H 600.0F

#define TAB_BASE 0x7AB001U

static const char *const s_tabs[] = {"One", "Two", "Three", "Four"};

void setUp(void) {
    nt_test_assert_install();
    nt_input_clear_all_keys();
    nt_input_poll();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) {
    nt_input_clear_all_keys();
    ui_walker_fixture_shutdown(&s_fx);
}

static nt_pointer_t pointer_at(float x, float y, bool is_down, bool is_pressed, bool is_released) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = is_down;
    p.buttons[NT_BUTTON_LEFT].is_pressed = is_pressed;
    p.buttons[NT_BUTTON_LEFT].is_released = is_released;
    return p;
}

/* One tab-bar frame with an OPTIONAL parallel icons[] array: a FIXED-width vertical bar floated at the
 * top-left so each tab's bbox is known. Returns the clicked index (-1 if none). */
static int tabbar_frame_icons(const nt_pointer_t *p, int count, int *active, const nt_atlas_region_ref_t *icons, nt_ui_tabbar_style_t *st) {
    int clicked = -1;
    nt_mem_scratch_reset(); /* per-frame scratch reset (icon payloads alloc here), exactly as the main loop does */
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    CLAY({.id = (Clay_ElementId){.id = 0x7AB0F0U},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 0.0F, .y = 0.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(300)}}}) {
        clicked = nt_ui_tabbar(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, TAB_BASE, s_tabs, icons, count, active, st);
    }
    nt_ui_end(s_fx.ctx);
    return clicked;
}

/* Text-only convenience wrapper (icons NULL) so existing tests keep their call shape. */
static int tabbar_frame(const nt_pointer_t *p, int count, int *active, nt_ui_tabbar_style_t *st) { return tabbar_frame_icons(p, count, active, NULL, st); }

/* The begin/end CORE driven directly: the game owns the per-tab content (a single label child here). */
static int tabbar_core_frame(const nt_pointer_t *p, int count, int *active, nt_ui_tabbar_style_t *st) {
    static const nt_ui_label_style_t lbl = {.font_id = 0U, .font_size = 14.0F, .color = {255.0F, 255.0F, 255.0F, 255.0F}};
    int clicked = -1;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    CLAY({.id = (Clay_ElementId){.id = 0x7AB0F0U},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 0.0F, .y = 0.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(300)}}}) {
        nt_ui_tabbar_begin(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, TAB_BASE, st);
        for (int i = 0; i < count; ++i) {
            if (nt_ui_tab_begin(s_fx.ctx, i, i == *active)) {
                *active = i;
                clicked = i;
            }
            nt_ui_label(s_fx.ctx, NT_UI_DATA_LAYER(2), s_tabs[i], &lbl);
            nt_ui_tab_end(s_fx.ctx);
        }
        nt_ui_tabbar_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    return clicked;
}

static float tab_center_y(const nt_ui_tabbar_style_t *st, int i);

/* Mirrors examples/ui_showcase render_tabs: TWO tab-bars in one frame -- a nav via the labels[] wrapper
 * plus a begin/end demo whose tabs each hold an icon (image) + a label, with the SAME icon ref repeated
 * across tabs. Reproduces the DUPLICATE_ID (Clay type=4) regression headlessly (no GL). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void two_tabbars_with_content_frame(const ui_walker_fixture_t *fx, const nt_pointer_t *p) {
    static const nt_ui_label_style_t lbl = {.font_id = 0U, .font_size = 14.0F, .color = {255.0F, 255.0F, 255.0F, 255.0F}};
    static const nt_ui_image_style_t img = {.color_packed = 0xFFFFFFFFU, .slice9_scale = 1.0F};
    /* One shared icon ref reused on every demo tab -- the showcase repeats the bunny icon across tabs. */
    nt_atlas_region_ref_t icon = nt_atlas_ref_idx(fx->atlas.handle, 0, fx->atlas.white_region_idx);

    nt_ui_tabbar_style_t nav_st = nt_ui_tabbar_style_defaults();
    nt_ui_tabbar_style_t demo_st = nt_ui_tabbar_style_defaults();
    int nav_active = 0;
    int demo_active = 1;

    nt_ui_begin(fx->ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT}}) {
        /* Nav: the labels[] convenience wrapper, base = TAB_BASE. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_GROW(0)}}}) { (void)nt_ui_tabbar(fx->ctx, NT_UI_DATA_LAYER(1), 2U, TAB_BASE, s_tabs, NULL, 4, &nav_active, &nav_st); }
        /* Demo: the begin/end core, a DIFFERENT base; each tab holds an icon + a label. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(72)}}}) {
            nt_ui_tabbar_begin(fx->ctx, NT_UI_DATA_LAYER(1), 2U, TAB_BASE + 0x100U, &demo_st);
            for (int i = 0; i < 4; ++i) {
                (void)nt_ui_tab_begin(fx->ctx, i, i == demo_active);
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24)}}}) { nt_ui_image(fx->ctx, NT_UI_DATA_LAYER(1), &icon, &img, NULL); }
                nt_ui_label(fx->ctx, NT_UI_DATA_LAYER(2), s_tabs[i], &lbl);
                nt_ui_tab_end(fx->ctx);
            }
            nt_ui_tabbar_end(fx->ctx);
        }
    }
    nt_ui_end(fx->ctx);
}

/* ---- Regression: two tab-bars + per-tab icon/label content in one frame must NOT
 * trip Clay's DUPLICATE_ID. nt_ui_end runs the Clay layout headlessly; a duplicate id would route
 * through nt_ui_clay_error_cb -> NT_ASSERT (the trap). A clean return == no duplicate. ---- */
static void test_tabbar_two_bars_with_content_no_duplicate_id(void) {
    nt_pointer_t idle = pointer_at(400.0F, 400.0F, false, false, false);
    two_tabbars_with_content_frame(&s_fx, &idle);
    TEST_PASS();
}

/* ---- ABI sanity ---- */
static void test_tabbar_abi_size(void) { TEST_ASSERT_EQUAL_UINT(144U, (unsigned)sizeof(nt_ui_tabbar_style_t)); }

static void test_tabbar_defaults_valid(void) {
    nt_ui_tabbar_style_t st = nt_ui_tabbar_style_defaults();
    TEST_ASSERT_TRUE(st.font_size > 0.0F);
    TEST_ASSERT_TRUE(st.tab_extent > 0U);
    /* Per-state model: every state scale/opacity must be a valid eased target. */
    TEST_ASSERT_TRUE(st.idle.scale > 0.0F && st.hover.scale > 0.0F && st.selected.scale > 0.0F);
    TEST_ASSERT_TRUE(st.state_speed >= 0.0F && st.value_speed >= 0.0F);
    TEST_ASSERT_TRUE(st.slice9_scale > 0.0F);
    /* Atlas-free defaults: idle carries no art (flat-color fallback path). */
    TEST_ASSERT_EQUAL_UINT(0U, st.idle.bg.atlas.id);
    /* accent_side defaults to LEFT (the current vertical-bar leading edge). */
    TEST_ASSERT_EQUAL_UINT((unsigned)NT_UI_TABBAR_ACCENT_LEFT, st.accent_side);
}

/* ---- begin/end core: a click on tab i sets *active = i (parity with the convenience wrapper). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_tabbar_core_click_sets_active(void) {
    nt_ui_tabbar_style_t st = nt_ui_tabbar_style_defaults();
    int active = 0;
    const float x = 80.0F;
    const float y2 = tab_center_y(&st, 2);

    nt_pointer_t w = pointer_at(x, y2, false, false, false);
    tabbar_core_frame(&w, 4, &active, &st);
    nt_pointer_t pr = pointer_at(x, y2, true, true, false);
    tabbar_core_frame(&pr, 4, &active, &st);
    nt_pointer_t rl = pointer_at(x, y2, false, false, true);
    const int clicked = tabbar_core_frame(&rl, 4, &active, &st);
    TEST_ASSERT_EQUAL_INT(2, active);
    TEST_ASSERT_EQUAL_INT(2, clicked);
}

/* ---- accent_side = NONE must not assert and still routes clicks (accent is purely visual). ---- */
static void test_tabbar_accent_none_ok(void) {
    nt_ui_tabbar_style_t st = nt_ui_tabbar_style_defaults();
    st.accent_side = (uint8_t)NT_UI_TABBAR_ACCENT_NONE;
    int active = 0;
    nt_pointer_t idle = pointer_at(400.0F, 400.0F, false, false, false);
    const int clicked = tabbar_core_frame(&idle, 4, &active, &st);
    TEST_ASSERT_EQUAL_INT(-1, clicked);
}

/* Center of tab i for a vertical bar floated at (0,0): each tab is `tab_extent` tall, gap between. */
static float tab_center_y(const nt_ui_tabbar_style_t *st, int i) {
    const float extent = (float)st->tab_extent;
    const float gap = (float)st->gap;
    const float pad = (float)st->pad;
    return pad + ((float)i * (extent + gap)) + (extent * 0.5F);
}

/* ---- A click on tab i sets *active = i; clicking another tab updates it. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_tabbar_click_sets_active(void) {
    nt_ui_tabbar_style_t st = nt_ui_tabbar_style_defaults();
    int active = 0;

    const float x = 80.0F;
    const float y2 = tab_center_y(&st, 2);

    /* Warm so the tab bboxes are baked. */
    nt_pointer_t w = pointer_at(x, y2, false, false, false);
    tabbar_frame(&w, 4, &active, &st);

    /* Press + release on tab 2 -> active = 2. */
    nt_pointer_t pr = pointer_at(x, y2, true, true, false);
    tabbar_frame(&pr, 4, &active, &st);
    nt_pointer_t rl = pointer_at(x, y2, false, false, true);
    const int clicked = tabbar_frame(&rl, 4, &active, &st);
    TEST_ASSERT_EQUAL_INT(2, active);
    TEST_ASSERT_EQUAL_INT(2, clicked);

    /* Now click tab 0 -> active updates to 0. */
    const float y0 = tab_center_y(&st, 0);
    nt_pointer_t w0 = pointer_at(x, y0, false, false, false);
    tabbar_frame(&w0, 4, &active, &st);
    nt_pointer_t pr0 = pointer_at(x, y0, true, true, false);
    tabbar_frame(&pr0, 4, &active, &st);
    nt_pointer_t rl0 = pointer_at(x, y0, false, false, true);
    tabbar_frame(&rl0, 4, &active, &st);
    TEST_ASSERT_EQUAL_INT(0, active);
}

/* ---- No click this frame -> returns -1, *active unchanged. ---- */
static void test_tabbar_no_click_returns_negative(void) {
    nt_ui_tabbar_style_t st = nt_ui_tabbar_style_defaults();
    int active = 1;
    nt_pointer_t idle = pointer_at(400.0F, 400.0F, false, false, false);
    const int clicked = tabbar_frame(&idle, 4, &active, &st);
    TEST_ASSERT_EQUAL_INT(-1, clicked);
    TEST_ASSERT_EQUAL_INT(1, active);
}

/* ---- icon_size default is text-only (no gutter), mirroring dropdown's default. ---- */
static void test_tabbar_icon_size_default_zero(void) {
    nt_ui_tabbar_style_t st = nt_ui_tabbar_style_defaults();
    TEST_ASSERT_EQUAL_UINT(0U, st.icon_size);
}

/* A wrapper tab's label x after two warm frames (1-frame IM lag bakes the bbox). */
static float tab_label_x(const nt_pointer_t *p, nt_ui_tabbar_style_t *st, const nt_atlas_region_ref_t *icons, int idx) {
    int active = 0;
    tabbar_frame_icons(p, 4, &active, icons, st);
    tabbar_frame_icons(p, 4, &active, icons, st);
    const nt_ui_bbox_t bb = nt_ui_tabbar_test_label_bbox(s_fx.ctx, TAB_BASE, idx);
    TEST_ASSERT_TRUE(bb.found);
    return bb.x;
}

/* ---- icon_size opens a leading gutter: the tab label shifts RIGHT vs the text-only (icon_size=0) tab.
 *      Mirrors test_dropdown_icon_size_gutter_shifts_label (identical gutter semantics). ---- */
static void test_tabbar_icon_size_gutter_shifts_label(void) {
    nt_pointer_t idle = pointer_at(400.0F, 400.0F, false, false, false);

    nt_ui_tabbar_style_t no_gut = nt_ui_tabbar_style_defaults();
    no_gut.icon_size = 0U; /* text-only: no gutter */
    const float x_text_only = tab_label_x(&idle, &no_gut, NULL, 0);

    nt_ui_tabbar_style_t gut = nt_ui_tabbar_style_defaults();
    gut.icon_size = 24U; /* reserves a 24px leading gutter */
    const float x_gutter = tab_label_x(&idle, &gut, NULL, 0);

    /* The gutter + child gap push the label right by at least the gutter width. */
    TEST_ASSERT_TRUE_MESSAGE(x_gutter >= x_text_only + (float)gut.icon_size, "icon_size gutter must shift the tab label right by >= icon_size");
}

/* ---- NULL-icon alignment: with the gutter open, a tab whose icon ref is set and a tab whose icon is
 *      absent both keep the SAME label x (the gutter holds alignment either way). Mirrors
 *      test_dropdown_null_icon_aligns (identical aligned-empty-gutter semantics). ---- */
static void test_tabbar_null_icon_aligns(void) {
    nt_pointer_t idle = pointer_at(400.0F, 400.0F, false, false, false);

    nt_ui_tabbar_style_t st = nt_ui_tabbar_style_defaults();
    st.icon_size = 24U;
    /* NONE accent: the selected tab adds a leading accent pad that would shift its content; compare two
     * UNSELECTED tabs with the accent off so only the icon gutter governs the label x. */
    st.accent_side = (uint8_t)NT_UI_TABBAR_ACCENT_NONE;

    /* Tab 0 carries an icon ref (atlas.id != 0); tabs 1..3 are unset ({0} = aligned-empty gutter). */
    const nt_atlas_region_ref_t icons[] = {nt_atlas_ref((nt_resource_t){.id = 1U}, 0x1234U), {0}, {0}, {0}};

    int active = 2; /* neither compared tab (0 = iconed, 1 = empty) is selected */
    tabbar_frame_icons(&idle, 4, &active, icons, &st);
    tabbar_frame_icons(&idle, 4, &active, icons, &st);

    const nt_ui_bbox_t iconed = nt_ui_tabbar_test_label_bbox(s_fx.ctx, TAB_BASE, 0);
    const nt_ui_bbox_t empty = nt_ui_tabbar_test_label_bbox(s_fx.ctx, TAB_BASE, 1);
    TEST_ASSERT_TRUE(iconed.found);
    TEST_ASSERT_TRUE(empty.found);
    /* Same gutter width -> same label x regardless of whether the icon is present (Unity float asserts
     * are disabled project-wide, so compare a manual epsilon). */
    const float dx = (iconed.x > empty.x) ? (iconed.x - empty.x) : (empty.x - iconed.x);
    TEST_ASSERT_TRUE_MESSAGE(dx <= 0.5F, "NULL icon must keep the same aligned tab label x as an iconed tab");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tabbar_abi_size);
    RUN_TEST(test_tabbar_defaults_valid);
    RUN_TEST(test_tabbar_icon_size_default_zero);
    RUN_TEST(test_tabbar_click_sets_active);
    RUN_TEST(test_tabbar_no_click_returns_negative);
    RUN_TEST(test_tabbar_core_click_sets_active);
    RUN_TEST(test_tabbar_accent_none_ok);
    RUN_TEST(test_tabbar_two_bars_with_content_no_duplicate_id);
    RUN_TEST(test_tabbar_icon_size_gutter_shifts_label);
    RUN_TEST(test_tabbar_null_icon_aligns);
    return UNITY_END();
}
