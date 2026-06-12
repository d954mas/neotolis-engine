/* Scroll container + custom engine physics tests.
 *
 * Physics (momentum decay, clamp, rubber-band, scroll-to, Clay sign) drive the
 * static integrator directly via the NT_TEST_ACCESS probe — no Clay frame needed.
 * Container + capture-steal cases declare a real scroll element and read back the
 * fed childOffset. UNITY_EXCLUDE_FLOAT: compare via int32 casts / eps. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_scroll.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* {friction, wheel_ease, rubber_c, bounce, wheel_step}. */
static const float s_tun[5] = {0.92F, 18.0F, 0.55F, 12.0F, 40.0F};

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
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
        nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, s_tun);
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
        nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, s_tun);
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
        nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, s_tun);
        ++frames;
    }
    TEST_ASSERT_TRUE(frames < 600);                                                   /* arrived */
    TEST_ASSERT_EQUAL_UINT8(0U, s.flags & NT_UI_SCROLL_FLAG_SCROLL_TO);               /* flag cleared */
    TEST_ASSERT_TRUE(float_near(s.pos[1], -500.0F, NT_UI_SCROLL_STEAL_THRESHOLD_PX)); /* near target */
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
        nt_ui_scroll_test_integrate(&s, (i == 0) ? wheel_down : none, 1.0F / 60.0F, content, container, s_tun);
    }
    /* target moved by wheel * wheel_step = -40; pos eased toward it (negative). */
    TEST_ASSERT_TRUE(float_near(s.target[1], -40.0F, 0.001F));
    TEST_ASSERT_TRUE(s.pos[1] < 0.0F);
    TEST_ASSERT_TRUE(float_near(s.pos[1], -40.0F, 1.0F));
}

/* ---- Container frame helper: a 200x200 scroll over 200x1000 content (y overflows). ---- */
static const uint32_t SCROLL_ID = 0x5C0011U;

static void scroll_frame_dt(nt_pointer_t *p, const nt_ui_scroll_style_t *style, float dt) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, dt, p, 1);
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

/* ---- Test 6: wheel applied ONCE — one notch moves childOffset by the wheel contribution ---- */
static void test_scroll_wheel_applied_once(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    style.wheel_ease_speed = 0.0F; /* instant ease so one frame lands exactly on target */
    style.wheel_step_px = 40.0F;

    nt_pointer_t p = {0};
    p.x = 100.0F; /* inside the 200x200 container */
    p.y = 100.0F;
    p.active = true;
    /* Frame 1 solves the bbox; frame 2's end-of-frame resolve then grants this pointer's wheel owner
     * (innermost-wins routing has a 1-frame ownership lag, same as every prev-frame bbox hit-test). */
    scroll_frame(&p, &style);
    scroll_frame(&p, &style);
    /* Frame 3: one wheel notch down — the owner resolved at frame 2's end now lets us consume it. */
    p.wheel_dy = 1.0F; /* input-edge negates to Clay -1 -> target -40 */
    scroll_frame(&p, &style);

    float ox = 0.0F;
    float oy = 0.0F;
    nt_ui_scroll_test_last_child_offset(&ox, &oy);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, nt_ui_scroll_test_last_scroll_id());
    /* Exactly ONE wheel contribution (-1 * 40); a Clay double-drive would be -80. */
    TEST_ASSERT_TRUE(float_near(oy, -40.0F, 0.5F));
    TEST_ASSERT_TRUE(float_near(ox, 0.0F, 0.001F)); /* y-only style */
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

    float oy = 0.0F;
    nt_ui_scroll_test_last_child_offset(NULL, &oy);
    TEST_ASSERT_TRUE(float_near(oy, -150.0F, 1.0F)); /* emitted offset == our pos */
}

/* Inject a simulated inner-widget capture on pointer 0 with the given press/cur pos,
 * marking it seen so nt_ui_begin's orphan-clear preserves it. */
static void inject_inner_capture(uint32_t inner_id, float press_x, float press_y, float cur_x, float cur_y) {
    s_fx.ctx->captures[0].active_id = inner_id;
    s_fx.ctx->captures[0].press_pos[0] = press_x;
    s_fx.ctx->captures[0].press_pos[1] = press_y;
    s_fx.ctx->captures[0].pos[0] = cur_x;
    s_fx.ctx->captures[0].pos[1] = cur_y;
    s_fx.ctx->capture_seen[0] = 1U;
}

