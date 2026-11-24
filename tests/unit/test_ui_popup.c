/* Popup-core tests: bands, the top overlay and its input ownership, the inert closing popup, the own close
 * and trigger-anchor edge-flip near all 4 borders (asymmetric data per AGENTS.md). Driven through the walker
 * fixture (no GL surface). UNITY_EXCLUDE_FLOAT: compare floats via an eps helper. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_input_test_frame.h"
#include "test_helpers/ui_fx_frame.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_modal.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_state.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define POP_A 0x50A001U
#define POP_B 0x50A002U

void setUp(void) {
    nt_test_assert_install();
    nt_input_init();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* What one popup frame reported: the info and whether begin showed the popup. */
typedef struct {
    nt_ui_popup_info_t info;
    bool visible;
} popup_frame_t;

static void popup_frame(uint32_t id, const nt_ui_popup_style_t *st, const nt_ui_popup_anchor_t *anc, bool open, popup_frame_t *out);

/* Begins a popup the test holds open: opened per call, so a raised close never sticks across frames. An
 * open popup always shows, so the caller declares the body and ends it unconditionally. */
static nt_ui_popup_info_t popup_begin_open(uint32_t id, const nt_ui_popup_style_t *st, const nt_ui_popup_anchor_t *anc) {
    nt_ui_open(s_fx.ctx, id);
    nt_ui_popup_info_t info = {0};
    TEST_ASSERT_TRUE(nt_ui_popup_begin(s_fx.ctx, id, st, anc, &info));
    return info;
}

/* popup_begin_open keeping the close it raises across frames. The popup must still show in the frame it
 * closes itself (an eased style), as the caller ends it. */
static void popup_begin_open_src(uint32_t id, const nt_ui_popup_style_t *st, const nt_ui_popup_anchor_t *anc, nt_ui_popup_close_t *src) {
    const nt_ui_popup_info_t info = popup_begin_open(id, st, anc);
    if (info.reason != NT_UI_POPUP_CLOSE_NONE) {
        *src = (nt_ui_popup_close_t)info.reason;
    }
}

/* ---- Defaults are a valid (non-zero-init) style: identity transforms dodge the anim scale>0 trap. ---- */
static void test_popup_defaults_valid(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    TEST_ASSERT_TRUE(st.open_xf_start.scale_x > 0.0F);
    TEST_ASSERT_TRUE(st.open_xf_start.scale_y > 0.0F);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_KIND_POPUP, st.kind);
}

/* ---- z-band: each overlay lands on its stack slot's absolute band, overlay_zband_stride + slot *
 *      NT_UI_OVERLAY_MAX, nested or not, asserted on the bands Clay stamped on the bodies' rectangles. ---- */
static void test_popup_zband_and_nesting(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 80.0F, .h = 30.0F, .prefer_side = NT_UI_POPUP_BELOW};
    ui_fx_frame_begin(s_fx.ctx, NULL);
    (void)popup_begin_open(POP_A, &st, &anc);
    /* Opaque bodies so each panel subtree emits a RECTANGLE, which Clay stamps with its root's band. */
    CLAY({.id = CLAY_ID("body_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}){}(void)popup_begin_open(POP_B, &st, &anc);
    CLAY({.id = CLAY_ID("body_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);

    const Clay_RenderCommand *a = ui_fx_find_rect(s_fx.ctx, CLAY_ID("body_a").id);
    const Clay_RenderCommand *b = ui_fx_find_rect(s_fx.ctx, CLAY_ID("body_b").id);
    TEST_ASSERT_TRUE(a != NULL && b != NULL);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(s_fx.ctx->overlay_zband_stride, a->zIndex, "stack slot 0 lands on the stride band");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(s_fx.ctx->overlay_zband_stride + NT_UI_OVERLAY_MAX, b->zIndex, "the nested popup lands on slot 1's band");
    TEST_ASSERT_TRUE_MESSAGE(b > a, "and paints above it");
}

/* ---- Opening one overlay past NT_UI_OVERLAY_MAX open ones fires NT_ASSERT (no silent fallback); every
 *      nested overlay holds a stack slot. ---- */
static void test_popup_live_cap_asserts(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = 0.0F, .y = 0.0F, .w = 10.0F, .h = 10.0F, .prefer_side = NT_UI_POPUP_BELOW};
    ui_fx_frame_begin(s_fx.ctx, NULL);
    for (uint32_t i = 0; i < NT_UI_OVERLAY_MAX; ++i) {
        (void)popup_begin_open(0x50B000U + i, &st, &anc);
    }
    NT_TEST_EXPECT_ASSERT(nt_ui_open(s_fx.ctx, 0x50BFFFU));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "more than NT_UI_OVERLAY_MAX open overlays"));
    for (uint32_t i = 0; i < NT_UI_OVERLAY_MAX; ++i) {
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- A read inside an inert subtree still checks its arguments: the misuse asserts run before the inert
 *      early return. ---- */
static void test_popup_inert_query_keeps_its_asserts(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 4.0F;
    nt_ui_popup_anchor_t anc = {.x = 0.0F, .y = 0.0F, .w = 10.0F, .h = 10.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_ui_open(s_fx.ctx, 0x50C200U);
    for (int f = 0; f < 2; ++f) {
        ui_fx_frame_begin(s_fx.ctx, NULL);
        if (f == 1) {
            nt_ui_close(s_fx.ctx, 0x50C200U); /* the body of a popup easing out is inert */
        }
        TEST_ASSERT_TRUE(nt_ui_popup_begin(s_fx.ctx, 0x50C200U, &st, &anc, NULL));
        if (f == 1) {
            NT_TEST_EXPECT_ASSERT((void)nt_ui_query_interaction(s_fx.ctx, 0U));
            TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "id must be non-zero"));
        }
        nt_ui_popup_end(s_fx.ctx);
        nt_ui_end(s_fx.ctx);
    }
}

/* ---- A fully-closed popup called every frame must NOT block a base widget under the same point. ---- */
static nt_ui_interaction_t closed_popup_base_frame(const nt_ui_popup_style_t *st, const nt_ui_popup_anchor_t *anc, const nt_pointer_t *p) {
    ui_fx_frame_begin(s_fx.ctx, p);
    nt_ui_interaction_t in;
    CLAY({.id = (Clay_ElementId){.id = nt_ui_id("closed_base")}, .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(120)}}}) { in = nt_ui_step(s_fx.ctx, nt_ui_id("closed_base"), NULL); }
    const bool declared = nt_ui_popup_begin(s_fx.ctx, POP_A, st, anc, NULL);
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

    nt_pointer_t f1 = ui_fx_pointer(100.0F, 60.0F, false, false, false);
    closed_popup_base_frame(&st, &anc, &f1);
    nt_pointer_t f2 = ui_fx_pointer(100.0F, 60.0F, true, true, false);
    nt_ui_interaction_t in2 = closed_popup_base_frame(&st, &anc, &f2);
    TEST_ASSERT_TRUE(in2.pressed_now);
    nt_pointer_t f3 = ui_fx_pointer(100.0F, 60.0F, false, false, true);
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
        ui_fx_frame_begin(s_fx.ctx, NULL);
        side = popup_begin_open(id, &st, &anc).side;
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
    ui_fx_frame_begin(s_fx.ctx, NULL);
    const nt_ui_popup_info_t r = popup_begin_open(0x50D001U, &st, &anc);
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
    ui_fx_frame_begin(s_fx.ctx, NULL);
    const nt_ui_popup_info_t r = popup_begin_open(0x50E001U, &st, &anc);
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_CENTER, r.side);
}

