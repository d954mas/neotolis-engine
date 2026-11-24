/* Modal tests: what the modal adds over popup-core (the backdrop, its band, Esc and backdrop close, the close
 * pad, the per-phase recipes, the stride) and the world gate via wants_pointer. Popup-core paths the modal
 * only delegates to live in test_ui_popup.c.
 *
 * Driven through the walker fixture + NT_TEST_ACCESS probes (no GL surface).
 * UNITY_EXCLUDE_FLOAT: compare floats via an eps helper. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "input/nt_input_feed.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/nt_input_test_frame.h"
#include "test_helpers/ui_fx_frame.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_modal.h"
#include "ui/nt_ui_popup.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

static nt_ui_popup_close_t s_frame_close_reason; /* reason from the last modal_button_frame; reset in setUp */

void setUp(void) {
    s_frame_close_reason = NT_UI_POPUP_CLOSE_NONE;
    nt_test_assert_install();
    nt_input_init();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

#define MODAL_A 0x4D0001U
#define MODAL_B 0x4D0002U

/* Begins a modal the test holds open: opened per call, so a raised close never sticks across frames. An
 * open modal always shows, so the caller declares the body and ends it unconditionally. */
static nt_ui_popup_info_t modal_begin_open(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *st) {
    nt_ui_open(ctx, id);
    nt_ui_popup_info_t info = {0};
    TEST_ASSERT_TRUE(nt_ui_modal_begin(ctx, id, st, &info));
    return info;
}

/* A 40x40 panel body, so the panel has an area to hit. */
static void modal_body(void) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40)}}}) {}
}

/* Begins a modal the test holds closed; declares the body and ends it only when it still shows (easing
 * closed). */
static void modal_begin_closed(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *st) {
    nt_ui_close(ctx, id);
    if (nt_ui_modal_begin(ctx, id, st, NULL)) {
        modal_body();
        nt_ui_modal_end(ctx);
    }
}

/* ---- Defaults are a valid (non-zero-init) style: scale_start > 0 dodges the anim trap. ---- */
static void test_modal_defaults_valid(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    TEST_ASSERT_TRUE(st.open.scale_start > 0.0F);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_MODAL_ANIM_SCALE_POP, st.open.type);
    TEST_ASSERT_TRUE(st.ease_speed > 0.0F);
    TEST_ASSERT_TRUE((st.flags & NT_UI_MODAL_LISTEN_ESC) != 0U);
    TEST_ASSERT_TRUE((st.flags & NT_UI_MODAL_CLOSE_ON_BACKDROP) != 0U);
}

/* The backdrop's id is engine-derived and deliberately unpublished, so identify it by shape: the only
 * full-viewport rectangle in the frame. Band alone would also match a game rect that happens to land
 * there. */
static bool band_has_backdrop(const nt_ui_context_t *ctx, int32_t band) {
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE && c->zIndex == band && c->boundingBox.width >= 800.0F && c->boundingBox.height >= 600.0F) {
            return true;
        }
    }
    return false;
}

/* ---- Esc raises a close (reason ESC) on the TOP modal only; a non-top modal stays silent.
 *      Top-targeting carries the standard 1-frame IM lag (the deepest modal resolved last frame
 *      is "top"), so frame 1 establishes the nesting and frame 2 holds Esc. ---- */
static void test_modal_esc_top_only(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F; /* eased: the frame it closes itself still shows, so the test ends it */
    st.flags = NT_UI_MODAL_LISTEN_ESC;

    /* Frame 1: establish A (outer) then B (inner, deepest) — no Esc yet. */
    ui_fx_frame_begin(s_fx.ctx, NULL);
    (void)modal_begin_open(s_fx.ctx, MODAL_A, &st);
    (void)modal_begin_open(s_fx.ctx, MODAL_B, &st);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);

    /* Frame 2: Esc held. Only B (last frame's deepest = top) consumes it; A stays silent. */
    nt_input_feed(nt_test_key(NT_KEY_ESCAPE, true));
    ui_fx_frame_begin(s_fx.ctx, NULL);
    const nt_ui_popup_info_t outer = modal_begin_open(s_fx.ctx, MODAL_A, &st); /* NOT top */
    const nt_ui_popup_info_t inner = modal_begin_open(s_fx.ctx, MODAL_B, &st); /* TOP */
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(outer.reason != NT_UI_POPUP_CLOSE_NONE); /* non-top never consumes Esc */
    TEST_ASSERT_TRUE(inner.reason != NT_UI_POPUP_CLOSE_NONE);
    TEST_ASSERT_EQUAL_INT(NT_UI_POPUP_CLOSE_ESC, inner.reason);
}

