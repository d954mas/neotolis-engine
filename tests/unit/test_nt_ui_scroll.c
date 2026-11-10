/* Scroll container + custom engine physics tests.
 *
 * Physics (momentum decay, clamp, rubber-band, scroll-to, Clay sign) drive the
 * static integrator directly via the NT_TEST_ACCESS probe — no Clay frame needed.
 * Container and gesture cases declare a real scroll element and read back its state
 * or the laid-out content. UNITY_EXCLUDE_FLOAT: compare via int32 casts / eps. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_bar_probe.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_scroll.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

static const nt_ui_scroll_style_t s_physics_style = {
    .friction = 0.92F,
    .wheel_ease_speed = 18.0F,
    .rubber_band_c = 0.55F,
    .bounce_speed = 12.0F,
    .wheel_step_px = 40.0F,
};

static void arb_scene_reset(void);

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    arb_scene_reset();
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* ---- Test 1: momentum decays monotonically frame-over-frame ---- */
static void test_scroll_momentum_decays_monotonic(void) {
    /* Long content so the whole fling stays in-bounds (no bounce interfering). */
    const float content[2] = {0.0F, 100000.0F};
    const float container[2] = {0.0F, 400.0F};
    const float no_wheel[2] = {0.0F, 0.0F};
    nt_ui_scroll_state_t s = {0};
    s.vel[1] = -3000.0F; /* fling up (content moves up = negative) */

    float prev_speed = fabsf(s.vel[1]);
    for (int i = 0; i < 60; ++i) {
        nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, &s_physics_style, false);
        const float speed = fabsf(s.vel[1]);
        TEST_ASSERT_TRUE(speed <= prev_speed + 0.001F); /* never speeds up */
        prev_speed = speed;
    }
    /* Decayed essentially to rest. */
    TEST_ASSERT_TRUE(fabsf(s.vel[1]) < 50.0F);
    /* Flung in the negative (down-scroll) direction. */
    TEST_ASSERT_TRUE(s.pos[1] < 0.0F);
}

/* ---- Test 2: pos stays within [min,0] after settle (clamp holds) ---- */
static void test_scroll_clamp_holds(void) {
    const float content[2] = {0.0F, 1000.0F};
    const float container[2] = {0.0F, 400.0F};
    const float no_wheel[2] = {0.0F, 0.0F};
    const float lo = -(1000.0F - 400.0F); /* -600 */
    nt_ui_scroll_state_t s = {0};
    s.vel[1] = -100000.0F; /* huge fling, would overshoot far past the edge */

    for (int i = 0; i < 240; ++i) {
        nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, &s_physics_style, false);
    }
    TEST_ASSERT_TRUE(s.pos[1] >= lo - 0.5F);
    TEST_ASSERT_TRUE(s.pos[1] <= 0.0F + 0.5F);
    /* Settled at the far edge (flung well past it). */
    TEST_ASSERT_TRUE(float_near(s.pos[1], lo, 1.0F));
}

/* ---- Test 3: rubber-band returns a compressed magnitude < raw d ---- */
static void test_scroll_rubber_band_compresses(void) {
    const float dim = 400.0F;
    const float raw = 300.0F;
    const float r = nt_ui_scroll_test_rubber_band(raw, dim);
    TEST_ASSERT_TRUE(r > 0.0F);
    TEST_ASSERT_TRUE(r < raw); /* asymptotic compression */
    /* Sign preserved for negative overscroll. */
    const float rn = nt_ui_scroll_test_rubber_band(-raw, dim);
    TEST_ASSERT_TRUE(rn < 0.0F);
    TEST_ASSERT_TRUE(float_near(rn, -r, 0.001F));
    /* Larger overscroll => more compression ratio (never linear). */
    const float r2 = nt_ui_scroll_test_rubber_band(2.0F * raw, dim);
    TEST_ASSERT_TRUE(r2 < 2.0F * r);
}

/* ---- Test 4: scroll-to reaches target within N frames and clears the flag ---- */
static void test_scroll_to_reaches_target(void) {
    const float content[2] = {0.0F, 2000.0F};
    const float container[2] = {0.0F, 400.0F};
    const float no_wheel[2] = {0.0F, 0.0F};
    nt_ui_scroll_state_t s = {0};
    s.target[1] = -500.0F; /* scroll down to offset -500 */
    s.flags |= NT_UI_SCROLL_FLAG_SCROLL_TO;

    int frames = 0;
    while ((s.flags & NT_UI_SCROLL_FLAG_SCROLL_TO) != 0U && frames < 600) {
        nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, &s_physics_style, false);
        ++frames;
    }
    TEST_ASSERT_TRUE(frames < 600);                                     /* arrived */
    TEST_ASSERT_EQUAL_UINT8(0U, s.flags & NT_UI_SCROLL_FLAG_SCROLL_TO); /* flag cleared */
    TEST_ASSERT_TRUE(float_near(s.pos[1], -500.0F, 1.0F));              /* near target */
}

/* ---- Test 5: Clay sign — a positive (Clay-sign) wheel down moves childOffset negative ---- */
static void test_scroll_wheel_sign(void) {
    /* The input edge (begin) negates wheel_dy into Clay sign before integrate. Here we
     * pin that a NEGATIVE wheel[] (scroll content down) drives pos negative (down/right). */
    const float content[2] = {0.0F, 4000.0F};
    const float container[2] = {0.0F, 400.0F};
    const float wheel_down[2] = {0.0F, -1.0F}; /* one wheel notch, Clay sign (down) */
    nt_ui_scroll_state_t s = {0};

    /* Run enough frames for the eased pos to follow target. */
    for (int i = 0; i < 120; ++i) {
        const float none[2] = {0.0F, 0.0F};
        nt_ui_scroll_test_integrate(&s, (i == 0) ? wheel_down : none, 1.0F / 60.0F, content, container, &s_physics_style, false);
    }
    /* target moved by wheel * wheel_step = -40; pos eased toward it (negative). */
    TEST_ASSERT_TRUE(float_near(s.target[1], -40.0F, 0.001F));
    TEST_ASSERT_TRUE(s.pos[1] < 0.0F);
    TEST_ASSERT_TRUE(float_near(s.pos[1], -40.0F, 1.0F));
}

/* ---- Container frame helper: a 200x200 scroll over 200x1000 content (y overflows). ---- */
static const uint32_t SCROLL_ID = 0x5C0011U;

static void scroll_frame_dt(nt_pointer_t *p, const nt_ui_scroll_style_t *style, float dt) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = dt, .pointers = p, .pointer_count = 1});
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, style, NULL);
        {
            /* Tall content forces y overflow so the container can scroll. */
            CLAY({.id = CLAY_ID("content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(1000)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

static void scroll_frame(nt_pointer_t *p, const nt_ui_scroll_style_t *style) { scroll_frame_dt(p, style, 1.0F / 60.0F); }

static nt_ui_interaction_t scroll_child_frame(nt_pointer_t *p, const nt_ui_scroll_style_t *style) {
    nt_ui_interaction_t interaction = {0};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = 1.0F / 60.0F, .pointers = p, .pointer_count = 1});
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, style, NULL);
        CLAY({.id = CLAY_ID("interactive_content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(1000)}}}) {
            interaction = nt_ui_step(s_fx.ctx, CLAY_ID("interactive_content").id, NULL);
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    return interaction;
}

static void test_scroll_release_crossing_threshold_allows_next_press(void) {
    const nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {.x = 100.0F, .y = 100.0F, .active = true};
    scroll_child_frame(&p, &style);
    scroll_child_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    TEST_ASSERT_TRUE(scroll_child_frame(&p, &style).pressed);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    p.y = 96.0F;
    scroll_child_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_down = false;
    p.buttons[NT_BUTTON_LEFT].is_released = true;
    p.y = 80.0F;
    TEST_ASSERT_FALSE(scroll_child_frame(&p, &style).clicked);
    /* The scroll owns the release frame, so the game does not see a world release there. */
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[0].active_id);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx));

    p.buttons[NT_BUTTON_LEFT].is_released = false;
    scroll_child_frame(&p, &style);
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->captures[0].active_id); /* nothing carried past the release */
    for (int i = 0; i < 180; ++i) {
        scroll_child_frame(&p, &style); /* let the release fling settle: a press on a fling only stops it */
    }
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    p.y = 120.0F;
    TEST_ASSERT_TRUE(scroll_child_frame(&p, &style).pressed);
    TEST_ASSERT_EQUAL_UINT32(CLAY_ID("interactive_content").id, s_fx.ctx->captures[0].active_id);
}

static void test_scroll_hard_edges_stop_release_fling(void) {
    const nt_ui_scroll_style_t style = {.friction = 0.92F, .wheel_step_px = 40.0F};
    nt_ui_scroll_state_t s = {.pos = {-1.0F, -799.0F}, .target = {-1.0F, -799.0F}, .vel = {1000.0F, -1000.0F}};
    const float content[2] = {500.0F, 1000.0F};
    const float container[2] = {200.0F, 200.0F};
    const float wheel[2] = {0.0F, 0.0F};
    nt_ui_scroll_test_integrate(&s, wheel, 1.0F / 60.0F, content, container, &style, false);
    TEST_ASSERT_TRUE(float_near(s.pos[0], 0.0F, 0.001F));
    TEST_ASSERT_TRUE(float_near(s.pos[1], -800.0F, 0.001F));
    TEST_ASSERT_TRUE(float_near(s.vel[0], 0.0F, 0.001F));
    TEST_ASSERT_TRUE(float_near(s.vel[1], 0.0F, 0.001F));
}

static void test_scroll_takes_real_child_and_tracks_the_pointer(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    style.rubber_band_c = 0.0F;
    nt_pointer_t p = {.x = 100.0F, .y = 100.0F, .active = true};
    scroll_child_frame(&p, &style);
    scroll_child_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_child_frame(&p, &style);
    TEST_ASSERT_EQUAL_UINT32(CLAY_ID("interactive_content").id, s_fx.ctx->captures[0].active_id);

    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    p.y = 80.0F;
    scroll_child_frame(&p, &style);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[0].active_id);
    const nt_ui_scroll_state_t *s = nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->raw[1], -12.0F, 0.001F)); /* the 20 px move minus the 8 dp slop */
    p.y = 60.0F;
    scroll_child_frame(&p, &style);
    s = nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_TRUE(float_near(s->raw[1], -32.0F, 0.001F)); /* then 1:1 */
}

static void test_scroll_threshold_ignores_disabled_axis(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    style.rubber_band_c = 0.0F;
    nt_pointer_t p = {.x = 100.0F, .y = 100.0F, .active = true};
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    p.x = 150.0F;
    p.y = 93.0F;
    scroll_frame(&p, &style);
    const nt_ui_scroll_state_t *s = nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->raw[1], 0.0F, 0.001F));
    p.y = 91.0F;
    scroll_frame(&p, &style);
    s = nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_TRUE(float_near(s->raw[1], -1.0F, 0.001F));
    p.y = 89.0F;
    scroll_frame(&p, &style);
    s = nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_TRUE(float_near(s->raw[1], -3.0F, 0.001F));
}

/* ---- Test 6: wheel applied ONCE — one notch moves childOffset by the wheel contribution ---- */
static void test_scroll_wheel_applied_once(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    style.wheel_ease_speed = 0.0F; /* instant ease so one frame lands exactly on target */
    style.wheel_step_px = 40.0F;

    nt_pointer_t p = {0};
    p.x = 100.0F; /* inside the 200x200 container */
    p.y = 100.0F;
    p.active = true;
    /* Two frames: the list must be laid out and registered before the pointer can be over it. */
    scroll_frame(&p, &style);
    scroll_frame(&p, &style);
    /* Frame 3: one wheel notch down, consumed the same frame. */
    p.wheel_dy = 1.0F; /* input-edge negates to Clay -1 -> target -40 */
    scroll_frame(&p, &style);

    const nt_ui_scroll_state_t *st = (const nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    /* Exactly ONE wheel contribution (-1 * 40); a Clay double-drive would be -80. */
    TEST_ASSERT_TRUE(float_near(st->pos[1], -40.0F, 0.5F));
    TEST_ASSERT_TRUE(float_near(st->pos[0], 0.0F, 0.001F)); /* y-only style */
}

/* ---- Test 7: childOffset fed from our pos (scroll_to drives the emitted offset) ---- */
static void test_scroll_childoffset_from_our_pos(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    style.wheel_ease_speed = 0.0F; /* scroll-to lands in one eased step (k clamps to 1) */

    nt_pointer_t p = {0};
    p.active = true;
    scroll_frame(&p, &style); /* create the container + state cell */

    nt_ui_scroll_to(s_fx.ctx, SCROLL_ID, 0.0F, -150.0F);
    scroll_frame(&p, &style); /* integrate eases pos toward target, feeds childOffset */

    const nt_ui_bbox_t content = nt_ui_get_bbox(s_fx.ctx, CLAY_ID("content").id);
    TEST_ASSERT_TRUE(content.found && float_near(content.y, -150.0F, 1.0F)); /* the layout got our pos */
}

/* ---- a scroll claim is not a gesture press: it must not report the hold left in its pointer slot ---- */
static void assert_scroll_claim_reports_no_hold(void) {
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
    TEST_ASSERT_TRUE(float_near(nt_ui_query_interaction(s_fx.ctx, SCROLL_ID).hold_secs, 0.0F, 0.001F));
}

static void leave_stale_hold_in_slot(void) {
    s_fx.ctx->captures[0].hold_secs = 0.9F;
    s_fx.ctx->captures[0].hold_prev_secs = 0.9F;
    s_fx.ctx->captures[0].hold_live = true;
}

static void test_scroll_empty_press_claims_report_no_hold(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style);

    /* Button released in this slot, then a drag on the scroll: the candidate claim. */
    leave_stale_hold_in_slot();
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    for (int i = 1; i <= 6; ++i) {
        p.y = 100.0F - (float)(i * 10);
        scroll_frame(&p, &style);
    }
    assert_scroll_claim_reports_no_hold();

    p.buttons[NT_BUTTON_LEFT].is_down = false;
    p.buttons[NT_BUTTON_LEFT].is_released = true;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_released = false;

    /* A press while the fling still runs: the tap-to-stop claim. */
    leave_stale_hold_in_slot();
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame(&p, &style);
    assert_scroll_claim_reports_no_hold();
}

/* ================= Scrollbar visual cases ================= */

/* A scroll frame with a chosen style + content height so the bar emit can be exercised.
 * Container 200x200; content 200 x content_h. */