/* ---- Test 8: capture-steal — drag>threshold cancels inner capture AND claims it, but routes ZERO
 *      delta on the LATCH frame (the inner widget, declared earlier, already applied that same delta;
 *      re-routing it here double-moves for one frame). The container CLAIMS the capture (active_id =
 *      scroll id, not 0) so the gesture has a single owner; the inner widget sees a foreign owner ->
 *      no click (cancelled). The scroll starts tracking 1:1 from the NEXT frame's per-frame delta. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_capture_steal_drag(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    const uint32_t inner_id = 0xBEEFU;

    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style); /* establish container bbox */

    /* Pointer dragged 40 px down (>> 8 px threshold) along the scrolling y axis. */
    p.x = 100.0F;
    p.y = 140.0F;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    /* Inject AFTER begin (orphan-clear ran) so the simulated inner capture is live. */
    inject_inner_capture(inner_id, 100.0F, 100.0F, 100.0F, 140.0F);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, &style, NULL);
        {
            CLAY({.id = CLAY_ID("content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(1000)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Container claimed the gesture -> inner capture cancelled (active id is the container, no click). */
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
    /* LATCH frame routes ZERO delta: pos AND raw stay at rest (fails before the fix, which dumped the
     * already-inner-applied 40 px delta into pos the same frame — the one-frame double-motion). */
    float oy = 0.0F;
    nt_ui_scroll_test_last_child_offset(NULL, &oy);
    TEST_ASSERT_TRUE(float_near(oy, 0.0F, 0.5F)); /* no movement on the steal frame */
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->pos[1], 0.0F, 0.5F)); /* pos unchanged on the latch frame */
    TEST_ASSERT_TRUE(float_near(s->raw[1], 0.0F, 0.5F)); /* raw unchanged on the latch frame */

    /* NEXT frame: the container OWNS the capture; a further 20 px drag must route 1:1 into pos. The
     * OWNED path tracks the live pointer and re-anchors press_pos each frame, so the offset advances
     * by exactly the per-frame finger delta (no threshold dead-zone left to consume). */
    p.y = 160.0F;                             /* +20 px further down */
    p.buttons[NT_BUTTON_LEFT].is_down = true; /* finger still held — the OWNED path routes the drag */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    s_fx.ctx->captures[0].active_id = SCROLL_ID; /* container owns it now (claimed last frame) */
    s_fx.ctx->captures[0].press_pos[0] = 100.0F; /* matches last frame's re-anchor (cap->pos was 140) */
    s_fx.ctx->captures[0].press_pos[1] = 140.0F;
    s_fx.ctx->captures[0].pos[0] = 100.0F;
    s_fx.ctx->captures[0].pos[1] = 160.0F;
    s_fx.ctx->capture_seen[0] = 1U;
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, &style, NULL);
        {
            CLAY({.id = CLAY_ID("content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(1000)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    /* 20 px finger move down -> raw tracks it 1:1 (Clay sign: positive raw past the top edge). */
    TEST_ASSERT_TRUE(float_near(s->raw[1], 20.0F, 0.5F));
}

/* ---- Test 8b: a capture-steal drag on a dt==0 frame zeroes velocity, so a prior
 *      fling's momentum can't leak past the drag (no phantom momentum on release). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_capture_steal_dt_zero_no_phantom(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    const uint32_t inner_id = 0xBEEFU;

    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style); /* establish container bbox + state cell */

    /* Seed a leftover fling velocity into the live state cell (as if mid-momentum). */
    nt_ui_scroll_state_t *s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    s->vel[1] = -5000.0F; /* a strong residual fling */

    /* A steal-worthy drag (40 px down) on a dt==0 frame. With dt<=0 there is no fresh
     * velocity to compute — it MUST be zeroed, not left at the stale -5000. */
    p.x = 100.0F;
    p.y = 140.0F;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &p, 1); /* dt == 0 */
    inject_inner_capture(inner_id, 100.0F, 100.0F, 100.0F, 140.0F);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, &style, NULL);
        {
            CLAY({.id = CLAY_ID("content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(1000)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Velocity zeroed by the steal on the dt==0 frame (no leftover fling). */
    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->vel[1], 0.0F, 0.001F));
}

/* ---- Test 9: tap<threshold leaves inner capture intact and does NOT scroll ---- */
static void test_scroll_capture_tap_no_steal(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    const uint32_t inner_id = 0xBEEFU;

    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style);

    /* Tiny 3 px move (< 8 px threshold) — a tap, not a drag. */
    p.x = 101.0F;
    p.y = 103.0F;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    inject_inner_capture(inner_id, 100.0F, 100.0F, 101.0F, 103.0F);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, &style, NULL);
        {
            CLAY({.id = CLAY_ID("content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(1000)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Inner capture survives -> inner widget will click normally. */
    TEST_ASSERT_EQUAL_UINT32(inner_id, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
    /* Did NOT scroll (offset stayed at rest). */
    float oy = 0.0F;
    nt_ui_scroll_test_last_child_offset(NULL, &oy);
    TEST_ASSERT_TRUE(float_near(oy, 0.0F, 0.5F));
}

/* ================= Scrollbar visual cases ================= */

/* A scroll frame with a chosen style + content height so the bar emit can be exercised.
 * Container 200x200; content 200 x content_h. */
static void scrollbar_frame(nt_pointer_t *p, const nt_ui_scroll_style_t *style, float content_h) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, p, 1);
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
    TEST_ASSERT_EQUAL_UINT8(0U, nt_ui_scroll_test_last_bar_emitted_axes() & 2U);

    /* Content LONGER than container (1000 > 200): overflow -> bar shown. */
    scrollbar_frame(&p, &style, 1000.0F);
    scrollbar_frame(&p, &style, 1000.0F);
    TEST_ASSERT_TRUE((nt_ui_scroll_test_last_bar_emitted_axes() & 2U) != 0U);
}

/* ---- Test 11: ALWAYS emits the bar even without overflow. ---- */
static void test_scrollbar_always_shows(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    nt_pointer_t p = {0};
    p.active = true;
    scrollbar_frame(&p, &style, 150.0F); /* no overflow */
    scrollbar_frame(&p, &style, 150.0F);
    TEST_ASSERT_TRUE((nt_ui_scroll_test_last_bar_emitted_axes() & 2U) != 0U);
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
    nt_ui_scroll_test_last_bar_geometry(1, &thumb_len, NULL, &track_len, NULL);
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
    nt_ui_scroll_test_last_bar_geometry(1, NULL, &off_top, NULL, NULL);
    TEST_ASSERT_TRUE(float_near(off_top, 0.0F, 0.5F));

    /* Scroll to the bottom (max negative offset = -(content-container) = -600). */
    nt_ui_scroll_to(s_fx.ctx, SCROLL_ID, 0.0F, -600.0F);
    scrollbar_frame(&p, &style, 800.0F);
    scrollbar_frame(&p, &style, 800.0F);
    float off_bot = 0.0F;
    float thumb_len = 0.0F;
    float track_len = 0.0F;
    nt_ui_scroll_test_last_bar_geometry(1, &thumb_len, &off_bot, &track_len, NULL);
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
    float oy = 0.0F;
    nt_ui_scroll_test_last_child_offset(NULL, &oy);
    /* Clicked near the bottom -> scrolled the offset substantially negative (down). */
    TEST_ASSERT_TRUE(oy < -50.0F);
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
        nt_ui_scroll_test_last_bar_geometry(1, NULL, NULL, NULL, &last_op);
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

/* ================= UX-fix round (free-press drag + scroll-to-from-button) ================= */

static const uint32_t SCROLL_ID_A = 0x5C00A1U;
static const uint32_t SCROLL_ID_B = 0x5C00B2U;

/* Two scroll containers side by side, mirroring the demo's twin lists; an OPTIONAL "scroll
 * both to top" call runs AFTER both are declared (exactly where the demo's button-end fires
 * nt_ui_scroll_to). do_scroll_to gates the call so we can establish state first. */
static void twin_scroll_frame(nt_pointer_t *p, const nt_ui_scroll_style_t *style, bool do_scroll_to) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, p, 1);
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

/* ---- Test 18: free-press drag (no inner widget capture) scrolls the container, and the
 *      release leaves momentum so a fling continues past the lifted finger. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_free_press_drag_and_fling(void) {
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

    /* Finger UP: free press ends; momentum carries on. */
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

/* ---- Test 19: a free-press TAP (below threshold) does NOT scroll the container. ---- */
static void test_scroll_free_press_tap_no_scroll(void) {
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

/* ---- Test 20: the container does NOT steal its OWN scrollbar's thumb drag. A drag on the
 *      bar id (>threshold) must reach the scrollbar (thumb maps the offset), not be cancelled
 *      by the steal path. We assert the bar capture survives the drag_check. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_does_not_steal_own_scrollbar(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    const uint32_t bar_id = nt_ui_scroll_test_bar_id(SCROLL_ID, 1);

    nt_pointer_t p = {0};
    p.x = 195.0F; /* over the right-edge vertical bar (thickness 12 in a 200-wide container) */
    p.y = 100.0F;
    p.active = true;
    scrollbar_frame(&p, &style, 1000.0F); /* establish bar bbox */
    scrollbar_frame(&p, &style, 1000.0F);

    /* Press on the bar so it captures, then drag 40 px (>threshold) along y. The steal path
     * MUST exclude the container's own bar id, leaving the bar capture intact. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    s_fx.ctx->captures[0].active_id = bar_id;
    s_fx.ctx->captures[0].press_pos[0] = 195.0F;
    s_fx.ctx->captures[0].press_pos[1] = 100.0F;
    s_fx.ctx->captures[0].pos[0] = 195.0F;
    s_fx.ctx->captures[0].pos[1] = 140.0F;
    s_fx.ctx->capture_seen[0] = 1U;
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, &style, NULL);
        {
            CLAY({.id = CLAY_ID("content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(1000)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Bar capture NOT cancelled by the container's steal path (own-bar exclusion). */
    TEST_ASSERT_EQUAL_UINT32(bar_id, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
}

/* IDs for the nested outer+inner scroll used by the nested-scrollbar exclusion test. */
static const uint32_t EXCL_OUTER_ID = 0x5CE001U;
static const uint32_t EXCL_INNER_ID = 0x5CE002U;

/* Outer scroll (300x300 over 300x1000) holding an INNER scroll (200x200 over 200x800) with bars,
 * pinned at the outer content's top-left. Both register their 'scrl' state cells. */
static void excl_nested_frame(nt_pointer_t *p, const nt_ui_scroll_style_t *style) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, p, 1);
    CLAY({.id = CLAY_ID("excl-root"), .layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(300)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, EXCL_OUTER_ID, style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(300)}}});
        {
            CLAY({.id = CLAY_ID("excl-outer-content"), .layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(1000)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
                nt_ui_scroll_begin(s_fx.ctx, NULL, EXCL_INNER_ID, style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}});
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

/* ---- Test 20b: a parent scroll must NOT steal a nested scroll's SCROLLBAR thumb drag. The bar id
 *      reverses (id ^ salt) to the inner container's id, whose always-on 'scrl' state cell is the
 *      all-builds exclusion path (the DEBUG_TOOLS widget registry is NOT relied on). A press on the
 *      inner bar dragged > threshold past the outer must leave the inner-bar capture intact. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_does_not_steal_nested_scrollbar(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_ALWAYS);
    const uint32_t inner_bar_id = nt_ui_scroll_test_bar_id(EXCL_INNER_ID, 1);

    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    excl_nested_frame(&p, &style); /* solve bboxes + create both 'scrl' cells */
    excl_nested_frame(&p, &style);

    /* The inner bar's 'scrl' reverse cell must exist all-builds (no registry). */
    TEST_ASSERT_TRUE(nt_ui_state_has_tag(s_fx.ctx, EXCL_INNER_ID, NT_UI_STATE_TAG('s', 'c', 'r', 'l')));

    /* Inject a capture held by the inner bar id, pressed inside the outer, dragged 40 px (>threshold).
     * The OUTER's steal path must exclude it (reverse-tag path), leaving the capture on the inner bar. */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    s_fx.ctx->captures[0].active_id = inner_bar_id;
    s_fx.ctx->captures[0].press_pos[0] = 100.0F;
    s_fx.ctx->captures[0].press_pos[1] = 100.0F;
    s_fx.ctx->captures[0].pos[0] = 100.0F;
    s_fx.ctx->captures[0].pos[1] = 140.0F;
    s_fx.ctx->capture_seen[0] = 1U;
    CLAY({.id = CLAY_ID("excl-root"), .layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(300)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, EXCL_OUTER_ID, &style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(300)}}});
        {
            CLAY({.id = CLAY_ID("excl-outer-content"), .layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(1000)}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
                nt_ui_scroll_begin(s_fx.ctx, NULL, EXCL_INNER_ID, &style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}});
                {
                    CLAY({.id = CLAY_ID("excl-inner-content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(800)}}}) {}
                }
                nt_ui_scroll_end(s_fx.ctx);
            }
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);

    /* Capture stayed on the inner bar: the outer did not steal a nested scrollbar's drag. */
    TEST_ASSERT_EQUAL_UINT32(inner_bar_id, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
}

/* ---- Test 21: free-press drag must NOT hijack a click on a non-scrolling tap. A press inside
 *      the container that releases before crossing the threshold scrolls nothing AND a free
 *      press that started OUTSIDE the bbox never scrolls (anchor gate). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_free_press_outside_bbox_ignored(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_pointer_t p = {0};
    p.active = true;
    p.x = 600.0F; /* far OUTSIDE the 200x200 container at the top-left */
    p.y = 400.0F;
    scroll_frame(&p, &style);

    /* Press began outside the bbox, then the finger moves over the container and drags. The
     * anchor must never latch (press did not begin inside) -> no scroll. */
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

/* ---- Test 22: free-press drag tracks the finger 1:1 — pos must equal the TOTAL finger
 *      displacement after each frame (no threshold-crossing jump, no cumulative runaway). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_free_press_tracks_finger_1to1(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    style.rubber_band_c = 0.0F; /* isolate tracking from overscroll compression */
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style); /* establish bbox + state cell */

    /* Finger DOWN inside the container (empty content -> free press, no widget capture). */
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
     * threshold consumed before the drag latched — i.e. it tracks 1:1 with no extra jump.
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

/* ---- Test 23: a SLOW free-press drag that crosses the threshold in tiny steps must not
 *      spike velocity from the accumulated threshold gap (no runaway fling on release). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_free_press_slow_cross_no_velocity_spike(void) {
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
 *      still DOWN at that y (latched). ---- */
static nt_ui_scroll_state_t *held_drag_to(nt_pointer_t *p, const nt_ui_scroll_style_t *style, float drag_to_y) {
    p->x = 100.0F;
    p->y = 100.0F;
    p->active = true;
    scroll_frame(p, style); /* establish bbox + state cell */
    p->buttons[NT_BUTTON_LEFT].is_down = true;
    p->buttons[NT_BUTTON_LEFT].is_pressed = true;
    scroll_frame(p, style); /* press frame: anchor set */
    p->buttons[NT_BUTTON_LEFT].is_pressed = false;
    /* Drag toward the target in a few steps so the gesture latches (crosses the threshold). */
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
        TEST_ASSERT_TRUE(float_near(s->pos[1], frozen, 0.001F));         /* pos frozen — no creep, no drift */
        TEST_ASSERT_TRUE((s->flags & NT_UI_SCROLL_FLAG_DRAGGING) != 0U); /* still dragging */
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

/* ---- Test 31: steal continuity — after the container claims an inner-widget gesture it keeps
 *      routing into that same container over subsequent frames (guards claim-not-zero). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_steal_continues_across_frames(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    const uint32_t inner_id = 0xBEEFU;

    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 100.0F;
    p.active = true;
    scroll_frame(&p, &style); /* establish bbox */

    /* Frame 1: inner widget owns the pointer; a 40 px drag steals + claims. */
    p.x = 100.0F;
    p.y = 140.0F;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    inject_inner_capture(inner_id, 100.0F, 100.0F, 100.0F, 140.0F);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(200)}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_ID, &style, NULL);
        {
            CLAY({.id = CLAY_ID("content"), .layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(1000)}}}) {}
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U)); /* claimed */
    float oy_claim = 0.0F;
    nt_ui_scroll_test_last_child_offset(NULL, &oy_claim);

    /* Subsequent frames: the container OWNS the capture (no inner injection). The held finger keeps
     * dragging down and the OWNED path must keep routing into THIS container. capture_seen is re-marked
     * each frame so begin's orphan-clear doesn't drop the claim. */
    for (int i = 1; i <= 5; ++i) {
        p.buttons[NT_BUTTON_LEFT].is_down = true;
        p.y = 140.0F + (float)(i * 10);
        scroll_frame(&p, &style);
        TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U)); /* still owned */
    }
    float oy_after = 0.0F;
    nt_ui_scroll_test_last_child_offset(NULL, &oy_after);
    /* The drag kept feeding the same container -> the offset advanced past the claim frame. */
    TEST_ASSERT_TRUE(fabsf(oy_after) > fabsf(oy_claim) + 1.0F);
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
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0U)); /* released */
    for (int i = 0; i < 8; ++i) {
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
        nt_ui_scroll_test_last_bar_geometry(1, NULL, NULL, NULL, &op_idle);
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
    nt_ui_scroll_test_last_bar_geometry(1, NULL, NULL, NULL, &op_prev);
    bool rose = false;
    for (int i = 0; i < 10; ++i) {
        scrollbar_frame(&p, &style, 800.0F); /* fling-in-motion keeps idle reset -> fade target stays 1 */
        float op = 0.0F;
        nt_ui_scroll_test_last_bar_geometry(1, NULL, NULL, NULL, &op);
        if (op > op_prev + 0.01F) {
            rose = true;
        }
        op_prev = op;
    }
    TEST_ASSERT_TRUE(rose);           /* the bar faded back IN on activity */
    TEST_ASSERT_TRUE(op_prev > 0.4F); /* climbed well off the floor */
}

