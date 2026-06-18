/* Tooltip tests (WGT-03) — engine-owned hover-delay reveal via popup-core WITHOUT a catcher
 * (D-65-08 exception) and WITHOUT a second mutating step on the target id (T-65-19). Driven through
 * the walker fixture + NT_TEST_ACCESS probes (no GL surface). UNITY_EXCLUDE_FLOAT: compare floats via
 * an eps helper.
 *
 * Hover model: nt_ui_query_interaction reports hovered only for a widget in the front-most arbitration
 * registry, which is populated by step_interaction (NOT query). A tooltip therefore attaches to an
 * INTERACTIVE target that steps itself; the tooltip read stays idempotent (query) so it never adds a
 * second mutating step on the target id (T-65-19). Arbitration reads the PREV-frame registry, so the
 * target is stepped each frame and the cursor needs one warm frame before hover registers. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "input/nt_input_internal.h"
#include "sprite_comp/nt_sprite_comp.h" /* NT_SPRITE_FLAG_FLIP_Y */
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_tooltip.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define VIEW_W 800.0F
#define VIEW_H 600.0F

#define TGT_ID 0x709001U

/* The target is a root child with no positioning, so Clay lays it out at the origin: bbox (0,0,W,H).
 * The hover pointer must land inside that real rect, not at a fictional offset. */
#define TGT_W 120.0F
#define TGT_H 30.0F

#define DT (1.0F / 60.0F)
#define DELAY 0.30F

void setUp(void) {
    nt_test_assert_install();
    nt_input_clear_all_keys();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) {
    nt_input_clear_all_keys();
    ui_walker_fixture_shutdown(&s_fx);
}

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static nt_pointer_t pointer_at(float x, float y) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    return p;
}

static nt_ui_tooltip_style_t test_style(void) {
    nt_ui_tooltip_style_t st = nt_ui_tooltip_style_defaults();
    st.delay_secs = DELAY;
    return st;
}

/* Floats the target at the given y (a fixed bbox); the bottom-pinned case overflows downward so the
 * popup edge-flips ABOVE (caret-flip). x stays 0 so the centered tooltip stays in the viewport. */
#define TGT_BOTTOM_Y 560.0F

/* One full frame: declare the INTERACTIVE target (step_interaction registers it for arbitration +
 * bakes its bbox) + declare the tooltip after it; returns whether the tooltip panel was declared. */
static bool tooltip_frame_at(const nt_pointer_t *p, nt_ui_tooltip_style_t *st, bool at_bottom) {
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, DT, p, 1);
    if (at_bottom) {
        /* Float the target near the bottom border (mirrors the dropdown edge-flip fixture). */
        CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 0.0F, .y = TGT_BOTTOM_Y}}}) {
            CLAY({.id = (Clay_ElementId){.id = TGT_ID}, .layout = {.sizing = {CLAY_SIZING_FIXED(TGT_W), CLAY_SIZING_FIXED(TGT_H)}}}) { (void)nt_ui_step_interaction(s_fx.ctx, TGT_ID); }
        }
    } else {
        CLAY({.id = (Clay_ElementId){.id = TGT_ID}, .layout = {.sizing = {CLAY_SIZING_FIXED(TGT_W), CLAY_SIZING_FIXED(TGT_H)}}}) { (void)nt_ui_step_interaction(s_fx.ctx, TGT_ID); }
    }
    const bool shown = nt_ui_tooltip(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, TGT_ID, "hint text", st);
    nt_ui_end(s_fx.ctx);
    return shown;
}

static bool tooltip_frame(const nt_pointer_t *p, nt_ui_tooltip_style_t *st) { return tooltip_frame_at(p, st, false); }

/* ---- ABI sanity: the _Static_assert compiles; assert the runtime size too. ---- */
static void test_tooltip_abi_size(void) { TEST_ASSERT_EQUAL_UINT(88U, (unsigned)sizeof(nt_ui_tooltip_style_t)); }