static void scrollbar_frame(nt_pointer_t *p, const nt_ui_scroll_style_t *style, float content_h) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = 1.0F / 60.0F, .pointers = p, .pointer_count = 1});
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, style, NULL);
        {
            CLAY({.id = CLAY_ID("content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(content_h)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* A style with track + thumb art bound from the fixture white region, y-only. */
static nt_ui_scroll_style_t bar_style(nt_ui_scrollbar_visibility_t vis) {
    const nt_atlas_region_ref_t art = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    style.scroll_y = true;
    style.scroll_x = false;
    style.bar_visibility = vis;
    style.track_ref = art;
    style.thumb_ref = art;
    return style;
}

/* ---- Test 10: AUTO emits the bar only when content overflows the container. ---- */
static void test_scrollbar_auto_only_on_overflow(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_AUTO);
    nt_pointer_t p = {0};
    p.active = true;

    /* Content SHORTER than container (150 < 200): no overflow -> bar hidden. */
    scrollbar_frame(&p, &style, 150.0F);
    scrollbar_frame(&p, &style, 150.0F); /* second frame: dims solved */
    TEST_ASSERT_FALSE(ui_bar_declared(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1)));

    /* Content LONGER than container (1000 > 200): overflow -> bar shown. */
    scrollbar_frame(&p, &style, 1000.0F);
    scrollbar_frame(&p, &style, 1000.0F);
    TEST_ASSERT_TRUE(ui_bar_declared(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1)));
}

/* ---- Test 11: ALWAYS emits the bar even without overflow. ---- */
static void test_scrollbar_always_shows(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    nt_pointer_t p = {0};
    p.active = true;
    scrollbar_frame(&p, &style, 150.0F); /* no overflow */
    scrollbar_frame(&p, &style, 150.0F);
    TEST_ASSERT_TRUE(ui_bar_declared(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1)));
}

/* ---- A standalone scroll (no floating encloses it) must not lift anything: with zIndex inherited from
 *      the enclosing floating, "no enclosing floating" means every command stays in band 0. ---- */
static void test_scrollbar_standalone_no_z_lift(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    nt_pointer_t p = {0};
    p.active = true;
    scrollbar_frame(&p, &style, 1000.0F); /* overflow */
    scrollbar_frame(&p, &style, 1000.0F);
    TEST_ASSERT_TRUE(ui_bar_declared(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1)));
    /* The bar's own band: Clay stamps root->zIndex on the clipTo SCISSOR that opens the bar's floating
     * root, which is the command immediately preceding the bar's art (image commands are stamped 0). */
    const Clay_RenderCommandArray *arr = &s_fx.ctx->frozen_cmds;
    const uint32_t bar_id = nt_ui_scroll_test_bar_id(SCROLL_ID, 1);
    int32_t last_scissor_z = INT32_MIN;
    int32_t bar_band = INT32_MIN;
    bool bar_found = false;
    for (int32_t i = 0; i < arr->length; ++i) {
        const Clay_RenderCommand *c = &arr->internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
            last_scissor_z = c->zIndex;
        }
        if (c->id == bar_id) {
            bar_found = true;
            bar_band = last_scissor_z;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(bar_found, "standalone scrollbar must reach the render commands");
    TEST_ASSERT_TRUE_MESSAGE(bar_band != INT32_MIN, "the bar's clipTo SCISSOR must precede its art");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, bar_band, "standalone scroll bar must stay in z band 0");
}

/* ---- Test 12: thumb length is proportional to container/content (clamped to min). ---- */
static void test_scrollbar_thumb_size_ratio(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    style.bar_thumb_min_px = 1.0F; /* tiny min so the ratio is not clamped */
    nt_pointer_t p = {0};
    p.active = true;

    /* container 200, content 800 -> thumb = track_len * 200/800 = 0.25 * track. */
    scrollbar_frame(&p, &style, 800.0F);
    scrollbar_frame(&p, &style, 800.0F);
    float thumb_len = 0.0F;
    float track_len = 0.0F;
    {
        const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
        thumb_len = bar_probe.thumb_len;
        track_len = bar_probe.track_len;
    }
    TEST_ASSERT_TRUE(track_len > 0.0F);
    TEST_ASSERT_TRUE(float_near(thumb_len, 0.25F * track_len, 2.0F));
}

/* ---- Test 13: Clay sign — at rest (pos 0) the thumb sits at the TOP (offset 0); a
 *      negative pos (scrolled down) puts the thumb toward the BOTTOM. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scrollbar_thumb_pos_clay_sign(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    style.bar_thumb_min_px = 1.0F;
    style.wheel_ease_speed = 0.0F; /* instant so scroll-to lands in one step */
    nt_pointer_t p = {0};
    p.active = true;

    /* At rest: pos 0 -> thumb offset 0 (top). */
    scrollbar_frame(&p, &style, 800.0F);
    scrollbar_frame(&p, &style, 800.0F);
    float off_top = -1.0F;
    {
        const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
        off_top = bar_probe.thumb_off;
    }
    TEST_ASSERT_TRUE(float_near(off_top, 0.0F, 0.5F));

    /* Scroll to the bottom (max negative offset = -(content-container) = -600). */
    nt_ui_scroll_to(s_fx.ctx, SCROLL_ID, 0.0F, -600.0F);
    scrollbar_frame(&p, &style, 800.0F);
    scrollbar_frame(&p, &style, 800.0F);
    float off_bot = 0.0F;
    float thumb_len = 0.0F;
    float track_len = 0.0F;
    {
        const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
        thumb_len = bar_probe.thumb_len;
        off_bot = bar_probe.thumb_off;
        track_len = bar_probe.track_len;
    }
    /* Bottom end: thumb offset == track_len - thumb_len. */
    TEST_ASSERT_TRUE(float_near(off_bot, track_len - thumb_len, 2.0F));
    TEST_ASSERT_TRUE(off_bot > off_top); /* moved DOWN the track */
}

/* ---- Test 14: a track click (off the thumb) calls nt_ui_scroll_to toward the clicked
 *      fraction — clicking near the bottom of the track scrolls the offset negative. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scrollbar_track_click_scroll_to(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    style.bar_thumb_min_px = 10.0F;
    nt_pointer_t p = {0};
    p.active = true;

    /* Establish the bar bbox (two frames so the bar id has a prev-frame bbox). */
    scrollbar_frame(&p, &style, 800.0F);
    scrollbar_frame(&p, &style, 800.0F);

    /* The container spans y=[0,200]; the vertical bar runs the same y range on the right
     * edge (x near 200-thickness..200). Click near the BOTTOM of the track, off the thumb
     * (which is at the top). */
    p.x = 195.0F; /* inside the right-edge bar (thickness 12) */
    p.y = 190.0F; /* near the bottom */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scrollbar_frame(&p, &style, 800.0F);

    /* Release. */
    p.buttons[NT_BUTTON_LEFT].is_down = false;
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    p.buttons[NT_BUTTON_LEFT].is_released = true;
    scrollbar_frame(&p, &style, 800.0F);
    p.buttons[NT_BUTTON_LEFT].is_released = false;

    /* Let the smooth scroll-to ease toward the clicked (bottom) fraction. */
    for (int i = 0; i < 120; ++i) {
        scrollbar_frame(&p, &style, 800.0F);
    }
    /* Clicked near the bottom -> scrolled the offset substantially negative (down). */
    TEST_ASSERT_TRUE(((const nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID))->pos[1] < -50.0F);
}

/* ---- Test 15: AUTO_HIDE fades toward 0 after idle (the eased fade opacity drops). ---- */
static void test_scrollbar_auto_hide_fades(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_AUTO_HIDE);
    style.bar_fade_speed = 8.0F;
    nt_pointer_t p = {0};
    p.active = true; /* pointer NOT over the bar -> idle -> fade target 0 */
    p.x = 10.0F;
    p.y = 10.0F;

    /* Many idle frames: the fade eases toward 0. */
    float last_op = 1.0F;
    for (int i = 0; i < 120; ++i) {
        scrollbar_frame(&p, &style, 800.0F);
        {
            const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
            last_op = bar_probe.opacity;
        }
    }
    TEST_ASSERT_TRUE(last_op < 0.2F); /* faded out while idle */
}

/* ---- Test 16: the per-axis bar id derives from the scroll id (own id for own anim). ---- */
static void test_scrollbar_id_derivation_distinct(void) {
    const uint32_t vy = nt_ui_scroll_test_bar_id(SCROLL_ID, 1);
    const uint32_t vx = nt_ui_scroll_test_bar_id(SCROLL_ID, 0);
    TEST_ASSERT_TRUE(vy != SCROLL_ID);
    TEST_ASSERT_TRUE(vx != SCROLL_ID);
    TEST_ASSERT_TRUE(vy != vx); /* the two axes never collide */
}

/* ================= UX-fix round (empty-area press drag + scroll-to-from-button) ================= */

static const uint32_t SCROLL_ID_A = 0x5C00A1U;
static const uint32_t SCROLL_ID_B = 0x5C00B2U;

/* Two scroll containers side by side, mirroring the demo's twin lists; an OPTIONAL "scroll
 * both to top" call runs AFTER both are declared (exactly where the demo's button-end fires
 * nt_ui_scroll_to). do_scroll_to gates the call so we can establish state first. */
static void twin_scroll_frame(nt_pointer_t *p, const nt_ui_scroll_style_t *style, bool do_scroll_to) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = 1.0F / 60.0F, .pointers = p, .pointer_count = 1});
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(500), CLAY_SIZING_FIXED(200)}, .layoutDirection = CLAY_LEFT_TO_RIGHT}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID_A, style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}});
        {
            CLAY({.id = CLAY_ID("contentA"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(1000)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID_B, style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}});
        {
            CLAY({.id = CLAY_ID("contentB"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(1000)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    /* Demo ordering: the scroll-to call fires DURING declaration, after both containers. */
    if (do_scroll_to) {
        nt_ui_scroll_to(s_fx.ctx, SCROLL_ID_A, 0.0F, 0.0F);
        nt_ui_scroll_to(s_fx.ctx, SCROLL_ID_B, 0.0F, 0.0F);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 17: "scroll both to top" — the DEMO sequence. Scroll both down, then call
 *      nt_ui_scroll_to(id,0,0) after declaration; both offsets must ease back to 0. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_to_top_from_button_sequence(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.active = true;

    /* Establish both containers + their state cells. */
    twin_scroll_frame(&p, &style, false);

    /* Scroll both down to the bottom (max negative offset). */
    nt_ui_scroll_to(s_fx.ctx, SCROLL_ID_A, 0.0F, -600.0F);
    nt_ui_scroll_to(s_fx.ctx, SCROLL_ID_B, 0.0F, -600.0F);
    for (int i = 0; i < 120; ++i) {
        twin_scroll_frame(&p, &style, false);
    }
    nt_ui_scroll_state_t *sa = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID_A);
    nt_ui_scroll_state_t *sb = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID_B);
    TEST_ASSERT_NOT_NULL(sa);
    TEST_ASSERT_NOT_NULL(sb);
    TEST_ASSERT_TRUE(sa->pos[1] < -100.0F); /* actually scrolled down */
    TEST_ASSERT_TRUE(sb->pos[1] < -100.0F);

    /* Now the "scroll both to top" button: scroll_to(0,0) fires after declaration. */
    twin_scroll_frame(&p, &style, true);
    /* Let the ease run. */
    for (int i = 0; i < 120; ++i) {
        twin_scroll_frame(&p, &style, false);
    }
    sa = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID_A);
    sb = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID_B);
    TEST_ASSERT_NOT_NULL(sa);
    TEST_ASSERT_NOT_NULL(sb);
    TEST_ASSERT_TRUE(float_near(sa->pos[1], 0.0F, 1.0F)); /* eased back to top */
    TEST_ASSERT_TRUE(float_near(sb->pos[1], 0.0F, 1.0F));
}

/* ---- Test 18: empty-area press drag (no inner widget capture) scrolls the container, and the
 *      release leaves momentum so a fling continues past the lifted finger. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_empty_press_drag_and_fling(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style); /* establish bbox + state cell */

    /* Finger DOWN inside the container, no widget captures it (empty content). */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    p.x = 100.0F;
    p.y = 100.0F;
    scroll_frame(&p, &style); /* press frame: anchor set */
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;

    /* Drag UP 60 px over several frames (content scrolls, pos goes negative). */
    for (int i = 1; i <= 6; ++i) {
        p.y = 100.0F - (float)(i * 10);
        scroll_frame(&p, &style);
    }
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->pos[1] < -1.0F);         /* dragged the content */
    TEST_ASSERT_TRUE(fabsf(s->vel[1]) > 100.0F); /* drag built velocity for a fling */

    /* Finger UP: empty-area press ends; momentum carries on. */
    const float pos_at_release = s->pos[1];
    p.buttons[NT_BUTTON_LEFT].is_down = false;
    p.buttons[NT_BUTTON_LEFT].is_released = true;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_released = false;
    for (int i = 0; i < 10; ++i) {
        scroll_frame(&p, &style);
    }
    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->pos[1] < pos_at_release - 1.0F); /* flung further than the release point */
}

/* ---- Test 19: a empty-area press TAP (below threshold) does NOT scroll the container. ---- */
static void test_scroll_empty_press_tap_no_scroll(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style);

    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;

    /* A 3 px wobble — below the 8 px threshold. */
    p.y = 103.0F;
    p.x = 101.0F;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_down = false;
    p.buttons[NT_BUTTON_LEFT].is_released = true;
    scroll_frame(&p, &style);

    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->pos[1], 0.0F, 0.5F)); /* tap left it at rest */
}

/* Pointer `pidx` owns scroll `id` by its thumb on `axis`. */
static void assert_thumb_owner(uint32_t pidx, uint32_t id, int axis) {
    TEST_ASSERT_EQUAL_UINT32(id, s_fx.ctx->captures[pidx].active_id);
    const nt_ui_scroll_state_t *st = (const nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, id);
    TEST_ASSERT_NOT_NULL(st);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(axis + 1), st->bar_axis);
}

/* ---- Test 20: the container does NOT take its OWN scrollbar's thumb drag: a real press on the
 *      thumb dragged past the slop stays a thumb drag, which moves the content. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_does_not_take_own_scrollbar(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);

    nt_pointer_t p = {0};
    p.x = 195.0F; /* on the thumb of the right-edge vertical bar */
    p.y = 20.0F;
    p.active = true;
    scrollbar_frame(&p, &style, 1000.0F);
    scrollbar_frame(&p, &style, 1000.0F);
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scrollbar_frame(&p, &style, 1000.0F);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    p.y = 60.0F; /* 40 down the bar, well past the slop */
    scrollbar_frame(&p, &style, 1000.0F);

    assert_thumb_owner(0U, SCROLL_ID, 1);
    const nt_ui_scroll_state_t *s = (const nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_TRUE(s->pos[1] < -1.0F); /* the thumb drove the content down */
}

/* IDs for the nested outer+inner scroll used by the nested-scrollbar exclusion test. */
static const uint32_t EXCL_OUTER_ID = 0x5CE001U;
static const uint32_t EXCL_INNER_ID = 0x5CE002U;

/* Outer scroll (300x300 over 300x1000) holding an INNER scroll (200x200 over 200x800) with bars,
 * pinned at the outer content's top-left. */
