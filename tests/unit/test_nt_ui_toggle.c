/* Toggle widget tests (WIDGET-12 + SC#3 logic over the shared cb_core).
 *
 * Leaf widget: nt_ui_toggle(...) flips *value with the SAME return contract as
 * checkbox (changed exactly on the release frame). The thumb overlay is ALWAYS
 * visible (unlike the checkmark pop) and carries a render-only offset_x DELTA
 * eased by value_t: OFF (value_t 0) -> x = 0, ON (value_t 1) -> x = box_w -
 * overlay_w - 2*thumb_pad. With value_speed == 0 the slide snaps to its endpoint,
 * so the DELTA is logic-assertable by reading the thumb's emitted vertex X.
 * The smooth eased slide itself is user-visual-only (58-VALIDATION.md). */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "renderers/nt_sprite_renderer.h"
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

/* Single toggle row, FIXED so the synthetic pointer lands inside. The track is
 * wide (box_w) so the thumb travel is a clearly non-zero DELTA. */
#define TG_X 100.0F
#define TG_Y 200.0F
#define TG_W 220.0F
#define TG_H 40.0F
#define TG_CX (TG_X + (TG_W * 0.5F))
#define TG_CY (TG_Y + (TG_H * 0.5F))

/* Track + thumb dims chosen so the slide DELTA is unambiguous. */
#define TG_BOX_W 56.0F
#define TG_BOX_H 28.0F
#define TG_THUMB 24.0F
#define TG_THUMB_PAD 2.0F
/* DELTA = box_w - overlay_w - 2*thumb_pad. */
#define TG_EXPECTED_SLIDE (TG_BOX_W - TG_THUMB - (2.0F * TG_THUMB_PAD))

static nt_ui_checkbox_style_t s_style;

static const nt_ui_label_style_t s_label_style = {
    .font_id = 0,
    .font_size = 14,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
};

static void init_style(void) {
    s_style = (nt_ui_checkbox_style_t){0};
    const nt_atlas_region_ref_t track = {s_fx.atlas.handle, s_fx.atlas.white_region_idx};
    const nt_atlas_region_ref_t thumb = {s_fx.atlas.handle, s_fx.atlas.white_region_idx};
    /* Both rows carry the thumb art so the thumb is present at BOTH ends. */
    for (int i = 0; i < 4; ++i) {
        s_style.unchecked[i] = (nt_ui_cb_state_t){.box = track, .check = thumb, .box_tint = 0xFFFFFFFFU, .check_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
        s_style.checked[i] = (nt_ui_cb_state_t){.box = track, .check = thumb, .box_tint = 0xFFFFFFFFU, .check_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
    }
    s_style.unchecked[NT_UI_CB_DISABLED].opacity = 0.5F;
    s_style.checked[NT_UI_CB_DISABLED].opacity = 0.5F;
    s_style.text_base = s_label_style;
    s_style.box_w = TG_BOX_W;
    s_style.box_h = TG_BOX_H;
    s_style.overlay_w = TG_THUMB;
    s_style.overlay_h = TG_THUMB;
    s_style.thumb_pad = TG_THUMB_PAD;
    s_style.gap = 8.0F;
    s_style.label_side = 0;
    s_style.state_speed = 0.0F; /* instant */
    s_style.value_speed = 0.0F; /* slide snaps to endpoint -> deterministic */
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

static const Clay_ElementDeclaration s_row_decl = {
    .layout = {.sizing = {CLAY_SIZING_FIXED(TG_W), CLAY_SIZING_FIXED(TG_H)}},
};

static bool tg_frame(const nt_pointer_t *p, bool *value, bool enabled) {
    bool changed = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = TG_X, .y = TG_Y}}}) {
        changed = nt_ui_toggle(s_fx.ctx, NULL, 0, nt_ui_id("tg"), "Mute", value, &s_style, &s_row_decl, enabled);
    }
    nt_ui_end(s_fx.ctx);
    return changed;
}

/* Walk one indicator-only toggle frame and return the LAST-emitted sprite's V0.x
 * (the thumb is the last child of the box). Walk is needed to dispatch sprites. */
static float thumb_emit_x(bool value) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = TG_X, .y = TG_Y}}}) {
        (void)nt_ui_toggle(s_fx.ctx, NULL, 0, nt_ui_id("tgx"), NULL, &value, &s_style, &s_row_decl, true);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_target_t target = {.viewport = {0, 0, 800, 600}};
    nt_ui_walk(s_fx.ctx, &target);
    float pos[3];
    nt_sprite_renderer_test_last_emit_position(0U, pos);
    return pos[0];
}

