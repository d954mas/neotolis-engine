/* Radio widget tests (WIDGET-11 + SC#2 exclusivity over the shared cb_core).
 *
 * Leaf widget: nt_ui_radio(...) returns `changed` = the frame *selected flips to
 * my_value. Exclusivity is FREE — every radio in a group reads the SAME int*, so
 * selecting one leaves the others unchecked next frame with NO engine-side state.
 * Re-selecting an already-selected radio is a no-op (no flicker, returns false).
 * Synthetic nt_pointer_t drives the full press/release loop headless. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_checkbox.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Three radios stacked vertically; each row is FIXED so the synthetic pointer
 * lands inside exactly one. Non-square, non-origin to expose any axis swap. */
#define RD_X 100.0F
#define RD_W 220.0F
#define RD_H 40.0F
#define RD_Y0 200.0F
#define RD_Y1 (RD_Y0 + RD_H)
#define RD_Y2 (RD_Y0 + (2.0F * RD_H))
#define RD_CX (RD_X + (RD_W * 0.5F))
/* Center Y of radio i. */
#define RD_CY(i) (RD_Y0 + (RD_H * (float)(i)) + (RD_H * 0.5F))

static nt_ui_checkbox_style_t s_style;

static const nt_ui_label_style_t s_label_style = {
    .font_id = 0,
    .font_size = 14,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
};

static void init_style(void) {
    s_style = (nt_ui_checkbox_style_t){0};
    const nt_atlas_region_ref_t box = {s_fx.atlas.handle, s_fx.atlas.white_region_idx};
    const nt_atlas_region_ref_t dot = {s_fx.atlas.handle, s_fx.atlas.white_region_idx};
    for (int i = 0; i < 4; ++i) {
        s_style.unchecked[i] = (nt_ui_cb_state_t){.box = box, .box_tint = 0xFFFFFFFFU, .check_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
        s_style.checked[i] = (nt_ui_cb_state_t){.box = box, .check = dot, .box_tint = 0xFFFFFFFFU, .check_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
    }
    s_style.unchecked[NT_UI_CB_DISABLED].opacity = 0.5F;
    s_style.checked[NT_UI_CB_DISABLED].opacity = 0.5F;
    s_style.text_base = s_label_style;
    s_style.box_w = 24.0F;
    s_style.box_h = 24.0F;
    s_style.overlay_w = 16.0F;
    s_style.overlay_h = 16.0F;
    s_style.gap = 8.0F;
    s_style.label_side = 0;
    s_style.state_speed = 0.0F; /* instant */
    s_style.value_speed = 0.0F; /* overlay pops instantly -> deterministic */
}

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    init_style();
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static nt_pointer_t make_pointer(float x, float y, bool is_down, bool is_pressed, bool is_released) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = is_down;
    p.buttons[NT_BUTTON_LEFT].is_pressed = is_pressed;
    p.buttons[NT_BUTTON_LEFT].is_released = is_released;
    return p;
}

/* Per-row FIXED decl so each radio gets a distinct hit bbox. */
static const Clay_ElementDeclaration s_row_decl = {
    .layout = {.sizing = {CLAY_SIZING_FIXED(RD_W), CLAY_SIZING_FIXED(RD_H)}},
};

/* Drive all three radios in one frame; out[] receives each radio's `changed`. */
static void rd_frame(const nt_pointer_t *p, int *selected, bool out[3], const bool enabled[3]) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = RD_X, .y = RD_Y0}}, .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        out[0] = nt_ui_radio(s_fx.ctx, NULL, nt_ui_id("r0"), "Low", selected, 0, &s_style, &s_row_decl, enabled[0]);
        out[1] = nt_ui_radio(s_fx.ctx, NULL, nt_ui_id("r1"), "Med", selected, 1, &s_style, &s_row_decl, enabled[1]);
        out[2] = nt_ui_radio(s_fx.ctx, NULL, nt_ui_id("r2"), "High", selected, 2, &s_style, &s_row_decl, enabled[2]);
    }
    nt_ui_end(s_fx.ctx);
}

/* Press+release over radio `idx` (full click) across 3 frames; returns the
 * `changed` of every radio captured on the RELEASE frame. */