static void excl_nested_frame2(nt_pointer_t *p, const nt_ui_scroll_style_t *outer, const nt_ui_scroll_style_t *inner) {
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = 1.0F / 60.0F, .pointers = p, .pointer_count = 1});
    CLAY({.id = CLAY_ID("excl-root"), .layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(300)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, EXCL_OUTER_ID, outer, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(300)}}});
        {
            CLAY({.id = CLAY_ID("excl-outer-content"), .layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(1000)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
                nt_ui_scroll_begin(s_fx.ctx, NULL, EXCL_INNER_ID, inner, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}});
                {
                    CLAY({.id = CLAY_ID("excl-inner-content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(800)}}}) {}
                }
                nt_ui_scroll_end(s_fx.ctx);
            }
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 20b: a parent scroll does NOT take a nested scroll's thumb drag along its own axis:
 *      a thumb press owns its own scroll at once. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_does_not_take_nested_scrollbar(void) {
    /* The outer bar is drawn before the inner scroll declares: the inner still reads its own dims. */
    const nt_ui_scroll_style_t outer = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    const nt_ui_scroll_style_t inner = bar_style(NT_UI_SCROLLBAR_ALWAYS);

    nt_pointer_t p = {0};
    p.x = 195.0F; /* on the inner scroll's vertical thumb, inside the outer */
    p.y = 20.0F;
    p.active = true;
    excl_nested_frame2(&p, &outer, &inner);
    excl_nested_frame2(&p, &outer, &inner);
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    excl_nested_frame2(&p, &outer, &inner);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    p.y = 60.0F; /* 40 down: the outer scroll could take it on y, the thumb keeps it */
    excl_nested_frame2(&p, &outer, &inner);

    assert_thumb_owner(0U, EXCL_INNER_ID, 1);
}

/* ---- Test 21: empty-area press drag must NOT hijack a click on a non-scrolling tap. A press inside
 *      the container that releases before crossing the threshold scrolls nothing AND a free
 *      press that started OUTSIDE the bbox never scrolls (anchor gate). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_empty_press_outside_bbox_ignored(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.active = true;
    p.x = 600.0F; /* far OUTSIDE the 200x200 container at the top-left */
    p.y = 400.0F;
    scroll_frame(&p, &style);

    /* Press began outside the bbox, then the finger moves over the container and drags. The
     * anchor must never claim (press did not begin inside) -> no scroll. */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    for (int i = 1; i <= 6; ++i) {
        p.x = 600.0F - (float)(i * 80); /* sweeps across the container */
        p.y = 100.0F;
        scroll_frame(&p, &style);
    }
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->pos[1], 0.0F, 0.5F)); /* press-outside never scrolls */
}

/* ---- Test 22: empty-area press drag tracks the finger 1:1 — pos must equal the TOTAL finger
 *      displacement after each frame (no threshold-crossing jump, no cumulative runaway). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_empty_press_tracks_finger_1to1(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    style.rubber_band_c = 0.0F; /* isolate tracking from overscroll compression */
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style); /* establish bbox + state cell */

    /* Finger DOWN inside the container (empty content -> empty-area press, no widget capture). */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    p.x = 100.0F;
    p.y = 150.0F;
    const float start_y = p.y;
    scroll_frame(&p, &style); /* press frame: anchor set */
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;

    /* Drag UP -3 px/frame for 10 frames (into the valid negative offset range, away from the
     * top overscroll edge). The threshold (8 px) is crossed mid-sequence. Once scrolling has
     * started, the content offset must equal the finger displacement MINUS the dead-zone
     * threshold consumed before the drag claimed — i.e. it tracks 1:1 with no extra jump.
     * The frame-over-frame offset delta must never exceed the 3 px the finger actually moved. */
    float prev_pos = 0.0F;
    bool scrolling = false;
    for (int i = 1; i <= 10; ++i) {
        p.y = start_y - (float)(i * 3);
        scroll_frame(&p, &style);
        nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
        TEST_ASSERT_NOT_NULL(s);
        if (scrolling) {
            /* Per-frame offset step tracks the 3 px finger step (never an 8 px gap dump). */
            const float step = fabsf(s->pos[1] - prev_pos);
            TEST_ASSERT_TRUE(step <= 3.0F + 1.0F);
        }
        if (s->pos[1] < -0.5F) {
            scrolling = true;
        }
        prev_pos = s->pos[1];
    }
}

/* ---- Test 23: a SLOW empty-area press drag that crosses the threshold in tiny steps must not
 *      spike velocity from the accumulated threshold gap (no runaway fling on release). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_empty_press_slow_cross_no_velocity_spike(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    style.rubber_band_c = 0.0F;
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style);

    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    const float start_y = 150.0F;
    p.y = start_y;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;

    /* Creep -2 px/frame for 8 frames (crosses the 8 px threshold around frame 4-5). The
     * crossing frame must route only the per-frame 2 px, not the whole accumulated gap, so
     * velocity reflects 2 px / dt (~120 px/s) — never the 8+ px jump (~480+ px/s). */
    for (int i = 1; i <= 8; ++i) {
        p.y = start_y - (float)(i * 2);
        scroll_frame(&p, &style);
        nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
        TEST_ASSERT_NOT_NULL(s);
        /* 2 px/frame at 60fps == 120 px/s; allow headroom but reject the 8px-gap spike. */
        TEST_ASSERT_TRUE(fabsf(s->vel[1]) < 240.0F);
    }
}

/* ---- Held-drag physics (defect 1): a finger held DOWN that owns the gesture moves content
 *      as a PURE FUNCTION of finger displacement; momentum integrates only AFTER release.
 *      Helper: press inside, drag to a target y over a few frames, return with the finger
 *      still DOWN at that y (claimed). ---- */
static nt_ui_scroll_state_t *held_drag_to(nt_pointer_t *p, const nt_ui_scroll_style_t *style, float drag_to_y) {
    p->x = 100.0F;
    p->y = 100.0F;
    p->active = true;
    scroll_frame(p, style); /* establish bbox + state cell */
    p->buttons[NT_BUTTON_LEFT].is_down = true;
    p->buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame(p, style); /* press frame: anchor set */
    p->buttons[NT_BUTTON_LEFT].is_pressed = false;
    /* Drag toward the target in a few steps so the gesture claims (crosses the threshold). */
    const float step = (drag_to_y - 100.0F) / 4.0F;
    for (int i = 1; i <= 4; ++i) {
        p->y = 100.0F + (step * (float)i);
        scroll_frame(p, style);
    }
    return (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
}

/* ---- Test 24: a finger held STILL mid-drag IN BOUNDS freezes pos for N frames (no creep,
 *      no momentum) — the content only moves when the finger moves. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_held_still_in_bounds_frozen(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    /* Drag UP to y=60 (content moves up = negative pos), well inside the valid range. */
    nt_ui_scroll_state_t *s = held_drag_to(&p, &style, 60.0F);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->pos[1] < -1.0F); /* dragged in-bounds */
    const float frozen = s->pos[1];
    /* Finger now HELD STILL (button down, y unchanged) for 30 frames. */
    for (int i = 0; i < 30; ++i) {
        scroll_frame(&p, &style);
        s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(float_near(s->pos[1], frozen, 0.001F));                         /* pos frozen — no creep, no drift */
        TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U)); /* still owned */
    }
}

/* ---- Test 25: a finger held STILL PAST THE EDGE freezes the COMPRESSED offset for N frames
 *      (the iterative-rubber creep bug: pos must not march toward the edge under a still finger). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_held_still_past_edge_frozen(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    /* Drag DOWN to y=240 (finger below the press): content overscrolls past the TOP edge
     * (pos > 0 territory in Clay sign), entering the rubber-band region. */
    nt_ui_scroll_state_t *s = held_drag_to(&p, &style, 260.0F);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->pos[1] > 1.0F);             /* past the top edge (rubber-banded) */
    TEST_ASSERT_TRUE(s->raw[1] > s->pos[1] + 1.0F); /* raw overdrag exceeds the compressed display */
    const float frozen = s->pos[1];
    /* Hold still past the edge for 30 frames: the compressed offset must NOT creep toward 0. */
    for (int i = 0; i < 30; ++i) {
        scroll_frame(&p, &style);
        s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(float_near(s->pos[1], frozen, 0.001F)); /* compressed offset frozen */
    }
}

/* ---- Test 26: releasing AFTER holding the finger still produces NO fling (velocity sampled
 *      near zero) — the content settles where the finger left it, no momentum kick. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_release_after_hold_no_fling(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    nt_ui_scroll_state_t *s = held_drag_to(&p, &style, 60.0F);
    TEST_ASSERT_NOT_NULL(s);
    /* Hold still 10 frames so the sampled velocity decays to zero. */
    for (int i = 0; i < 10; ++i) {
        scroll_frame(&p, &style);
    }
    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    const float pos_at_release = s->pos[1];
    /* A genuine hold drains the time-windowed velocity below the integrator's dead-momentum eps
     * (0.5 px/s), so the fling branch never fires on release -> no fling. (We pin the contract —
     * vel is dead — not the exact decay value; time-windowed tracking never hard-zeroes a frame.) */
    TEST_ASSERT_TRUE(fabsf(s->vel[1]) < 0.5F); /* held still -> momentum dead (below VEL_EPS) */
    /* Release; the post-release frames must not fling (settles at the release point). */
    p.buttons[NT_BUTTON_LEFT].is_down = false;
    p.buttons[NT_BUTTON_LEFT].is_released = true;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_released = false;
    for (int i = 0; i < 20; ++i) {
        scroll_frame(&p, &style);
    }
    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->pos[1], pos_at_release, 1.0F)); /* no momentum after a held-still release */
}

/* ---- Test 27: raw-anchored rubber — dragging N px past the edge then back M px tracks
 *      rubber(raw) BOTH ways with no hysteresis (the offset retraces, not a one-way creep). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_raw_anchored_rubber_no_hysteresis(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    /* Drag past the top edge to ~100 px of raw overdrag. */
    nt_ui_scroll_state_t *s = held_drag_to(&p, &style, 300.0F);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->pos[1] > 1.0F);
    const float raw_far = s->raw[1];
    const float pos_far = s->pos[1];
    /* Pull the finger BACK 50 px (toward the press): raw shrinks, pos must retrace toward rest. */
    for (int i = 1; i <= 5; ++i) {
        p.y = 300.0F - (float)(i * 10);
        scroll_frame(&p, &style);
    }
    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    const float raw_near = s->raw[1];
    const float pos_near = s->pos[1];
    TEST_ASSERT_TRUE(raw_near < raw_far - 1.0F); /* raw came back ~50 px */
    TEST_ASSERT_TRUE(pos_near < pos_far - 1.0F); /* compressed display retraced (no stuck hysteresis) */
    /* pos is exactly the rubber-band of the current raw overdrag (pure function, recomputed from raw). */
    const float expect = nt_ui_scroll_test_rubber_band(raw_near, 200.0F); /* container height 200 */
    TEST_ASSERT_TRUE(float_near(pos_near, expect, 1.5F));
}

/* ---- Test 28: a held drag does NOT leak momentum mid-hold. Drag fast, then hold still: the
 *      integrator must stay in the DRAGGING branch (no vel*dt advance under a still finger). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_held_no_momentum_midhold(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    /* A FAST drag builds velocity, then we freeze. Drag up to y=40 (fast) in 4 steps. */
    nt_ui_scroll_state_t *s = held_drag_to(&p, &style, 40.0F);
    TEST_ASSERT_NOT_NULL(s);
    const float pos_after_drag = s->pos[1];
    const float vel_after_drag = fabsf(s->vel[1]);
    /* First held-still frame: pos must NOT advance by the prior fling velocity (the DRAGGING branch
     * owns pos as a pure function of finger position; vel is read only at release). */
    scroll_frame(&p, &style);
    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->pos[1], pos_after_drag, 0.001F)); /* no momentum step mid-hold */
    /* A single still frame DECAYS the time-windowed velocity (never hard-zeroes — that parity-zero
     * was the flaky-fling bug); the genuine-hold drain to zero is covered by the release tests. */
    TEST_ASSERT_TRUE(fabsf(s->vel[1]) < vel_after_drag); /* still frame bleeds momentum, not a spike */
}

/* ================= Gesture ownership (one owner per pointer, claimed at the down edge) ================= */

/* ---- Test 29: THE user bug — a finger that presses inside container A and is dragged across into
 *      container B's bbox scrolls ONLY A. B never adopts the held finger (no down-edge inside B). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_cross_container_no_adoption(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.active = true;
    p.x = 100.0F; /* inside A (x=[0,200]) */
    p.y = 100.0F;
    twin_scroll_frame(&p, &style, false); /* establish both bboxes + state cells */

    /* Press EDGE inside A. */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    twin_scroll_frame(&p, &style, false);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;

    /* Drag the still-held finger RIGHT across into B's bbox (x 100 -> 360), then keep it there.
     * Vertical stays put; the demo lists scroll y, so to make A move we add a y component too. */
    const float xs[6] = {120.0F, 180.0F, 240.0F, 300.0F, 340.0F, 360.0F};
    for (int i = 0; i < 6; ++i) {
        p.x = xs[i];
        p.y = 100.0F - (float)((i + 1) * 8); /* drag up so A's y scrolls */
        twin_scroll_frame(&p, &style, false);
    }
    /* A few frames fully inside B with the finger still held. */
    for (int i = 0; i < 4; ++i) {
        p.x = 360.0F;
        p.y = 40.0F - (float)(i * 4);
        twin_scroll_frame(&p, &style, false);
    }

    nt_ui_scroll_state_t *sa = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID_A);
    nt_ui_scroll_state_t *sb = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID_B);
    TEST_ASSERT_NOT_NULL(sa);
    TEST_ASSERT_NOT_NULL(sb);
    TEST_ASSERT_TRUE(sa->pos[1] < -1.0F);                 /* A (the press owner) scrolled */
    TEST_ASSERT_TRUE(float_near(sb->pos[1], 0.0F, 0.5F)); /* B never adopted the held finger */
    /* The single owner is A; the capture is NOT held by B. */
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID_A, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
}

/* ---- Test 30: a finger ALREADY HELD (no press edge) entering a container never starts a gesture.
 *      Without the down-edge gate this is the adoption bug; with it, the container ignores it. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_held_entry_no_edge_no_gesture(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style); /* establish bbox + state cell */

    /* Finger arrives ALREADY DOWN (is_down true, is_pressed false — no edge this frame) and then
     * drags well past the threshold inside the bbox. No gesture must ever arm. */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    for (int i = 1; i <= 8; ++i) {
        p.y = 100.0F - (float)(i * 10); /* 80 px of drag, no edge */
        scroll_frame(&p, &style);
    }
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->pos[1], 0.0F, 0.5F));                      /* never scrolled */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0U)); /* nothing claimed */
}

/* ---- Test 32: releasing a claimed gesture releases the capture (re-arms), flings, and the NEXT
 *      press edge starts a FRESH gesture (the owner is reassigned only at a new down edge). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_release_reclaims_on_next_edge(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style);

    /* Press edge inside, drag up fast to build a fling, finger still down (owned). */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    for (int i = 1; i <= 5; ++i) {
        p.y = 100.0F - (float)(i * 14);
        scroll_frame(&p, &style);
    }
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U)); /* owned mid-drag */
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(fabsf(s->vel[1]) > 100.0F); /* built a fling */
    const float pos_at_release = s->pos[1];

    /* Release: the capture must clear (re-arm) and the fling carries on. */
    p.buttons[NT_BUTTON_LEFT].is_down = false;
    p.buttons[NT_BUTTON_LEFT].is_released = true;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_released = false;
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U)); /* owns its release frame */
    scroll_frame(&p, &style);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0U)); /* released */
    for (int i = 0; i < 7; ++i) {
        scroll_frame(&p, &style);
    }
    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->pos[1] < pos_at_release - 1.0F); /* flung past the release point */

    /* A fresh press edge starts a brand-new gesture -> the container claims again. */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    p.y = 100.0F;
    scroll_frame(&p, &style); /* arm */
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    for (int i = 1; i <= 3; ++i) {
        p.y = 100.0F - (float)(i * 12);
        scroll_frame(&p, &style);
    }
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U)); /* fresh claim */
}

