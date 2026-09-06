/* Popup-core tests — present-only catcher guard, trigger-anchor edge-flip near all 4
 * borders (asymmetric data per AGENTS.md), outside-click dismiss raising close_requested, and the
 * one-bool wrapper clearing the game's open. Driven through the walker fixture + NT_TEST_ACCESS probes
 * (no GL surface). UNITY_EXCLUDE_FLOAT: compare floats via an eps helper. */

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
#include "ui/nt_ui_state.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define VIEW_W 800.0F
#define VIEW_H 600.0F

#define POP_A 0x50A001U
#define POP_B 0x50A002U

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

static void popup_frame(uint32_t id, const nt_ui_popup_style_t *st, const nt_ui_popup_anchor_t *anc, bool open, nt_ui_popup_result_t *out);

/* ---- ABI sanity: the _Static_asserts compile; assert the runtime sizes match too. ---- */
static void test_popup_abi_sizes(void) {
    TEST_ASSERT_EQUAL_UINT(8U, (unsigned)sizeof(nt_ui_popup_result_t));
    TEST_ASSERT_EQUAL_UINT(20U, (unsigned)sizeof(nt_ui_popup_anchor_t));
    TEST_ASSERT_EQUAL_UINT(80U, (unsigned)sizeof(nt_ui_popup_style_t));
}

/* ---- Defaults are a valid (non-zero-init) style: identity transforms dodge the anim scale>0 trap. ---- */
static void test_popup_defaults_valid(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    TEST_ASSERT_TRUE(st.open_xf_start.scale_x > 0.0F);
    TEST_ASSERT_TRUE(st.open_xf_start.scale_y > 0.0F);
    TEST_ASSERT_TRUE((st.flags & NT_UI_POPUP_LIGHT_DISMISS) != 0U);
}

/* ---- Floating smoke: a popup declared over base content; the walker exits with a balanced scissor
 *      stack and a balanced popup stack. ---- */
static void test_popup_floating_smoke(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F; /* instant: t snaps to 1 */
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 80.0F, .h = 30.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_pointer_t p = {.active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    CLAY({.id = CLAY_ID("base"), .layout = {.sizing = {CLAY_SIZING_FIXED(800), CLAY_SIZING_FIXED(600)}}}) {}
    nt_ui_popup_result_t r = nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, true);
    {
        CLAY({.id = CLAY_ID("popup_body"), .layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(90)}}}) {}
    }
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(r.visible);
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->active_modal_depth);
}