/* ---- Test 35 (fix 1b): hovering the bar keeps it visible even with no scroll motion. ---- */
static void test_scrollbar_auto_hide_hover_keeps_visible(void) {
    nt_ui_scroll_style_t style = bar_style(NT_UI_SCROLLBAR_AUTO_HIDE);
    style.bar_fade_speed = 8.0F;
    style.bar_hide_delay = 0.2F;
    nt_pointer_t p = {0};
    p.active = true;
    p.x = 195.0F; /* over the right-edge vertical bar */
    p.y = 100.0F;

    /* Idle clock runs past the linger, but the pointer hovers the bar -> stays opaque. */
    float op = 0.0F;
    for (int i = 0; i < 120; ++i) {
        scrollbar_frame(&p, &style, 800.0F);
        nt_ui_scroll_test_last_bar_geometry(1, NULL, NULL, NULL, &op);
    }
    TEST_ASSERT_TRUE(op > 0.8F); /* hover held it visible past the idle linger */
}

/* ---- Test 36 (fix 3): a wheel notch during a fling kills momentum and eases toward the wheel
 *      target the SAME frame (the notch is not swallowed by the momentum overwrite). ---- */
static void test_scroll_wheel_kills_fling(void) {
    const float content[2] = {0.0F, 100000.0F};
    const float container[2] = {0.0F, 400.0F};
    nt_ui_scroll_state_t s = {0};
    s.vel[1] = -3000.0F; /* a fling in motion */
    const float no_wheel[2] = {0.0F, 0.0F};
    nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, s_tun);
    TEST_ASSERT_TRUE(fabsf(s.vel[1]) > 100.0F); /* still flinging */

    const float target_before = s.target[1];
    const float wheel_up[2] = {0.0F, 1.0F}; /* one notch toward 0 (Clay sign up) */
    nt_ui_scroll_test_integrate(&s, wheel_up, 1.0F / 60.0F, content, container, s_tun);
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
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.001F, &p, 1);
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