/* ---- LISTEN_ESC off: Esc raises nothing even on the top modal (flag gates the source). ---- */
static void test_modal_esc_requires_flag(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    st.flags = 0U; /* no LISTEN_ESC, no CLOSE_ON_BACKDROP */
    ui_fx_frame_begin(s_fx.ctx, NULL);
    (void)modal_begin_open(s_fx.ctx, MODAL_A, &st); /* top from the next frame on */
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    nt_input_feed(nt_test_key(NT_KEY_ESCAPE, true));
    ui_fx_frame_begin(s_fx.ctx, NULL);
    const nt_ui_popup_info_t r = modal_begin_open(s_fx.ctx, MODAL_A, &st);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(r.reason != NT_UI_POPUP_CLOSE_NONE);
}

/* What one modal frame reported: the info and whether begin showed the modal. */
typedef struct {
    nt_ui_popup_info_t info;
    bool visible;
} modal_frame_t;

static void modal_frame(uint32_t id, const nt_ui_modal_style_t *st, bool open, modal_frame_t *out) {
    ui_fx_frame_begin(s_fx.ctx, NULL);
    if (open) {
        nt_ui_open(s_fx.ctx, id);
    } else {
        nt_ui_close(s_fx.ctx, id);
    }
    out->visible = nt_ui_modal_begin(s_fx.ctx, id, st, &out->info);
    if (out->visible) {
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Entrance animation: a modal declared already-open on its very FIRST frame must ease from 0
 *      (scale-pop/fade), not snap to 1. With a finite ease_speed the first visible frame's t is well
 *      below 1, and after several frames it settles near 1. ---- */
static void test_modal_open_entrance_eases_from_zero(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F; /* finite: entrance must animate, not snap */
    modal_frame_t r;

    /* Frame 1 (first ever sight, open): t starts at ~0 and has only one dt of ease -> well under 1. */
    modal_frame(MODAL_A, &st, true, &r);
    TEST_ASSERT_TRUE(r.info.t < 0.5F);  /* a fresh open slot eases, never snaps to 1 */
    TEST_ASSERT_TRUE(r.info.t >= 0.0F); /* eased up from 0, not negative */

    /* After many frames it settles near 1. */
    for (int i = 0; i < 120; ++i) {
        modal_frame(MODAL_A, &st, true, &r);
    }
    TEST_ASSERT_TRUE(float_near(r.info.t, 1.0F, 0.02F));
}

/* ---- An explicitly-open modal declares its body on frame 1 even before t crosses epsilon: visible
 *      carries the open term, so a slow ease (t still ~0 the first frames) never skips the body. ---- */
static void test_modal_open_visible_before_epsilon(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.1F; /* very slow: first eased t stays well below epsilon (1/256) */
    modal_frame_t r;
    modal_frame(MODAL_A, &st, true, &r);
    TEST_ASSERT_TRUE(r.info.t < (1.0F / 256.0F)); /* still under epsilon on frame 1 */
    TEST_ASSERT_TRUE(r.visible);                  /* yet visible because open == true */
}

/* ---- ease_speed=0 (instant) entrance still snaps to 1 on the first frame: the prime-then-snap path
 *      must not regress the documented instant-open contract. ---- */
static void test_modal_open_instant_snaps(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    modal_frame_t r;
    modal_frame(MODAL_A, &st, true, &r);
    TEST_ASSERT_TRUE(float_near(r.info.t, 1.0F, 0.0001F));
    TEST_ASSERT_TRUE(r.visible);
}

/* ---- World auto-gate: while the backdrop is up, nt_ui_wants_pointer is true. The occluder is
 *      recorded this frame and wins the resolve on the NEXT frame's step/query. ---- */
static void test_modal_wants_pointer_while_open(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    nt_pointer_t p = {.x = 400.0F, .y = 300.0F, .active = true}; /* mid-viewport, over the full-screen backdrop */

    /* Frame 1: declare the modal; the backdrop occluder records for next-frame arbitration. */
    ui_fx_frame_begin(s_fx.ctx, &p);
    (void)modal_begin_open(s_fx.ctx, MODAL_A, &st);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);

    /* Frame 2: the prev-frame backdrop wins topmost-z; the hot record is declared again -> wants_pointer. */
    ui_fx_frame_begin(s_fx.ctx, &p);
    (void)modal_begin_open(s_fx.ctx, MODAL_A, &st);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx));
}