/* ---- An outside press closes the popup at its begin, which keeps declaring while it fades and returns false
 *      once fully closed. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_popup_outside_press_closes_then_fades(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 8.0F; /* finite ease so the close tween plays out */
    nt_ui_popup_anchor_t anc = {.x = 20.0F, .y = 20.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};
    nt_ui_open(s_fx.ctx, POP_A);

    /* Frame 1: open -> declared, register body + catcher. */
    nt_pointer_t f1 = ui_fx_pointer(700.0F, 500.0F, false, false, false);
    ui_fx_frame_begin(s_fx.ctx, &f1);
    bool declared = nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, NULL);
    TEST_ASSERT_TRUE(declared);
    if (declared) {
        CLAY({.id = (Clay_ElementId){.id = POP_A ^ 0xB0D70U}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, POP_A));

    /* Frame 2: a press on the catcher far from the panel closes it, as Dear ImGui closes a popup on the
     * press, and begin STILL returns true (animating closed). */
    nt_pointer_t f2 = ui_fx_pointer(700.0F, 500.0F, false, false, true);
    f2.buttons[NT_BUTTON_LEFT].is_pressed = true;
    ui_fx_frame_begin(s_fx.ctx, &f2);
    nt_ui_popup_info_t info = {0};
    declared = nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, &info);
    TEST_ASSERT_TRUE(declared);
    TEST_ASSERT_FALSE(nt_ui_is_open(s_fx.ctx, POP_A));
    TEST_ASSERT_EQUAL_INT(NT_UI_POPUP_CLOSE_OUTSIDE, info.reason);
    if (declared) {
        CLAY({.id = (Clay_ElementId){.id = POP_A ^ 0xB0D70U}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Subsequent frames: t decays; eventually the wrapper returns false (fully closed). */
    bool went_false = false;
    for (int i = 0; i < 300; ++i) {
        ui_fx_frame_begin(s_fx.ctx, NULL);
        declared = nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, NULL);
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

/* ---- An open popup switched from snap to eased replays no entrance: it was shown last frame. ---- */
static void test_popup_snap_to_eased_stays_open(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};
    popup_frame_t r = {0};
    popup_frame(POP_A, &st, &anc, true, &r);
    popup_frame(POP_A, &st, &anc, true, &r);
    st.ease_speed = 8.0F;
    popup_frame(POP_A, &st, &anc, true, &r);
    TEST_ASSERT_TRUE_MESSAGE(r.info.t > 0.99F, "the open popup stays fully open");
}

/* ---- A closed popup a snap frame hid stays hidden when it eases again, and an open one a snap frame showed
 *      stays fully open: an eased step after a snap one starts from what the snap frame showed. ---- */
static void test_popup_snap_then_eased_keeps_what_it_showed(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};
    popup_frame_t r = {0};
    for (int f = 0; f < 90; ++f) {
        st.ease_speed = 8.0F;
        popup_frame(POP_A, &st, &anc, true, &r);
    }
    st.ease_speed = 0.0F;
    popup_frame(POP_A, &st, &anc, false, &r);
    TEST_ASSERT_FALSE(r.visible);
    st.ease_speed = 8.0F;
    popup_frame(POP_A, &st, &anc, false, &r);
    TEST_ASSERT_FALSE_MESSAGE(r.visible, "a popup a snap close hid does not show again");
    for (uint32_t k = 0; k < NT_UI_ANIM_SLOTS; ++k) {
        const nt_ui_anim_interaction_t *slot = &s_fx.ctx->anim[k];
        TEST_ASSERT_FALSE_MESSAGE(slot->id == POP_A && s_fx.ctx->current_generation - slot->last_touch <= 1U, "a hidden popup touches no anim slot");
    }

    popup_frame(POP_B, &st, &anc, true, &r);
    popup_frame(POP_B, &st, &anc, true, &r);
    st.ease_speed = 0.0F;
    popup_frame(POP_B, &st, &anc, true, &r);
    popup_frame(POP_B, &st, &anc, true, &r);
    st.ease_speed = 8.0F;
    popup_frame(POP_B, &st, &anc, true, &r);
    TEST_ASSERT_TRUE_MESSAGE(r.info.t > 0.99F, "an open popup a snap frame showed stays fully open");
}

/* ---- A hidden popup at the nesting cap declares nothing, so it does not assert. ---- */
static void test_popup_hidden_at_the_depth_cap_does_not_assert(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_anchor_t anc = {.x = 0.0F, .y = 0.0F, .w = 10.0F, .h = 10.0F, .prefer_side = NT_UI_POPUP_BELOW};
    ui_fx_frame_begin(s_fx.ctx, NULL);
    for (uint32_t i = 0; i < NT_UI_OVERLAY_MAX; ++i) {
        (void)popup_begin_open(0x50B000U + i, &st, &anc);
    }
    TEST_ASSERT_FALSE(nt_ui_popup_begin(s_fx.ctx, 0x50BFFFU, &st, &anc, NULL));
    for (uint32_t i = 0; i < NT_UI_OVERLAY_MAX; ++i) {
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- A popup keeps no retained state, snap or eased: opened and closed, it leaves no state cell. ---- */
static void test_popup_keeps_no_state(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};
    const float speeds[] = {0.0F, 8.0F};
    for (int i = 0; i < 2; ++i) {
        st.ease_speed = speeds[i];
        popup_frame(POP_A + (uint32_t)i, &st, &anc, true, &(popup_frame_t){0});
        popup_frame(POP_A + (uint32_t)i, &st, &anc, false, &(popup_frame_t){0});
        TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_state_used_slots(s_fx.ctx));
    }
}

/* ---- Open/close tween clamp: t in [0,1], rises while open, decays after close; visible == (t>eps). ---- */
static void popup_frame(uint32_t id, const nt_ui_popup_style_t *st, const nt_ui_popup_anchor_t *anc, bool open, popup_frame_t *out) {
    ui_fx_frame_begin(s_fx.ctx, NULL);
    if (open) {
        nt_ui_open(s_fx.ctx, id);
    } else {
        nt_ui_close(s_fx.ctx, id);
    }
    out->visible = nt_ui_popup_begin(s_fx.ctx, id, st, anc, &out->info);
    if (out->visible) {
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_popup_tween_clamp(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 8.0F;
    nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 40.0F, .h = 24.0F, .prefer_side = NT_UI_POPUP_BELOW};
    popup_frame_t r;

    float prev = 0.0F;
    for (int i = 0; i < 120; ++i) {
        popup_frame(POP_A, &st, &anc, true, &r);
        TEST_ASSERT_TRUE(r.info.t >= -0.0001F && r.info.t <= 1.0001F);
        TEST_ASSERT_TRUE(r.info.t >= prev - 0.0001F);
        prev = r.info.t;
        TEST_ASSERT_TRUE(r.visible);
    }
    TEST_ASSERT_TRUE(float_near(r.info.t, 1.0F, 0.02F));

    bool saw_closed = false;
    for (int i = 0; i < 300; ++i) {
        popup_frame(POP_A, &st, &anc, false, &r);
        TEST_ASSERT_TRUE(r.info.t >= -0.0001F && r.info.t <= 1.0001F);
        if (!r.visible) {
            saw_closed = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(saw_closed);
}

#define POP_C 0x50A006U

/* One frame: modal C holding a game floating at zIndex 500 that holds modal A (a modal, so B's opening leaves it
 * open), then, while `declare_b`, popup B at the root, opened after A; keeps the closes A and B raise. */
static void lifted_frame(nt_pointer_t p, bool declare_b, nt_ui_popup_close_t *src_a, nt_ui_popup_close_t *src_b) {
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ms.ease_speed = 0.0F;
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 8.0F; /* eased: the frame it closes itself still shows, so the test ends it */
    const nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .w = 80.0F, .h = 30.0F, .prefer_side = NT_UI_POPUP_BELOW};
    ui_fx_frame_begin(s_fx.ctx, &p);
    nt_ui_open(s_fx.ctx, POP_C);
    TEST_ASSERT_TRUE(nt_ui_modal_begin(s_fx.ctx, POP_C, &ms, NULL));
    CLAY({.id = CLAY_ID("game_hud"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 500},
          .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(60)}},
          .backgroundColor = {8, 8, 8, 255}}) {
        nt_ui_open(s_fx.ctx, POP_A);
        nt_ui_popup_info_t info_a = {0};
        TEST_ASSERT_TRUE(nt_ui_modal_begin(s_fx.ctx, POP_A, &ms, &info_a));
        CLAY({.id = CLAY_ID("body_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
        nt_ui_modal_end(s_fx.ctx);
        if (info_a.reason != NT_UI_POPUP_CLOSE_NONE) {
            *src_a = (nt_ui_popup_close_t)info_a.reason;
        }
    }
    nt_ui_modal_end(s_fx.ctx);
    if (declare_b) {
        popup_begin_open_src(POP_B, &st, &anc, src_b);
        CLAY({.id = CLAY_ID("body_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- An overlay's band ignores the floating that encloses it: a game floating at zIndex 500 inside modal C
 *      rises over the later root popup B, yet modal A declared in it lies under B, and B owns the outside
 *      press. With B gone, A is the top again. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_popup_band_ignores_the_enclosing_floating(void) {
    nt_ui_popup_close_t src_a = NT_UI_POPUP_CLOSE_NONE;
    nt_ui_popup_close_t src_b = NT_UI_POPUP_CLOSE_NONE;
    lifted_frame(ui_fx_pointer(700.0F, 500.0F, false, false, false), true, &src_a, &src_b);
    lifted_frame(ui_fx_pointer(700.0F, 500.0F, false, false, false), true, &src_a, &src_b);
    const Clay_RenderCommand *hud = ui_fx_find_rect(s_fx.ctx, CLAY_ID("game_hud").id);
    const Clay_RenderCommand *a = ui_fx_find_rect(s_fx.ctx, CLAY_ID("body_a").id);
    const Clay_RenderCommand *b = ui_fx_find_rect(s_fx.ctx, CLAY_ID("body_b").id);
    TEST_ASSERT_TRUE_MESSAGE(hud != NULL && a != NULL && b != NULL, "the floating and both popups must reach the render commands");
    TEST_ASSERT_TRUE_MESSAGE(hud->zIndex > b->zIndex, "the game floating rises over the later overlay");
    TEST_ASSERT_TRUE_MESSAGE(a->zIndex < b->zIndex && a < b, "the modal inside it stays under the later overlay");
    TEST_ASSERT_EQUAL_UINT32(POP_B, s_fx.ctx->overlay_top);

    lifted_frame(ui_fx_pointer(700.0F, 500.0F, true, true, false), true, &src_a, &src_b);
    lifted_frame(ui_fx_pointer(700.0F, 500.0F, false, false, true), true, &src_a, &src_b);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_UI_POPUP_CLOSE_OUTSIDE, src_b, "the outside press dismisses the later overlay");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_UI_POPUP_CLOSE_NONE, src_a, "the modal under it stays open");

    lifted_frame(ui_fx_pointer(700.0F, 500.0F, false, false, false), false, &src_a, &src_b);
    lifted_frame(ui_fx_pointer(700.0F, 500.0F, false, false, false), false, &src_a, &src_b);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(POP_A, s_fx.ctx->overlay_top, "with the later overlay gone the nested modal is the top");
}

#define ORDER_X_BODY 0x50A0E1U
#define ORDER_Y_BODY 0x50A0E2U

/* One frame: modal X declared open while `x_open`, then modal Y while `y_open` (else not declared at all), each
 * with an opaque body; returns ctx->overlay_count right after nt_ui_begin. */
static uint8_t two_modals_frame(bool x_open, bool y_open) {
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ms.ease_speed = 0.0F;
    const uint32_t ids[2] = {POP_A, POP_B};
    const uint32_t bodies[2] = {ORDER_X_BODY, ORDER_Y_BODY};
    const bool shown[2] = {x_open, y_open};
    ui_fx_frame_begin(s_fx.ctx, NULL);
    const uint8_t count = s_fx.ctx->overlay_count;
    for (int i = 0; i < 2; ++i) {
        if (shown[i]) {
            nt_ui_open(s_fx.ctx, ids[i]);
        }
        if (shown[i] && nt_ui_modal_begin(s_fx.ctx, ids[i], &ms, NULL)) {
            CLAY({.id = (Clay_ElementId){.id = bodies[i]}, .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
            nt_ui_modal_end(s_fx.ctx);
        }
    }
    nt_ui_end(s_fx.ctx);
    return count;
}

/* True when X's body paints above Y's in the last frame, by band and by command order. */
static bool x_paints_above_y(void) {
    const Clay_RenderCommand *x = ui_fx_find_rect(s_fx.ctx, ORDER_X_BODY);
    const Clay_RenderCommand *y = ui_fx_find_rect(s_fx.ctx, ORDER_Y_BODY);
    TEST_ASSERT_TRUE_MESSAGE(x != NULL && y != NULL, "both modals must reach the render commands");
    return x->zIndex > y->zIndex && x > y;
}

/* ---- Open order beats declaration order: modal X, declared before Y every frame but opened a frame after
 *      it, is the top and paints above Y. ---- */
static void test_popup_open_order_beats_declaration_order(void) {
    (void)two_modals_frame(false, true);
    (void)two_modals_frame(true, true);
    (void)two_modals_frame(true, true);
    TEST_ASSERT_TRUE_MESSAGE(x_paints_above_y(), "the overlay opened last paints above the one declared after it");
}

/* ---- An overlay the game stops declaring leaves the stack at the next nt_ui_begin; declared open again, it
 *      enters on top of the overlay opened after it the first time. ---- */
static void test_popup_undeclared_overlay_reenters_on_top(void) {
    (void)two_modals_frame(true, false);
    (void)two_modals_frame(true, true);
    (void)two_modals_frame(false, true);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1U, two_modals_frame(true, true), "the undeclared X left the stack at this begin");
    (void)two_modals_frame(true, true);
    TEST_ASSERT_TRUE_MESSAGE(x_paints_above_y(), "X declared again enters on top");
}

/* What one frame of combo_modal_frame reported for each overlay. */
typedef struct {
    nt_ui_popup_info_t combo;
    nt_ui_popup_info_t modal;
} combo_modal_info_t;

/* One frame at the base UI: popup A (a combo list), then modal B, each declared whatever its open state. */
static combo_modal_info_t combo_modal_frame(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ms.ease_speed = 0.0F;
    const nt_ui_popup_anchor_t anc_a = {.x = 100.0F, .y = 100.0F, .prefer_side = NT_UI_POPUP_BELOW};
    combo_modal_info_t out = {0};
    ui_fx_frame_begin(s_fx.ctx, NULL);
    if (nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc_a, &out.combo)) {
        nt_ui_popup_end(s_fx.ctx);
    }
    if (nt_ui_modal_begin(s_fx.ctx, POP_B, &ms, &out.modal)) {
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    return out;
}

/* ---- A modal opening next to an open combo closes it at the modal's first declaration: the combo, declared
 *      first, reports REPLACED at its next begin. ---- */
static void test_popup_modal_closes_an_open_combo(void) {
    nt_ui_open(s_fx.ctx, POP_A);
    (void)combo_modal_frame();
    (void)combo_modal_frame();
    nt_ui_open(s_fx.ctx, POP_B);
    combo_modal_info_t r = combo_modal_frame();
    TEST_ASSERT_EQUAL_INT(NT_UI_POPUP_CLOSE_NONE, r.combo.reason);
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_is_open(s_fx.ctx, POP_A), "the modal's declaration closed the combo");
    r = combo_modal_frame();
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_UI_POPUP_CLOSE_REPLACED, r.combo.reason, "the combo reports the close at its next begin");
    TEST_ASSERT_TRUE(nt_ui_is_open(s_fx.ctx, POP_B));
}

#define POP_A_BOX 0x50A0B1U

/* One frame: popup A at the root holding a stepped box and, after it, popup B (B on top, as a combo in a
 * popup); returns whether A's box clicked and B's close source. */
static bool popup_under_frame(nt_pointer_t p, nt_ui_popup_close_t *src_b) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 8.0F; /* eased: the frame it closes itself still shows, so the test ends it */
    const nt_ui_popup_anchor_t anc_a = {.x = 100.0F, .y = 100.0F, .prefer_side = NT_UI_POPUP_BELOW};
    const nt_ui_popup_anchor_t anc_b = {.x = 500.0F, .y = 350.0F, .prefer_side = NT_UI_POPUP_BELOW};
    ui_fx_frame_begin(s_fx.ctx, &p);
    (void)popup_begin_open(POP_A, &st, &anc_a);
    CLAY({.id = (Clay_ElementId){.id = POP_A_BOX}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
    const bool clicked = nt_ui_step(s_fx.ctx, POP_A_BOX, NULL).clicked;
    popup_begin_open_src(POP_B, &st, &anc_b, src_b);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    return clicked;
}

/* ---- The top overlay owns the pointer: a full press and release on the lower popup's control outside the
 *      top popup clicks nothing there; the press is the top popup's outside click. ---- */
static void test_popup_top_owns_the_pointer(void) {
    nt_ui_popup_close_t src_b = NT_UI_POPUP_CLOSE_NONE;
    (void)popup_under_frame(ui_fx_pointer(10.0F, 10.0F, false, false, false), &src_b);
    (void)popup_under_frame(ui_fx_pointer(10.0F, 10.0F, false, false, false), &src_b);
    const nt_ui_bbox_t box = nt_ui_get_bbox(s_fx.ctx, POP_A_BOX);
    TEST_ASSERT_TRUE(box.found);
    const float x = box.x + 10.0F;
    const float y = box.y + 10.0F;
    bool clicked = popup_under_frame(ui_fx_pointer(x, y, true, true, false), &src_b);
    clicked = popup_under_frame(ui_fx_pointer(x, y, false, false, true), &src_b) || clicked;
    TEST_ASSERT_FALSE_MESSAGE(clicked, "the lower popup's control under the top overlay's catcher clicks nothing");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_UI_POPUP_CLOSE_OUTSIDE, src_b, "the press is the top popup's outside click");
}

#define POP_A2 0x50A003U
#define POP_A2_BOX 0x50A0B2U
#define POP_B_BOX 0x50A0B3U

/* One frame: modal A holding a catcher-less nested popup A2 (a tooltip) with a stepped box, then popup B at
 * the root holding a stepped box at the same place; ORs both boxes' clicks into the outputs. */
static void nested_under_frame(nt_pointer_t p, bool *a2_clicked, bool *b_clicked) {
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ms.ease_speed = 0.0F;
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_style_t level = st;
    level.kind = NT_UI_POPUP_KIND_TOOLTIP;
    const nt_ui_popup_anchor_t anc_box = {.x = 400.0F, .y = 300.0F, .prefer_side = NT_UI_POPUP_BELOW};
    ui_fx_frame_begin(s_fx.ctx, &p);
    nt_ui_open(s_fx.ctx, POP_A);
    TEST_ASSERT_TRUE(nt_ui_modal_begin(s_fx.ctx, POP_A, &ms, NULL));
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}){}(void)popup_begin_open(POP_A2, &level, &anc_box);
    CLAY({.id = (Clay_ElementId){.id = POP_A2_BOX}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}, .backgroundColor = {8, 8, 8, 255}}){} *a2_clicked =
        nt_ui_step(s_fx.ctx, POP_A2_BOX, NULL).clicked || *a2_clicked;
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_modal_end(s_fx.ctx);
    (void)popup_begin_open(POP_B, &st, &anc_box);
    CLAY({.id = (Clay_ElementId){.id = POP_B_BOX}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}, .backgroundColor = {8, 8, 8, 255}}){} *b_clicked =
        nt_ui_step(s_fx.ctx, POP_B_BOX, NULL).clicked || *b_clicked;
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* ---- An overlay opened later stacks above an earlier overlay's nested level: B paints over the tooltip
 *      nested in modal A and its own box takes the click where the two overlap. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_popup_later_overlay_above_nested_level(void) {
    bool a2 = false;
    bool b = false;
    nested_under_frame(ui_fx_pointer(10.0F, 10.0F, false, false, false), &a2, &b);
    nested_under_frame(ui_fx_pointer(10.0F, 10.0F, false, false, false), &a2, &b);
    const Clay_RenderCommand *a2_rect = ui_fx_find_rect(s_fx.ctx, POP_A2_BOX);
    TEST_ASSERT_TRUE_MESSAGE(a2_rect != NULL && ui_fx_find_rect(s_fx.ctx, POP_B_BOX) > a2_rect, "the later overlay paints over the earlier one's nested level");
    const nt_ui_bbox_t box = nt_ui_get_bbox(s_fx.ctx, POP_B_BOX);
    TEST_ASSERT_TRUE(box.found);
    nested_under_frame(ui_fx_pointer(box.x + 10.0F, box.y + 10.0F, true, true, false), &a2, &b);
    nested_under_frame(ui_fx_pointer(box.x + 10.0F, box.y + 10.0F, false, false, true), &a2, &b);
    TEST_ASSERT_TRUE_MESSAGE(b, "the top overlay's own box takes the click");
    TEST_ASSERT_FALSE_MESSAGE(a2, "the nested level under it clicks nothing");
}

#define POP_B2 0x50A004U
#define POP_B2_BOX 0x50A0B4U
#define PART_ID 0x50A0B5U

/* One frame: popup A holding nested popup B2 (a box) and, declared after B2 inside A, a delta-0 floating
 * part over the same place, as a slider thumb or a scrollbar; ORs both clicks into the outputs. */
static void nested_over_parts_frame(nt_pointer_t p, bool *b2_clicked, bool *part_clicked) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    const nt_ui_popup_anchor_t anc_a = {.x = 100.0F, .y = 100.0F, .prefer_side = NT_UI_POPUP_BELOW};
    const nt_ui_popup_anchor_t anc_b = {.x = 400.0F, .y = 300.0F, .prefer_side = NT_UI_POPUP_BELOW};
    ui_fx_frame_begin(s_fx.ctx, &p);
    (void)popup_begin_open(POP_A, &st, &anc_a);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}){}(void)popup_begin_open(POP_B2, &st, &anc_b);
    CLAY({.id = (Clay_ElementId){.id = POP_B2_BOX}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}, .backgroundColor = {8, 8, 8, 255}}){} *b2_clicked =
        nt_ui_step(s_fx.ctx, POP_B2_BOX, NULL).clicked || *b2_clicked;
    nt_ui_popup_end(s_fx.ctx);
    const nt_ui_bbox_t box = nt_ui_get_bbox(s_fx.ctx, POP_B2_BOX);
    CLAY({.id = (Clay_ElementId){.id = PART_ID},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = box.x, .y = box.y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}},
          .backgroundColor = {9, 9, 9, 255}}){} *part_clicked = nt_ui_step(s_fx.ctx, PART_ID, NULL).clicked || *part_clicked;
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* ---- A nested overlay covers its parent's floating parts declared after it (a combo over a later slider):
 *      it paints over them and takes the click. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_popup_nested_above_parent_parts(void) {
    bool b2 = false;
    bool part = false;
    for (int f = 0; f < 3; ++f) {
        nested_over_parts_frame(ui_fx_pointer(10.0F, 10.0F, false, false, false), &b2, &part);
    }
    const Clay_RenderCommand *part_rect = ui_fx_find_rect(s_fx.ctx, PART_ID);
    TEST_ASSERT_TRUE_MESSAGE(part_rect != NULL && ui_fx_find_rect(s_fx.ctx, POP_B2_BOX) > part_rect, "the nested overlay paints over its parent's later part");
    const nt_ui_bbox_t box = nt_ui_get_bbox(s_fx.ctx, POP_B2_BOX);
    TEST_ASSERT_TRUE(box.found);
    nested_over_parts_frame(ui_fx_pointer(box.x + 10.0F, box.y + 10.0F, true, true, false), &b2, &part);
    nested_over_parts_frame(ui_fx_pointer(box.x + 10.0F, box.y + 10.0F, false, false, true), &b2, &part);
    TEST_ASSERT_TRUE_MESSAGE(b2, "the nested overlay takes the click");
    TEST_ASSERT_FALSE(part);
}