/* ================= UX-audit fixes (fades, tap-stop, wheel-vs-fling, vel clamp, no-overflow,
 *                   thumb grab-offset, dt-invariant overscroll, content-shrink clamp) ================= */

/* ---- Test 34 (fix 1): AUTO_HIDE bar fades IN on scroll activity. Idle frames fade it out, then
 *      a single wheel notch resets the activity clock and the eased opacity rises again. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scrollbar_auto_hide_fades_in_on_activity(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_AUTO_HIDE);
    style.bar_fade_speed = 8.0F;
    style.bar_hide_delay = 0.3F; /* short linger so the idle fade-out lands within the test window */
    nt_pointer_t p = {0};
    p.active = true; /* pointer NOT over the bar */
    p.x = 10.0F;
    p.y = 10.0F;

    /* Idle until the bar has faded out (idle past the linger, opacity drops). */
    float op_idle = 1.0F;
    for (int i = 0; i < 120; ++i) {
        scrollbar_frame(&p, &style, 800.0F);
        {
            const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
            op_idle = bar_probe.opacity;
        }
    }
    TEST_ASSERT_TRUE(op_idle < 0.2F); /* faded out while idle */

    /* One wheel notch over the container resets the activity clock -> opacity must RISE over frames
     * (this fails on the dead-bar code: with target stuck at 0 the opacity never leaves ~0). */
    p.wheel_dy = 1.0F;
    p.x = 100.0F; /* over the container so the wheel is gathered */
    p.y = 100.0F;
    scrollbar_frame(&p, &style, 800.0F);
    p.wheel_dy = 0.0F;
    float op_prev = 0.0F;
    {
        const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
        op_prev = bar_probe.opacity;
    }
    bool rose = false;
    for (int i = 0; i < 10; ++i) {
        scrollbar_frame(&p, &style, 800.0F); /* fling-in-motion keeps idle reset -> fade target stays 1 */
        float op = 0.0F;
        {
            const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
            op = bar_probe.opacity;
        }
        if (op > op_prev + 0.01F) {
            rose = true;
        }
        op_prev = op;
    }
    TEST_ASSERT_TRUE(rose);           /* the bar faded back IN on activity */
    TEST_ASSERT_TRUE(op_prev > 0.4F); /* climbed well off the floor */
}

/* ---- Test 35 (fix 1b): hovering the bar keeps it visible even with no scroll motion. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scrollbar_auto_hide_hover_keeps_visible(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_AUTO_HIDE);
    style.bar_fade_speed = 8.0F;
    style.bar_hide_delay = 0.2F;
    nt_pointer_t p = {0};
    p.active = true;
    p.x = 195.0F; /* over the right-edge vertical bar (also inside the 200-wide container) */
    p.y = 100.0F;

    /* Born idle: reveal the bar first via wheel notches so it draws (and registers its hit bbox);
     * an invisible overlay bar has nothing on screen to hover, mirroring macOS overlay scrollbars.
     * The pointer already sits on the bar, so hover takes over once the bar has drawn a frame. */
    for (int i = 0; i < 12; ++i) {
        p.wheel_dy = 1.0F; /* keep activity alive while the fade climbs and the bbox registers */
        scrollbar_frame(&p, &style, 800.0F);
    }
    p.wheel_dy = 0.0F;

    /* Idle clock now runs past the linger, but the pointer hovers the bar -> stays opaque. */
    float op = 0.0F;
    for (int i = 0; i < 120; ++i) {
        scrollbar_frame(&p, &style, 800.0F);
        {
            const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
            op = bar_probe.opacity;
        }
    }
    TEST_ASSERT_TRUE(op > 0.8F); /* hover held it visible past the idle linger */
}

/* ---- Test 35b (born-idle): a fresh AUTO_HIDE scroll starts HIDDEN — no frame-1 flash. The bar
 *      stays faded out with no activity, then fades IN once a wheel notch resets the idle clock. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scrollbar_auto_hide_born_idle_no_flash(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_AUTO_HIDE);
    style.bar_fade_speed = 8.0F;
    nt_pointer_t p = {0};
    p.active = true; /* pointer NOT over the bar */
    p.x = 10.0F;
    p.y = 10.0F;

    /* Frame 1 solves dims (no bar yet); frame 2 emits with the born-idle cell -> target 0, hidden. */
    scrollbar_frame(&p, &style, 800.0F);
    scrollbar_frame(&p, &style, 800.0F);
    float op_start = 1.0F;
    {
        const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
        op_start = bar_probe.opacity;
    }
    TEST_ASSERT_TRUE(op_start < 0.1F); /* born hidden — no startup flash */

    /* Stays hidden across more idle frames (idle never dips below the linger on its own). */
    float op_idle = 1.0F;
    for (int i = 0; i < 30; ++i) {
        scrollbar_frame(&p, &style, 800.0F);
        {
            const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
            op_idle = bar_probe.opacity;
        }
    }
    TEST_ASSERT_TRUE(op_idle < 0.1F);

    /* A wheel notch over the container resets idle to 0 -> the bar fades IN. */
    p.wheel_dy = 1.0F;
    p.x = 100.0F; /* over the container so the wheel is gathered */
    p.y = 100.0F;
    scrollbar_frame(&p, &style, 800.0F);
    p.wheel_dy = 0.0F;
    bool rose = false;
    float op_prev = 0.0F;
    {
        const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
        op_prev = bar_probe.opacity;
    }
    for (int i = 0; i < 10; ++i) {
        scrollbar_frame(&p, &style, 800.0F); /* fling-in-motion keeps idle reset -> target stays 1 */
        float op = 0.0F;
        {
            const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
            op = bar_probe.opacity;
        }
        if (op > op_prev + 0.01F) {
            rose = true;
        }
        op_prev = op;
    }
    TEST_ASSERT_TRUE(rose);           /* appeared on real scroll activity */
    TEST_ASSERT_TRUE(op_prev > 0.4F); /* climbed well off the floor */
}

/* ---- Test 35c (born-idle): ALWAYS bars ignore the idle seed — visible on frame 1 regardless. ---- */
static void test_scrollbar_always_born_visible(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    nt_pointer_t p = {0};
    p.active = true;
    p.x = 10.0F;
    p.y = 10.0F;

    /* No activity at all; ALWAYS opacity is a constant 1, unaffected by the born-idle seed. */
    scrollbar_frame(&p, &style, 800.0F);
    scrollbar_frame(&p, &style, 800.0F);
    float op = 0.0F;
    {
        const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
        op = bar_probe.opacity;
    }
    TEST_ASSERT_TRUE(op > 0.9F); /* always visible from the first emit */
}

/* ---- Test 36 (fix 3): a wheel notch during a fling kills momentum and eases toward the wheel
 *      target the SAME frame (the notch is not swallowed by the momentum overwrite). ---- */
static void test_scroll_wheel_kills_fling(void) {
    const float content[2] = {0.0F, 100000.0F};
    const float container[2] = {0.0F, 400.0F};
    nt_ui_scroll_state_t s = {0};
    s.vel[1] = -3000.0F; /* a fling in motion */
    const float no_wheel[2] = {0.0F, 0.0F};
    nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, &s_physics_style, false);
    TEST_ASSERT_TRUE(fabsf(s.vel[1]) > 100.0F); /* still flinging */

    const float target_before = s.target[1];
    const float wheel_up[2] = {0.0F, 1.0F}; /* one notch toward 0 (Clay sign up) */
    nt_ui_scroll_test_integrate(&s, wheel_up, 1.0F / 60.0F, content, container, &s_physics_style, false);
    /* Momentum killed so the easing branch picks up the new target (target moved by +step). */
    TEST_ASSERT_TRUE(float_near(s.vel[1], 0.0F, 0.001F));
    TEST_ASSERT_TRUE(s.target[1] > target_before + 1.0F); /* the notch registered (not lost) */
}

/* ---- Test 37 (fix 4): a huge single-frame drag delta on a tiny dt is EMA-smoothed + clamped,
 *      never exploding the release-fling velocity past the cap. ---- */
static void test_scroll_velocity_clamp_on_dt_hitch(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style);

    /* Press, then a massive jump on a near-zero dt (frame hitch). Without the clamp this is
     * delta/dt -> astronomically large; with EMA+clamp it can never exceed the cap. */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;

    /* One frame with a 500 px jump at a 0.001 s dt would sample 500000 px/s raw. */
    p.y = 100.0F - 500.0F;
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = 0.001F, .pointers = &p, .pointer_count = 1});
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, &style, NULL);
        {
            CLAY({.id = CLAY_ID("content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(1000)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(fabsf(s->vel[1]) <= 8000.0F + 0.5F); /* clamped to the velocity cap */
}

/* ---- Test 38 (fix 5): a container whose content FITS (no overflow) takes no gesture — a empty-area press
 *      drag across it never claims a capture and never scrolls (inner widgets stay clickable). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_no_overflow_takes_no_gesture(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;

    /* Content 150 < container 200 -> no overflow on the only enabled axis. */
    scrollbar_frame(&p, &style, 150.0F);
    scrollbar_frame(&p, &style, 150.0F); /* dims solved */

    /* Press inside, then drag 50 px — well past the threshold. */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scrollbar_frame(&p, &style, 150.0F);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;
    for (int i = 1; i <= 6; ++i) {
        p.y = 100.0F - (float)(i * 10);
        scrollbar_frame(&p, &style, 150.0F);
    }
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->pos[1], 0.0F, 0.5F));                      /* never scrolled (no-op axis) */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0U)); /* never stole a capture */
}

/* ---- Test 39 (fix 2): a tap on a FLINGING container stops the fling immediately and swallows the
 *      tap (claims the capture so moving inner content can't be clicked). A tap on a RESTING
 *      container does NOT claim (inner widgets stay clickable). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_tap_stops_fling_and_swallows(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style);

    /* Seed a strong fling into the live cell. */
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    s->vel[1] = -2000.0F;
    s->pos[1] = -100.0F;
    s->raw[1] = -100.0F;
    s->target[1] = -100.0F;

    /* Press edge inside the container while it flings. */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame(&p, &style);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;

    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->vel[1], 0.0F, 0.001F)); /* fling stopped on tap */
    const float frozen = s->pos[1];
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U)); /* tap swallowed (claimed) */

    /* Held still: pos must stay frozen (no momentum continuation). */
    scroll_frame(&p, &style);
    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->pos[1], frozen, 1.0F)); /* frozen next frame */
}

/* ---- Test 40 (fix 2b): a press on a RESTING container does NOT claim the capture (no fling to
 *      stop) so a tap still reaches inner widgets. ---- */
static void test_scroll_tap_on_resting_does_not_swallow(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style); /* at rest, vel 0 */

    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame(&p, &style);
    /* No fling -> the press only arms a empty-area press (does not claim a capture this frame). */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
}

/* ---- Test 41 (fix 6): grabbing the thumb away from its center keeps the grabbed point under the
 *      cursor — content does NOT jump on press, and a 10 px drag moves the thumb exactly 10 px. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scrollbar_thumb_grab_offset_no_jump(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    style.bar_thumb_min_px = 40.0F; /* a sizable thumb so the grab offset is measurable */
    nt_pointer_t p = {0};
    p.active = true;

    /* Establish the bar bbox (thumb at the TOP, offset 0). */
    scrollbar_frame(&p, &style, 800.0F);
    scrollbar_frame(&p, &style, 800.0F);
    float thumb_len = 0.0F;
    float thumb_off0 = 0.0F;
    float track_len = 0.0F;
    {
        const ui_bar_probe_t bar_probe = ui_bar_probe(s_fx.ctx, nt_ui_scroll_test_bar_id(SCROLL_ID, 1), 1);
        thumb_len = bar_probe.thumb_len;
        thumb_off0 = bar_probe.thumb_off;
        track_len = bar_probe.track_len;
    }
    TEST_ASSERT_TRUE(float_near(thumb_off0, 0.0F, 1.0F)); /* thumb at the top at rest */

    /* over = content - container = 800 - 200 = 600; thumb travel maps pos: a thumb_off delta of D
     * corresponds to a pos delta of -D * over / (track_len - thumb_len). Read s->pos DIRECTLY (the
     * bar-geometry readback lags one frame), so the grab + drag effects are observed without lag. */
    const float over = 600.0F;
    const float travel = track_len - thumb_len;

    /* Press the thumb near its BOTTOM edge (bar runs y=[0,200]; thumb_lo=0). */
    const float grab_y = thumb_len - 5.0F; /* 5 px from the thumb bottom */
    p.x = 195.0F;
    p.y = grab_y;
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scrollbar_frame(&p, &style, 800.0F);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;

    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    /* No jump-to-center: the grabbed point stays under the cursor, so pos barely moves on press.
     * (Absolute thumb_len/2 mapping would jump pos to map the cursor to the thumb CENTER instead.) */
    const float pos_press = s->pos[1];
    const float thumb_off_press = (-pos_press / over) * travel;
    TEST_ASSERT_TRUE(fabsf(thumb_off_press - thumb_off0) < 2.0F);

    /* Drag the cursor +10 px down the track: the thumb must follow ~10 px (1:1). */
    p.y = grab_y + 10.0F;
    scrollbar_frame(&p, &style, 800.0F);
    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    const float thumb_off_drag = (-s->pos[1] / over) * travel;
    TEST_ASSERT_TRUE(float_near(thumb_off_drag - thumb_off_press, 10.0F, 2.0F));
}

/* ---- Test 42 (fix 7): fling overscroll depth is FPS-independent — the same release velocity
 *      integrated at 60 vs 120 fps reaches the same max overscroll depth (within tolerance). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static float fling_max_overshoot(float dt, int steps) {
    /* Container 200, content 1000 -> bottom edge at -800. Fling hard past it; record max depth. */
    const float content[2] = {0.0F, 1000.0F};
    const float container[2] = {0.0F, 200.0F};
    const float no_wheel[2] = {0.0F, 0.0F};
    nt_ui_scroll_state_t s = {0};
    s.pos[1] = -800.0F + 5.0F; /* start just inside the bottom edge */
    s.raw[1] = s.pos[1];
    s.target[1] = s.pos[1];
    s.vel[1] = -4000.0F; /* fling down past the edge */
    float deepest = 0.0F;
    for (int i = 0; i < steps; ++i) {
        nt_ui_scroll_test_integrate(&s, no_wheel, dt, content, container, &s_physics_style, false);
        const float depth = (-800.0F) - s.pos[1]; /* positive = past the bottom edge */
        if (depth > deepest) {
            deepest = depth;
        }
    }
    return deepest;
}

static void test_scroll_overscroll_depth_fps_independent(void) {
    /* Equal wall time: 0.5 s at 60fps (30 steps) vs 120fps (60 steps). */
    const float d60 = fling_max_overshoot(1.0F / 60.0F, 30);
    const float d120 = fling_max_overshoot(1.0F / 120.0F, 60);
    TEST_ASSERT_TRUE(d60 > 1.0F); /* it actually overshot */
    TEST_ASSERT_TRUE(d120 > 1.0F);
    /* Depths within 10% of each other (FPS-invariant overscroll). */
    const float rel = fabsf(d60 - d120) / d60;
    TEST_ASSERT_TRUE(rel < 0.10F);
}