/* ---- Modal body button must be clickable: the helper declares no panel self-occluder, so the button
 *      is the sole interactive record and wins. Needs 3 frames (register / press / release-inside). ---- */
/* The panel floats attached-to-root CENTER_CENTER, so it sits at the viewport center. The button is a
 * NORMAL (non-floating) child of the panel -> it shares the panel's floating tree root, declared after
 * the panel; with no self-occluder it's the sole interactive record and wins. With the
 * panel shrink-wrapping the button, the panel == the button bbox, both centered in the 800x600 frame. */
#define MODAL_BTN_W 200.0F
#define MODAL_BTN_H 120.0F
/* Centered panel top-left + the button (== panel) center. */
#define MODAL_BTN_X0 ((UI_FX_VIEW_W - MODAL_BTN_W) * 0.5F)
#define MODAL_BTN_Y0 ((UI_FX_VIEW_H - MODAL_BTN_H) * 0.5F)
#define MODAL_BTN_CX (MODAL_BTN_X0 + (MODAL_BTN_W * 0.5F))
#define MODAL_BTN_CY (MODAL_BTN_Y0 + (MODAL_BTN_H * 0.5F))

static nt_pointer_t modal_btn_pointer(bool is_down, bool is_pressed, bool is_released) { return ui_fx_pointer(MODAL_BTN_CX, MODAL_BTN_CY, is_down, is_pressed, is_released); }

/* ---- A modal WITHOUT CLOSE_ON_BACKDROP (LISTEN_ESC only) still gates click-through: the backdrop
 *      registers as an inert block_pointer occluder that wins topmost-z, so a base widget behind the
 *      open modal never goes hot and gets neither press nor click. Two frames for the IM-lag. ---- */
static nt_ui_interaction_t modal_gate_base_frame(const nt_ui_modal_style_t *st, const nt_pointer_t *p, bool open, bool *shown) {
    ui_fx_frame_begin(s_fx.ctx, p);
    nt_ui_interaction_t in;
    CLAY({.id = (Clay_ElementId){.id = nt_ui_id("gated_base_btn")}, .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(120)}}}) {
        in = nt_ui_step(s_fx.ctx, nt_ui_id("gated_base_btn"), NULL);
    }
    if (open) {
        nt_ui_open(s_fx.ctx, MODAL_A);
    } else {
        nt_ui_close(s_fx.ctx, MODAL_A);
    }
    *shown = nt_ui_modal_begin(s_fx.ctx, MODAL_A, st, NULL);
    if (*shown) {
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    return in;
}

static void test_modal_no_backdrop_close_still_gates(void) {
    bool shown = false;
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    st.flags = NT_UI_MODAL_LISTEN_ESC; /* no CLOSE_ON_BACKDROP */

    /* Frame 1: register the base button + the modal's block_pointer occluder. */
    nt_pointer_t f1 = ui_fx_pointer(100.0F, 60.0F, false, false, false);
    modal_gate_base_frame(&st, &f1, true, &shown);

    /* Frame 2: press over the base button. The block_pointer backdrop won the prev-frame topmost-z,
     * so the base widget never goes hot and the press is gated. */
    nt_pointer_t f2 = ui_fx_pointer(100.0F, 60.0F, true, true, false);
    nt_ui_interaction_t in2 = modal_gate_base_frame(&st, &f2, true, &shown);
    TEST_ASSERT_FALSE(in2.pressed_now); /* base click-through blocked by the occluder */

    /* Frame 3: release over the base button -> still no click; the occluder gated the whole gesture. */
    nt_pointer_t f3 = ui_fx_pointer(100.0F, 60.0F, false, false, true);
    nt_ui_interaction_t in3 = modal_gate_base_frame(&st, &f3, true, &shown);
    TEST_ASSERT_FALSE(in3.clicked); /* base widget never clicks behind the open modal */
}