/* ---- z-band: each popup declares ONE stride above its enclosing floating, catcher one below its own
 *      panel. A popup nested inside another must therefore reach 2*stride — asserted on the band Clay
 *      stamped on the bodies' render commands, not on the engine's own arithmetic. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_popup_zband_and_nesting(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 80.0F, .h = 30.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_pointer_t p = {.active = true};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
    nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, true);
    /* Opaque bodies so each panel subtree emits a RECTANGLE, which Clay stamps with its root's band. */
    CLAY({.id = CLAY_ID("body_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    nt_ui_popup_begin(s_fx.ctx, POP_B, &st, &anc, true);
    TEST_ASSERT_EQUAL_UINT8(2U, s_fx.ctx->active_modal_depth);
    CLAY({.id = CLAY_ID("body_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_popup_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->active_modal_depth);
    nt_ui_end(s_fx.ctx);

    const int32_t stride = (int32_t)s_fx.ctx->modal_zband_stride;
    int32_t z_a = INT32_MIN;
    int32_t z_b = INT32_MIN;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType != CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
            continue;
        }
        if (c->id == CLAY_ID("body_a").id) {
            z_a = c->zIndex;
        } else if (c->id == CLAY_ID("body_b").id) {
            z_b = c->zIndex;
        }
    }
    TEST_ASSERT_EQUAL_INT32_MESSAGE(stride, z_a, "outer popup must land one stride above the base band");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(2 * stride, z_b, "a popup nested inside another must accumulate a second stride");
}

/* ---- Depth overflow fires NT_ASSERT before the push (no silent fallback). ---- */
static void test_popup_depth_overflow_asserts(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = 0.0F, .y = 0.0F, .w = 10.0F, .h = 10.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    for (uint32_t i = 0; i < NT_UI_MODAL_MAX_DEPTH; ++i) {
        nt_ui_popup_begin(s_fx.ctx, 0x50B000U + i, &st, &anc, true);
    }
    NT_TEST_EXPECT_ASSERT(nt_ui_popup_begin(s_fx.ctx, 0x50BFFFU, &st, &anc, true));
    for (uint32_t i = 0; i < NT_UI_MODAL_MAX_DEPTH; ++i) {
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Present-only catcher guard: a fully-closed popup (open=false, settled t=0) declares NO catcher,
 *      so base UI stays clickable; an open/settling popup DOES declare one. ---- */
static void test_popup_present_only_catcher(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F; /* instant: open->t=1, closed->t=0 */
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 80.0F, .h = 30.0F, .prefer_side = NT_UI_POPUP_BELOW};

    /* Open: a catcher is declared. */
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, true);
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(nt_ui_popup_test_last_catcher_present());

    /* Fully closed: NO catcher (present-only guard). */
    bool open = false;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    const bool declared = nt_ui_popup_visible(s_fx.ctx, POP_A, &st, &anc, &open);
    if (declared) {
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(declared);
    TEST_ASSERT_FALSE(nt_ui_popup_test_last_catcher_present());
}

/* ---- A fully-closed popup called every frame must NOT block a base widget under the same point. ---- */
static nt_ui_interaction_t closed_popup_base_frame(const nt_ui_popup_style_t *st, const nt_ui_popup_anchor_t *anc, const nt_pointer_t *p) {
    bool open = false;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    nt_ui_interaction_t in;
    CLAY({.id = (Clay_ElementId){.id = nt_ui_id("closed_base")}, .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(120)}}}) {
        in = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("closed_base"));
    }
    const bool declared = nt_ui_popup_visible(s_fx.ctx, POP_A, st, anc, &open);
    if (declared) {
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    return in;
}

static void test_popup_closed_does_not_block_base(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = 0.0F, .y = 0.0F, .w = 80.0F, .h = 30.0F, .prefer_side = NT_UI_POPUP_BELOW};

    nt_pointer_t f1 = pointer_at(100.0F, 60.0F, false, false, false);
    closed_popup_base_frame(&st, &anc, &f1);
    nt_pointer_t f2 = pointer_at(100.0F, 60.0F, true, true, false);
    nt_ui_interaction_t in2 = closed_popup_base_frame(&st, &anc, &f2);
    TEST_ASSERT_TRUE(in2.pressed_now);
    nt_pointer_t f3 = pointer_at(100.0F, 60.0F, false, false, true);
    nt_ui_interaction_t in3 = closed_popup_base_frame(&st, &anc, &f3);
    TEST_ASSERT_TRUE(in3.clicked);
}

/* ---- Edge-flip near all 4 borders (asymmetric data). prev-frame bbox feeds the projection, so each
 *      probe runs 2 frames; frame 1 establishes the popup size, frame 2 reads it for the flip decision.
 *      The popup is 200x150; viewport 800x600. ---- */
#define POP_W 200.0F
#define POP_H 150.0F

static uint8_t edge_flip_side(uint32_t id, float ax, float ay, uint8_t prefer) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = ax, .y = ay, .w = 40.0F, .h = 24.0F, .prefer_side = prefer};

    uint8_t side = (uint8_t)NT_UI_POPUP_CENTER;
    for (int f = 0; f < 2; ++f) {
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
        side = nt_ui_popup_begin(s_fx.ctx, id, &st, &anc, true).side;
        {
            CLAY({.id = (Clay_ElementId){.id = id ^ 0xB0D70U}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
        }
        nt_ui_popup_end(s_fx.ctx);
        nt_ui_end(s_fx.ctx);
    }
    return side;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_popup_edge_flip_four_borders(void) {
    /* BELOW preferred, anchor near TOP -> fits below, stays BELOW. */
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_BELOW, edge_flip_side(0x50C001U, 100.0F, 20.0F, NT_UI_POPUP_BELOW));
    /* BELOW preferred, anchor near BOTTOM -> overflows bottom, flips ABOVE. */
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_ABOVE, edge_flip_side(0x50C002U, 100.0F, 560.0F, NT_UI_POPUP_BELOW));
    /* RIGHT preferred, anchor near LEFT -> fits right, stays RIGHT. */
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_RIGHT, edge_flip_side(0x50C003U, 20.0F, 100.0F, NT_UI_POPUP_RIGHT));
    /* RIGHT preferred, anchor near RIGHT -> overflows right, flips LEFT. */
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_LEFT, edge_flip_side(0x50C004U, 700.0F, 100.0F, NT_UI_POPUP_RIGHT));
}