#define TOAST 0x50A005U
#define TOAST_BTN 0x50A0B6U

/* One frame: catcher-bearing popup A, then a catcher-less popup (a toast) declared after it with a button;
 * returns whether the button clicked and reports A's close source. */
static bool toast_frame(nt_pointer_t p, nt_ui_popup_close_t *src_a) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_popup_style_t toast = st;
    toast.kind = NT_UI_POPUP_KIND_TOOLTIP;
    const nt_ui_popup_anchor_t anc_a = {.x = 100.0F, .y = 100.0F, .prefer_side = NT_UI_POPUP_BELOW};
    const nt_ui_popup_anchor_t anc_t = {.x = 500.0F, .y = 350.0F, .prefer_side = NT_UI_POPUP_BELOW};
    ui_fx_frame_begin(s_fx.ctx, &p);
    popup_begin_open_src(POP_A, &st, &anc_a, src_a);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
    nt_ui_popup_end(s_fx.ctx);
    (void)popup_begin_open(TOAST, &toast, &anc_t);
    CLAY({.id = (Clay_ElementId){.id = TOAST_BTN}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
    const bool clicked = nt_ui_step(s_fx.ctx, TOAST_BTN, NULL).clicked;
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    return clicked;
}

/* ---- Input follows stacking alone: a catcher-less popup declared after the top one lies above it, so its
 *      button keeps the press through to the click and the top popup is not dismissed. ---- */
static void test_popup_press_above_the_top_completes(void) {
    nt_ui_popup_close_t src_a = NT_UI_POPUP_CLOSE_NONE;
    (void)toast_frame(ui_fx_pointer(10.0F, 10.0F, false, false, false), &src_a);
    (void)toast_frame(ui_fx_pointer(10.0F, 10.0F, false, false, false), &src_a);
    const nt_ui_bbox_t btn = nt_ui_get_bbox(s_fx.ctx, TOAST_BTN);
    TEST_ASSERT_TRUE(btn.found);
    const float x = btn.x + 10.0F;
    const float y = btn.y + 10.0F;
    bool clicked = toast_frame(ui_fx_pointer(x, y, true, true, false), &src_a);
    clicked = toast_frame(ui_fx_pointer(x, y, true, false, false), &src_a) || clicked;
    clicked = toast_frame(ui_fx_pointer(x, y, true, false, false), &src_a) || clicked;
    clicked = toast_frame(ui_fx_pointer(x, y, false, false, true), &src_a) || clicked;
    TEST_ASSERT_TRUE_MESSAGE(clicked, "a held press above the top overlay is not dropped");
    TEST_ASSERT_EQUAL_INT(NT_UI_POPUP_CLOSE_NONE, src_a);
}

#define NESTED_NONE 0U
#define NESTED_TOP 1U          /* an open popup: the top overlay */
#define NESTED_LEVEL 2U        /* an open catcher-less overlay, as a menu's submenu level */
#define NESTED_LEVEL_CLOSED 3U /* that level, closed by the game */

#define INERT_POPUP 0x50A0C1U
#define INERT_NESTED 0x50A0C2U
#define INERT_BUTTON 0x50A0C3U
#define INERT_BASE 0x50A0C4U

typedef struct {
    bool visible;        /* the eased popup shows */
    bool open;           /* nt_ui_is_open after begin */
    bool button_clicked; /* the button inside it */
    bool button_queried; /* nt_ui_query_interaction's click on that button */
    bool button_hovered; /* nt_ui_hovered_within on that button */
    bool base_clicked;   /* the base floating under its panel */
    bool nested_open;    /* nt_ui_is_open of the nested popup after begin */
} inert_frame_t;

/* One frame: a base floating on band 0 at the panel's place, then an eased popup holding an eased `nested`
 * popup and, declared after it, a stepped button. */
static inert_frame_t inert_frame(nt_pointer_t p, bool open, uint8_t nested) {
    inert_frame_t out = {0};
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 8.0F;
    const nt_ui_popup_anchor_t anc = {.x = 300.0F, .y = 200.0F, .prefer_side = NT_UI_POPUP_BELOW};
    ui_fx_frame_begin(s_fx.ctx, &p);
    CLAY({.id = (Clay_ElementId){.id = INERT_BASE},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 300.0F, .y = 200.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
    out.base_clicked = nt_ui_step(s_fx.ctx, INERT_BASE, NULL).clicked;
    if (open) {
        nt_ui_open(s_fx.ctx, INERT_POPUP);
    } else {
        nt_ui_close(s_fx.ctx, INERT_POPUP);
    }
    out.visible = nt_ui_popup_begin(s_fx.ctx, INERT_POPUP, &st, &anc, NULL);
    out.open = nt_ui_is_open(s_fx.ctx, INERT_POPUP);
    if (out.visible) {
        nt_ui_block_pointer(s_fx.ctx, INERT_POPUP, NULL); /* the panel blocks the pointer, as a menu level does */
        if (nested != NESTED_NONE) {
            nt_ui_popup_style_t ns = nt_ui_popup_style_defaults();
            ns.ease_speed = 8.0F;
            ns.kind = (nested == NESTED_TOP) ? NT_UI_POPUP_KIND_POPUP : NT_UI_POPUP_KIND_TOOLTIP;
            const nt_ui_popup_anchor_t na = {.x = 600.0F, .y = 450.0F, .prefer_side = NT_UI_POPUP_BELOW};
            if (nested == NESTED_LEVEL_CLOSED) {
                nt_ui_close(s_fx.ctx, INERT_NESTED);
            } else {
                nt_ui_open(s_fx.ctx, INERT_NESTED);
            }
            if (nt_ui_popup_begin(s_fx.ctx, INERT_NESTED, &ns, &na, NULL)) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}}) {}
                nt_ui_popup_end(s_fx.ctx);
            }
            out.nested_open = nt_ui_is_open(s_fx.ctx, INERT_NESTED);
        }
        CLAY({.id = (Clay_ElementId){.id = INERT_BUTTON}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}}) {}
        out.button_queried = nt_ui_query_interaction(s_fx.ctx, INERT_BUTTON).clicked;
        out.button_hovered = nt_ui_hovered_within(s_fx.ctx, INERT_BUTTON);
        out.button_clicked = nt_ui_step(s_fx.ctx, INERT_BUTTON, NULL).clicked;
        nt_ui_popup_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    return out;
}

