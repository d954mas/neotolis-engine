/* Modal helper tests — Clay-floating smoke, z-band math, stack push/pop balance,
 * depth-overflow death, top-only close targeting, open/close tween clamp +
 * fully_closed/visible transition, high-level wrapper contract, and the world
 * auto-gate via wants_pointer.
 *
 * Driven through the walker fixture + NT_TEST_ACCESS probes (no GL surface).
 * UNITY_EXCLUDE_FLOAT: compare floats via an eps helper. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "input/nt_input_internal.h" /* nt_input_set_key / nt_input_clear_all_keys */
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_modal.h"
#include "ui/nt_ui_popup.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

static nt_ui_modal_close_reason_t s_frame_close_reason; /* reason from the last modal_button_frame; reset in setUp */

void setUp(void) {
    s_frame_close_reason = NT_UI_MODAL_CLOSE_NONE;
    nt_test_assert_install();
    nt_input_clear_all_keys();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) {
    nt_input_clear_all_keys();
    ui_walker_fixture_shutdown(&s_fx);
}

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

#define MODAL_A 0x4D0001U
#define MODAL_B 0x4D0002U

/* ---- ABI sanity: the _Static_asserts compile; assert the runtime sizes match too. ---- */
static void test_modal_abi_sizes(void) {
    TEST_ASSERT_EQUAL_UINT(12U, (unsigned)sizeof(nt_ui_modal_result_t));
    TEST_ASSERT_EQUAL_UINT(12U, (unsigned)sizeof(nt_ui_modal_anim_t));
    TEST_ASSERT_EQUAL_UINT(40U, (unsigned)sizeof(nt_ui_modal_style_t));
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

/* Bands are read off what the frame actually ships: Clay stamps the owning floating root's zIndex on
 * RECTANGLE and TEXT commands. INT32_MIN = the id emitted no rectangle this frame. */
static int32_t band_of_rect(const nt_ui_context_t *ctx, uint32_t id) {
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->id == id && c->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
            return c->zIndex;
        }
    }
    return INT32_MIN;
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

/* ---- Floating-modal smoke: a floating modal (z=1000, transform + opacity) declared over base content;
 *      the walker exits with a balanced scissor stack and composes the panel transform. ---- */
static void test_modal_floating_smoke(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F; /* instant: t snaps to 1 on first touch */
    nt_pointer_t p = {.active = true};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    CLAY({.id = CLAY_ID("base"), .layout = {.sizing = {CLAY_SIZING_FIXED(800), CLAY_SIZING_FIXED(600)}}}) {}
    nt_ui_modal_result_t r = nt_ui_modal_begin(s_fx.ctx, MODAL_A, &st, true);
    {
        CLAY({.id = CLAY_ID("modal_body"), .layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(200)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    }
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx); /* walks: balanced scissor assert + transform compose must not trip */
    TEST_ASSERT_EQUAL_INT32((int32_t)1000, band_of_rect(s_fx.ctx, CLAY_ID("modal_body").id));
    TEST_ASSERT_TRUE(r.visible);
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->active_modal_depth); /* balanced */
}