/* ---- Defaults are a valid (non-zero) style; the flat baseline declares no art/caret/border/shadow. ---- */
static void test_tooltip_defaults_valid(void) {
    nt_ui_tooltip_style_t st = nt_ui_tooltip_style_defaults();
    TEST_ASSERT_TRUE(st.delay_secs >= 0.0F);
    TEST_ASSERT_TRUE(st.font_size > 0.0F);
    TEST_ASSERT_TRUE(st.open_ease_speed == 0.0F);      /* plumbed knob; default snaps (0) */
    TEST_ASSERT_TRUE(st.slice9_scale > 0.0F);          /* never trips the slice9 walker assert */
    TEST_ASSERT_EQUAL_UINT(0U, st.panel_art.atlas.id); /* flat fallback by default (no art) */
    TEST_ASSERT_EQUAL_UINT(0U, st.caret.atlas.id);
    TEST_ASSERT_EQUAL_UINT(0U, st.border_color);
    TEST_ASSERT_EQUAL_UINT(0U, st.shadow_color);
}

/* ---- Delayed reveal: the tooltip is NOT declared before delay_secs of accumulated hover and IS
 *      declared once the hover timer crosses the delay. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_tooltip_delayed_reveal(void) {
    nt_ui_tooltip_style_t st = test_style();

    /* Frame 1: pointer over the target, but the arbitration registry is empty (warm frame) -> not
     * hovered yet -> no accrual, tooltip stays hidden. */
    nt_pointer_t over = pointer_at(TGT_W * 0.5F, TGT_H * 0.5F);
    bool shown = tooltip_frame(&over, &st);
    TEST_ASSERT_FALSE(shown);

    /* Now the bbox exists; accrue hover frame by frame. ceil(DELAY/DT) frames cross the threshold. */
    const int needed = (int)ceilf(DELAY / DT);
    bool saw_reveal = false;
    for (int i = 0; i < needed + 4; ++i) {
        shown = tooltip_frame(&over, &st);
        const float hov = nt_ui_tooltip_test_hover_secs(s_fx.ctx, TGT_ID);
        if (hov < DELAY) {
            TEST_ASSERT_FALSE(shown); /* not yet past the delay -> hidden */
        } else if (shown) {
            saw_reveal = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(saw_reveal);
}

/* ---- Hide on leave: once revealed, moving the cursor off the target resets the timer to 0 and hides
 *      the tooltip. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_tooltip_hide_on_leave(void) {
    nt_ui_tooltip_style_t st = test_style();
    nt_pointer_t over = pointer_at(TGT_W * 0.5F, TGT_H * 0.5F);

    /* Warm + accrue past the delay until revealed. */
    tooltip_frame(&over, &st);
    bool shown = false;
    for (int i = 0; i < 64; ++i) {
        shown = tooltip_frame(&over, &st);
        if (shown) {
            break;
        }
    }
    TEST_ASSERT_TRUE(shown);
    TEST_ASSERT_TRUE(nt_ui_tooltip_test_hover_secs(s_fx.ctx, TGT_ID) >= DELAY);

    /* Move the cursor far away: the timer resets to 0 and the tooltip hides this same frame. */
    nt_pointer_t away = pointer_at(VIEW_W - 10.0F, VIEW_H - 10.0F);
    bool shown_after = tooltip_frame(&away, &st);
    TEST_ASSERT_FALSE(shown_after);
    TEST_ASSERT_TRUE(float_near(nt_ui_tooltip_test_hover_secs(s_fx.ctx, TGT_ID), 0.0F, 0.0001F));
}

/* ---- No catcher (D-65-08): even while the tooltip is shown, popup-core declares NO light-dismiss
 *      catcher, so base UI stays clickable. ---- */
static void test_tooltip_declares_no_catcher(void) {
    nt_ui_tooltip_style_t st = test_style();
    nt_pointer_t over = pointer_at(TGT_W * 0.5F, TGT_H * 0.5F);

    tooltip_frame(&over, &st);
    bool shown = false;
    for (int i = 0; i < 64; ++i) {
        shown = tooltip_frame(&over, &st);
        if (shown) {
            break;
        }
    }
    TEST_ASSERT_TRUE(shown);
    TEST_ASSERT_FALSE(nt_ui_popup_test_last_catcher_present());
}

