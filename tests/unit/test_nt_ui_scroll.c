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

/* ---- Test 8: capture-steal — drag>threshold cancels inner capture AND scrolls ---- */
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

    /* Inner capture cancelled (active_id cleared) -> inner widget gets no click. */
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_test_capture_active_id(s_fx.ctx, 0U));
    /* Scrolled: drag delta routed into pos -> non-trivial childOffset. */
    float oy = 0.0F;
    nt_ui_scroll_test_last_child_offset(NULL, &oy);
    TEST_ASSERT_TRUE(fabsf(oy) > 1.0F); /* the container moved */
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
    RUN_TEST(test_scroll_capture_tap_no_steal);
    RUN_TEST(test_scrollbar_auto_only_on_overflow);
    RUN_TEST(test_scrollbar_always_shows);
    RUN_TEST(test_scrollbar_thumb_size_ratio);
    RUN_TEST(test_scrollbar_thumb_pos_clay_sign);
    RUN_TEST(test_scrollbar_track_click_scroll_to);
    RUN_TEST(test_scrollbar_auto_hide_fades);
    RUN_TEST(test_scrollbar_id_derivation_distinct);
    return UNITY_END();
}