static void click_radio(int idx, int *selected, bool out[3], const bool enabled[3]) {
    const float cx = RD_CX;
    const float cy = RD_CY(idx);
    nt_pointer_t f_press = make_pointer(cx, cy, true, true, false);
    bool ignore[3];
    rd_frame(&f_press, selected, ignore, enabled);
    nt_pointer_t f_rel = make_pointer(cx, cy, false, false, true);
    rd_frame(&f_rel, selected, out, enabled);
}

static const bool s_all_enabled[3] = {true, true, true};

/* ---- Test 1: selecting one radio sets *selected and flips the others off. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_select_one_excludes_others(void) {
    int selected = 0; /* radio 0 starts selected */
    bool out[3];

    /* Warm-up frame so Clay caches each row bbox for next frame's hit-test. */
    nt_pointer_t warm = make_pointer(0.0F, 0.0F, false, false, false);
    rd_frame(&warm, &selected, out, s_all_enabled);

    /* Click radio 1 -> *selected becomes 1; radio 1 returns changed, others not. */
    click_radio(1, &selected, out, s_all_enabled);
    TEST_ASSERT_EQUAL_INT(1, selected);
    TEST_ASSERT_FALSE(out[0]);
    TEST_ASSERT_TRUE(out[1]);
    TEST_ASSERT_FALSE(out[2]);

    /* Click radio 2 -> *selected becomes 2; radio 1 flips off next frame. */
    click_radio(2, &selected, out, s_all_enabled);
    TEST_ASSERT_EQUAL_INT(2, selected);
    TEST_ASSERT_FALSE(out[0]);
    TEST_ASSERT_FALSE(out[1]);
    TEST_ASSERT_TRUE(out[2]);
}

/* ---- Test 2: re-selecting the already-selected radio is a no-op (no flicker). ---- */
static void test_reselect_is_noop(void) {
    int selected = 1; /* radio 1 already selected */
    bool out[3];

    nt_pointer_t warm = make_pointer(0.0F, 0.0F, false, false, false);
    rd_frame(&warm, &selected, out, s_all_enabled);

    click_radio(1, &selected, out, s_all_enabled);
    TEST_ASSERT_EQUAL_INT(1, selected); /* unchanged */
    TEST_ASSERT_FALSE(out[1]);          /* no spurious changed -> no flicker */
}

/* ---- Test 3: a disabled radio ignores clicks; *selected unchanged. ---- */
static void test_disabled_radio_ignored(void) {
    int selected = 0;
    bool out[3];
    const bool enabled[3] = {true, false, true}; /* radio 1 disabled */

    nt_pointer_t warm = make_pointer(0.0F, 0.0F, false, false, false);
    rd_frame(&warm, &selected, out, enabled);

    click_radio(1, &selected, out, enabled);
    TEST_ASSERT_EQUAL_INT(0, selected); /* disabled radio did not select */
    TEST_ASSERT_FALSE(out[1]);
}

/* ---- Test 4: checked radio draws box + dot overlay; unchecked draws box only.
 *      Indicator-only (label==NULL) -> 0 TEXT commands. ---- */
static void test_radio_overlay_command_counts(void) {
    int selected = 0;
    nt_pointer_t mouse = {0};

    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root"), .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        (void)nt_ui_radio(s_fx.ctx, NULL, nt_ui_id("ra"), NULL, &selected, 0, &s_style, &s_row_decl, true); /* selected -> box + dot */
        (void)nt_ui_radio(s_fx.ctx, NULL, nt_ui_id("rb"), NULL, &selected, 1, &s_style, &s_row_decl, true); /* unselected -> box only */
    }
    nt_ui_end(s_fx.ctx);

    uint32_t text = 0;
    uint32_t image = 0;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommandType t = s_fx.ctx->frozen_cmds.internalArray[i].commandType;
        if (t == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            ++text;
        } else if (t == CLAY_RENDER_COMMAND_TYPE_IMAGE) {
            ++image;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(0U, text);  /* indicator only */
    TEST_ASSERT_EQUAL_UINT32(3U, image); /* selected: box+dot; unselected: box */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_select_one_excludes_others);
    RUN_TEST(test_reselect_is_noop);
    RUN_TEST(test_disabled_radio_ignored);
    RUN_TEST(test_radio_overlay_command_counts);
    return UNITY_END();
}
