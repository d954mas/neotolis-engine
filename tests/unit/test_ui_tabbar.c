/* Reusable tab-bar tests (WGT-02). Driven through the walker fixture (no GL surface). A click on tab i
 * sets the game-owned int* active to i; clicking another tab updates it (Model D). */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "input/nt_input_internal.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
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

/* One tab-bar frame: a FIXED-width vertical bar floated at the top-left so each tab's bbox is known.
 * Returns the clicked index (-1 if none). */
static int tabbar_frame(const nt_pointer_t *p, int count, int *active, const nt_ui_tabbar_style_t *st) {
    int clicked = -1;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    CLAY({.id = (Clay_ElementId){.id = 0x7AB0F0U},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 0.0F, .y = 0.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(300)}}}) {
        clicked = nt_ui_tabbar(s_fx.ctx, TAB_BASE, s_tabs, count, active, st);
    }
    nt_ui_end(s_fx.ctx);
    return clicked;
}

/* ---- ABI sanity ---- */
static void test_tabbar_abi_size(void) { TEST_ASSERT_EQUAL_UINT(44U, (unsigned)sizeof(nt_ui_tabbar_style_t)); }

static void test_tabbar_defaults_valid(void) {
    nt_ui_tabbar_style_t st = nt_ui_tabbar_style_defaults();
    TEST_ASSERT_TRUE(st.font_size > 0.0F);
    TEST_ASSERT_TRUE(st.tab_extent > 0U);
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tabbar_abi_size);
    RUN_TEST(test_tabbar_defaults_valid);
    RUN_TEST(test_tabbar_click_sets_active);
    RUN_TEST(test_tabbar_no_click_returns_negative);
    return UNITY_END();
}