static void inert_open_fully(uint8_t nested) {
    for (int f = 0; f < 90; ++f) {
        (void)inert_frame(ui_fx_pointer(5.0F, 5.0F, false, false, false), true, nested);
    }
}

/* ---- A press held on a button when its popup starts closing ends without a click, and every read inside the
 *      closing popup is inert: nt_ui_query_interaction reads no click on the release, and no hover. ---- */
static void test_popup_closing_ends_a_held_press(void) {
    inert_open_fully(NESTED_NONE);
    const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, INERT_BUTTON);
    TEST_ASSERT_TRUE(bb.found);
    const float x = bb.x + 5.0F;
    const float y = bb.y + 5.0F;
    (void)inert_frame(ui_fx_pointer(x, y, true, true, false), true, NESTED_NONE);
    (void)inert_frame(ui_fx_pointer(x, y, true, false, false), true, NESTED_NONE);
    bool clicked = inert_frame(ui_fx_pointer(x, y, true, false, false), false, NESTED_NONE).button_clicked;
    const inert_frame_t r = inert_frame(ui_fx_pointer(x, y, false, false, true), false, NESTED_NONE);
    TEST_ASSERT_TRUE_MESSAGE(r.visible, "the popup still fades");
    TEST_ASSERT_FALSE_MESSAGE(clicked || r.button_clicked, "the release over a closing popup's button clicks nothing");
}