/* ---- Test 43 (also): content shrinks -> the offset animates back to the new (smaller) bound
 *      instead of staying parked past it. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_content_shrink_clamps_pos(void) {
    const float no_wheel[2] = {0.0F, 0.0F};
    const float container[2] = {0.0F, 200.0F};
    nt_ui_scroll_state_t s = {0};

    /* Tall content (-800 is the bottom edge); scroll to the bottom. */
    float content[2] = {0.0F, 1000.0F};
    s.pos[1] = -800.0F;
    s.raw[1] = -800.0F;
    s.target[1] = -800.0F;
    nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, &s_physics_style, false);
    TEST_ASSERT_TRUE(float_near(s.pos[1], -800.0F, 1.0F)); /* parked at the old bottom */

    /* Content SHRINKS to 400 -> new bottom edge is -(400-200) = -200. pos at -800 is now past it. */
    content[1] = 400.0F;
    for (int i = 0; i < 240; ++i) {
        nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, &s_physics_style, false);
    }
    /* The offset must animate back up to the new bound (-200), not stay parked at -800. */
    TEST_ASSERT_TRUE(float_near(s.pos[1], -200.0F, 1.0F));
}

/* ---- Test 44: at high FPS pointer events post slower than the render loop, so a fast drag
 *      alternates a moving frame with a zero-delta frame. Releasing ON a zero-delta frame must
 *      STILL fling — time-windowed velocity tracking keeps it. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_fling_survives_release_on_zero_delta_frame(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    const float dt = 1.0F / 144.0F; /* high refresh: mouse events lag the render loop */
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame_dt(&p, &style, dt); /* establish bbox + state cell */

    /* Press inside the container (empty content -> empty-area press, no widget capture). */
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame_dt(&p, &style, dt);
    p.buttons[NT_BUTTON_LEFT].is_pressed = false;

    /* Fast drag UP, but the pointer only ADVANCES on alternating frames: a 10 px move frame is
     * followed by a zero-delta (mouse-quantized) frame. The whole gesture stays held DOWN. */
    float y = 100.0F;
    for (int i = 0; i < 12; ++i) {
        y -= 10.0F;
        p.y = y;
        scroll_frame_dt(&p, &style, dt); /* move frame */
        scroll_frame_dt(&p, &style, dt); /* zero-delta frame (pointer unchanged) */
    }
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->pos[1] < -1.0F); /* the drag scrolled */
    /* The loop ended on a zero-delta frame: under the old hard-zero sampler vel is now 0 here. */

    /* RELEASE while the last sampled frame was a zero-delta one. A real fling must follow. */
    const float pos_at_release = s->pos[1];
    p.buttons[NT_BUTTON_LEFT].is_down = false;
    p.buttons[NT_BUTTON_LEFT].is_released = true;
    scroll_frame_dt(&p, &style, dt);
    p.buttons[NT_BUTTON_LEFT].is_released = false;
    for (int i = 0; i < 30; ++i) {
        scroll_frame_dt(&p, &style, dt);
    }
    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(s->pos[1] < pos_at_release - 1.0F); /* flung past the release point (no parity lottery) */
}

/* ================= Wheel exclusive-routing (broadcast bug) ================= */

/* Four scroll containers stacked VERTICALLY, mirroring the demo's scroll tiles (each a 220x78 clip
 * over tall overflowing content). The wheel-broadcast bug is a STALE-bbox hit-test artifact: every
 * scroll_begin hit-tests the pointer against its OWN PREV-frame bbox (Clay_GetElementData). On a
 * frame where a layout transition SHIFTS the stack vertically (a spacer appearing/growing above),
 * each container's prev-frame bbox is at the OLD position while the pointer is conceptually over the
 * NEW one. A pointer parked at a fixed Y then falls inside the stale bbox of the container that USED
 * to be there AND the one that is there now — so a single wheel notch routes to BOTH (a broadcast).
 * The churn knobs: a growing spacer sweeps the stack across the parked pointer; tile A is AUTO_HIDE
 * so its bar element churns; tile B's row count toggles (clip child-count churn). */
static const uint32_t WHEEL_ID_A = 0x5CE0A1U;
static const uint32_t WHEEL_ID_B = 0x5CE0B2U;
static const uint32_t WHEEL_ID_C = 0x5CE0C3U;
static const uint32_t WHEEL_ID_D = 0x5CE0D4U;

/* Current childOffset.y for a scroll id, or 0 if its state cell isn't live yet. */
static float wheel_pos_y(uint32_t id) {
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, id);
    return (s != NULL) ? s->pos[1] : 0.0F;
}

/* Tile geometry: 220x78 clip, 0px gap so vertically-adjacent tiles share an edge (the stale-vs-new
 * overlap band is exactly the shift distance at that shared edge). */
#define WHEEL_TILE_W 220.0F
#define WHEEL_TILE_H 78.0F

static void wheel_tile(uint32_t id, const nt_ui_scroll_style_t *style, int rows) {
    const Clay_ElementDeclaration decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(WHEEL_TILE_W), CLAY_SIZING_FIXED(WHEEL_TILE_H)}}};
    nt_ui_scroll_begin(s_fx.ctx, NULL, id, style, &decl);
    {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6}}) {
            for (int r = 0; r < rows; ++r) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(WHEEL_TILE_W), CLAY_SIZING_FIXED(30)}}}) {}
            }
        }
    }
    nt_ui_scroll_end(s_fx.ctx);
}

/* One churning vertical-stack frame. spacer_h shifts the whole stack DOWN by that many px (a panel
 * resizing above the tiles); b_rows churns tile B's child count; if `drop_a` is set tile A is NOT
 * declared this frame (a conditionally-hidden tile — its Clay hashmap bbox then goes STALE while the
 * other tiles reflow up into the space A vacated). A stale bbox is still returned found=true by
 * Clay_GetElementData, so on the frames A reappears its ancient bbox can overlap the live tiles. */
static void wheel_grid_frame(nt_pointer_t *p, const nt_ui_scroll_style_t *style_a, const nt_ui_scroll_style_t *style_other, int b_rows, float spacer_h, bool drop_a) {
    nt_arena_reset(&g_nt_mem_scratch); /* per-frame scratch reset, exactly as the engine main loop does */
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = 1.0F / 60.0F, .pointers = p, .pointer_count = 1});
    CLAY({.id = CLAY_ID("wheel-root"), .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 0}}) {
        if (spacer_h > 0.0F) {
            CLAY({.id = CLAY_ID("wheel-spacer"), .layout = {.sizing = {CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(spacer_h)}}}) {}
        }
        if (!drop_a) {
            wheel_tile(WHEEL_ID_A, style_a, 40); /* tall: overflows, AUTO_HIDE bar churns */
        }
        wheel_tile(WHEEL_ID_B, style_other, b_rows); /* churning row count (still overflows: >=3 rows) */
        wheel_tile(WHEEL_ID_C, style_other, 40);
        wheel_tile(WHEEL_ID_D, style_other, 40);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 45 (THE wheel-broadcast bug): a wheel notch EVERY frame, pointer parked at the center
 *      of tile A, while the grid churns (A's AUTO_HIDE bar, B's row count, a root spacer). No frame
 *      moves more than one container. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_wheel_no_broadcast_under_churn(void) {
    nt_ui_scroll_style_t style_a = nt_ui_scroll_style_defaults();
    style_a.bar_visibility = NT_UI_SCROLLBAR_AUTO_HIDE;
    style_a.bar_hide_delay = 0.15F; /* short linger so the bar fades out and back in repeatedly */
    style_a.bar_fade_speed = 10.0F;
    const nt_atlas_region_ref_t art = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    style_a.track_ref = art;
    style_a.thumb_ref = art;

    nt_ui_scroll_style_t style_other = nt_ui_scroll_style_defaults();
    style_a.wheel_ease_speed = 0.0F; /* instant wheel: a container moves only on the frame it is routed a notch */
    style_other.wheel_ease_speed = 0.0F;

    nt_pointer_t p = {0};
    p.active = true;
    /* Park the pointer in the TOP tile slot (y in [0,78]). When A is dropped, B/C/D reflow UP so B
     * now occupies [0,78] — but A's prev-frame bbox is ALSO [0,78] and Clay still returns it found.
     * So both A (stale) and B (live) hit-test the parked pointer on the drop frame -> broadcast. */
    p.x = WHEEL_TILE_W * 0.5F;
    p.y = 40.0F; /* inside the top tile slot */

    /* Two establishing frames so every container has a solved prev-frame bbox + dims. */
    wheel_grid_frame(&p, &style_a, &style_other, 6, 0.0F, false);
    wheel_grid_frame(&p, &style_a, &style_other, 6, 0.0F, false);

    /* A wheel notch EVERY frame for ~300 frames while the stack churns. A is conditionally dropped
     * every few frames (its stale bbox lingers); the spacer + B row count add layout-slot churn. No
     * single frame may route the notch to more than ONE container (exclusive routing, never a
     * broadcast). */
    for (int f = 0; f < 300; ++f) {
        p.wheel_dy = 1.0F;                              /* one notch this frame */
        const int b_rows = ((f / 30) % 2 == 0) ? 6 : 3; /* B's row count toggles every ~30 frames */
        const float spacer_h = (float)((f * 5) % 40);   /* sawtooth 0..35 px of extra slot churn */
        const bool drop_a = (f % 3) == 0;               /* A vanishes every 3rd frame (stale-bbox lingers) */

        const uint32_t ids[4] = {WHEEL_ID_A, WHEEL_ID_B, WHEEL_ID_C, WHEEL_ID_D};
        float before[4];
        for (int k = 0; k < 4; ++k) {
            before[k] = wheel_pos_y(ids[k]);
        }
        wheel_grid_frame(&p, &style_a, &style_other, b_rows, spacer_h, drop_a);
        int got = 0;
        for (int k = 0; k < 4; ++k) {
            got += (wheel_pos_y(ids[k]) < before[k] - 0.01F) ? 1 : 0; /* a notch scrolls toward negative pos; a settle moves back */
        }
        TEST_ASSERT_TRUE_MESSAGE(got <= 1, "wheel notch broadcast to more than one scroll container in a single frame");
    }

    /* Sanity: the notch was actually consumed by SOME container (the pointer sat over the stack the
     * whole run); exclusivity above is the real pin, so we don't assert WHICH tile owns it. */
    int moved = 0;
    if (wheel_pos_y(WHEEL_ID_A) < -0.5F) {
        ++moved;
    }
    if (wheel_pos_y(WHEEL_ID_B) < -0.5F) {
        ++moved;
    }
    if (wheel_pos_y(WHEEL_ID_C) < -0.5F) {
        ++moved;
    }
    if (wheel_pos_y(WHEEL_ID_D) < -0.5F) {
        ++moved;
    }
    TEST_ASSERT_TRUE(moved >= 1);
}

/* ================= Innermost-wins routing (nested + tie-break) ================= */

static const uint32_t NEST_OUTER_ID = 0x5CF001U;
static const uint32_t NEST_INNER_ID = 0x5CF002U;

/* Outer scroll (300x300 clip over 300x1000 content); an INNER scroll (200x200 over 200x800 content)
 * sits at the TOP-LEFT of the outer's content via real nt_ui_scroll_begin nesting. At rest both
 * childOffsets are 0, so the inner occupies outer-screen [0,200]x[0,200] and the band x>200 (or
 * y>200) is outer-only. The pointer is parked by the caller. */
static void nested_scroll_frame(nt_pointer_t *p) {
    const nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = 1.0F / 60.0F, .pointers = p, .pointer_count = 1});
    CLAY({.id = CLAY_ID("nest-root"), .layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(300)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, NEST_OUTER_ID, &style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(300)}}});
        {
            CLAY({.id = CLAY_ID("nest-outer-content"), .layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(1000)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
                /* Inner scroll pinned to the content's top-left corner. */
                nt_ui_scroll_begin(s_fx.ctx, NULL, NEST_INNER_ID, &style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}});
                {
                    CLAY({.id = CLAY_ID("nest-inner-content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(800)}}}) {}
                }
                nt_ui_scroll_end(s_fx.ctx);
            }
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 46 (innermost-wins): pointer over the INNER nested scroll routes the wheel to the INNER
 *      container ONLY; the outer must not move. FAILS on outermost-wins (the old claim-as-you-go code
 *      gave the wheel to the first-declared outer container). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_wheel_nested_innermost_wins(void) {
    nt_pointer_t p = {0};
    p.active = true;
    p.x = 100.0F; /* inside the inner ([0,200]x[0,200]) AND the outer */
    p.y = 100.0F;

    /* Warm-up: frame 1 solves the bboxes, frame 2 registers them, so the pointer is over the list from frame 3 (1-frame
     * ownership lag — same as every prev-frame bbox hit-test). */
    nested_scroll_frame(&p);
    nested_scroll_frame(&p);

    /* Wheel for several frames so the eased inner offset accumulates clearly. */
    for (int i = 0; i < 20; ++i) {
        p.wheel_dy = 1.0F;
        nested_scroll_frame(&p);
    }
    p.wheel_dy = 0.0F;

    nt_ui_scroll_state_t *outer = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, NEST_OUTER_ID);
    nt_ui_scroll_state_t *inner = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, NEST_INNER_ID);
    TEST_ASSERT_NOT_NULL(outer);
    TEST_ASSERT_NOT_NULL(inner);
    TEST_ASSERT_TRUE(inner->pos[1] < -1.0F);                 /* the INNER scrolled */
    TEST_ASSERT_TRUE(float_near(outer->pos[1], 0.0F, 0.5F)); /* the OUTER stayed put (innermost won) */
}

/* ---- Test 47 (nested, outer band): pointer over the outer but OUTSIDE the inner routes to the
 *      OUTER (the inner's bbox does not hold the pointer, so only the outer candidate wins). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_wheel_nested_outer_band(void) {
    nt_pointer_t p = {0};
    p.active = true;
    p.x = 250.0F; /* x>200: inside the outer, OUTSIDE the inner (inner spans x[0,200]) */
    p.y = 100.0F;

    nested_scroll_frame(&p);
    nested_scroll_frame(&p);
    for (int i = 0; i < 20; ++i) {
        p.wheel_dy = 1.0F;
        nested_scroll_frame(&p);
    }
    p.wheel_dy = 0.0F;

    nt_ui_scroll_state_t *outer = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, NEST_OUTER_ID);
    nt_ui_scroll_state_t *inner = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, NEST_INNER_ID);
    TEST_ASSERT_NOT_NULL(outer);
    TEST_ASSERT_NOT_NULL(inner);
    TEST_ASSERT_TRUE(outer->pos[1] < -1.0F);                 /* the OUTER scrolled */
    TEST_ASSERT_TRUE(float_near(inner->pos[1], 0.0F, 0.5F)); /* the inner never received the wheel */
}

/* Two overlapping sibling scroll containers: a BIG one, then a SMALL one FLOATING over the big's
 * top-left corner. The pointer sits in the overlap; front-most arbitration gives the wheel to the
 * floating one only. */
static const uint32_t TIE_BIG_ID = 0x5CF101U;
static const uint32_t TIE_SMALL_ID = 0x5CF102U;