/* ---- z-band: panel = 1000*(depth+1), backdrop = panel-1. Nested = 2000/1999, depth 2. ---- */
static void test_modal_zband_and_nesting(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    nt_pointer_t p = {.active = true};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_A, &st, true);
    /* Opaque bodies so each panel subtree emits a rectangle carrying its root's band. */
    CLAY({.id = CLAY_ID("body_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    nt_ui_modal_begin(s_fx.ctx, MODAL_B, &st, true); /* modal-over-modal */
    CLAY({.id = CLAY_ID("body_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    TEST_ASSERT_EQUAL_UINT8(2U, s_fx.ctx->active_modal_depth);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_modal_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->active_modal_depth);
    nt_ui_end(s_fx.ctx);

    TEST_ASSERT_EQUAL_INT32((int32_t)1000, band_of_rect(s_fx.ctx, CLAY_ID("body_a").id));
    TEST_ASSERT_EQUAL_INT32((int32_t)2000, band_of_rect(s_fx.ctx, CLAY_ID("body_b").id));
    TEST_ASSERT_TRUE_MESSAGE(band_has_backdrop(s_fx.ctx, 999), "the outer backdrop must paint one band under its panel");
    TEST_ASSERT_TRUE_MESSAGE(band_has_backdrop(s_fx.ctx, 1999), "the nested backdrop must paint one band under its panel");
}

/* ---- Stack depth is zero at frame start and after balanced begin/end. ---- */
static void test_modal_stack_depth_zero_balanced(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->active_modal_depth);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_A, &st, true);
    TEST_ASSERT_EQUAL_UINT8(1U, s_fx.ctx->active_modal_depth);
    nt_ui_modal_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->active_modal_depth);
    nt_ui_end(s_fx.ctx);
}

/* ---- Pushing past NT_UI_MODAL_MAX_DEPTH fires NT_ASSERT (no silent fallback). ---- */
static void test_modal_depth_overflow_asserts(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    for (uint32_t i = 0; i < NT_UI_MODAL_MAX_DEPTH; ++i) {
        nt_ui_modal_begin(s_fx.ctx, 0x4D1000U + i, &st, true);
    }
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NT_UI_MODAL_MAX_DEPTH, s_fx.ctx->active_modal_depth);
    NT_TEST_EXPECT_ASSERT(nt_ui_modal_begin(s_fx.ctx, 0x4D1FFFU, &st, true));
    for (uint32_t i = 0; i < NT_UI_MODAL_MAX_DEPTH; ++i) {
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Esc raises close_requested{ESC} on the TOP modal only; a non-top modal stays silent.
 *      Top-targeting carries the standard 1-frame IM lag (the deepest modal resolved last frame
 *      is "top"), so frame 1 establishes the nesting and frame 2 holds Esc. ---- */
static void test_modal_esc_top_only(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    st.flags = NT_UI_MODAL_LISTEN_ESC;

    /* Frame 1: establish A (outer) then B (inner, deepest) — no Esc yet. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_A, &st, true);
    nt_ui_modal_begin(s_fx.ctx, MODAL_B, &st, true);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);

    /* Frame 2: Esc held. Only B (last frame's deepest = top) consumes it; A stays silent. */
    nt_input_set_key(NT_KEY_ESCAPE, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_modal_result_t outer = nt_ui_modal_begin(s_fx.ctx, MODAL_A, &st, true); /* NOT top */
    nt_ui_modal_result_t inner = nt_ui_modal_begin(s_fx.ctx, MODAL_B, &st, true); /* TOP */
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(outer.close_requested); /* non-top never consumes Esc */
    TEST_ASSERT_TRUE(inner.close_requested);
    TEST_ASSERT_EQUAL_INT(NT_UI_MODAL_CLOSE_ESC, inner.reason);
}

/* ---- LISTEN_ESC off: Esc raises nothing even on the top modal (flag gates the source). ---- */
static void test_modal_esc_requires_flag(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    st.flags = 0U; /* no LISTEN_ESC, no CLOSE_ON_BACKDROP */
    nt_input_set_key(NT_KEY_ESCAPE, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_modal_result_t r = nt_ui_modal_begin(s_fx.ctx, MODAL_A, &st, true);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(r.close_requested);
}

/* ---- Open/close tween: t rises toward 1 while open, decays toward 0 after close, stays in [0,1];
 *      visible == (t>eps); fully_closed only when (!open && t<=eps). ---- */
static void modal_frame(uint32_t id, const nt_ui_modal_style_t *st, bool open, nt_ui_modal_result_t *out) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    *out = nt_ui_modal_begin(s_fx.ctx, id, st, open);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_modal_tween_clamp_and_transition(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F; /* finite ease so t moves gradually, never snaps */
    nt_ui_modal_result_t r;

    /* Open: t monotonically rises toward 1, always in [0,1]. */
    float prev = 0.0F;
    for (int i = 0; i < 120; ++i) {
        modal_frame(MODAL_A, &st, true, &r);
        TEST_ASSERT_TRUE(r.t >= -0.0001F && r.t <= 1.0001F);
        TEST_ASSERT_TRUE(r.t >= prev - 0.0001F); /* non-decreasing while opening */
        prev = r.t;
        TEST_ASSERT_TRUE(r.visible);
        TEST_ASSERT_FALSE(r.fully_closed);
    }
    TEST_ASSERT_TRUE(float_near(r.t, 1.0F, 0.02F));

    /* Close: t decays toward 0; once t<=eps the modal reports fully_closed + !visible. */
    bool saw_fully_closed = false;
    for (int i = 0; i < 300; ++i) {
        modal_frame(MODAL_A, &st, false, &r);
        TEST_ASSERT_TRUE(r.t >= -0.0001F && r.t <= 1.0001F);
        TEST_ASSERT_EQUAL_INT((r.t > (1.0F / 256.0F)), r.visible);
        if (r.fully_closed) {
            TEST_ASSERT_FALSE(r.visible);
            saw_fully_closed = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(saw_fully_closed);
}

/* ---- Entrance animation: a modal declared already-open on its very FIRST frame must ease from 0
 *      (scale-pop/fade), not snap to 1. With a finite ease_speed the first visible frame's t is well
 *      below 1, and after several frames it settles near 1. ---- */
static void test_modal_open_entrance_eases_from_zero(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F; /* finite: entrance must animate, not snap */
    nt_ui_modal_result_t r;

    /* Frame 1 (first ever sight, open): t starts at ~0 and has only one dt of ease -> well under 1. */
    modal_frame(MODAL_A, &st, true, &r);
    TEST_ASSERT_TRUE(r.t < 0.5F);  /* a fresh open slot eases, never snaps to 1 */
    TEST_ASSERT_TRUE(r.t >= 0.0F); /* eased up from 0, not negative */

    /* After many frames it settles near 1. */
    for (int i = 0; i < 120; ++i) {
        modal_frame(MODAL_A, &st, true, &r);
    }
    TEST_ASSERT_TRUE(float_near(r.t, 1.0F, 0.02F));
}

/* ---- An explicitly-open modal declares its body on frame 1 even before t crosses epsilon: visible
 *      carries the open term, so a slow ease (t still ~0 the first frames) never skips the body. ---- */
static void test_modal_open_visible_before_epsilon(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.1F; /* very slow: first eased t stays well below epsilon (1/256) */
    nt_ui_modal_result_t r;
    modal_frame(MODAL_A, &st, true, &r);
    TEST_ASSERT_TRUE(r.t < (1.0F / 256.0F)); /* still under epsilon on frame 1 */
    TEST_ASSERT_TRUE(r.visible);             /* yet visible because open == true */
    TEST_ASSERT_FALSE(r.fully_closed);
}

/* ---- ease_speed=0 (instant) entrance still snaps to 1 on the first frame: the prime-then-snap path
 *      must not regress the documented instant-open contract. ---- */
static void test_modal_open_instant_snaps(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_modal_result_t r;
    modal_frame(MODAL_A, &st, true, &r);
    TEST_ASSERT_TRUE(float_near(r.t, 1.0F, 0.0001F));
    TEST_ASSERT_TRUE(r.visible);
}

/* ---- High-level wrapper: returns true while animating (open OR closing), clears *p_open on
 *      close_requested, returns false once fully closed. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_modal_wrapper_contract(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F;
    st.flags = NT_UI_MODAL_LISTEN_ESC;
    bool open = true;

    /* Frame 1: open -> wrapper returns true, declares body. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    bool declared = nt_ui_modal_visible(s_fx.ctx, MODAL_A, &st, &open);
    TEST_ASSERT_TRUE(declared);
    if (declared) {
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(open);

    /* Frame 2: Esc -> wrapper clears *open but STILL returns true (animating closed). */
    nt_input_set_key(NT_KEY_ESCAPE, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    declared = nt_ui_modal_visible(s_fx.ctx, MODAL_A, &st, &open);
    TEST_ASSERT_TRUE(declared); /* body still declared during the close animation */
    TEST_ASSERT_FALSE(open);    /* close_requested cleared the bool */
    if (declared) {
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    nt_input_clear_all_keys();

    /* Subsequent frames: t decays; eventually the wrapper returns false (fully closed, no body). */
    bool went_false = false;
    for (int i = 0; i < 300; ++i) {
        nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
        declared = nt_ui_modal_visible(s_fx.ctx, MODAL_A, &st, &open);
        if (declared) {
            nt_ui_modal_end(s_fx.ctx);
        }
        nt_ui_end(s_fx.ctx);
        if (!declared) {
            went_false = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(went_false);
}

/* ---- World auto-gate: while the backdrop is up, nt_ui_wants_pointer is true. The occluder is
 *      recorded this frame and wins the resolve on the NEXT frame's step/query. ---- */
static void test_modal_wants_pointer_while_open(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    nt_pointer_t p = {.x = 400.0F, .y = 300.0F, .active = true}; /* mid-viewport, over the full-screen backdrop */

    /* Frame 1: declare the modal; the backdrop occluder records for next-frame arbitration. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_A, &st, true);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);

    /* Frame 2: the prev-frame backdrop wins topmost-z; query resolves pointer_over_any -> wants_pointer. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_A, &st, true);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx));
}

/* ---- Modal body button must be clickable: the helper declares no panel self-occluder, so the button
 *      is the sole interactive record and wins. Needs 3 frames (register / press / release-inside). ---- */
/* The panel floats attached-to-root CENTER_CENTER, so it sits at the viewport center. The button is a
 * NORMAL (non-floating) child of the panel -> it shares the panel's floating tree root and inherits the
 * panel's uniform zindex; with no self-occluder it's the sole interactive record and wins. With the
 * panel shrink-wrapping the button, the panel == the button bbox, both centered in the 800x600 frame. */
#define MODAL_VIEW_W 800.0F
#define MODAL_VIEW_H 600.0F
#define MODAL_BTN_W 200.0F
#define MODAL_BTN_H 120.0F
/* Centered panel top-left + the button (== panel) center. */
#define MODAL_BTN_X0 ((MODAL_VIEW_W - MODAL_BTN_W) * 0.5F)
#define MODAL_BTN_Y0 ((MODAL_VIEW_H - MODAL_BTN_H) * 0.5F)
#define MODAL_BTN_CX (MODAL_BTN_X0 + (MODAL_BTN_W * 0.5F))
#define MODAL_BTN_CY (MODAL_BTN_Y0 + (MODAL_BTN_H * 0.5F))

static nt_pointer_t modal_pointer_at(float x, float y, bool is_down, bool is_pressed, bool is_released) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = is_down;
    p.buttons[NT_BUTTON_LEFT].is_pressed = is_pressed;
    p.buttons[NT_BUTTON_LEFT].is_released = is_released;
    return p;
}

static nt_pointer_t modal_btn_pointer(bool is_down, bool is_pressed, bool is_released) { return modal_pointer_at(MODAL_BTN_CX, MODAL_BTN_CY, is_down, is_pressed, is_released); }

/* ---- A modal WITHOUT CLOSE_ON_BACKDROP (LISTEN_ESC only) still gates click-through: the backdrop
 *      registers as an inert block_pointer occluder that wins topmost-z, so a base widget behind the
 *      open modal never goes hot and gets neither press nor click. Two frames for the IM-lag. ---- */
static nt_ui_interaction_t modal_gate_base_frame(const nt_ui_modal_style_t *st, const nt_pointer_t *p) {
    bool open = true;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, p, 1);
    nt_ui_interaction_t in;
    CLAY({.id = (Clay_ElementId){.id = nt_ui_id("gated_base_btn")}, .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(120)}}}) {
        in = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("gated_base_btn"));
    }
    const bool declared = nt_ui_modal_visible(s_fx.ctx, MODAL_A, st, &open);
    if (declared) {
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    return in;
}

static void test_modal_no_backdrop_close_still_gates(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    st.flags = NT_UI_MODAL_LISTEN_ESC; /* no CLOSE_ON_BACKDROP */

    /* Frame 1: register the base button + the modal's block_pointer occluder. */
    nt_pointer_t f1 = modal_pointer_at(100.0F, 60.0F, false, false, false);
    modal_gate_base_frame(&st, &f1);

    /* Frame 2: press over the base button. The block_pointer backdrop won the prev-frame topmost-z,
     * so the base widget never goes hot and the press is gated. */
    nt_pointer_t f2 = modal_pointer_at(100.0F, 60.0F, true, true, false);
    nt_ui_interaction_t in2 = modal_gate_base_frame(&st, &f2);
    TEST_ASSERT_FALSE(in2.pressed_now); /* base click-through blocked by the occluder */

    /* Frame 3: release over the base button -> still no click; the occluder gated the whole gesture. */
    nt_pointer_t f3 = modal_pointer_at(100.0F, 60.0F, false, false, true);
    nt_ui_interaction_t in3 = modal_gate_base_frame(&st, &f3);
    TEST_ASSERT_FALSE(in3.clicked); /* base widget never clicks behind the open modal */
}

/* One modal frame with a stepped button child. The button id is stepped INSIDE the body — mirrors the
 * showcase's action-button pattern. */

static nt_ui_interaction_t modal_button_frame(const nt_ui_modal_style_t *st, const nt_pointer_t *p) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, p, 1);
    s_frame_close_reason = nt_ui_modal_begin(s_fx.ctx, MODAL_A, st, true).reason;
    nt_ui_interaction_t in;
    CLAY({.id = (Clay_ElementId){.id = nt_ui_id("modal_btn")}, .layout = {.sizing = {CLAY_SIZING_FIXED(MODAL_BTN_W), CLAY_SIZING_FIXED(MODAL_BTN_H)}}}) {
        in = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("modal_btn"));
    }
    nt_ui_modal_end(s_fx.ctx);
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
static nt_ui_modal_close_reason_t modal_backdrop_click_reason(const nt_ui_modal_style_t *st, float cx, float cy) {
    nt_pointer_t f1 = modal_pointer_at(cx, cy, false, false, false);
    modal_button_frame(st, &f1); /* establish top (modal_top_id_prev) */
    /* F2: full press+release in one frame at (cx,cy) -> backdrop click fires this frame. */
    nt_pointer_t f2 = modal_pointer_at(cx, cy, false, false, true);
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
    TEST_ASSERT_EQUAL_INT(NT_UI_MODAL_CLOSE_NONE, (int)modal_backdrop_click_reason(&st, MODAL_BTN_X0 + MODAL_BTN_W + 8.0F, MODAL_BTN_CY));

    /* Click in a far corner (40, 40): clearly off panel+pad -> CLOSE_BACKDROP. */
    TEST_ASSERT_EQUAL_INT(NT_UI_MODAL_CLOSE_BACKDROP, (int)modal_backdrop_click_reason(&st, 40.0F, 40.0F));
}

/* ---- A FULLY-CLOSED modal (open=false, settled t=0) called every frame must NOT block a base widget:
 *      it declares no backdrop, so the base button (at the same screen point) is the sole interactive
 *      record and clicks. The wrapper (open=false, ease_speed=0) reports fully_closed on frame 1. ---- */
#define CLOSED_BTN_W 200.0F
#define CLOSED_BTN_H 120.0F
#define CLOSED_BTN_CX (CLOSED_BTN_W * 0.5F)
#define CLOSED_BTN_CY (CLOSED_BTN_H * 0.5F)

/* One frame: a base button at (0,0)+size, plus a fully-closed modal at the same point. The wrapper
 * returns false (fully closed) so no body is declared and the stack stays balanced. */
static nt_ui_interaction_t closed_modal_base_frame(const nt_ui_modal_style_t *st, const nt_pointer_t *p) {
    bool open = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, p, 1);
    nt_ui_interaction_t in;
    CLAY({.id = (Clay_ElementId){.id = nt_ui_id("closed_base_btn")}, .layout = {.sizing = {CLAY_SIZING_FIXED(CLOSED_BTN_W), CLAY_SIZING_FIXED(CLOSED_BTN_H)}}}) {
        in = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("closed_base_btn"));
    }
    /* Settled-closed modal called every frame (the showcase Modals-tab pattern). */
    const bool declared = nt_ui_modal_visible(s_fx.ctx, MODAL_A, st, &open);
    TEST_ASSERT_FALSE(declared); /* fully closed -> wrapper balances its own stack, no body */
    nt_ui_end(s_fx.ctx);
    return in;
}

static void test_modal_closed_does_not_block_base(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F; /* instant: open=false -> t snaps to 0, fully closed from frame 1 */

    /* Frame 1: register the base button (empty prev-frame registry). */
    nt_pointer_t f1 = modal_pointer_at(CLOSED_BTN_CX, CLOSED_BTN_CY, false, false, false);
    closed_modal_base_frame(&st, &f1);

    /* Frame 2: press inside the base button. With the closed modal declaring NO backdrop occluder,
     * the base button is the sole interactive record and the press lands on it. */
    nt_pointer_t f2 = modal_pointer_at(CLOSED_BTN_CX, CLOSED_BTN_CY, true, true, false);
    nt_ui_interaction_t in2 = closed_modal_base_frame(&st, &f2);
    TEST_ASSERT_TRUE(in2.pressed_now);

    /* Frame 3: release inside -> clicked. The closed modal never gated the click-through. */
    nt_pointer_t f3 = modal_pointer_at(CLOSED_BTN_CX, CLOSED_BTN_CY, false, false, true);
    nt_ui_interaction_t in3 = closed_modal_base_frame(&st, &f3);
    TEST_ASSERT_TRUE(in3.clicked);

    /* The closed modal must not auto-gate the world pointer over EMPTY space (no base widget there).
     * A point far from the base button: with no backdrop occluder, nothing claims it. Two frames for
     * the IM-lag registry to settle on the empty-point query. */
    nt_pointer_t empty1 = modal_pointer_at(700.0F, 500.0F, false, false, false);
    closed_modal_base_frame(&st, &empty1);
    nt_pointer_t empty2 = modal_pointer_at(700.0F, 500.0F, false, false, false);
    closed_modal_base_frame(&st, &empty2);
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx)); /* closed modal must not gate empty space */
}

/* ---- nt_ui_modal_active reports PREV-FRAME presence (not the live within-frame depth, which is
 *      always 0 once nt_ui_end balanced the stack). A game polls it before its next nt_ui_begin to
 *      gate hotkeys. Frame with a modal up -> active next; frame with no modal -> inactive next. ---- */
static void test_modal_active_prev_frame(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F; /* instant */

    /* Before any frame: nothing was up. */
    TEST_ASSERT_FALSE(nt_ui_modal_active(s_fx.ctx));

    /* Frame 1: a modal is open. The live depth is 0 after end, but presence must be published. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_A, &st, true);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT8(0U, s_fx.ctx->active_modal_depth); /* live depth IS 0 ... */
    TEST_ASSERT_TRUE(nt_ui_modal_active(s_fx.ctx));            /* ... yet prev-frame presence is true */

    /* Frame 2: no modal declared at all -> presence falls back to inactive. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    CLAY({.id = CLAY_ID("base2"), .layout = {.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_FIXED(100)}}}) {}
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(nt_ui_modal_active(s_fx.ctx));

    /* A fully-closed modal (settled, declares no backdrop) does NOT count as present. */
    bool open = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    const bool declared = nt_ui_modal_visible(s_fx.ctx, MODAL_A, &st, &open);
    if (declared) {
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(nt_ui_modal_active(s_fx.ctx));
}

/* ---- Sequential SAME-DEPTH modals in one frame: a FULLY-CLOSED first must not steal top from an
 *      OPEN second at the same level. Top goes to the highest effective band, ties to the last PRESENT
 *      declaration, so next
 *      frame the second (open) modal's Esc fires and the first (closed) stays silent. ---- */
static void test_modal_closed_same_depth_does_not_steal_top(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F; /* instant: open -> t=1, closed -> t=0 fully closed */
    st.flags = NT_UI_MODAL_LISTEN_ESC;

    /* Frame 1: establish top. A declared first but CLOSED (no backdrop), B declared second and OPEN.
     * Both sit at depth 1 (sequential, not nested) since each balances before the next. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    bool a_open = false;
    const bool a_decl = nt_ui_modal_visible(s_fx.ctx, MODAL_A, &st, &a_open); /* closed -> balances itself */
    if (a_decl) {
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_modal_begin(s_fx.ctx, MODAL_B, &st, true); /* open at the same depth */
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_FALSE(a_decl); /* the closed modal declared no body */

    /* Frame 2: Esc held. The OPEN second modal (B) must be top and consume Esc. */
    nt_input_set_key(NT_KEY_ESCAPE, true);
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    a_open = false;
    const bool a_decl2 = nt_ui_modal_visible(s_fx.ctx, MODAL_A, &st, &a_open);
    if (a_decl2) {
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_modal_result_t b = nt_ui_modal_begin(s_fx.ctx, MODAL_B, &st, true);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_TRUE(b.close_requested); /* the open second modal is top -> Esc fires */
    TEST_ASSERT_EQUAL_INT(NT_UI_MODAL_CLOSE_ESC, b.reason);
}

/* ---- Per-context modal_zband_stride: a custom stride drives panel_z = stride*(depth+1). The default
 *      fixture ctx uses 1000; a second ctx created with a custom desc reflects the configured value.
 *      Module is already inited by setUp, so a second ctx coexists (mirrors test_nt_ui_multictx). ---- */
alignas(NT_UI_ARENA_ALIGN) static uint8_t s_custom_arena[NT_UI_TEST_ARENA_SIZE];

static void test_modal_custom_zband_stride(void) {
    const int16_t custom_stride = 500;
    nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    desc.modal_zband_stride = custom_stride;
    nt_ui_context_t *ctx = nt_ui_create_context(s_custom_arena, sizeof s_custom_arena, &desc);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    nt_pointer_t p = {.active = true};
    nt_ui_begin(ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    nt_ui_modal_begin(ctx, MODAL_A, &st, true); /* depth 0 -> stride*1 */
    CLAY({.id = CLAY_ID("cs_a"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    nt_ui_modal_begin(ctx, MODAL_B, &st, true); /* depth 1 -> stride*2 */
    CLAY({.id = CLAY_ID("cs_b"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    nt_ui_modal_end(ctx);
    nt_ui_modal_end(ctx);
    nt_ui_end(ctx);

    TEST_ASSERT_EQUAL_INT32((int32_t)(custom_stride * 1), band_of_rect(ctx, CLAY_ID("cs_a").id));
    TEST_ASSERT_EQUAL_INT32((int32_t)(custom_stride * 2), band_of_rect(ctx, CLAY_ID("cs_b").id));
    TEST_ASSERT_TRUE(band_has_backdrop(ctx, (custom_stride * 1) - 1));
    TEST_ASSERT_TRUE(band_has_backdrop(ctx, (custom_stride * 2) - 1));

    nt_ui_destroy_context(ctx);
}

/* ---- No 0->default fallback: the default lives only in nt_ui_create_desc_defaults() (same as
 *      max_elements). A 0 stride is invalid and must fail-early at create. ---- */
static void test_modal_zero_stride_asserts(void) {
    nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    desc.modal_zband_stride = 0;
    NT_TEST_EXPECT_ASSERT((void)nt_ui_create_context(s_custom_arena, sizeof s_custom_arena, &desc));
}

/* ---- An over-large stride (stride*MAX_DEPTH > INT16_MAX) trips the create-time NT_ASSERT
 *      (fail-early, no silent clamp). ---- */
static void test_modal_oversize_stride_asserts(void) {
    nt_ui_create_desc_t desc = nt_ui_create_desc_defaults();
    desc.modal_zband_stride = (int16_t)(INT16_MAX / NT_UI_MODAL_MAX_DEPTH + 1000);
    NT_TEST_EXPECT_ASSERT((void)nt_ui_create_context(s_custom_arena, sizeof s_custom_arena, &desc));
}

/* ---- SLIDE edge: each edge drives the panel offset on the right axis + sign. A finite ease keeps the
 *      first frame's t well below 1, so offset ~= anim.offset on the active axis and ~0 on the other.
 *      +x = right, +y = down; the panel starts displaced toward `edge`. ---- */
static void modal_slide_offset_frame(nt_ui_modal_edge_t edge, float *ox, float *oy) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F; /* finite: first frame t << 1 -> offset near anim.offset */
    st.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = (uint8_t)edge, .offset = 40.0F};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_A + (uint32_t)edge, &st, true); /* distinct id per edge -> each eases from a fresh slot */
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    nt_ui_modal_test_last_panel_offset(ox, oy);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_modal_slide_dir(void) {
    float ox = 0.0F;
    float oy = 0.0F;

    modal_slide_offset_frame(NT_UI_MODAL_BOTTOM, &ox, &oy);
    TEST_ASSERT_TRUE(oy > 0.0F); /* starts below center, rises */
    TEST_ASSERT_TRUE(float_near(ox, 0.0F, 0.0001F));

    modal_slide_offset_frame(NT_UI_MODAL_TOP, &ox, &oy);
    TEST_ASSERT_TRUE(oy < 0.0F); /* starts above center, descends */
    TEST_ASSERT_TRUE(float_near(ox, 0.0F, 0.0001F));

    modal_slide_offset_frame(NT_UI_MODAL_LEFT, &ox, &oy);
    TEST_ASSERT_TRUE(ox < 0.0F); /* starts left of center, slides right */
    TEST_ASSERT_TRUE(float_near(oy, 0.0F, 0.0001F));

    modal_slide_offset_frame(NT_UI_MODAL_RIGHT, &ox, &oy);
    TEST_ASSERT_TRUE(ox > 0.0F); /* starts right of center, slides left */
    TEST_ASSERT_TRUE(float_near(oy, 0.0F, 0.0001F));
}

/* ---- Asymmetric open/close: open=slide-from-bottom shows a vertical offset while opening; close=fade
 *      shows none (identity) while closing. Proves the per-phase recipe is picked by `open`. ---- */
static void test_modal_asymmetric_open_close(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F;
    st.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = NT_UI_MODAL_BOTTOM, .offset = 40.0F};
    st.close = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_FADE};
    float ox = 0.0F;
    float oy = 0.0F;

    /* Opening (open=true): slide recipe -> vertical offset present. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_B, &st, true);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    nt_ui_modal_test_last_panel_offset(&ox, &oy);
    TEST_ASSERT_TRUE(oy > 0.0F); /* slide-from-bottom while opening */

    /* Closing (open=false): fade recipe -> identity, no offset regardless of t. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_B, &st, false);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    nt_ui_modal_test_last_panel_offset(&ox, &oy);
    TEST_ASSERT_TRUE(float_near(ox, 0.0F, 0.0001F)); /* fade close -> no offset */
    TEST_ASSERT_TRUE(float_near(oy, 0.0F, 0.0001F));
}

/* ---- Invalid anim recipes fail early at begin. Asserts run BEFORE the push, so a trapped begin leaves
 *      the stack balanced and the next case runs in the same frame. ---- */
static void test_modal_anim_asserts(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 0.0F;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);

    nt_ui_modal_style_t bad = st;
    bad.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SCALE_POP, .scale_start = 0.0F};
    NT_TEST_EXPECT_ASSERT(nt_ui_modal_begin(s_fx.ctx, MODAL_A, &bad, true)); /* SCALE_POP scale_start must be > 0 */

    bad = st;
    bad.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = NT_UI_MODAL_BOTTOM, .offset = NAN};
    NT_TEST_EXPECT_ASSERT(nt_ui_modal_begin(s_fx.ctx, MODAL_A, &bad, true)); /* SLIDE offset must be finite */

    bad = st;
    bad.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = 4, .offset = 10.0F};
    NT_TEST_EXPECT_ASSERT(nt_ui_modal_begin(s_fx.ctx, MODAL_A, &bad, true)); /* edge out of range */

    bad = st;
    bad.open.type = 7;
    NT_TEST_EXPECT_ASSERT(nt_ui_modal_begin(s_fx.ctx, MODAL_A, &bad, true)); /* type out of range */

    nt_ui_end(s_fx.ctx);
}

/* ---- The close phase uses style.close, not style.open: open=slide-LEFT (offset_x < 0 opening),
 *      close=slide-RIGHT (offset_x > 0 closing). A bug reading style.open on close keeps the sign < 0. ---- */
static void test_modal_close_uses_close_recipe(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    st.ease_speed = 8.0F;
    st.open = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = NT_UI_MODAL_LEFT, .offset = 40.0F};
    st.close = (nt_ui_modal_anim_t){.type = NT_UI_MODAL_ANIM_SLIDE, .edge = NT_UI_MODAL_RIGHT, .offset = 40.0F};
    float ox = 0.0F;
    float oy = 0.0F;

    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_B, &st, true);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    nt_ui_modal_test_last_panel_offset(&ox, &oy);
    TEST_ASSERT_TRUE(ox < 0.0F); /* open recipe = LEFT */

    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_B, &st, false);
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    nt_ui_modal_test_last_panel_offset(&ox, &oy);
    TEST_ASSERT_TRUE(ox > 0.0F); /* close recipe = RIGHT (from style.close) */
}