/* ---- A closing modal only paints: its fading backdrop lets a tap through to the base button. ---- */
static void test_modal_closing_backdrop_lets_the_pointer_through(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F;
    bool shown = false;
    for (int f = 0; f < 90; ++f) {
        (void)modal_gate_base_frame(&st, &(nt_pointer_t){.x = 100.0F, .y = 60.0F, .active = true}, true, &shown);
    }
    for (int f = 0; f < 2; ++f) {
        (void)modal_gate_base_frame(&st, &(nt_pointer_t){.x = 100.0F, .y = 60.0F, .active = true}, false, &shown);
    }
    nt_pointer_t tap = ui_fx_pointer(100.0F, 60.0F, false, true, true);
    tap.type = NT_POINTER_TOUCH;
    const nt_ui_interaction_t in = modal_gate_base_frame(&st, &tap, false, &shown);
    TEST_ASSERT_TRUE_MESSAGE(shown, "the modal still fades");
    TEST_ASSERT_TRUE_MESSAGE(in.clicked, "the base under the fading backdrop takes the tap");
}

/* One modal frame with a stepped button child. The button id is stepped INSIDE the body — mirrors the
 * showcase's action-button pattern. */

static nt_ui_interaction_t modal_button_frame(const nt_ui_modal_style_t *st, const nt_pointer_t *p) {
    ui_fx_frame_begin(s_fx.ctx, p);
    nt_ui_open(s_fx.ctx, MODAL_A);
    nt_ui_popup_info_t info = {0};
    nt_ui_interaction_t in = {0};
    if (nt_ui_modal_begin(s_fx.ctx, MODAL_A, st, &info)) { /* a snap modal that closed itself shows nothing */
        CLAY({.id = (Clay_ElementId){.id = nt_ui_id("modal_btn")}, .layout = {.sizing = {CLAY_SIZING_FIXED(MODAL_BTN_W), CLAY_SIZING_FIXED(MODAL_BTN_H)}}}) {
            in = nt_ui_step(s_fx.ctx, nt_ui_id("modal_btn"), NULL);
        }
        nt_ui_modal_end(s_fx.ctx);
    }
    s_frame_close_reason = info.reason;
    nt_ui_end(s_fx.ctx);
    return in;
}

static void test_modal_body_button_clickable(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F; /* t snaps to 1: panel lives from frame 1 */

    /* Frame 1: register the button (empty prev-frame registry -> nothing hot yet). */
    nt_pointer_t f1 = modal_btn_pointer(false, false, false);
    modal_button_frame(&st, &f1);

    /* Frame 2: press inside the button. Resolve runs off frame-1 registry: with no self-occluder the
     * button is the sole interactive record, so the press lands on it. */
    nt_pointer_t f2 = modal_btn_pointer(true, true, false);
    nt_ui_interaction_t in2 = modal_button_frame(&st, &f2);
    TEST_ASSERT_TRUE(in2.pressed_now); /* press lands on the sole interactive record */

    /* Frame 3: release inside -> clicked exactly once. */
    nt_pointer_t f3 = modal_btn_pointer(false, false, true);
    nt_ui_interaction_t in3 = modal_button_frame(&st, &f3);
    TEST_ASSERT_TRUE(in3.clicked); /* action button is clickable inside a modal */
}

/* ---- backdrop_close_pad: a CLOSE_ON_BACKDROP click JUST OUTSIDE the panel but within the pad must NOT
 *      close; a click well beyond panel+pad DOES raise CLOSE_BACKDROP. The panel (= MODAL_A) is the
 *      shrink-wrapped 200x120 box at (0,0); is_top needs the 1-frame IM lag so each probe runs 2 frames
 *      (F1 establishes top, F2 holds the click). ---- */