/* ---- First-frame (no prev bbox) falls back to prefer_side, no UB / no flip. ---- */
static void test_popup_first_frame_uses_prefer_side(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    /* Anchor near the bottom border but it is frame 1 -> no measured size -> stays BELOW (A3). */
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 560.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_popup_result_t r = nt_ui_popup_begin(s_fx.ctx, 0x50D001U, &st, &anc, true);
    {
        CLAY({.id = (Clay_ElementId){.id = 0x50D002U}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
    }
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_BELOW, r.side);
}

/* ---- CENTER anchor never edge-flips (modal placement). ---- */
static void test_popup_center_no_flip(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.prefer_side = NT_UI_POPUP_CENTER};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_popup_result_t r = nt_ui_popup_begin(s_fx.ctx, 0x50E001U, &st, &anc, true);
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CENTER, r.side);
}

/* ---- Outside-click dismiss: a click on the catcher (away from the popup body) raises close_requested;
 *      the one-bool wrapper clears the game's open. The popup body shrink-wraps a 200x150 box anchored
 *      below a top-left trigger; the catcher click lands far in the bottom-right corner. ---- */
static void test_popup_outside_click_dismiss(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = 20.0F, .y = 20.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};

    /* Frame 1: open, register the popup body + catcher (establish prev-frame registry). */
    nt_pointer_t f1 = pointer_at(700.0F, 500.0F, false, false, false);
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &f1, 1);
    nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, true);
    {
        CLAY({.id = (Clay_ElementId){.id = POP_A ^ 0xB0D70U}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
    }
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);

    /* Frame 2: full press+release in the far corner (over the catcher, outside the popup body) ->
     * the catcher wins the prev-frame topmost-z and reports a click -> close_requested. */
    nt_pointer_t f2 = pointer_at(700.0F, 500.0F, false, false, true);
    f2.buttons[NT_BUTTON_LEFT].is_pressed = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &f2, 1);
    nt_ui_popup_result_t r = nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, true);
    {
        CLAY({.id = (Clay_ElementId){.id = POP_A ^ 0xB0D70U}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
    }
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(r.close_requested);
}

/* ---- One-bool wrapper clears *open on close, keeps declaring while animating, returns false once
 *      fully closed. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_popup_wrapper_clears_open(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 8.0F; /* finite ease so the close tween plays out */
    nt_ui_popup_anchor_t anc = {.x = 20.0F, .y = 20.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};
    bool open = true;

    /* Frame 1: open -> declared, register body + catcher. */
    nt_pointer_t f1 = pointer_at(700.0F, 500.0F, false, false, false);
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &f1, 1);
    bool declared = nt_ui_popup_visible(s_fx.ctx, POP_A, &st, &anc, &open);
    TEST_ASSERT_TRUE(declared);
    if (declared) {
        CLAY({.id = (Clay_ElementId){.id = POP_A ^ 0xB0D70U}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(open);

    /* Frame 2: outside-click -> wrapper clears *open but STILL returns true (animating closed). */
    nt_pointer_t f2 = pointer_at(700.0F, 500.0F, false, false, true);
    f2.buttons[NT_BUTTON_LEFT].is_pressed = true;
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &f2, 1);
    declared = nt_ui_popup_visible(s_fx.ctx, POP_A, &st, &anc, &open);
    TEST_ASSERT_TRUE(declared);
    TEST_ASSERT_FALSE(open); /* outside-click cleared the bool */
    if (declared) {
        CLAY({.id = (Clay_ElementId){.id = POP_A ^ 0xB0D70U}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Subsequent frames: t decays; eventually the wrapper returns false (fully closed). */
    bool went_false = false;
    for (int i = 0; i < 300; ++i) {
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
        declared = nt_ui_popup_visible(s_fx.ctx, POP_A, &st, &anc, &open);
        if (declared) {
            CLAY({.id = (Clay_ElementId){.id = POP_A ^ 0xB0D70U}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
            nt_ui_popup_end(s_fx.ctx);
        }
        nt_ui_end(s_fx.ctx);
        if (!declared) {
            went_false = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(went_false);
}

/* ---- A no-dismiss popup (LIGHT_DISMISS off, e.g. tooltip) declares NO catcher even while open. ---- */
static void test_popup_no_dismiss_declares_no_catcher(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    st.flags = 0U; /* no light-dismiss */
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, true);
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(nt_ui_popup_test_last_catcher_present());
}

static void test_popup_clear_state_releases_retained_tween_cell(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};

    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_state_used_slots(s_fx.ctx));
    popup_frame(POP_A, &st, &anc, true, &(nt_ui_popup_result_t){0});
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_state_used_slots(s_fx.ctx));

    nt_ui_popup_clear_state(s_fx.ctx, POP_A);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_state_used_slots(s_fx.ctx));
}

/* ---- Open/close tween clamp: t in [0,1], rises while open, decays after close; visible == (t>eps). ---- */
static void popup_frame(uint32_t id, const nt_ui_popup_style_t *st, const nt_ui_popup_anchor_t *anc, bool open, nt_ui_popup_result_t *out) {
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    *out = nt_ui_popup_begin(s_fx.ctx, id, st, anc, open);
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_popup_tween_clamp(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 8.0F;
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_ui_popup_result_t r;

    float prev = 0.0F;
    for (int i = 0; i < 120; ++i) {
        popup_frame(POP_A, &st, &anc, true, &r);
        TEST_ASSERT_TRUE(r.t >= -0.0001F && r.t <= 1.0001F);
        TEST_ASSERT_TRUE(r.t >= prev - 0.0001F);
        prev = r.t;
        TEST_ASSERT_TRUE(r.visible);
    }
    TEST_ASSERT_TRUE(float_near(r.t, 1.0F, 0.02F));

    bool saw_closed = false;
    for (int i = 0; i < 300; ++i) {
        popup_frame(POP_A, &st, &anc, false, &r);
        TEST_ASSERT_TRUE(r.t >= -0.0001F && r.t <= 1.0001F);
        if (r.fully_closed) {
            TEST_ASSERT_FALSE(r.visible);
            saw_closed = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(saw_closed);
}

/* ---- The overlay that eats Esc / runs the close-scan is the one PAINTED on top, not the one deepest in
 *      the begin/end counter. A game floating with its own zIndex shifts the band of the popup declared
 *      inside it, so the two answers can disagree; the arbitration key must be the band Clay sorts by. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_popup_top_id_follows_paint_order(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 80.0F, .h = 30.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_pointer_t p = {.active = true};
    /* Two frames: the close-scan target is committed at the next nt_ui_begin (1-frame IM lag). */
    for (int frame = 0; frame < 2; ++frame) {
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
        CLAY({.id = CLAY_ID("game_hud"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 500}}) {
            nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, true);
            CLAY({.id = CLAY_ID("body_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
            nt_ui_popup_end(s_fx.ctx);
        }
        /* Same nesting depth as A, but no enclosing band -> lands lower even though it is declared later. */
        nt_ui_popup_begin(s_fx.ctx, POP_B, &st, &anc, true);
        CLAY({.id = CLAY_ID("body_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
        nt_ui_popup_end(s_fx.ctx);
        nt_ui_end(s_fx.ctx);
    }

    int32_t z_a = INT32_MIN;
    int32_t z_b = INT32_MIN;
    int32_t at_a = -1;
    int32_t at_b = -1;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType != CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
            continue;
        }
        if (c->id == CLAY_ID("body_a").id) {
            z_a = c->zIndex;
            at_a = i;
        } else if (c->id == CLAY_ID("body_b").id) {
            z_b = c->zIndex;
            at_b = i;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(at_a >= 0 && at_b >= 0, "both popups must reach the render commands");
    TEST_ASSERT_TRUE_MESSAGE(z_a > z_b, "the popup inside the lifted game floating must land in a higher band");
    TEST_ASSERT_TRUE_MESSAGE(at_a > at_b, "and must therefore paint after the root-level popup");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(POP_A, s_fx.ctx->modal_top_id_prev, "the painted-on-top popup must own Esc and the close-scan");

    /* And the consequence, not just the gate: an outside click must dismiss the painted-on-top popup and
     * leave the one under it alone (a non-top catcher is an inert pointer gate). */
    nt_ui_popup_result_t ra = {0};
    nt_ui_popup_result_t rb = {0};
    for (int frame = 0; frame < 2; ++frame) {
        const bool pressing = (frame == 0);
        nt_pointer_t click = pointer_at(700.0F, 500.0F, pressing, pressing, !pressing);
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &click, 1);
        CLAY({.id = CLAY_ID("game_hud"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 500}}) {
            ra = nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, true);
            nt_ui_popup_end(s_fx.ctx);
        }
        rb = nt_ui_popup_begin(s_fx.ctx, POP_B, &st, &anc, true);
        nt_ui_popup_end(s_fx.ctx);
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_TRUE_MESSAGE(ra.close_requested, "the outside click must dismiss the popup painted on top");
    TEST_ASSERT_FALSE_MESSAGE(rb.close_requested, "the popup underneath must stay an inert gate");

    /* The band claim resets each frame: with the lifted wrapper gone, the root-level popup takes top. */
    for (int frame = 0; frame < 2; ++frame) {
        nt_pointer_t idle = pointer_at(700.0F, 500.0F, false, false, false);
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &idle, 1);
        nt_ui_popup_begin(s_fx.ctx, POP_B, &st, &anc, true);
        nt_ui_popup_end(s_fx.ctx);
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(POP_B, s_fx.ctx->modal_top_id_prev, "the top-band claim must reset per frame, not stick to the highest band ever seen");
}

/* ---- Two catcher-bearing popups on ONE band: whoever owns the close-scan slot must also be the one
 *      the pointer hands the click to. The slot is picked by band with ties to the last declared, which
 *      is the pointer arbiter's own key — ranking the slot by draw layer instead left the scan with a
 *      popup whose catcher never won the click, and NEITHER overlay dismissed. ---- */
static void test_popup_dismiss_survives_a_band_tie(void) {
    nt_ui_popup_style_t hi = nt_ui_popup_style_defaults();
    hi.ease_speed = 0.0F;
    hi.layer = 2U; /* a draw layer that does NOT move the close-scan slot */
    nt_ui_popup_style_t lo = nt_ui_popup_style_defaults();
    lo.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 80.0F, .h = 30.0F, .prefer_side = NT_UI_POPUP_BELOW};

    nt_ui_popup_result_t ra = {0};
    nt_ui_popup_result_t rb = {0};
    for (int frame = 0; frame < 3; ++frame) {
        const bool pressing = (frame == 1);
        const bool releasing = (frame == 2);
        nt_pointer_t p = pointer_at(700.0F, 500.0F, pressing, pressing, releasing);
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
        ra = nt_ui_popup_begin(s_fx.ctx, POP_A, &hi, &anc, true);
        nt_ui_popup_end(s_fx.ctx);
        rb = nt_ui_popup_begin(s_fx.ctx, POP_B, &lo, &anc, true); /* same band, declared last -> owns the slot */
        nt_ui_popup_end(s_fx.ctx);
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_TRUE_MESSAGE(rb.close_requested, "the popup holding the close-scan slot must receive the outside click");
    TEST_ASSERT_FALSE_MESSAGE(ra.close_requested, "the other popup stays an inert gate");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_popup_abi_sizes);
    RUN_TEST(test_popup_defaults_valid);
    RUN_TEST(test_popup_floating_smoke);
    RUN_TEST(test_popup_zband_and_nesting);
    RUN_TEST(test_popup_top_id_follows_paint_order);
    RUN_TEST(test_popup_dismiss_survives_a_band_tie);
    RUN_TEST(test_popup_depth_overflow_asserts);
    RUN_TEST(test_popup_present_only_catcher);
    RUN_TEST(test_popup_closed_does_not_block_base);
    RUN_TEST(test_popup_edge_flip_four_borders);
    RUN_TEST(test_popup_first_frame_uses_prefer_side);
    RUN_TEST(test_popup_center_no_flip);
    RUN_TEST(test_popup_outside_click_dismiss);
    RUN_TEST(test_popup_wrapper_clears_open);
    RUN_TEST(test_popup_no_dismiss_declares_no_catcher);
    RUN_TEST(test_popup_clear_state_releases_retained_tween_cell);
    RUN_TEST(test_popup_tween_clamp);
    return UNITY_END();
}