/* ---- No double-mutation: the tooltip reads the target via an IDEMPOTENT query, so the game's own
 *      mutating step_interaction on the SAME target id sees an unperturbed capture machine. A press on
 *      the target still produces pressed_now/clicked exactly as if the tooltip were not present. ---- */
static nt_ui_interaction_t target_frame_with_tooltip(const nt_pointer_t *p, nt_ui_tooltip_style_t *st) {
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, DT, p, 1);
    nt_ui_interaction_t in;
    CLAY({.id = (Clay_ElementId){.id = TGT_ID}, .layout = {.sizing = {CLAY_SIZING_FIXED(TGT_W), CLAY_SIZING_FIXED(TGT_H)}}}) {
        in = nt_ui_step_interaction(s_fx.ctx, TGT_ID); /* the game's single mutating step */
    }
    (void)nt_ui_tooltip(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, TGT_ID, "hint text", st); /* idempotent read on the same id */
    nt_ui_end(s_fx.ctx);
    return in;
}

static void test_tooltip_no_double_mutation(void) {
    nt_ui_tooltip_style_t st = test_style();
    const float cx = TGT_W * 0.5F;
    const float cy = TGT_H * 0.5F;

    /* Frame 1: hover only (bake bbox + warm registry). */
    nt_pointer_t f1 = pointer_at(cx, cy);
    target_frame_with_tooltip(&f1, &st);

    /* Frame 2: press over the target -> pressed_now (capture latched once, not twice). */
    nt_pointer_t f2 = pointer_at(cx, cy);
    f2.buttons[NT_BUTTON_LEFT].is_down = true;
    f2.buttons[NT_BUTTON_LEFT].is_pressed = true;
    nt_ui_interaction_t in2 = target_frame_with_tooltip(&f2, &st);
    TEST_ASSERT_TRUE(in2.pressed_now);

    /* Frame 3: release over the target -> clicked exactly once. */
    nt_pointer_t f3 = pointer_at(cx, cy);
    f3.buttons[NT_BUTTON_LEFT].is_released = true;
    nt_ui_interaction_t in3 = target_frame_with_tooltip(&f3, &st);
    TEST_ASSERT_TRUE(in3.clicked);
}

/* A caret ref pre-resolved to the fixture atlas's white region (index 0) so the tooltip actually
 * declares the caret (atlas.id != 0 && region != INVALID). */
static nt_ui_tooltip_style_t caret_style(void) {
    nt_ui_tooltip_style_t st = test_style();
    st.caret = nt_atlas_ref_idx(s_fx.atlas.handle, 0U, s_fx.atlas.white_region_idx);
    st.caret_size = 14U;
    return st;
}

/* Accrue hover past the delay at the given target placement; once revealed, run a few MORE frames so the
 * popup's prev-frame bbox feeds the edge-flip (frame 1 measures, frame 2 flips) before the probes read. */
