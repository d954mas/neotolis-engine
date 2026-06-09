/* Checkbox widget tests (WIDGET-10 + SC#1/#4 + death tests D-58-04/10/14).
 *
 * Leaf widget: nt_ui_checkbox(...) returns `changed`. A press-then-release over
 * the row (box OR label) flips *value and returns true exactly on the release
 * frame. Synthetic nt_pointer_t drives the full interaction loop headless. */

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

/* Row bbox declared at a FIXED absolute position so the synthetic pointer lands
 * exactly inside. Non-square, non-origin -- a stray axis swap would be visible. */
#define CB_X 100.0F
#define CB_Y 200.0F
#define CB_W 220.0F
#define CB_H 40.0F
#define CB_CX (CB_X + (CB_W * 0.5F))
#define CB_CY (CB_Y + (CB_H * 0.5F))

/* Box only occupies the left edge of the row; the label region is to its right.
 * Pointer at the far right hits the label (not the box) to prove the WHOLE row
 * is one clickable id. */
#define CB_LABEL_X (CB_X + CB_W - 20.0F)

/* Both value rows carry box + overlay art so the indicator + checkmark emit.
 * value_speed 0 = the overlay pops instantly (value_t snaps to 1) -- deterministic
 * for the 1-frame command-count assertions. state_speed 0 = instant hover/press. */
static nt_ui_checkbox_style_t s_style;

static const nt_ui_label_style_t s_label_style = {
    .font_id = 0,
    .font_size = 14,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
};

static void init_style(void) {
    s_style = (nt_ui_checkbox_style_t){0};
    const nt_atlas_region_ref_t box = {s_fx.atlas.handle, s_fx.atlas.white_region_idx};
    const nt_atlas_region_ref_t check = {s_fx.atlas.handle, s_fx.atlas.white_region_idx};
    for (int i = 0; i < 4; ++i) {
        s_style.unchecked[i] = (nt_ui_cb_state_t){.box = box, .box_tint = 0xFFFFFFFFU, .check_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
        s_style.checked[i] = (nt_ui_cb_state_t){.box = box, .check = check, .box_tint = 0xFFFFFFFFU, .check_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
    }
    s_style.unchecked[NT_UI_CB_DISABLED].opacity = 0.5F; /* disabled cell dims */
    s_style.checked[NT_UI_CB_DISABLED].opacity = 0.5F;
    s_style.text_base = s_label_style;
    s_style.box_w = 24.0F;
    s_style.box_h = 24.0F;
    s_style.overlay_w = 16.0F;
    s_style.overlay_h = 16.0F;
    s_style.gap = 8.0F;
    s_style.label_side = 0;
    s_style.state_speed = 0.0F;
    s_style.value_speed = 0.0F;
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

static uint32_t count_cmd_of_type(const nt_ui_context_t *ctx, Clay_RenderCommandType type) {
    uint32_t n = 0;
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        if (ctx->frozen_cmds.internalArray[i].commandType == type) {
            ++n;
        }
    }
    return n;
}

/* Pin the row at a fixed bbox so the synthetic pointer lands inside; the checkbox
 * decl carries a FIXED row size, the outer floating CLAY anchors its position. */
static const Clay_ElementDeclaration s_row_decl = {
    .layout = {.sizing = {CLAY_SIZING_FIXED(CB_W), CLAY_SIZING_FIXED(CB_H)}},
};

static bool cb_frame(const nt_pointer_t *p, bool *value, bool enabled) {
    bool changed = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = CB_X, .y = CB_Y}}}) {
        changed = nt_ui_checkbox(s_fx.ctx, NULL, 0, nt_ui_id("cb"), "Enable", value, &s_style, &s_row_decl, enabled);
    }
    nt_ui_end(s_fx.ctx);
    return changed;
}

/* ---- Test 1: press→release inside the BOX toggles *value, returns true on the
 *      release frame only; the press-down-only frame returns false (SC #1). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_click_box_toggles_value(void) {
    bool value = false;

    /* Frame 1: warm-up so Clay caches the row bbox for next frame's hit-test. */
    nt_pointer_t f1 = make_pointer(CB_CX, CB_CY, false, false, false);
    TEST_ASSERT_FALSE(cb_frame(&f1, &value, true));
    TEST_ASSERT_FALSE(value);

    /* Frame 2: press-down inside the box -> NOT yet a flip. */
    nt_pointer_t f2 = make_pointer(CB_X + 12.0F, CB_CY, true, true, false);
    TEST_ASSERT_FALSE(cb_frame(&f2, &value, true));
    TEST_ASSERT_FALSE(value);

    /* Frame 3: release inside -> flips exactly this frame. */
    nt_pointer_t f3 = make_pointer(CB_X + 12.0F, CB_CY, false, false, true);
    TEST_ASSERT_TRUE(cb_frame(&f3, &value, true));
    TEST_ASSERT_TRUE(value);

    /* Frame 4: idle -> no re-flip. */
    nt_pointer_t f4 = make_pointer(CB_CX, CB_CY, false, false, false);
    TEST_ASSERT_FALSE(cb_frame(&f4, &value, true));
    TEST_ASSERT_TRUE(value);
}

/* ---- Test 2: press→release over the LABEL region (not the box) toggles the
 *      same id -- the whole row is one clickable area (D-58-05). ---- */