/* ---- Delegation proof: the modal's shipped band equals the popup's for the same depth, i.e. the modal
 *      is re-expressed ON popup-core, not a divergent parallel copy of the z-band math. The catcher half
 *      is unmeasurable here on purpose: a plain popup's catcher is transparent and emits no command. ---- */
static void test_modal_zband_matches_popup_core(void) {
    nt_ui_modal_style_t mst = nt_ui_modal_style_defaults();
    mst.ease_speed = 0.0F;
    nt_ui_popup_style_t pst = nt_ui_popup_style_defaults();
    pst.ease_speed = 0.0F;
    nt_ui_popup_anchor_t panc = {.prefer_side = NT_UI_POPUP_CENTER};
    nt_pointer_t p = {.active = true};

    /* Modal at depth 0 -> panel z. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    nt_ui_modal_begin(s_fx.ctx, MODAL_A, &mst, true);
    CLAY({.id = CLAY_ID("dele_body"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    nt_ui_modal_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t modal_z = band_of_rect(s_fx.ctx, CLAY_ID("dele_body").id);
    TEST_ASSERT_TRUE_MESSAGE(modal_z != INT32_MIN, "the modal body must emit a rectangle to measure");
    const bool modal_backdrop = band_has_backdrop(s_fx.ctx, modal_z - 1);

    /* Popup at depth 0 -> the same panel band (one shared depth counter + stride). */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    nt_ui_popup_begin(s_fx.ctx, MODAL_A, &pst, &panc, true);
    CLAY({.id = CLAY_ID("dele_body"), .layout = {.sizing = {CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(20)}}, .backgroundColor = {8, 8, 8, 255}}) {}
    nt_ui_popup_end(s_fx.ctx);
    nt_ui_end(s_fx.ctx);
    const int32_t popup_z = band_of_rect(s_fx.ctx, CLAY_ID("dele_body").id);
    TEST_ASSERT_TRUE_MESSAGE(popup_z != INT32_MIN, "the popup body must emit a rectangle to measure");

    TEST_ASSERT_EQUAL_INT32(popup_z, modal_z);
    TEST_ASSERT_TRUE_MESSAGE(modal_backdrop, "the modal backdrop paints one band under its panel");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_modal_abi_sizes);
    RUN_TEST(test_modal_zband_matches_popup_core);
    RUN_TEST(test_modal_defaults_valid);
    RUN_TEST(test_modal_floating_smoke);
    RUN_TEST(test_modal_zband_and_nesting);
    RUN_TEST(test_modal_stack_depth_zero_balanced);
    RUN_TEST(test_modal_depth_overflow_asserts);
    RUN_TEST(test_modal_esc_top_only);
    RUN_TEST(test_modal_esc_requires_flag);
    RUN_TEST(test_modal_tween_clamp_and_transition);
    RUN_TEST(test_modal_open_entrance_eases_from_zero);
    RUN_TEST(test_modal_open_visible_before_epsilon);
    RUN_TEST(test_modal_open_instant_snaps);
    RUN_TEST(test_modal_wrapper_contract);
    RUN_TEST(test_modal_wants_pointer_while_open);
    RUN_TEST(test_modal_no_backdrop_close_still_gates);
    RUN_TEST(test_modal_body_button_clickable);
    RUN_TEST(test_modal_backdrop_close_pad);
    RUN_TEST(test_modal_closed_does_not_block_base);
    RUN_TEST(test_modal_active_prev_frame);
    RUN_TEST(test_modal_closed_same_depth_does_not_steal_top);
    RUN_TEST(test_modal_slide_dir);
    RUN_TEST(test_modal_asymmetric_open_close);
    RUN_TEST(test_modal_close_uses_close_recipe);
    RUN_TEST(test_modal_anim_asserts);
    RUN_TEST(test_modal_custom_zband_stride);
    RUN_TEST(test_modal_zero_stride_asserts);
    RUN_TEST(test_modal_oversize_stride_asserts);
    return UNITY_END();
}