static void tie_break_frame(nt_pointer_t *p) {
    const nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = 1.0F / 60.0F, .pointers = p, .pointer_count = 1});
    CLAY({.id = CLAY_ID("tie-root"), .layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(400)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, TIE_BIG_ID, &style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(400)}}});
        {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(400), CLAY_SIZING_FIXED(1200)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
        /* SMALL floats over the root's top-left so it overlaps BIG's [0,400]x[0,400] in [0,120]x[0,120]. */
        nt_ui_scroll_begin(s_fx.ctx, NULL, TIE_SMALL_ID, &style,
                           &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(120)}},
                                                      .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP}}});
        {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(600)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Test 48: overlapping sibling scrolls -> the front-most (floating) one receives the wheel, and
 *      ONLY one does. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_wheel_front_most_sibling_wins(void) {
    nt_pointer_t p = {0};
    p.active = true;
    p.x = 60.0F; /* inside the overlap [0,120]x[0,120] of both BIG and the floating SMALL */
    p.y = 60.0F;

    /* Warm-up so the bboxes solve and the lists are registered (1-frame lag). */
    tie_break_frame(&p);
    tie_break_frame(&p);

    for (int f = 0; f < 20; ++f) {
        p.wheel_dy = 1.0F;
        tie_break_frame(&p);
    }

    nt_ui_scroll_state_t *big = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, TIE_BIG_ID);
    nt_ui_scroll_state_t *small = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, TIE_SMALL_ID);
    TEST_ASSERT_NOT_NULL(big);
    TEST_ASSERT_NOT_NULL(small);
    TEST_ASSERT_TRUE(small->pos[1] < -1.0F);               /* the front-most of the two overlapping containers took it */
    TEST_ASSERT_TRUE(float_near(big->pos[1], 0.0F, 0.5F)); /* the bigger sibling never received it */
}

/* ================= Scrollbar floating clipTo (clip to the attached container viewport) ================= */

/* Innermost still-open both-axes-false (floating clipTo marker) SCISSOR bbox active at cmd_index. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool floating_clip_at(int32_t cmd_index, Clay_BoundingBox *out_clip) {
    Clay_BoundingBox stack[16];
    bool marker[16];
    int depth = 0;
    for (int32_t i = 0; i < cmd_index; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
            if (depth < 16) {
                stack[depth] = c->boundingBox;
                marker[depth] = !c->renderData.clip.horizontal && !c->renderData.clip.vertical;
                ++depth;
            }
        } else if (c->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END && depth > 0) {
            --depth;
        }
    }
    for (int d = depth - 1; d >= 0; --d) {
        if (marker[d]) {
            if (out_clip != NULL) {
                *out_clip = stack[d];
            }
            return true;
        }
    }
    return false;
}

/* The vertical bar's TRACK image carries the bar id; the THUMB is the other axis-thin image
 * (thumb_len tall, no id). Standalone container is 200x200 at the root origin. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scrollbar_standalone_renders_inside_clip(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    const uint32_t bar_id = nt_ui_scroll_test_bar_id(SCROLL_ID, 1);
    nt_pointer_t p = {0};
    p.active = true;

    scrollbar_frame(&p, &style, 1000.0F); /* overflow -> bar shown */
    scrollbar_frame(&p, &style, 1000.0F);

    const Clay_RenderCommand *track = NULL;
    int32_t track_idx = -1;
    const Clay_RenderCommand *thumb = NULL;
    int32_t thumb_idx = -1;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType != CLAY_RENDER_COMMAND_TYPE_IMAGE) {
            continue;
        }
        if (c->id == bar_id) {
            track = c;
            track_idx = i;
        } else if (c->boundingBox.width > 0.0F && c->boundingBox.height > 0.0F) {
            thumb = c; /* the only other image is the thumb */
            thumb_idx = i;
        }
    }
    TEST_ASSERT_NOT_NULL(track);
    TEST_ASSERT_NOT_NULL(thumb);

    /* clipTo did NOT collapse the bar: both images keep a sane non-zero bbox fully inside the
     * 200x200 container viewport (track == full track_len, thumb a positive slice). */
    TEST_ASSERT_TRUE(track->boundingBox.width > 0.0F && track->boundingBox.height > 0.0F);
    TEST_ASSERT_TRUE(float_near(track->boundingBox.height, 200.0F, 1.0F)); /* full container height */
    TEST_ASSERT_TRUE(thumb->boundingBox.width > 0.0F && thumb->boundingBox.height > 0.0F);

    /* Both are enclosed by a floating-clip SCISSOR == the container viewport, but the viewport
     * fully contains them (standalone over-clip guard: visible == full image, nothing trimmed). */
    Clay_BoundingBox tclip = {0};
    Clay_BoundingBox hclip = {0};
    TEST_ASSERT_TRUE(floating_clip_at(track_idx, &tclip));
    TEST_ASSERT_TRUE(floating_clip_at(thumb_idx, &hclip));
    TEST_ASSERT_TRUE(track->boundingBox.y >= tclip.y - 0.5F);
    TEST_ASSERT_TRUE(track->boundingBox.y + track->boundingBox.height <= tclip.y + tclip.height + 0.5F);
    TEST_ASSERT_TRUE(thumb->boundingBox.y >= hclip.y - 0.5F);
    TEST_ASSERT_TRUE(thumb->boundingBox.y + thumb->boundingBox.height <= hclip.y + hclip.height + 0.5F);
}

/* Sample the walker scissor at the original track/thumb command positions. */
static void read_scrollbar_scissor(const nt_ui_custom_frame_t *frame, void *userdata) {
    (void)frame;
    TEST_ASSERT_TRUE(nt_gfx_test_scissor_enabled());
    nt_gfx_test_scissor_rect(userdata);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scrollbar_nested_clipped_to_outer(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    style.scroll_x = true;
    style.wheel_ease_speed = 0.0F;
    style.bar_thickness = 9.0F;
    style.bar_thumb_min_px = 24.0F;
    nt_ui_scroll_style_t outer_style = style;
    outer_style.track_ref = (nt_atlas_region_ref_t){0};
    outer_style.thumb_ref = (nt_atlas_region_ref_t){0};
    const uint32_t inner_bar_id = nt_ui_scroll_test_bar_id(EXCL_INNER_ID, 1);
    nt_pointer_t p = {0};

    for (int frame = 0; frame < 4; ++frame) {
        nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
        CLAY({.id = CLAY_ID("nested-clip-root"), .layout = {.sizing = {CLAY_SIZING_FIXED(800), CLAY_SIZING_FIXED(600)}, .padding = {.left = 37, .top = 53}}}) {
            nt_ui_scroll_begin(s_fx.ctx, NULL, EXCL_OUTER_ID, &outer_style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(310), CLAY_SIZING_FIXED(190)}}});
            {
                CLAY({.id = CLAY_ID("nested-clip-outer-content"), .layout = {.sizing = {CLAY_SIZING_FIXED(600), CLAY_SIZING_FIXED(900)}}}) {
                    nt_ui_scroll_begin(s_fx.ctx, NULL, EXCL_INNER_ID, &style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(230), CLAY_SIZING_FIXED(260)}}});
                    {
                        CLAY({.id = CLAY_ID("nested-clip-inner-content"), .layout = {.sizing = {CLAY_SIZING_FIXED(600), CLAY_SIZING_FIXED(900)}}}) {}
                    }
                    nt_ui_scroll_end(s_fx.ctx);
                }
            }
            nt_ui_scroll_end(s_fx.ctx);
        }
        nt_ui_end(s_fx.ctx);
        if (frame == 1) {
            nt_ui_scroll_to(s_fx.ctx, EXCL_OUTER_ID, -80.0F, -90.0F);
            nt_ui_scroll_to(s_fx.ctx, EXCL_INNER_ID, -30.0F, -180.0F);
        }
    }

    const nt_ui_bbox_t inner = nt_ui_get_bbox(s_fx.ctx, EXCL_INNER_ID);
    TEST_ASSERT_TRUE(inner.found);
    TEST_ASSERT_TRUE(float_near(inner.x, -43.0F, 0.01F));
    TEST_ASSERT_TRUE(float_near(inner.y, -37.0F, 0.01F));
    Clay_RenderCommand *bars[2] = {NULL, NULL};
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE && float_near(c->boundingBox.width, style.bar_thickness, 0.01F)) {
            const int bar = c->id == inner_bar_id ? 0 : 1;
            TEST_ASSERT_NULL(bars[bar]);
            bars[bar] = c;
        }
    }
    TEST_ASSERT_NOT_NULL(bars[0]);
    TEST_ASSERT_NOT_NULL(bars[1]);

    int scissors[2][4] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}};
    nt_ui_custom_data_t probes[2];
    for (int bar = 0; bar < 2; ++bar) {
        const Clay_BoundingBox b = bars[bar]->boundingBox;
        TEST_ASSERT_TRUE(b.y < 53.0F);
        TEST_ASSERT_TRUE(b.y + b.height > 53.0F);
        TEST_ASSERT_TRUE(b.x >= 37.0F && b.x + b.width <= 187.0F);
        probes[bar] = (nt_ui_custom_data_t){.type = NT_UI_CUSTOM_TYPE_CALLBACK, .data = scissors[bar], .emit = read_scrollbar_scissor};
        bars[bar]->commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM;
        bars[bar]->renderData.custom.customData = &probes[bar];
    }
    const nt_ui_target_t target = {.viewport = {0.0F, 0.0F, 800.0F, 600.0F}};
    nt_ui_walk(s_fx.ctx, &target);

    /* Intersect outer (37,53,310,190) with inner (-43,-37,230,260), then flip Y for GL. */
    const int expected[4] = {37, 377, 150, 170};
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, scissors[0], 4);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, scissors[1], 4);
}

// #region gesture arbitration (real declaration order)
/* A 200x200 list at the layout origin with 1000-tall content. `body` declares what sits inside the
 * list (stepped between its begin and end); `outside_before` / `outside_after` declare root-level
 * floating UI before / after the list. */
typedef struct {
    void (*outside_before)(void);
    void (*body)(void);
    void (*outside_after)(void);
    nt_ui_scroll_style_t style;
    float content_h;
    float content_w; /* 0 = the list width */
    float px_per_dp; /* 0 = keep the identity viewport */
} arb_scene_t;

static arb_scene_t s_scene;
static float s_arb_dt = 1.0F / 60.0F;
static nt_ui_interaction_t s_child_in;
static nt_ui_interaction_t s_fg_in;

static void arb_frame(const nt_pointer_t *pointers, uint32_t count) {
    const nt_ui_viewport_t vp = {.w = 800.0F, .h = 600.0F, .px_per_dp = (s_scene.px_per_dp > 0.0F) ? s_scene.px_per_dp : 1.0F};
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = s_arb_dt, .pointers = pointers, .pointer_count = count, .viewport = vp});
    if (s_scene.outside_before != NULL) {
        s_scene.outside_before();
    }
    CLAY({.id = CLAY_ID("arb_root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, &s_scene.style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}});
        CLAY({.id = CLAY_ID("arb_content"),
              .layout = {.sizing = {CLAY_SIZING_FIXED((s_scene.content_w > 0.0F) ? s_scene.content_w : 200.0F), CLAY_SIZING_FIXED(s_scene.content_h)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
            if (s_scene.body != NULL) {
                s_scene.body();
            }
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    if (s_scene.outside_after != NULL) {
        s_scene.outside_after();
    }
    nt_ui_end(s_fx.ctx);
}

static void arb_scene_reset(void) {
    s_scene = (arb_scene_t){.style = bar_style(NT_UI_SCROLLBAR_ALWAYS), .content_h = 1000.0F}; /* bar art: a bar takes presses only with a thumb */
    s_scene.style.rubber_band_c = 0.0F;                                                        /* raw == the routed drag, easy to read */
    s_arb_dt = 1.0F / 60.0F;
    s_child_in = (nt_ui_interaction_t){0};
    s_fg_in = (nt_ui_interaction_t){0};
}

static nt_pointer_t arb_pointer(float x, float y) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    return p;
}

static void arb_press(nt_pointer_t *p) {
    p->buttons[NT_BUTTON_LEFT].is_down = true;
    p->buttons[NT_BUTTON_LEFT].is_pressed = true;
}

static void arb_hold(nt_pointer_t *p) { p->buttons[NT_BUTTON_LEFT].is_pressed = false; }

static void arb_release(nt_pointer_t *p) {
    p->buttons[NT_BUTTON_LEFT].is_pressed = false;
    p->buttons[NT_BUTTON_LEFT].is_down = false;
    p->buttons[NT_BUTTON_LEFT].is_released = true;
}

static const nt_ui_scroll_state_t *arb_list(void) { return (const nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID); }

/* Bodies: a full-width 60-tall row, as a button, a disabled row, or an x-draggable. */
static void body_button_row(void) {
    CLAY({.id = CLAY_ID("arb_row"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}}}) { s_child_in = nt_ui_step(s_fx.ctx, CLAY_ID("arb_row").id, NULL); }
}
static void body_disabled_row(void) {
    CLAY({.id = CLAY_ID("arb_row"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}}}) { nt_ui_block_pointer(s_fx.ctx, CLAY_ID("arb_row").id, NULL); }
}
static void body_x_draggable_row(void) {
    CLAY({.id = CLAY_ID("arb_row"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(60)}}}) {
        s_child_in = nt_ui_step(s_fx.ctx, CLAY_ID("arb_row").id, &(nt_ui_step_desc_t){.drag_axes = NT_UI_DRAG_X});
    }
}
/* A floating foreground button over the list, declared OUTSIDE it (before). */
static void outside_fg_button(void) {
    CLAY({.id = CLAY_ID("arb_fg"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 10, .offset = {.x = 50.0F, .y = 50.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(80)}}}) {
        s_fg_in = nt_ui_step(s_fx.ctx, CLAY_ID("arb_fg").id, NULL);
    }
}
/* An inert modal catcher over the whole screen, declared after the list. */
static void outside_modal_blocker(void) {
    CLAY({.id = CLAY_ID("arb_modal"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 10}, .layout = {.sizing = {CLAY_SIZING_FIXED(800), CLAY_SIZING_FIXED(600)}}}) {
        nt_ui_block_pointer(s_fx.ctx, CLAY_ID("arb_modal").id, NULL);
    }
}

static void arb_warm(nt_pointer_t *p) {
    arb_frame(p, 1);
    arb_frame(p, 1);
}

/* A widget declared outside a list, floating over it, keeps its drag. */
static void test_arb_foreground_outside_list_not_stolen(void) {
    s_scene.outside_before = outside_fg_button;
    nt_pointer_t p = arb_pointer(90.0F, 90.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(CLAY_ID("arb_fg").id, s_fx.ctx->captures[0].active_id);
    arb_hold(&p);
    p.y = 50.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(CLAY_ID("arb_fg").id, s_fx.ctx->captures[0].active_id);
    TEST_ASSERT_TRUE(float_near(arb_list()->raw[1], 0.0F, 0.001F));
}

/* An inert modal over a flinging list: the press neither stops the fling nor scrolls the list. */
static void test_arb_modal_blocks_tap_to_stop_and_drag(void) {
    s_scene.outside_after = outside_modal_blocker;
    nt_pointer_t p = arb_pointer(100.0F, 100.0F);
    arb_warm(&p);
    ((nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID))->vel[1] = -2000.0F;
    arb_press(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->captures[0].active_id);
    TEST_ASSERT_TRUE(fabsf(arb_list()->vel[1]) > 100.0F); /* still flinging */
    arb_hold(&p);
    p.y = 60.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->captures[0].active_id);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx)); /* the modal is UI under the pointer */
}