/* ---- Test 38 (fix 5): a container whose content FITS (no overflow) takes no gesture — a free-press
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
    TEST_ASSERT_EQUAL_UINT8(0U, s->flags & NT_UI_SCROLL_FLAG_DRAGGING);       /* no gesture armed */
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
    /* No fling -> the press only arms a free-press (does not claim a capture this frame). */
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
    nt_ui_scroll_test_last_bar_geometry(1, &thumb_len, &thumb_off0, &track_len, NULL);
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
        nt_ui_scroll_test_integrate(&s, no_wheel, dt, content, container, s_tun);
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
    nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, s_tun);
    TEST_ASSERT_TRUE(float_near(s.pos[1], -800.0F, 1.0F)); /* parked at the old bottom */

    /* Content SHRINKS to 400 -> new bottom edge is -(400-200) = -200. pos at -800 is now past it. */
    content[1] = 400.0F;
    for (int i = 0; i < 240; ++i) {
        nt_ui_scroll_test_integrate(&s, no_wheel, 1.0F / 60.0F, content, container, s_tun);
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

    /* Press inside the container (empty content -> free press, no widget capture). */
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
    nt_mem_scratch_reset(); /* per-frame scratch reset, exactly as the engine main loop does */
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, p, 1);
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
 *      of tile A, while the grid churns (A's AUTO_HIDE bar, B's row count, a root spacer). ONLY A's
 *      offset may change on any frame; B/C/D must stay frozen at 0. ---- */
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
    uint32_t max_recipients = 0U;
    for (int f = 0; f < 300; ++f) {
        p.wheel_dy = 1.0F;                              /* one notch this frame */
        const int b_rows = ((f / 30) % 2 == 0) ? 6 : 3; /* B's row count toggles every ~30 frames */
        const float spacer_h = (float)((f * 5) % 40);   /* sawtooth 0..35 px of extra slot churn */
        const bool drop_a = (f % 3) == 0;               /* A vanishes every 3rd frame (stale-bbox lingers) */

        nt_ui_scroll_test_wheel_recipients_reset();
        wheel_grid_frame(&p, &style_a, &style_other, b_rows, spacer_h, drop_a);

        const uint32_t got = nt_ui_scroll_test_wheel_recipients();
        if (got > max_recipients) {
            max_recipients = got;
        }
        /* The notch reached at most ONE container this frame (exclusive routing — never a broadcast). */
        TEST_ASSERT_TRUE_MESSAGE(got <= 1U, "wheel notch broadcast to more than one scroll container in a single frame");
    }
    TEST_ASSERT_TRUE(max_recipients <= 1U); /* no frame ever broadcast */

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
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, p, 1);
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

    /* Warm-up: frame 1 solves the bboxes, frame 2's end-of-frame resolve grants the owner (1-frame
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

/* Two SAME-DEPTH overlapping scroll containers (both direct children of root, neither nested in the
 * other): a BIG one declared FIRST, then a SMALL one FLOATING over the big's top-left corner so their
 * bboxes genuinely overlap there (the deterministic stand-in for the stale-bbox overlap the routing
 * must arbitrate). The pointer sits in the overlap. Declaration order (BIG first) is deliberately the
 * OPPOSITE of the area order so only the smaller-area rule can pick the small one. */