static nt_ui_popup_close_t modal_backdrop_click_reason(const nt_ui_modal_style_t *st, float cx, float cy) {
    nt_pointer_t f1 = ui_fx_pointer(cx, cy, false, false, false);
    modal_button_frame(st, &f1); /* establish top (overlay_top) */
    /* F2: full press+release in one frame at (cx,cy) -> backdrop click fires this frame. */
    nt_pointer_t f2 = ui_fx_pointer(cx, cy, false, false, true);
    f2.buttons[NT_BUTTON_LEFT].is_pressed = true;
    modal_button_frame(st, &f2);
    return s_frame_close_reason;
}

static void test_modal_backdrop_close_pad(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    st.backdrop_close_pad = 16;
    st.flags = NT_UI_MODAL_CLOSE_ON_BACKDROP;

    /* Click 8px to the right of the (centered) panel's right edge: outside the bbox but inside the 16px
     * pad -> close suppressed. */
    TEST_ASSERT_EQUAL_INT(NT_UI_POPUP_CLOSE_NONE, (int)modal_backdrop_click_reason(&st, MODAL_BTN_X0 + MODAL_BTN_W + 8.0F, MODAL_BTN_CY));

    /* Click in a far corner (40, 40): clearly off panel+pad -> CLOSE_BACKDROP. */
    TEST_ASSERT_EQUAL_INT(NT_UI_POPUP_CLOSE_OUTSIDE, (int)modal_backdrop_click_reason(&st, 40.0F, 40.0F));
}

/* ---- nt_ui_wants_keyboard reports the keyboard owner nt_ui_begin fixed for the frame (last frame's top
 *      overlay): it holds through nt_ui_end until the next begin, so a game reads it after begin. ---- */
static void test_wants_keyboard_follows_the_top(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F; /* instant */
    TEST_ASSERT_FALSE(nt_ui_wants_keyboard(s_fx.ctx));

    /* Frame 1 opens the modal: that frame's keys were fixed at its begin, before anything was up. */
    ui_fx_frame_begin(s_fx.ctx, NULL);
    (void)modal_begin_open(s_fx.ctx, MODAL_A, &st);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(nt_ui_wants_keyboard(s_fx.ctx));

    /* Frame 2: the modal is the top and owns the keys from begin, also after end. */
    ui_fx_frame_begin(s_fx.ctx, NULL);
    TEST_ASSERT_TRUE(nt_ui_wants_keyboard(s_fx.ctx));
    (void)modal_begin_open(s_fx.ctx, MODAL_A, &st);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(nt_ui_wants_keyboard(s_fx.ctx));

    /* A closed modal declares nothing and claims no top: the keys are the game's again. */
    nt_ui_close(s_fx.ctx, MODAL_A);
    for (int f = 0; f < 2; ++f) {
        ui_fx_frame_begin(s_fx.ctx, NULL);
        if (nt_ui_modal_begin(s_fx.ctx, MODAL_A, &st, NULL)) {
            nt_ui_modal_end(s_fx.ctx);
        }
        nt_ui_end(s_fx.ctx);
    }
    TEST_ASSERT_FALSE(nt_ui_wants_keyboard(s_fx.ctx));
}

/* ---- Per-context overlay_zband_stride: a root modal and its backdrop sit on the stride band, a nested one
 *      and its backdrop on stack slot 1's band. A second ctx coexists with the fixture's (module inited by setUp). ---- */
alignas(NT_UI_ARENA_ALIGN) static uint8_t s_custom_arena[NT_UI_TEST_ARENA_SIZE];

