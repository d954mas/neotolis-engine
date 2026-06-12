/* Scroll container + custom engine physics tests (59-02).
 *
 * Physics (momentum decay, clamp, rubber-band, scroll-to, Clay sign) drive the
 * static integrator directly via the NT_TEST_ACCESS probe — no Clay frame needed.
 * Container + capture-steal cases (Task 2) declare a real scroll element and read
 * back the fed childOffset. UNITY_EXCLUDE_FLOAT: compare via int32 casts / eps. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
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

static void scroll_frame(nt_pointer_t *p, const nt_ui_scroll_style_t *style) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, p, 1);
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

/* ---- Test 6: wheel applied ONCE — one notch moves childOffset by the wheel contribution ---- */
static void test_scroll_wheel_applied_once(void) {
    nt_ui_scroll_style_t style = nt_ui_scroll_style_defaults();
    style.wheel_ease_speed = 0.0F; /* instant ease so one frame lands exactly on target */
    style.wheel_step_px = 40.0F;

    nt_pointer_t p = {0};
    p.x = 100.0F; /* inside the 200x200 container */
    p.y = 100.0F;
    p.active = true;
    /* Frame 1: establish layout + content dims (no wheel). */
    scroll_frame(&p, &style);
    /* Frame 2: one wheel notch down. */
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

/* ---- Test 8: capture-steal — drag>threshold cancels inner capture AND scrolls. The container now
 *      CLAIMS the capture (active_id = scroll id, not 0) so the gesture has a single owner that can't
 *      be re-adopted by a neighbour; the inner widget sees a foreign owner -> no click (cancelled). ---- */
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

    /* Container claimed the gesture -> inner capture cancelled (no longer inner_id, no click). */
    TEST_ASSERT_EQUAL_UINT32(SCROLL_ID, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
    /* Scrolled: drag delta routed into pos -> non-trivial childOffset. */
    float oy = 0.0F;
    nt_ui_scroll_test_last_child_offset(NULL, &oy);
    TEST_ASSERT_TRUE(fabsf(oy) > 1.0F); /* the container moved */
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

/* ================= Scrollbar visual cases (Plan 04, WIDGET-20 / D-59-14) ================= */

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
 *      negative pos (scrolled down) puts the thumb toward the BOTTOM. (Pitfall 3) ---- */
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
    TEST_ASSERT_TRUE(float_near(s->vel[1], 0.0F, 0.001F)); /* still finger -> zero sampled vel */
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
    /* First held-still frame: vel samples to ~0; pos must NOT advance by the prior fling velocity. */
    scroll_frame(&p, &style);
    s = (nt_ui_scroll_state_t *)nt_ui_state_find(s_fx.ctx, SCROLL_ID);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE(float_near(s->pos[1], pos_after_drag, 0.001F)); /* no momentum step mid-hold */
    TEST_ASSERT_TRUE(float_near(s->vel[1], 0.0F, 0.001F));           /* vel sampled to zero on the still frame */
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
    return UNITY_END();
}