/* A drag that starts on a disabled row (an inert blocker inside the list) still scrolls. */
static void test_arb_drag_from_disabled_row_scrolls(void) {
    s_scene.body = body_disabled_row;
    nt_pointer_t p = arb_pointer(100.0F, 30.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    p.y = 0.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[0].active_id);
    TEST_ASSERT_TRUE(float_near(arb_list()->raw[1], -22.0F, 0.001F));
}

/* A thumb arc (14, 10): the list takes it once its own axis crosses the slop, despite more x. */
static void test_arb_diagonal_arc_scrolls_vertical_list(void) {
    s_scene.body = body_button_row;
    nt_pointer_t p = arb_pointer(100.0F, 30.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    p.x = 114.0F;
    p.y = 20.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[0].active_id);
    arb_release(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_FALSE(s_child_in.clicked); /* the row was cancelled */
}

/* Both axes cross in one frame: the stronger crossing wins — a mostly-vertical flick scrolls the
 * list, a mostly-horizontal one stays with the x-draggable row. */
static void test_arb_strongest_crossing_wins(void) {
    s_scene.body = body_x_draggable_row;
    nt_pointer_t p = arb_pointer(100.0F, 40.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(s_child_in.gesture_pending); /* the list may still take it */
    arb_hold(&p);
    p.x = 110.0F;
    p.y = 12.0F; /* (10, -28) */
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[0].active_id);
}

static void test_arb_strongest_crossing_keeps_horizontal_with_row(void) {
    s_scene.body = body_x_draggable_row;
    nt_pointer_t p = arb_pointer(100.0F, 40.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    p.x = 128.0F;
    p.y = 30.0F; /* (28, -10) */
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(CLAY_ID("arb_row").id, s_fx.ctx->captures[0].active_id);
    TEST_ASSERT_FALSE(s_child_in.gesture_pending);
}

/* A second finger dragging the same list takes it over; the first finger lets go of it. */
static void test_arb_newest_pointer_takes_list(void) {
    nt_pointer_t ps[2] = {arb_pointer(50.0F, 150.0F), arb_pointer(150.0F, 150.0F)};
    ps[1].id = 7U;
    arb_frame(ps, 2);
    arb_frame(ps, 2);
    arb_press(&ps[0]);
    arb_frame(ps, 2);
    arb_hold(&ps[0]);
    ps[0].y = 120.0F;
    arb_frame(ps, 2);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[0].active_id);
    arb_press(&ps[1]);
    arb_frame(ps, 2);
    arb_hold(&ps[1]);
    ps[1].y = 120.0F;
    arb_frame(ps, 2);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[1].active_id);
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->captures[0].active_id);
}

/* A list whose content fits is see-through; an overflowing one counts as UI over its empty area. */
static void test_arb_fitting_list_is_see_through(void) {
    s_scene.content_h = 100.0F;
    nt_pointer_t p = arb_pointer(100.0F, 150.0F);
    arb_warm(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx));
    s_scene.content_h = 1000.0F;
    arb_warm(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx));
}

/* The slop is physical: at 3 device px per dp the 8 dp slop is 24 layout units here. */
static void test_arb_slop_is_in_dp(void) {
    s_scene.px_per_dp = 3.0F;
    nt_pointer_t p = arb_pointer(100.0F, 150.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    p.y = 130.0F; /* 20 units: below 24 */
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->captures[0].active_id);
    p.y = 120.0F; /* 30 units: past it, the excess 6 is routed */
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[0].active_id);
    TEST_ASSERT_TRUE(float_near(arb_list()->raw[1], -6.0F, 0.001F));
}

/* Nested X track inside the Y list: each axis goes to the scroll that moves on it, also for wheel. */
static void body_x_track(void) {
    nt_ui_scroll_style_t st = nt_ui_scroll_style_defaults();
    st.scroll_x = true;
    st.scroll_y = false;
    st.rubber_band_c = 0.0F;
    st.wheel_ease_speed = 0.0F;
    nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID + 1U, &st, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(100)}}});
    CLAY({.id = CLAY_ID("arb_track_content"), .layout = {.sizing = {CLAY_SIZING_FIXED(1000), CLAY_SIZING_FIXED(100)}}}) {}
    nt_ui_scroll_end(s_fx.ctx);
}

static void test_arb_nested_axes_route_to_their_scroll(void) {
    s_scene.body = body_x_track;
    s_scene.style.wheel_ease_speed = 0.0F;
    nt_pointer_t p = arb_pointer(100.0F, 50.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    p.y = 20.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[0].active_id); /* vertical: the outer list */
}

static void test_arb_nested_horizontal_drag_goes_to_track(void) {
    s_scene.body = body_x_track;
    nt_pointer_t p = arb_pointer(100.0F, 50.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    p.x = 70.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID + 1U, s_fx.ctx->captures[0].active_id); /* horizontal: the track */
}

static void test_arb_nested_vertical_wheel_passes_to_list(void) {
    s_scene.body = body_x_track;
    s_scene.style.wheel_ease_speed = 0.0F;
    nt_pointer_t p = arb_pointer(100.0F, 50.0F);
    arb_warm(&p);
    const float before = arb_list()->pos[1];
    p.wheel_dy = 1.0F; /* vertical wheel over the X track reaches the Y list */
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(arb_list()->pos[1] < before);
}

/* A tap below the slop on a row inside a list clicks it and leaves the list at rest. */
static void test_arb_tap_below_slop_clicks(void) {
    s_scene.body = body_button_row;
    nt_pointer_t p = arb_pointer(100.0F, 30.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(s_child_in.gesture_pending);
    arb_hold(&p);
    p.x = 103.0F;
    p.y = 27.0F;
    arb_frame(&p, 1);
    arb_release(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(s_child_in.clicked);
    TEST_ASSERT_TRUE(float_near(arb_list()->raw[1], 0.0F, 0.001F));
}

/* A scroll claim is not a press: it reports no hold left in its pointer slot. */
static void test_arb_claim_reports_no_hold(void) {
    s_scene.body = body_button_row;
    nt_pointer_t p = arb_pointer(100.0F, 30.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    leave_stale_hold_in_slot();
    p.y = 0.0F;
    arb_frame(&p, 1);
    assert_scroll_claim_reports_no_hold();
}

/* A drag on dt == 0 frames samples no velocity: the release does not fling. */
static void test_arb_dt_zero_drag_no_phantom_fling(void) {
    s_scene.body = body_button_row;
    nt_pointer_t p = arb_pointer(100.0F, 150.0F);
    arb_warm(&p);
    s_arb_dt = 0.0F;
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    p.y = 120.0F;
    arb_frame(&p, 1);
    p.y = 90.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[0].active_id);
    arb_release(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(float_near(arb_list()->vel[1], 0.0F, 0.001F));
}

/* The slop first crossed on the release frame, outside every hitbox: the list takes it, flings, and
 * the game does not see a world release that frame. */
static void test_arb_release_crossing_outside_flings(void) {
    nt_pointer_t p = arb_pointer(100.0F, 150.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    p.y = 146.0F;
    arb_frame(&p, 1);
    arb_release(&p);
    p.x = 300.0F;
    p.y = 110.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[0].active_id);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx));
    TEST_ASSERT_TRUE(arb_list()->vel[1] < -100.0F);
    TEST_ASSERT_TRUE(float_near(arb_list()->pos[1], -32.0F, 0.5F)); /* the 40 move minus the slop */
}

/* A press on the thumb of a flinging list grabs the thumb (no tap-to-stop claim on the list). */
static void test_arb_thumb_press_during_fling_grabs_bar(void) {
    s_scene.style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    nt_pointer_t p = arb_pointer(195.0F, 20.0F);
    arb_warm(&p);
    ((nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID))->vel[1] = -1500.0F;
    arb_press(&p);
    arb_frame(&p, 1);
    assert_thumb_owner(0U, SCROLL_ID, 1);
}

/* A thumb press by a second finger takes the list from the finger dragging its content. */
static void test_arb_thumb_press_takes_list_from_content_drag(void) {
    s_scene.style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    nt_pointer_t ps[2] = {arb_pointer(80.0F, 150.0F), arb_pointer(195.0F, 20.0F)};
    ps[1].id = 7U;
    arb_frame(ps, 2);
    arb_frame(ps, 2);
    arb_press(&ps[0]);
    arb_frame(ps, 2);
    arb_hold(&ps[0]);
    ps[0].y = 130.0F;
    arb_frame(ps, 2);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[0].active_id);
    arb_press(&ps[1]);
    arb_frame(ps, 2);
    assert_thumb_owner(1U, SCROLL_ID, 1);
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->captures[0].active_id);
}

/* A floating panel declared inside the list takes no clip, so no scroll is on its chain: it keeps its drags. */
static void body_barrier_panel(void) {
    CLAY({.id = CLAY_ID("arb_popup"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 10, .offset = {.x = 50.0F, .y = 50.0F}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(80)}}}) {
        s_fg_in = nt_ui_step(s_fx.ctx, CLAY_ID("arb_popup").id, NULL);
    }
}

static void test_arb_unclipped_floating_keeps_drag_from_list(void) {
    s_scene.body = body_barrier_panel;
    nt_pointer_t p = arb_pointer(90.0F, 90.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_FALSE(s_fg_in.gesture_pending); /* no scroll on its chain */
    arb_hold(&p);
    p.y = 50.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(CLAY_ID("arb_popup").id, s_fx.ctx->captures[0].active_id);
    TEST_ASSERT_TRUE(float_near(arb_list()->raw[1], 0.0F, 0.001F));
}

/* A real popup opened from inside a list: a drag on its content never scrolls the list behind it. */
static void body_real_popup(void) {
    nt_ui_popup_style_t st = nt_ui_popup_style_defaults();
    st.ease_speed = 0.0F;
    const nt_ui_popup_anchor_t anchor = {.x = 40.0F, .y = 20.0F, .w = 80.0F, .h = 30.0F, .prefer_side = NT_UI_POPUP_BELOW};
    (void)nt_ui_popup_begin(s_fx.ctx, CLAY_ID("arb_real_popup").id, &st, &anchor, true);
    CLAY({.id = CLAY_ID("arb_popup_btn"), .layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(80)}}}) { s_fg_in = nt_ui_step(s_fx.ctx, CLAY_ID("arb_popup_btn").id, NULL); }
    nt_ui_popup_end(s_fx.ctx);
}

static void test_arb_popup_in_list_keeps_drag_from_list(void) {
    s_scene.body = body_real_popup;
    nt_pointer_t p = arb_pointer(80.0F, 90.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(s_fg_in.pressed_now);
    TEST_ASSERT_FALSE(s_fg_in.gesture_pending);
    arb_hold(&p);
    p.y = 50.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(CLAY_ID("arb_popup_btn").id, s_fx.ctx->captures[0].active_id);
    TEST_ASSERT_TRUE(float_near(arb_list()->raw[1], 0.0F, 0.001F));
}

/* A tap's pending gesture ends with its frame: a button found held when the context runs again
 * (frames skipped, no edges) is not that press and drags nothing. */
static void test_arb_tap_gesture_does_not_outlive_its_frame(void) {
    nt_pointer_t p = arb_pointer(100.0F, 150.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_release(&p);
    arb_frame(&p, 1);
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true};
    p.y = 110.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->captures[0].active_id);
    TEST_ASSERT_TRUE(float_near(arb_list()->raw[1], 0.0F, 0.001F));
}

/* A thumb press over a full-width row takes the thumb, not the row under it. */
static void test_arb_thumb_over_row_takes_the_thumb(void) {
    s_scene.body = body_button_row;
    nt_pointer_t p = arb_pointer(195.0F, 20.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    assert_thumb_owner(0U, SCROLL_ID, 1);
    TEST_ASSERT_FALSE(s_child_in.pressed);
}

/* bar_hit_pad widens the bar's touch target toward the content and wins over the row under it; 0
 * leaves that strip to the row. The bar is 188..200; 180 is inside a 12 pad. */
static void test_arb_bar_hit_pad_is_the_style_s(void) {
    s_scene.body = body_button_row;
    nt_pointer_t p = arb_pointer(180.0F, 20.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    assert_thumb_owner(0U, SCROLL_ID, 1);
    arb_release(&p);
    arb_frame(&p, 1);
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){0};
    s_scene.style.bar_hit_pad = 0.0F;
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(s_child_in.pressed_now);
}

/* A thumb drag moves the content in the frame of the move: grabbed 20 into a 40 thumb on a 200 track,
 * the pointer at 60 is frac (60 - 20) / 160 of the 800 overflow. */
static void test_arb_thumb_drag_moves_content_the_same_frame(void) {
    nt_pointer_t p = arb_pointer(195.0F, 20.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    p.y = 60.0F;
    arb_frame(&p, 1);
    /* The laid-out content, not the state cell: the offset this frame's layout was given. */
    const nt_ui_bbox_t content = nt_ui_get_bbox(s_fx.ctx, CLAY_ID("arb_content").id);
    TEST_ASSERT_TRUE(content.found && float_near(content.y, -200.0F, 0.5F));
}

/* A bar takes presses only while it shows a thumb that can move: over a list that fits, and over a
 * bar with no art, the strip belongs to the row under it. */
static void test_arb_bar_without_a_movable_thumb_takes_no_press(void) {
    s_scene.body = body_button_row;
    s_scene.content_h = 100.0F; /* fits */
    nt_pointer_t p = arb_pointer(195.0F, 20.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(s_child_in.pressed_now);

    arb_release(&p);
    arb_frame(&p, 1);
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){0};
    s_scene.content_h = 1000.0F;
    s_scene.style = nt_ui_scroll_style_defaults(); /* overflows, but the bar has no art */
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(s_child_in.pressed_now);
}

/* Nested lists with aligned right edges: the inner bar paints over the outer one and takes the press. */
static void body_inner_list_with_bar(void) {
    const nt_ui_scroll_style_t st = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID + 1U, &st, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(150)}}});
    CLAY({.id = CLAY_ID("arb_inner_content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(600)}}}) {}
    nt_ui_scroll_end(s_fx.ctx);
}
static void test_arb_inner_bar_over_outer_bar_takes_the_press(void) {
    s_scene.body = body_inner_list_with_bar;
    nt_pointer_t p = arb_pointer(195.0F, 10.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    assert_thumb_owner(0U, SCROLL_ID + 1U, 1);
}

/* The wheel does not move the axis a thumb holds. */
static void test_arb_wheel_does_not_move_a_held_thumb(void) {
    nt_pointer_t p = arb_pointer(195.0F, 20.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    p.y = 60.0F;
    arb_frame(&p, 1);
    const float held = arb_list()->pos[1];
    p.wheel_dy = 1.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(float_near(arb_list()->pos[1], held, 0.001F));
}

/* A pointer slot the game stops passing holds no pending gesture. */
static void test_arb_dropped_pointer_slot_holds_no_gesture(void) {
    nt_pointer_t ps[2] = {arb_pointer(700.0F, 500.0F), arb_pointer(100.0F, 150.0F)};
    arb_frame(ps, 2);
    arb_frame(ps, 2);
    arb_press(&ps[1]);
    arb_frame(ps, 2);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx));
    arb_frame(ps, 1);
    TEST_ASSERT_FALSE(nt_ui_wants_pointer(s_fx.ctx));
}

/* The container clips its bar: a press past its edge is not a thumb press. */
static void test_arb_bar_is_clipped_to_its_container(void) {
    nt_pointer_t p = arb_pointer(203.0F, 20.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->captures[0].active_id);
}

/* A pending press keeps the pointer for the UI after it leaves the list: the list may still take it. */
static void test_arb_pending_press_wants_pointer_off_the_list(void) {
    nt_pointer_t p = arb_pointer(100.0F, 150.0F);
    arb_warm(&p);
    arb_press(&p);
    arb_frame(&p, 1);
    arb_hold(&p);
    p.x = 500.0F;
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(nt_ui_wants_pointer(s_fx.ctx));
}

/* A track tap centers the thumb under the pointer, like a held track press: 150 on a 200 track with a
 * 40 thumb is frac (150 - 20) / 160 of the 800 overflow. */
static void test_arb_track_tap_centers_the_thumb(void) {
    nt_pointer_t p = arb_pointer(195.0F, 150.0F);
    arb_warm(&p);
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_pressed = true, .is_released = true};
    arb_frame(&p, 1);
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){0};
    for (int i = 0; i < 240; ++i) {
        arb_frame(&p, 1);
    }
    TEST_ASSERT_TRUE(float_near(arb_list()->pos[1], -650.0F, 1.0F));
}