static void test_modal_custom_zband_stride(void) {
    const int16_t custom_stride = 500;
    nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    desc.overlay_zband_stride = custom_stride;
    nt_ui_context_t *ctx = nt_ui_create_context(s_custom_arena, sizeof s_custom_arena, &desc);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    ui_fx_frame_begin(ctx, NULL);
    (void)modal_begin_open(ctx, MODAL_A, &st); /* root -> stride */
    CLAY({.id = CLAY_ID("cs_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}){}(void)modal_begin_open(ctx, MODAL_B,
                                                                                                                                                                   &st); /* nested -> slot 1 */
    CLAY({.id = CLAY_ID("cs_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    nt_ui_modal_end(ctx);
    nt_ui_modal_end(ctx);
    nt_ui_end(ctx);

    const Clay_RenderCommand *a = ui_fx_find_rect(ctx, CLAY_ID("cs_a").id);
    const Clay_RenderCommand *b = ui_fx_find_rect(ctx, CLAY_ID("cs_b").id);
    TEST_ASSERT_TRUE(a != NULL && b != NULL);
    TEST_ASSERT_EQUAL_INT32(custom_stride, a->zIndex);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(custom_stride + NT_UI_OVERLAY_MAX, b->zIndex, "a nested modal sits on its stack slot's band");
    TEST_ASSERT_TRUE_MESSAGE(band_has_backdrop(ctx, custom_stride), "the outer backdrop paints on its panel's band");
    TEST_ASSERT_TRUE_MESSAGE(band_has_backdrop(ctx, custom_stride + NT_UI_OVERLAY_MAX), "the nested backdrop paints on its own panel's band");

    nt_ui_destroy_context(ctx);
}

/* ---- A stride of 1 is valid: the catcher shares the panel's band 1 and still gates the base band. ---- */
static void test_modal_stride_one_gates_base_ui(void) {
    nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    desc.overlay_zband_stride = 1;
    nt_ui_context_t *ctx = nt_ui_create_context(s_custom_arena, sizeof s_custom_arena, &desc);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F; /* eased: the frame it closes itself still shows, so the test ends it */
    bool clicked = false;
    for (int f = 0; f < 4; ++f) {
        nt_pointer_t p = {.x = 20.0F, .y = 20.0F, .active = true};
        p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = f == 2, .is_pressed = f == 2, .is_released = f == 3};
        ui_fx_frame_begin(ctx, &p);
        (void)modal_begin_open(ctx, MODAL_A, &st);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}}) {}
        nt_ui_modal_end(ctx);
        /* A game floating on the base band declared after the modal: only the catcher's band keeps it under. */
        CLAY({.id = CLAY_ID("s1_base"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT}, .layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(80)}}}) {}
        clicked = nt_ui_step(ctx, CLAY_ID("s1_base").id, NULL).clicked || clicked;
        nt_ui_end(ctx);
    }
    TEST_ASSERT_FALSE_MESSAGE(clicked, "the base UI under a stride-1 modal clicks nothing");
    nt_ui_destroy_context(ctx);
}

/* ---- A closed modal takes no stack slot: declared between the 15th and the 16th shown modals at the largest
 *      valid stride, it neither asserts nor moves the 16th off the last slot's band. ---- */
static void test_modal_closed_takes_no_stack_slot(void) {
    nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    desc.overlay_zband_stride = (int16_t)(INT16_MAX - ((NT_UI_OVERLAY_MAX - 1) * NT_UI_OVERLAY_MAX));
    nt_ui_context_t *ctx = nt_ui_create_context(s_custom_arena, sizeof s_custom_arena, &desc);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    ui_fx_frame_begin(ctx, NULL);
    for (uint32_t i = 0; i + 1U < NT_UI_OVERLAY_MAX; ++i) {
        (void)modal_begin_open(ctx, MODAL_A + 0x100U + i, &st);
        nt_ui_modal_end(ctx);
    }
    modal_begin_closed(ctx, MODAL_B, &st);
    (void)modal_begin_open(ctx, MODAL_A, &st);
    CLAY({.id = CLAY_ID("last_body"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    nt_ui_modal_end(ctx);
    nt_ui_end(ctx);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_OVERLAY_MAX, ctx->overlay_count);
    const Clay_RenderCommand *last = ui_fx_find_rect(ctx, CLAY_ID("last_body").id);
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_INT32(INT16_MAX, last->zIndex);
    nt_ui_destroy_context(ctx);
}

/* ---- A 0 stride (no fallback to the default, which lives only in nt_ui_create_desc_defaults) and a stride
 *      whose last stack slot's band passes int16 by one assert at create. ---- */
static void test_modal_stride_out_of_range_asserts(void) {
    const int16_t strides[] = {0, (int16_t)(INT16_MAX - ((NT_UI_OVERLAY_MAX - 1) * NT_UI_OVERLAY_MAX) + 1)};
    for (int i = 0; i < 2; ++i) {
        nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
        desc.overlay_zband_stride = strides[i];
        NT_TEST_EXPECT_ASSERT((void)nt_ui_create_context(s_custom_arena, sizeof s_custom_arena, &desc));
        TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "overlay_zband_stride must be > 0 with room"));
    }
}