static void reveal_at(nt_pointer_t *over, nt_ui_tooltip_style_t *st, bool at_bottom) {
    tooltip_frame_at(over, st, at_bottom); /* warm frame */
    bool shown = false;
    for (int i = 0; i < 64; ++i) {
        if (tooltip_frame_at(over, st, at_bottom)) {
            shown = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(shown, "tooltip never revealed");
    for (int i = 0; i < 3; ++i) {
        tooltip_frame_at(over, st, at_bottom); /* settle: popup bbox -> edge-flip side */
    }
}

/* ---- Caret flips with the popup side: a top-placed target -> popup BELOW -> caret on the panel TOP edge
 *      pointing UP (no flip); a bottom-pinned target -> edge-flip ABOVE -> caret on the panel BOTTOM edge
 *      pointing DOWN (FLIP_Y). The side that drives the flip is read from the popup result probe. ---- */
static void test_tooltip_caret_flips_with_side(void) {
    /* BELOW: target at the top, room beneath -> caret points up, no flip. */
    nt_ui_tooltip_style_t below = caret_style();
    nt_pointer_t over_top = pointer_at(TGT_W * 0.5F, TGT_H * 0.5F);
    reveal_at(&over_top, &below, false);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_BELOW, nt_ui_tooltip_test_last_side());
    TEST_ASSERT_TRUE(nt_ui_tooltip_test_last_caret_present());
    TEST_ASSERT_EQUAL_UINT8(0U, nt_ui_tooltip_test_last_caret_flip()); /* up: art's native direction */

    /* ABOVE: target pinned to the bottom border -> edge-flip ABOVE -> caret points down (FLIP_Y). The
     * pointer must land on the bottom-pinned target rect (its top is near the bottom of the viewport). */
    nt_ui_tooltip_style_t above = caret_style();
    nt_pointer_t over_bottom = pointer_at(TGT_W * 0.5F, TGT_BOTTOM_Y + (TGT_H * 0.5F));
    reveal_at(&over_bottom, &above, true);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_ABOVE, nt_ui_tooltip_test_last_side());
    TEST_ASSERT_TRUE(nt_ui_tooltip_test_last_caret_present());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NT_SPRITE_FLAG_FLIP_Y, nt_ui_tooltip_test_last_caret_flip());
}

/* ---- Panel art path drops cornerRadius (IMAGE bg can't round, same rule as dropdown/menu/tabbar): with
 *      a resolvable panel_art ref the panel uses art and applies NO cornerRadius even when corner_radius
 *      is set; the flat fallback (no art) keeps its rounded rect. ---- */
static void test_tooltip_panel_art_no_corner_radius(void) {
    /* Art path: cornerRadius must be 0 regardless of style->corner_radius. */
    nt_ui_tooltip_style_t art = test_style();
    art.panel_art = nt_atlas_ref_idx(s_fx.atlas.handle, 0U, s_fx.atlas.white_region_idx);
    art.corner_radius = 8U; /* set but MUST be ignored under art */
    nt_pointer_t over = pointer_at(TGT_W * 0.5F, TGT_H * 0.5F);
    reveal_at(&over, &art, false);
    TEST_ASSERT_TRUE(nt_ui_tooltip_test_last_panel_art());
    TEST_ASSERT_TRUE(float_near(nt_ui_tooltip_test_last_panel_corner_radius(), 0.0F, 0.0001F));

    /* Flat fallback (no art): the rounded rect is kept. */
    nt_ui_tooltip_style_t flat = test_style();
    flat.corner_radius = 8U;
    nt_pointer_t over2 = pointer_at(TGT_W * 0.5F, TGT_H * 0.5F);
    reveal_at(&over2, &flat, false);
    TEST_ASSERT_FALSE(nt_ui_tooltip_test_last_panel_art());
    TEST_ASSERT_TRUE(float_near(nt_ui_tooltip_test_last_panel_corner_radius(), 8.0F, 0.0001F));
}

/* ---- The timer cell uses a SALTED id distinct from the target id (no aliasing of the target's own
 *      state). ---- */
static void test_tooltip_timer_id_salted(void) {
    TEST_ASSERT_NOT_EQUAL_UINT32(TGT_ID, nt_ui_tooltip_test_timer_id(TGT_ID));
    TEST_ASSERT_NOT_EQUAL_UINT32(0U, nt_ui_tooltip_test_timer_id(TGT_ID));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tooltip_abi_size);
    RUN_TEST(test_tooltip_defaults_valid);
    RUN_TEST(test_tooltip_delayed_reveal);
    RUN_TEST(test_tooltip_hide_on_leave);
    RUN_TEST(test_tooltip_declares_no_catcher);
    RUN_TEST(test_tooltip_no_double_mutation);
    RUN_TEST(test_tooltip_caret_flips_with_side);
    RUN_TEST(test_tooltip_panel_art_no_corner_radius);
    RUN_TEST(test_tooltip_timer_id_salted);
    return UNITY_END();
}