/* The wheel over a scrollbar scrolls its list. */
static void test_arb_wheel_over_bar_scrolls_list(void) {
    s_scene.style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    s_scene.style.wheel_ease_speed = 0.0F;
    nt_pointer_t p = arb_pointer(195.0F, 100.0F);
    arb_warm(&p);
    p.wheel_dy = 1.0F;
    arb_frame(&p, 1);
    arb_frame(&p, 1);
    TEST_ASSERT_TRUE(arb_list()->pos[1] < -1.0F);
}

/* A second finger takes a list whose thumb just moved: the content drag starts from the thumb's position. */
static void test_arb_content_take_keeps_last_thumb_move(void) {
    s_scene.style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    s_scene.style.rubber_band_c = 0.0F;
    nt_pointer_t ps[2] = {arb_pointer(195.0F, 10.0F), arb_pointer(80.0F, 150.0F)};
    ps[1].id = 7U;
    arb_frame(ps, 2);
    arb_frame(ps, 2);
    arb_press(&ps[0]);
    arb_frame(ps, 2);
    arb_hold(&ps[0]);
    ps[0].y = 30.0F;
    arb_frame(ps, 2);
    ps[0].y = 50.0F; /* the thumb moves in the same frame the second finger presses */
    arb_press(&ps[1]);
    arb_frame(ps, 2);
    const float after_thumb = arb_list()->pos[1];
    TEST_ASSERT_TRUE(after_thumb < -1.0F);
    arb_hold(&ps[1]);
    ps[1].y = 110.0F; /* 40 up: claims the list, the excess past the 8 slop routes */
    arb_frame(ps, 2);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, s_fx.ctx->captures[1].active_id);
    TEST_ASSERT_TRUE(float_near(arb_list()->pos[1], after_thumb - 32.0F, 0.5F));
}

/* Two fingers press both bars of one list in one frame: one takes it, and a still frame moves nothing. */
static void test_arb_both_bars_pressed_together(void) {
    s_scene.style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    s_scene.style.scroll_x = true;
    s_scene.content_w = 1000.0F;
    nt_pointer_t ps[2] = {arb_pointer(30.0F, 195.0F), arb_pointer(195.0F, 10.0F)};
    ps[1].id = 7U;
    arb_frame(ps, 2);
    arb_frame(ps, 2);
    arb_press(&ps[0]);
    arb_press(&ps[1]);
    arb_frame(ps, 2);
    const float x0 = arb_list()->pos[0];
    const float y0 = arb_list()->pos[1];
    arb_hold(&ps[0]);
    arb_hold(&ps[1]);
    arb_frame(ps, 2);
    TEST_ASSERT_TRUE(s_fx.ctx->captures[0].active_id == 0U || s_fx.ctx->captures[1].active_id == 0U);
    TEST_ASSERT_TRUE(float_near(arb_list()->pos[0], x0, 0.001F));
    TEST_ASSERT_TRUE(float_near(arb_list()->pos[1], y0, 0.001F));
}

/* A tap on the second bar while the first is held: a still frame moves nothing. */
static void test_arb_tap_on_other_bar_while_held(void) {
    s_scene.style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    s_scene.style.scroll_x = true;
    s_scene.content_w = 1000.0F;
    nt_pointer_t ps[2] = {arb_pointer(30.0F, 195.0F), arb_pointer(195.0F, 10.0F)};
    ps[1].id = 7U;
    arb_frame(ps, 2);
    arb_frame(ps, 2);
    arb_press(&ps[0]);
    arb_frame(ps, 2);
    arb_hold(&ps[0]);
    const float x0 = arb_list()->pos[0];
    ps[1].buttons[NT_BUTTON_LEFT].is_pressed = true;
    ps[1].buttons[NT_BUTTON_LEFT].is_released = true;
    arb_frame(ps, 2);
    ps[1].buttons[NT_BUTTON_LEFT].is_pressed = false;
    ps[1].buttons[NT_BUTTON_LEFT].is_released = false;
    arb_frame(ps, 2);
    TEST_ASSERT_TRUE(float_near(arb_list()->pos[0], x0, 0.001F));
}

/* A second finger pressing a held thumb takes it (newest pointer wins). */
static void test_arb_second_finger_takes_held_bar(void) {
    s_scene.style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    nt_pointer_t ps[2] = {arb_pointer(195.0F, 10.0F), arb_pointer(195.0F, 12.0F)};
    ps[1].id = 7U;
    arb_frame(ps, 2);
    arb_frame(ps, 2);
    arb_press(&ps[0]);
    arb_frame(ps, 2);
    arb_hold(&ps[0]);
    arb_press(&ps[1]);
    arb_frame(ps, 2);
    assert_thumb_owner(1U, SCROLL_ID, 1);
    TEST_ASSERT_EQUAL_UINT32(0U, s_fx.ctx->captures[0].active_id);
}

/* rubber_band_c == 0: a drag past the edge leaves no dead zone — reversing moves content at once. */
static void test_hard_edge_drag_reversal_has_no_dead_zone(void) {
    const nt_ui_scroll_style_t style = {.friction = 0.92F, .wheel_step_px = 40.0F};
    const float content[2] = {0.0F, 1000.0F};
    const float container[2] = {0.0F, 200.0F};
    const float wheel[2] = {0.0F, 0.0F};
    nt_ui_scroll_state_t s = {0};
    s.raw[1] = 50.0F; /* dragged 50 past the top edge */
    nt_ui_scroll_test_integrate(&s, wheel, 1.0F / 60.0F, content, container, &style, true);
    TEST_ASSERT_TRUE(float_near(s.pos[1], 0.0F, 0.001F));
    s.raw[1] -= 10.0F; /* finger reverses 10 */
    nt_ui_scroll_test_integrate(&s, wheel, 1.0F / 60.0F, content, container, &style, true);
    TEST_ASSERT_TRUE(float_near(s.pos[1], -10.0F, 0.001F));
}
// #endregion

#if NT_ASSERT_MODE == NT_ASSERT_FULL
/* nt_input publishes a press that follows a release one frame later; a frame with both is not input. */
static void release_with_newer_press_frame(void) {
    nt_pointer_t p = arb_pointer(100.0F, 150.0F);
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = true, .is_pressed = true, .is_released = true};
    arb_frame(&p, 1);
}
static void test_assert_release_with_newer_press(void) { NT_TEST_EXPECT_ASSERT(release_with_newer_press_frame()); }

/* A begin without its end leaves an element open at nt_ui_end. */
static void missing_scroll_end_frame(void) {
    nt_pointer_t p = arb_pointer(0.0F, 0.0F);
    const nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_ui_begin(s_fx.ctx, &(nt_ui_frame_desc_t){.layout_w = 800.0F, .layout_h = 600.0F, .dt = 1.0F / 60.0F, .pointers = &p, .pointer_count = 1});
    nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, &style, NULL);
    nt_ui_end(s_fx.ctx);
}
static void test_assert_missing_scroll_end(void) { NT_TEST_EXPECT_ASSERT(missing_scroll_end_frame()); }
#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_arb_foreground_outside_list_not_stolen);
    RUN_TEST(test_arb_bar_hit_pad_is_the_style_s);
    RUN_TEST(test_arb_thumb_drag_moves_content_the_same_frame);
    RUN_TEST(test_arb_bar_is_clipped_to_its_container);
    RUN_TEST(test_arb_bar_without_a_movable_thumb_takes_no_press);
    RUN_TEST(test_arb_inner_bar_over_outer_bar_takes_the_press);
    RUN_TEST(test_arb_wheel_does_not_move_a_held_thumb);
    RUN_TEST(test_arb_dropped_pointer_slot_holds_no_gesture);
    RUN_TEST(test_arb_tap_gesture_does_not_outlive_its_frame);
    RUN_TEST(test_arb_thumb_over_row_takes_the_thumb);
    RUN_TEST(test_arb_pending_press_wants_pointer_off_the_list);
    RUN_TEST(test_arb_track_tap_centers_the_thumb);
    RUN_TEST(test_arb_modal_blocks_tap_to_stop_and_drag);
    RUN_TEST(test_arb_drag_from_disabled_row_scrolls);
    RUN_TEST(test_arb_diagonal_arc_scrolls_vertical_list);
    RUN_TEST(test_arb_strongest_crossing_wins);
    RUN_TEST(test_arb_strongest_crossing_keeps_horizontal_with_row);
    RUN_TEST(test_arb_newest_pointer_takes_list);
    RUN_TEST(test_arb_fitting_list_is_see_through);
    RUN_TEST(test_arb_slop_is_in_dp);
    RUN_TEST(test_arb_nested_axes_route_to_their_scroll);
    RUN_TEST(test_arb_nested_horizontal_drag_goes_to_track);
    RUN_TEST(test_arb_nested_vertical_wheel_passes_to_list);
    RUN_TEST(test_arb_tap_below_slop_clicks);
    RUN_TEST(test_arb_claim_reports_no_hold);
    RUN_TEST(test_arb_dt_zero_drag_no_phantom_fling);
    RUN_TEST(test_arb_release_crossing_outside_flings);
    RUN_TEST(test_arb_thumb_press_during_fling_grabs_bar);
    RUN_TEST(test_arb_thumb_press_takes_list_from_content_drag);
    RUN_TEST(test_arb_unclipped_floating_keeps_drag_from_list);
    RUN_TEST(test_arb_popup_in_list_keeps_drag_from_list);
    RUN_TEST(test_arb_wheel_over_bar_scrolls_list);
    RUN_TEST(test_arb_content_take_keeps_last_thumb_move);
    RUN_TEST(test_arb_second_finger_takes_held_bar);
    RUN_TEST(test_arb_both_bars_pressed_together);
    RUN_TEST(test_arb_tap_on_other_bar_while_held);
    RUN_TEST(test_hard_edge_drag_reversal_has_no_dead_zone);
    RUN_TEST(test_scroll_momentum_decays_monotonic);
    RUN_TEST(test_scroll_takes_real_child_and_tracks_the_pointer);
    RUN_TEST(test_scroll_release_crossing_threshold_allows_next_press);
    RUN_TEST(test_scroll_hard_edges_stop_release_fling);
    RUN_TEST(test_scroll_threshold_ignores_disabled_axis);
    RUN_TEST(test_scroll_clamp_holds);
    RUN_TEST(test_scroll_rubber_band_compresses);
    RUN_TEST(test_scroll_to_reaches_target);
    RUN_TEST(test_scroll_wheel_sign);
    RUN_TEST(test_scroll_wheel_applied_once);
    RUN_TEST(test_scroll_childoffset_from_our_pos);
    RUN_TEST(test_scroll_empty_press_claims_report_no_hold);
    RUN_TEST(test_scrollbar_auto_only_on_overflow);
    RUN_TEST(test_scrollbar_always_shows);
    RUN_TEST(test_scrollbar_standalone_no_z_lift);
    RUN_TEST(test_scrollbar_thumb_size_ratio);
    RUN_TEST(test_scrollbar_thumb_pos_clay_sign);
    RUN_TEST(test_scrollbar_track_click_scroll_to);
    RUN_TEST(test_scrollbar_auto_hide_fades);
    RUN_TEST(test_scrollbar_id_derivation_distinct);
    RUN_TEST(test_scroll_to_top_from_button_sequence);
    RUN_TEST(test_scroll_empty_press_drag_and_fling);
    RUN_TEST(test_scroll_empty_press_tap_no_scroll);
    RUN_TEST(test_scroll_does_not_take_own_scrollbar);
    RUN_TEST(test_scroll_does_not_take_nested_scrollbar);
    RUN_TEST(test_scroll_empty_press_outside_bbox_ignored);
    RUN_TEST(test_scroll_empty_press_tracks_finger_1to1);
    RUN_TEST(test_scroll_empty_press_slow_cross_no_velocity_spike);
    RUN_TEST(test_scroll_held_still_in_bounds_frozen);
    RUN_TEST(test_scroll_held_still_past_edge_frozen);
    RUN_TEST(test_scroll_release_after_hold_no_fling);
    RUN_TEST(test_scroll_raw_anchored_rubber_no_hysteresis);
    RUN_TEST(test_scroll_held_no_momentum_midhold);
    RUN_TEST(test_scroll_cross_container_no_adoption);
    RUN_TEST(test_scroll_held_entry_no_edge_no_gesture);
    RUN_TEST(test_scroll_release_reclaims_on_next_edge);
    RUN_TEST(test_scrollbar_auto_hide_fades_in_on_activity);
    RUN_TEST(test_scrollbar_auto_hide_hover_keeps_visible);
    RUN_TEST(test_scrollbar_auto_hide_born_idle_no_flash);
    RUN_TEST(test_scrollbar_always_born_visible);
    RUN_TEST(test_scroll_wheel_kills_fling);
    RUN_TEST(test_scroll_velocity_clamp_on_dt_hitch);
    RUN_TEST(test_scroll_no_overflow_takes_no_gesture);
    RUN_TEST(test_scroll_tap_stops_fling_and_swallows);
    RUN_TEST(test_scroll_tap_on_resting_does_not_swallow);
    RUN_TEST(test_scrollbar_thumb_grab_offset_no_jump);
    RUN_TEST(test_scroll_overscroll_depth_fps_independent);
    RUN_TEST(test_scroll_content_shrink_clamps_pos);
    RUN_TEST(test_scroll_fling_survives_release_on_zero_delta_frame);
    RUN_TEST(test_scroll_wheel_no_broadcast_under_churn);
    RUN_TEST(test_scroll_wheel_nested_innermost_wins);
    RUN_TEST(test_scroll_wheel_nested_outer_band);
    RUN_TEST(test_scroll_wheel_front_most_sibling_wins);
    RUN_TEST(test_scrollbar_standalone_renders_inside_clip);
    RUN_TEST(test_scrollbar_nested_clipped_to_outer);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_assert_release_with_newer_press);
    RUN_TEST(test_assert_missing_scroll_end);
#endif
    return UNITY_END();
}