/* ---- SLIDE edge: each edge displaces the panel on the right axis with the right sign (+x right, +y down);
 *      a finite ease keeps the first frame's offset near anim.offset (40), past the body's half size. ---- */
/* The axis signs of the panel's eased shift last frame: +1/-1 when a probe 30 px from the body center along
 * that axis hits the panel, 0 when neither probe does. */
static void panel_shift(uint32_t id, int *sx, int *sy) {
    const nt_ui_bbox_t bb = nt_ui_get_bbox(s_fx.ctx, id);
    TEST_ASSERT_TRUE_MESSAGE(bb.found, "the panel was laid out");
    const float cx = bb.x + (bb.width * 0.5F);
    const float cy = bb.y + (bb.height * 0.5F);
    *sx = (int)nt_ui_test_hit(s_fx.ctx, id, cx + 30.0F, cy) - (int)nt_ui_test_hit(s_fx.ctx, id, cx - 30.0F, cy);
    *sy = (int)nt_ui_test_hit(s_fx.ctx, id, cx, cy + 30.0F) - (int)nt_ui_test_hit(s_fx.ctx, id, cx, cy - 30.0F);
}

/* One frame of the open modal `id` with a body. */
static void modal_open_frame(uint32_t id, const nt_ui_modal_style_t *st) {
    ui_fx_frame_begin(s_fx.ctx, NULL);
    (void)modal_begin_open(s_fx.ctx, id, st);
    modal_body();
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

static void test_modal_slide_dir(void) {
    const nt_ui_modal_edge_t edges[] = {NT_UI_MODAL_BOTTOM, NT_UI_MODAL_TOP, NT_UI_MODAL_LEFT, NT_UI_MODAL_RIGHT};
    const int want_x[] = {0, 0, -1, 1};
    const int want_y[] = {1, -1, 0, 0};
    for (int i = 0; i < 4; ++i) {
        nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
        st.ease_speed = 8.0F;
        st.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = (uint8_t)edges[i], .offset = 40.0F};
        modal_open_frame(MODAL_A + (uint32_t)i, &st); /* distinct id per edge: each eases from a fresh slot */
        int sx = 0;
        int sy = 0;
        panel_shift(MODAL_A + (uint32_t)i, &sx, &sy);
        TEST_ASSERT_EQUAL_INT(want_x[i], sx);
        TEST_ASSERT_EQUAL_INT(want_y[i], sy);
    }
}

/* ---- Asymmetric open/close: open=slide-from-bottom shows a vertical offset while opening; close=fade
 *      shows none (identity) while closing. Proves the per-phase recipe is picked by `open`. ---- */
static void test_modal_asymmetric_open_close(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F;
    st.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = NT_UI_MODAL_BOTTOM, .offset = 40.0F};
    st.close = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_FADE};
    int sx = 0;
    int sy = 0;
    modal_open_frame(MODAL_B, &st);
    panel_shift(MODAL_B, &sx, &sy);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sy, "slide-from-bottom while opening");

    ui_fx_frame_begin(s_fx.ctx, NULL);
    modal_begin_closed(s_fx.ctx, MODAL_B, &st);
    nt_ui_end(s_fx.ctx);
    panel_shift(MODAL_B, &sx, &sy);
    TEST_ASSERT_TRUE_MESSAGE(sx == 0 && sy == 0, "the fade close keeps the panel in place");
}

/* ---- Invalid anim recipes fail early at begin. Asserts run BEFORE the push, so a trapped begin leaves
 *      the stack balanced and the next case runs in the same frame. ---- */
