/* Tooltip tests (WGT-03) — engine-owned hover-delay reveal via popup-core WITHOUT a catcher
 * (D-65-08 exception) and WITHOUT a second mutating step on the target id (T-65-19). Driven through
 * the walker fixture + NT_TEST_ACCESS probes (no GL surface). UNITY_EXCLUDE_FLOAT: compare floats via
 * an eps helper.
 *
 * Hover model: nt_ui_query_interaction reports hovered from THIS-frame pointer vs PREV-frame bbox
 * (1-frame IM lag), so each probe declares the target one frame to bake its bbox, then drives the
 * pointer over it on the following frames. */

#include <math.h>
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
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_tooltip.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define VIEW_W 800.0F
#define VIEW_H 600.0F

#define TGT_ID 0x709001U

/* Target rect (top-left so the tooltip fits below). */
#define TGT_X 100.0F
#define TGT_Y 80.0F
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

/* One full frame: declare the target widget (so it carries a queryable bbox) + declare the tooltip
 * after it; returns whether the tooltip panel was declared this frame. */
static bool tooltip_frame(const nt_pointer_t *p, const nt_ui_tooltip_style_t *st) {
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, DT, p, 1);
    CLAY({.id = (Clay_ElementId){.id = TGT_ID}, .layout = {.sizing = {CLAY_SIZING_FIXED(TGT_W), CLAY_SIZING_FIXED(TGT_H)}}}) {}
    const bool shown = nt_ui_tooltip(s_fx.ctx, TGT_ID, "hint text", st);
    nt_ui_end(s_fx.ctx);
    return shown;
}

/* ---- ABI sanity: the _Static_assert compiles; assert the runtime size too. ---- */
static void test_tooltip_abi_size(void) {
    TEST_ASSERT_EQUAL_UINT(24U, (unsigned)sizeof(nt_ui_tooltip_style_t));
}

/* ---- Defaults are a valid (non-zero) style. ---- */
static void test_tooltip_defaults_valid(void) {
    nt_ui_tooltip_style_t st = nt_ui_tooltip_style_defaults();
    TEST_ASSERT_TRUE(st.delay_secs >= 0.0F);
    TEST_ASSERT_TRUE(st.font_size > 0.0F);
}

/* ---- Delayed reveal: the tooltip is NOT declared before delay_secs of accumulated hover and IS
 *      declared once the hover timer crosses the delay. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_tooltip_delayed_reveal(void) {
    nt_ui_tooltip_style_t st = test_style();

    /* Frame 1: pointer over the target, but no prev-frame bbox yet -> not hovered -> no accrual. */
    nt_pointer_t over = pointer_at(TGT_X + TGT_W * 0.5F, TGT_Y + TGT_H * 0.5F);
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
    nt_pointer_t over = pointer_at(TGT_X + TGT_W * 0.5F, TGT_Y + TGT_H * 0.5F);

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
    nt_pointer_t over = pointer_at(TGT_X + TGT_W * 0.5F, TGT_Y + TGT_H * 0.5F);

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
static nt_ui_interaction_t target_frame_with_tooltip(const nt_pointer_t *p, const nt_ui_tooltip_style_t *st) {
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, DT, p, 1);
    nt_ui_interaction_t in;
    CLAY({.id = (Clay_ElementId){.id = TGT_ID}, .layout = {.sizing = {CLAY_SIZING_FIXED(TGT_W), CLAY_SIZING_FIXED(TGT_H)}}}) {
        in = nt_ui_step_interaction(s_fx.ctx, TGT_ID); /* the game's single mutating step */
    }
    (void)nt_ui_tooltip(s_fx.ctx, TGT_ID, "hint text", st); /* idempotent read on the same id */
    nt_ui_end(s_fx.ctx);
    return in;
}

static void test_tooltip_no_double_mutation(void) {
    nt_ui_tooltip_style_t st = test_style();
    const float cx = TGT_X + TGT_W * 0.5F;
    const float cy = TGT_Y + TGT_H * 0.5F;

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
    RUN_TEST(test_tooltip_timer_id_salted);
    return UNITY_END();
}