static void test_click_label_toggles_value(void) {
    bool value = false;

    nt_pointer_t f1 = make_pointer(CB_LABEL_X, CB_CY, false, false, false);
    TEST_ASSERT_FALSE(cb_frame(&f1, &value, true));

    nt_pointer_t f2 = make_pointer(CB_LABEL_X, CB_CY, true, true, false);
    TEST_ASSERT_FALSE(cb_frame(&f2, &value, true));

    nt_pointer_t f3 = make_pointer(CB_LABEL_X, CB_CY, false, false, true);
    TEST_ASSERT_TRUE(cb_frame(&f3, &value, true));
    TEST_ASSERT_TRUE(value);
}

/* ---- Test 3: indicator-only (label==NULL) emits a box IMAGE (+overlay IMAGE
 *      when checked) and 0 TEXT commands (WIDGET-10). ---- */
static void test_indicator_only_no_text(void) {
    bool value = false;

    /* Unchecked indicator: box IMAGE present, NO overlay, 0 TEXT. */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { (void)nt_ui_checkbox(s_fx.ctx, NULL, 0, nt_ui_id("cb_off"), NULL, &value, &s_style, &s_row_decl, true); }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(0U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_TEXT));
    TEST_ASSERT_EQUAL_UINT32(1U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_IMAGE)); /* box only */

    /* Checked indicator: box IMAGE + overlay IMAGE = 2, still 0 TEXT. */
    value = true;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { (void)nt_ui_checkbox(s_fx.ctx, NULL, 0, nt_ui_id("cb_on"), NULL, &value, &s_style, &s_row_decl, true); }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(0U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_TEXT));
    TEST_ASSERT_EQUAL_UINT32(2U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_IMAGE)); /* box + overlay */
}

/* ---- Test 4: disabled -> click attempt returns false, *value unchanged, the
 *      disabled cell is forced, and the Clay open/close stays balanced (SC #4). ---- */
static void test_disabled_no_click_balanced(void) {
    bool value = false;

    /* Warm-up + full press/release sequence, all with enabled=false. */
    nt_pointer_t f1 = make_pointer(CB_CX, CB_CY, false, false, false);
    TEST_ASSERT_FALSE(cb_frame(&f1, &value, false));

    nt_pointer_t f2 = make_pointer(CB_CX, CB_CY, true, true, false);
    TEST_ASSERT_FALSE(cb_frame(&f2, &value, false));

    nt_pointer_t f3 = make_pointer(CB_CX, CB_CY, false, false, true);
    TEST_ASSERT_FALSE(cb_frame(&f3, &value, false)); /* disabled never clicks */
    TEST_ASSERT_FALSE(value);                        /* value untouched */

    /* nt_ui_end completed each frame above with no transform/opacity underflow
     * -> the disabled path opened->emitted->closed (balanced). Box still drawn. */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_IMAGE));
}

/* ---- Death tests (NT_ASSERT_FULL only) ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* data->flags must NOT set HAS_TRANSFORM/HAS_OPACITY -- the widget owns those (D-58-04). */
static void test_assert_data_flags_transform(void) {
    bool value = false;
    nt_ui_transform_t t = nt_ui_transform_defaults();
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        const nt_ui_element_data_t *bad = NT_UI_DATA_XFORM(0U, &t, 1.0F); /* sets HAS_TRANSFORM|HAS_OPACITY */
        NT_TEST_EXPECT_ASSERT((void)nt_ui_checkbox(s_fx.ctx, bad, 0, nt_ui_id("cb"), "x", &value, &s_style, &s_row_decl, true));
    }
    nt_ui_end(s_fx.ctx);
}

/* box_w <= 0 -> assert (D-58-14). */
static void test_assert_box_w_zero(void) {
    bool value = false;
    nt_ui_checkbox_style_t bad = s_style;
    bad.box_w = 0.0F;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_checkbox(s_fx.ctx, NULL, 0, nt_ui_id("cb"), "x", &value, &bad, &s_row_decl, true)); }
    nt_ui_end(s_fx.ctx);
}

/* box_h <= 0 -> assert (D-58-14). */
static void test_assert_box_h_zero(void) {
    bool value = false;
    nt_ui_checkbox_style_t bad = s_style;
    bad.box_h = 0.0F;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_checkbox(s_fx.ctx, NULL, 0, nt_ui_id("cb"), "x", &value, &bad, &s_row_decl, true)); }
    nt_ui_end(s_fx.ctx);
}

/* A value row with neither box.idle nor check.idle art -> assert (D-58-10). */
static void test_assert_empty_value_row(void) {
    bool value = false;
    nt_ui_checkbox_style_t bad = s_style;
    bad.unchecked[NT_UI_CB_IDLE].box = (nt_atlas_region_ref_t){0};
    bad.unchecked[NT_UI_CB_IDLE].check = (nt_atlas_region_ref_t){0};
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_checkbox(s_fx.ctx, NULL, 0, nt_ui_id("cb"), "x", &value, &bad, &s_row_decl, true)); }
    nt_ui_end(s_fx.ctx);
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_click_box_toggles_value);
    RUN_TEST(test_click_label_toggles_value);
    RUN_TEST(test_indicator_only_no_text);
    RUN_TEST(test_disabled_no_click_balanced);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_assert_data_flags_transform);
    RUN_TEST(test_assert_box_w_zero);
    RUN_TEST(test_assert_box_h_zero);
    RUN_TEST(test_assert_empty_value_row);
#endif
    return UNITY_END();
}