/* ---- A query and a hover read inside a closing popup report nothing, also for a release in the frame the
 *      game closes it, while the capture and the hot record still point at the button. ---- */
static void test_popup_closing_content_reads_no_click(void) {
    inert_open_fully(NESTED_NONE);
    const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, INERT_BUTTON);
    TEST_ASSERT_TRUE(bb.found);
    const float x = bb.x + 5.0F;
    const float y = bb.y + 5.0F;
    (void)inert_frame(ui_fx_pointer(x, y, true, true, false), true, NESTED_NONE);
    (void)inert_frame(ui_fx_pointer(x, y, true, false, false), true, NESTED_NONE);
    const inert_frame_t r = inert_frame(ui_fx_pointer(x, y, false, false, true), false, NESTED_NONE);
    TEST_ASSERT_FALSE_MESSAGE(r.button_queried, "a query in a closing popup reads no click");
    TEST_ASSERT_FALSE_MESSAGE(r.button_hovered, "nor a hover");
}

/* ---- Esc closes the top popup (a combo), and its body is inert in the frame it closes: the
 *      release of a press held on its button clicks nothing. It decides that before declaring anything, so it
 *      registers no catcher and the next press reaches the UI under it at once. ---- */
static void test_popup_esc_closes_the_top_and_its_body(void) {
    inert_open_fully(NESTED_NONE);
    const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, INERT_BUTTON);
    TEST_ASSERT_TRUE(bb.found);
    const float x = bb.x + 5.0F;
    const float y = bb.y + 5.0F;
    (void)inert_frame(ui_fx_pointer(x, y, true, true, false), true, NESTED_NONE);
    (void)inert_frame(ui_fx_pointer(x, y, true, false, false), true, NESTED_NONE);
    nt_input_feed(nt_test_key(NT_KEY_ESCAPE, true));
    const inert_frame_t r = inert_frame(ui_fx_pointer(x, y, false, false, true), true, NESTED_NONE);
    nt_input_init();
    TEST_ASSERT_FALSE_MESSAGE(r.open, "Esc closes the top popup");
    TEST_ASSERT_FALSE_MESSAGE(r.button_clicked, "its body is inert in the frame it closes");
    nt_pointer_t tap = ui_fx_pointer(300.0F + 5.0F, 200.0F + 5.0F, false, true, true);
    tap.type = NT_POINTER_TOUCH;
    TEST_ASSERT_TRUE_MESSAGE(inert_frame(tap, false, NESTED_NONE).base_clicked, "the first tap after the close reaches the base");
}