static void test_modal_anim_asserts(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    ui_fx_frame_begin(s_fx.ctx, NULL);
    nt_ui_open(s_fx.ctx, MODAL_A);

    nt_ui_modal_style_t bad = st;
    bad.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SCALE_POP, .scale_start = 0.0F};
    NT_TEST_EXPECT_ASSERT(nt_ui_modal_begin(s_fx.ctx, MODAL_A, &bad, NULL));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "scale_start must be finite > 0"));

    bad = st;
    bad.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = NT_UI_MODAL_BOTTOM, .offset = NAN};
    NT_TEST_EXPECT_ASSERT(nt_ui_modal_begin(s_fx.ctx, MODAL_A, &bad, NULL));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "SLIDE offset finite + edge in range"));

    bad = st;
    bad.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = 4, .offset = 10.0F};
    NT_TEST_EXPECT_ASSERT(nt_ui_modal_begin(s_fx.ctx, MODAL_A, &bad, NULL));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "SLIDE offset finite + edge in range"));

    bad = st;
    bad.open.type = 7;
    NT_TEST_EXPECT_ASSERT(nt_ui_modal_begin(s_fx.ctx, MODAL_A, &bad, NULL));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "anim.type out of range"));

    nt_ui_end(s_fx.ctx);
}

/* ---- The close phase uses style.close, not style.open: open=slide-LEFT (offset_x < 0 opening),
 *      close=slide-RIGHT (offset_x > 0 closing). A bug reading style.open on close keeps the sign < 0. ---- */
static void test_modal_close_uses_close_recipe(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F;
    st.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = NT_UI_MODAL_LEFT, .offset = 40.0F};
    st.close = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = NT_UI_MODAL_RIGHT, .offset = 40.0F};
    int sx = 0;
    int sy = 0;
    modal_open_frame(MODAL_B, &st);
    panel_shift(MODAL_B, &sx, &sy);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, sx, "the open recipe slides from the left");

    ui_fx_frame_begin(s_fx.ctx, NULL);
    modal_begin_closed(s_fx.ctx, MODAL_B, &st);
    nt_ui_end(s_fx.ctx);
    panel_shift(MODAL_B, &sx, &sy);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sx, "the close recipe slides to the right");

    /* The frame the modal closes itself on Esc eases by the close recipe too. */
    st.flags = NT_UI_MODAL_LISTEN_ESC;
    for (int f = 0; f < 2; ++f) {
        if (f == 1) {
            nt_input_feed(nt_test_key(NT_KEY_ESCAPE, true));
        }
        ui_fx_frame_begin(s_fx.ctx, NULL);
        const nt_ui_popup_info_t info = modal_begin_open(s_fx.ctx, MODAL_A, &st);
        modal_body();
        nt_ui_modal_end(s_fx.ctx);
        nt_ui_end(s_fx.ctx);
        TEST_ASSERT_EQUAL_INT(f == 1 ? NT_UI_POPUP_CLOSE_ESC : NT_UI_POPUP_CLOSE_NONE, info.reason);
    }
    panel_shift(MODAL_A, &sx, &sy);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sx, "a self-close eases by the close recipe from its first frame");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_modal_defaults_valid);
    RUN_TEST(test_modal_esc_top_only);
    RUN_TEST(test_modal_esc_requires_flag);
    RUN_TEST(test_modal_open_entrance_eases_from_zero);
    RUN_TEST(test_modal_open_visible_before_epsilon);
    RUN_TEST(test_modal_open_instant_snaps);
    RUN_TEST(test_modal_wants_pointer_while_open);
    RUN_TEST(test_modal_no_backdrop_close_still_gates);
    RUN_TEST(test_modal_closing_backdrop_lets_the_pointer_through);
    RUN_TEST(test_modal_body_button_clickable);
    RUN_TEST(test_modal_backdrop_close_pad);
    RUN_TEST(test_wants_keyboard_follows_the_top);
    RUN_TEST(test_modal_slide_dir);
    RUN_TEST(test_modal_asymmetric_open_close);
    RUN_TEST(test_modal_close_uses_close_recipe);
    RUN_TEST(test_modal_anim_asserts);
    RUN_TEST(test_modal_custom_zband_stride);
    RUN_TEST(test_modal_stride_out_of_range_asserts);
    RUN_TEST(test_modal_stride_one_gates_base_ui);
    RUN_TEST(test_modal_closed_takes_no_stack_slot);
    return UNITY_END();
}