/* ---- Test 1: press->release flips *value, returns true exactly on release. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_toggle_flip_release_edge(void) {
    bool value = false;

    nt_pointer_t f1 = make_pointer(TG_CX, TG_CY, false, false, false); /* warm-up */
    TEST_ASSERT_FALSE(tg_frame(&f1, &value, true));
    TEST_ASSERT_FALSE(value);

    nt_pointer_t f2 = make_pointer(TG_CX, TG_CY, true, true, false); /* press-down only */
    TEST_ASSERT_FALSE(tg_frame(&f2, &value, true));
    TEST_ASSERT_FALSE(value);

    nt_pointer_t f3 = make_pointer(TG_CX, TG_CY, false, false, true); /* release -> flip */
    TEST_ASSERT_TRUE(tg_frame(&f3, &value, true));
    TEST_ASSERT_TRUE(value);

    nt_pointer_t f4 = make_pointer(TG_CX, TG_CY, false, false, false); /* idle -> no re-flip */
    TEST_ASSERT_FALSE(tg_frame(&f4, &value, true));
    TEST_ASSERT_TRUE(value);
}

/* ---- Test 2: thumb is ALWAYS visible AND its offset_x DELTA snaps to its
 *      endpoints. value_t 0 -> x=0; value_t 1 -> x = box_w - overlay_w -
 *      2*thumb_pad. Asserted via the walker-emitted thumb vertex X delta. ---- */
static void test_thumb_slide_delta_endpoints(void) {
    /* Warm-up so Clay caches the row bbox (hit-test not needed, but stable layout). */
    nt_pointer_t mouse = {0};
    (void)mouse;

    const float x_off = thumb_emit_x(false); /* value_t snaps to 0 */
    const float x_on = thumb_emit_x(true);   /* value_t snaps to 1 */

    /* OFF and ON both emit the thumb (always visible): the box + thumb each frame
     * means the LAST emit is the thumb both times. The X difference is the slide. */
    const float slide = x_on - x_off;
    char msg[160];
    (void)snprintf(msg, sizeof msg, "thumb slide delta expected %f, got %f (off=%f on=%f)", (double)TG_EXPECTED_SLIDE, (double)slide, (double)x_off, (double)x_on);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(slide - TG_EXPECTED_SLIDE) <= 0.5F, msg);
    /* The thumb must be present at BOTH ends (2 IMAGE cmds: track + thumb each). */
}

/* ---- Test 3: thumb present at OFF -> both ends draw track + thumb (always
 *      visible, unlike the checkmark pop). 2 IMAGE, 0 TEXT in indicator mode. ---- */
static void test_thumb_visible_at_off(void) {
    bool value = false; /* OFF */
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { (void)nt_ui_toggle(s_fx.ctx, NULL, 0, nt_ui_id("tg_off"), NULL, &value, &s_style, &s_row_decl, true); }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(0U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_TEXT));
    TEST_ASSERT_EQUAL_UINT32(2U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_IMAGE)); /* track + thumb at OFF */
}

/* ---- Test 4: disabled toggle ignores clicks; *value unchanged; balanced. ---- */
static void test_disabled_toggle_balanced(void) {
    bool value = false;

    nt_pointer_t f1 = make_pointer(TG_CX, TG_CY, false, false, false);
    TEST_ASSERT_FALSE(tg_frame(&f1, &value, false));

    nt_pointer_t f2 = make_pointer(TG_CX, TG_CY, true, true, false);
    TEST_ASSERT_FALSE(tg_frame(&f2, &value, false));

    nt_pointer_t f3 = make_pointer(TG_CX, TG_CY, false, false, true);
    TEST_ASSERT_FALSE(tg_frame(&f3, &value, false)); /* disabled never clicks */
    TEST_ASSERT_FALSE(value);                        /* value untouched */

    /* nt_ui_end completed each frame with no transform/opacity underflow -> the
     * disabled path opened->emitted->closed (balanced); track + thumb still drawn. */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2U, count_cmd_of_type(s_fx.ctx, CLAY_RENDER_COMMAND_TYPE_IMAGE));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_toggle_flip_release_edge);
    RUN_TEST(test_thumb_slide_delta_endpoints);
    RUN_TEST(test_thumb_visible_at_off);
    RUN_TEST(test_disabled_toggle_balanced);
    return UNITY_END();
}