/* ---- A popup left open inside a closing one is inert too: no catcher, no top, and the Esc of the frame the
 *      game closes the parent is not its close although it was the top. ---- */
static void test_popup_nested_in_a_closing_parent_is_inert(void) {
    inert_open_fully(NESTED_TOP);
    TEST_ASSERT_EQUAL_UINT32(INERT_NESTED, s_fx.ctx->overlay_top);
    nt_input_feed(nt_test_key(NT_KEY_ESCAPE, true));
    const inert_frame_t r = inert_frame(ui_fx_pointer(5.0F, 5.0F, false, false, false), false, NESTED_TOP);
    nt_input_init();
    TEST_ASSERT_TRUE_MESSAGE(r.nested_open, "the nested popup of a closing parent does not take the Esc");
    (void)inert_frame(ui_fx_pointer(5.0F, 5.0F, false, false, false), false, NESTED_TOP);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0U, s_fx.ctx->overlay_top, "a nested popup in a closing parent claims no top");
}

/* ---- A nested popup closing with its closing parent leaves the parent's later content inert: the release
 *      of a press held on the parent's button, declared after the nested popup, clicks nothing. ---- */
static void test_popup_closing_nested_keeps_the_parent_inert(void) {
    inert_open_fully(NESTED_LEVEL);
    const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, INERT_BUTTON);
    TEST_ASSERT_TRUE(bb.found);
    const float x = bb.x + 5.0F;
    const float y = bb.y + 5.0F;
    (void)inert_frame(ui_fx_pointer(x, y, true, true, false), true, NESTED_LEVEL);
    (void)inert_frame(ui_fx_pointer(x, y, true, false, false), true, NESTED_LEVEL);
    bool clicked = inert_frame(ui_fx_pointer(x, y, true, false, false), false, NESTED_LEVEL_CLOSED).button_clicked;
    clicked = inert_frame(ui_fx_pointer(x, y, false, false, true), false, NESTED_LEVEL_CLOSED).button_clicked || clicked;
    TEST_ASSERT_FALSE(clicked);
}