static const uint32_t TIE_BIG_ID = 0x5CF101U;
static const uint32_t TIE_SMALL_ID = 0x5CF102U;

static void tie_break_frame(nt_pointer_t *p) {
    const nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, p, 1);
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

/* ---- Test 48 (tie-break): two SAME-DEPTH candidates whose bboxes overlap the parked pointer -> the
 *      SMALLER-area one receives the wheel, and ONLY one does. Declaration order favours the big one,
 *      so a pass proves area (not order) decides the tie. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scroll_wheel_tie_break_smaller_area(void) {
    nt_pointer_t p = {0};
    p.active = true;
    p.x = 60.0F; /* inside the overlap [0,120]x[0,120] of both BIG and the floating SMALL */
    p.y = 60.0F;

    /* Warm-up so the bboxes solve and the end-of-frame resolve grants the owner (1-frame lag). */
    tie_break_frame(&p);
    tie_break_frame(&p);

    uint32_t max_recipients = 0U;
    for (int f = 0; f < 20; ++f) {
        p.wheel_dy = 1.0F;
        nt_ui_scroll_test_wheel_recipients_reset();
        tie_break_frame(&p);
        const uint32_t got = nt_ui_scroll_test_wheel_recipients();
        if (got > max_recipients) {
            max_recipients = got;
        }
        TEST_ASSERT_TRUE_MESSAGE(got <= 1U, "tie-break: wheel reached more than one same-depth container");
    }
    TEST_ASSERT_TRUE(max_recipients <= 1U);

    nt_ui_scroll_state_t *big = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, TIE_BIG_ID);
    nt_ui_scroll_state_t *small = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, TIE_SMALL_ID);
    TEST_ASSERT_NOT_NULL(big);
    TEST_ASSERT_NOT_NULL(small);
    TEST_ASSERT_TRUE(small->pos[1] < -1.0F);               /* the smaller-area overlapping container won */
    TEST_ASSERT_TRUE(float_near(big->pos[1], 0.0F, 0.5F)); /* the bigger sibling never received it */
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
/* Declaring more scroll containers in one frame than the candidate cap traps (no silent wheel-dead). */
static void candidate_overflow_frame(void) {
    nt_pointer_t p = {0};
    p.active = true;
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &p, 1);
    CLAY({.id = CLAY_ID("root"), .layout = {.sizing = {CLAY_SIZING_FIXED(800), CLAY_SIZING_FIXED(600)}}}) {
        for (uint32_t i = 0; i <= (uint32_t)NT_UI_WHEEL_CANDIDATES; ++i) {
            nt_ui_scroll_begin(s_fx.ctx, NULL, 0x100U + i, &style, NULL);
            nt_ui_scroll_end(s_fx.ctx);
        }
    }
    nt_ui_end(s_fx.ctx);
}

