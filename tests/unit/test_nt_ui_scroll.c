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
    return UNITY_END();
}