/* ---- A press off every record (a letterbox bar, a 3D ray past the UI plane) is outside the top overlay too:
 *      it dismisses, and the UI keeps the gesture. ---- */
static void test_popup_press_off_every_record_dismisses(void) {
    inert_open_fully(NESTED_NONE);
    TEST_ASSERT_FALSE(inert_frame(ui_fx_pointer(-20.0F, -20.0F, true, true, false), true, NESTED_NONE).open);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx));
}

#define GAME_WINDOW 0x50A0D1U
#define TOP_A_BODY 0x50A0D2U
#define TOP_B_BODY 0x50A0D3U

/* One frame: modal A inside a game floating window at `window_z`, then popup B at the root. */
static void top_layer_frame(int16_t window_z) {
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ms.ease_speed = 0.0F;
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    const nt_ui_popup_anchor_t anc_b = {.x = 500.0F, .y = 350.0F, .prefer_side = NT_UI_POPUP_BELOW};
    const nt_pointer_t idle = ui_fx_pointer(5.0F, 5.0F, false, false, false);
    ui_fx_frame_begin(s_fx.ctx, &idle);
    CLAY({.id = (Clay_ElementId){.id = GAME_WINDOW},
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = window_z},
          .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}},
          .backgroundColor = {8, 8, 8, 255}}) {
        nt_ui_open(s_fx.ctx, POP_A);
        TEST_ASSERT_TRUE(nt_ui_modal_begin(s_fx.ctx, POP_A, &ms, NULL));
        CLAY({.id = (Clay_ElementId){.id = TOP_A_BODY}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}, .backgroundColor = {8, 8, 8, 255}}) {}
        nt_ui_modal_end(s_fx.ctx);
    }
    (void)popup_begin_open(POP_B, &st, &anc_b);
    CLAY({.id = (Clay_ElementId){.id = TOP_B_BODY}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

/* ---- Root overlays are a top layer: a modal declared inside a game window floating at zIndex 20 paints above
 *      that window, yet under a root overlay opened after it. ---- */
static void test_popup_root_overlay_is_a_top_layer(void) {
    top_layer_frame(20);
    top_layer_frame(20);
    const Clay_RenderCommand *window = ui_fx_find_rect(s_fx.ctx, GAME_WINDOW);
    const Clay_RenderCommand *a = ui_fx_find_rect(s_fx.ctx, TOP_A_BODY);
    const Clay_RenderCommand *b = ui_fx_find_rect(s_fx.ctx, TOP_B_BODY);
    TEST_ASSERT_TRUE_MESSAGE(window != NULL && a != NULL && b != NULL, "the window and both overlays must reach the render commands");
    TEST_ASSERT_TRUE_MESSAGE(a->zIndex > window->zIndex, "the modal rises out of the game window");
    TEST_ASSERT_TRUE_MESSAGE(b->zIndex > a->zIndex, "the later root overlay paints over it");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(POP_B, s_fx.ctx->overlay_top, "and takes input over it");
}

#define REOPEN_A_BTN 0x50A0F1U
#define REOPEN_B_BOX 0x50A0F2U

/* One frame: eased modal A, then snap modal B, each a centered box over the same place; ORs the boxes' clicks
 * into the outputs. */
static void reopen_frame(nt_pointer_t p, bool a_open, bool b_open, bool *a_clicked, bool *b_clicked) {
    nt_ui_modal_style_t ms = nt_ui_modal_style_defaults();
    ui_fx_frame_begin(s_fx.ctx, &p);
    const bool open[2] = {a_open, b_open};
    const uint32_t ids[2] = {POP_A, POP_B};
    const uint32_t boxes[2] = {REOPEN_A_BTN, REOPEN_B_BOX};
    bool *clicked[2] = {a_clicked, b_clicked};
    for (int i = 0; i < 2; ++i) {
        ms.ease_speed = (i == 0) ? 8.0F : 0.0F;
        if (open[i]) {
            nt_ui_open(s_fx.ctx, ids[i]);
        } else {
            nt_ui_close(s_fx.ctx, ids[i]);
        }
        if (nt_ui_modal_begin(s_fx.ctx, ids[i], &ms, NULL)) {
            CLAY({.id = (Clay_ElementId){.id = boxes[i]}, .layout = {.sizing = {CLAY_SIZING_FIXED(POP_W), CLAY_SIZING_FIXED(POP_H)}}, .backgroundColor = {8, 8, 8, 255}}){} * clicked[i] =
                nt_ui_step(s_fx.ctx, boxes[i], NULL).clicked || *clicked[i];
            nt_ui_modal_end(s_fx.ctx);
        }
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- An overlay reopened during its fade is the last opened: modal A, closed by the game under modal B and
 *      reopened before its fade ends, goes on top and takes the click where the two overlap. ---- */
static void test_popup_reopened_during_its_fade_goes_on_top(void) {
    const nt_pointer_t idle = ui_fx_pointer(5.0F, 5.0F, false, false, false);
    bool a = false;
    bool b = false;
    for (int f = 0; f < 90; ++f) {
        reopen_frame(idle, true, false, &a, &b);
    }
    reopen_frame(idle, true, true, &a, &b);
    reopen_frame(idle, false, true, &a, &b); /* A fades out under B */
    reopen_frame(idle, true, true, &a, &b);
    reopen_frame(idle, true, true, &a, &b);
    const nt_ui_bbox_t box = nt_ui_get_bbox(s_fx.ctx, REOPEN_A_BTN);
    TEST_ASSERT_TRUE(box.found);
    const float x = box.x + (box.width * 0.5F);
    const float y = box.y + (box.height * 0.5F);
    reopen_frame(ui_fx_pointer(x, y, true, true, false), true, true, &a, &b);
    reopen_frame(ui_fx_pointer(x, y, false, false, true), true, true, &a, &b);
    TEST_ASSERT_TRUE_MESSAGE(a, "the reopened modal takes the click over the one opened meanwhile");
    TEST_ASSERT_FALSE(b);
}

#define STACK_POP(i) (0x50E100U + (uint32_t)(i))
#define STACK_FADE_0 0x50E200U
#define STACK_FADE_1 0x50E201U
#define STACK_NEW 0x50E300U

/* A plain popup at the root, as a toast; returns whether begin shows it. */
static bool stack_popup(uint32_t id, float ease, bool open) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.kind = NT_UI_POPUP_KIND_TOOLTIP;
    st.ease_speed = ease;
    const nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .prefer_side = NT_UI_POPUP_BELOW};
    if (open) {
        nt_ui_open(s_fx.ctx, id);
    } else {
        nt_ui_close(s_fx.ctx, id);
    }
    const bool shown = nt_ui_popup_begin(s_fx.ctx, id, &st, &anc, NULL);
    if (shown) {
        nt_ui_popup_end(s_fx.ctx);
    }
    return shown;
}

/* Declares the open snap popups 0 .. count - 1. */
static void stack_open_popups(int count) {
    for (int i = 0; i < count; ++i) {
        TEST_ASSERT_TRUE(stack_popup(STACK_POP(i), 0.0F, true));
    }
}

/* ---- A full stack gives a fading slot away: with NT_UI_OVERLAY_MAX - 1 popups open and the rest fading, each
 *      new popup takes the lowest fading slot and shows, and a fading popup whose slot was taken is hidden from
 *      its next begin. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — one linear script over a full stack
static void test_popup_full_stack_gives_a_fading_slot_away(void) {
    for (int f = 0; f < 5; ++f) {
        ui_fx_frame_begin(s_fx.ctx, NULL);
        TEST_ASSERT_TRUE(stack_popup(STACK_FADE_0, 8.0F, true));
        TEST_ASSERT_TRUE(stack_popup(STACK_FADE_1, 8.0F, true));
        stack_open_popups(NT_UI_OVERLAY_MAX - 2);
        nt_ui_end(s_fx.ctx);
    }
    ui_fx_frame_begin(s_fx.ctx, NULL);
    TEST_ASSERT_TRUE_MESSAGE(stack_popup(STACK_FADE_0, 8.0F, false), "the game closed it: it fades");
    TEST_ASSERT_TRUE(stack_popup(STACK_FADE_1, 8.0F, false));
    stack_open_popups(NT_UI_OVERLAY_MAX - 2);
    TEST_ASSERT_TRUE_MESSAGE(stack_popup(STACK_POP(NT_UI_OVERLAY_MAX - 2), 0.0F, true), "a popup opening on a full stack shows");
    nt_ui_end(s_fx.ctx);

    ui_fx_frame_begin(s_fx.ctx, NULL);
    TEST_ASSERT_FALSE_MESSAGE(stack_popup(STACK_FADE_0, 8.0F, false), "the fading popup whose slot was taken is hidden");
    TEST_ASSERT_TRUE_MESSAGE(stack_popup(STACK_FADE_1, 8.0F, false), "the other one still fades");
    stack_open_popups(NT_UI_OVERLAY_MAX - 1);
    TEST_ASSERT_TRUE(stack_popup(STACK_NEW, 0.0F, true));
    nt_ui_end(s_fx.ctx);

    ui_fx_frame_begin(s_fx.ctx, NULL);
    TEST_ASSERT_FALSE(stack_popup(STACK_FADE_1, 8.0F, false));
    stack_open_popups(NT_UI_OVERLAY_MAX - 1);
    TEST_ASSERT_TRUE(stack_popup(STACK_NEW, 0.0F, true));
    nt_ui_end(s_fx.ctx);
}

/* ---- A root overlay inside a game floating at or above the overlay bands, or so far below them that its
 *      delta passes int16, asserts. ---- */
static void test_popup_root_overlay_out_of_the_bands_asserts(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    const nt_ui_popup_anchor_t anc = {.x = 100.0F, .y = 100.0F, .prefer_side = NT_UI_POPUP_BELOW};
    const nt_pointer_t idle = ui_fx_pointer(5.0F, 5.0F, false, false, false);
    const int16_t window_z[] = {NT_UI_OVERLAY_ZBAND_STRIDE, -32000};
    for (int i = 0; i < 2; ++i) {
        ui_fx_frame_begin(s_fx.ctx, &idle);
        CLAY(
            {.id = (Clay_ElementId){.id = GAME_WINDOW}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = window_z[i]}, .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
            nt_ui_open(s_fx.ctx, POP_A);
            NT_TEST_EXPECT_ASSERT((void)nt_ui_popup_begin(s_fx.ctx, POP_A, &st, &anc, NULL)); /* asserts before the push */
            TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "enclosing floating must lie under overlay_zband_stride"));
        }
        nt_ui_end(s_fx.ctx);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_popup_defaults_valid);
    RUN_TEST(test_popup_zband_and_nesting);
    RUN_TEST(test_popup_band_ignores_the_enclosing_floating);
    RUN_TEST(test_popup_open_order_beats_declaration_order);
    RUN_TEST(test_popup_undeclared_overlay_reenters_on_top);
    RUN_TEST(test_popup_modal_closes_an_open_combo);
    RUN_TEST(test_popup_esc_closes_the_top_and_its_body);
    RUN_TEST(test_popup_closing_ends_a_held_press);
    RUN_TEST(test_popup_closing_content_reads_no_click);
    RUN_TEST(test_popup_nested_in_a_closing_parent_is_inert);
    RUN_TEST(test_popup_closing_nested_keeps_the_parent_inert);
    RUN_TEST(test_popup_press_off_every_record_dismisses);
    RUN_TEST(test_popup_root_overlay_is_a_top_layer);
    RUN_TEST(test_popup_reopened_during_its_fade_goes_on_top);
    RUN_TEST(test_popup_full_stack_gives_a_fading_slot_away);
    RUN_TEST(test_popup_root_overlay_out_of_the_bands_asserts);
    RUN_TEST(test_popup_live_cap_asserts);
    RUN_TEST(test_popup_inert_query_keeps_its_asserts);
    RUN_TEST(test_popup_closed_does_not_block_base);
    RUN_TEST(test_popup_edge_flip_four_borders);
    RUN_TEST(test_popup_first_frame_uses_prefer_side);
    RUN_TEST(test_popup_center_no_flip);
    RUN_TEST(test_popup_outside_press_closes_then_fades);
    RUN_TEST(test_popup_snap_to_eased_stays_open);
    RUN_TEST(test_popup_snap_then_eased_keeps_what_it_showed);
    RUN_TEST(test_popup_hidden_at_the_depth_cap_does_not_assert);
    RUN_TEST(test_popup_keeps_no_state);
    RUN_TEST(test_popup_tween_clamp);
    RUN_TEST(test_popup_top_owns_the_pointer);
    RUN_TEST(test_popup_later_overlay_above_nested_level);
    RUN_TEST(test_popup_nested_above_parent_parts);
    RUN_TEST(test_popup_press_above_the_top_completes);
    return UNITY_END();
}