static void test_assert_wheel_candidate_overflow(void) { NT_TEST_EXPECT_ASSERT(candidate_overflow_frame()); }
#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_scroll_momentum_decays_monotonic);
    RUN_TEST(test_scroll_clamp_holds);
    RUN_TEST(test_scroll_rubber_band_compresses);
    RUN_TEST(test_scroll_to_reaches_target);
    RUN_TEST(test_scroll_wheel_sign);
    RUN_TEST(test_scroll_wheel_applied_once);
    RUN_TEST(test_scroll_childoffset_from_our_pos);
    RUN_TEST(test_scroll_capture_steal_drag);
    RUN_TEST(test_scroll_capture_steal_dt_zero_no_phantom);
    RUN_TEST(test_scroll_capture_tap_no_steal);
    RUN_TEST(test_scrollbar_auto_only_on_overflow);
    RUN_TEST(test_scrollbar_always_shows);
    RUN_TEST(test_scrollbar_thumb_size_ratio);
    RUN_TEST(test_scrollbar_thumb_pos_clay_sign);
    RUN_TEST(test_scrollbar_track_click_scroll_to);
    RUN_TEST(test_scrollbar_auto_hide_fades);
    RUN_TEST(test_scrollbar_id_derivation_distinct);
    RUN_TEST(test_scroll_to_top_from_button_sequence);
    RUN_TEST(test_scroll_free_press_drag_and_fling);
    RUN_TEST(test_scroll_free_press_tap_no_scroll);
    RUN_TEST(test_scroll_does_not_steal_own_scrollbar);
    RUN_TEST(test_scroll_does_not_steal_nested_scrollbar);
    RUN_TEST(test_scroll_free_press_outside_bbox_ignored);
    RUN_TEST(test_scroll_free_press_tracks_finger_1to1);
    RUN_TEST(test_scroll_free_press_slow_cross_no_velocity_spike);
    RUN_TEST(test_scroll_held_still_in_bounds_frozen);
    RUN_TEST(test_scroll_held_still_past_edge_frozen);
    RUN_TEST(test_scroll_release_after_hold_no_fling);
    RUN_TEST(test_scroll_raw_anchored_rubber_no_hysteresis);
    RUN_TEST(test_scroll_held_no_momentum_midhold);
    RUN_TEST(test_scroll_cross_container_no_adoption);
    RUN_TEST(test_scroll_held_entry_no_edge_no_gesture);
    RUN_TEST(test_scroll_steal_continues_across_frames);
    RUN_TEST(test_scroll_release_reclaims_on_next_edge);
    RUN_TEST(test_scrollbar_auto_hide_fades_in_on_activity);
    RUN_TEST(test_scrollbar_auto_hide_hover_keeps_visible);
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
    RUN_TEST(test_scroll_wheel_tie_break_smaller_area);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_assert_wheel_candidate_overflow);
#endif
    return UNITY_END();
}
