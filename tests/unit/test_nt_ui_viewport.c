/* Device<->layout viewport ownership (nt_ui_begin contract).
 *
 * The ctx owns the single device<->layout mapping: nt_ui_begin takes the RAW DEVICE pointer and the
 * lazy hit-test resolve converts it through ctx->viewport. These cases lock:
 *   1. DEFAULT viewport (no set_viewport) is identity -> device == layout (unscaled paths unchanged).
 *   2. A non-trivial LETTERBOX-like viewport maps a device pointer onto the widget at the matching
 *      LAYOUT position (the hot-resolve hits it).
 *   3. nt_ui_screen_to_layout / _layout_to_screen round-trip; default is identity.
 *
 * Asymmetric known geometry (160x48 @ (100,200) in 800x600) so an axis swap / flip is visible. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define SCREEN_W 800.0F
#define SCREEN_H 600.0F
#define BTN_X 100.0F
#define BTN_Y 200.0F
#define BTN_W 160.0F
#define BTN_H 48.0F
#define BTN_CX (BTN_X + (BTN_W * 0.5F))
#define BTN_CY (BTN_Y + (BTN_H * 0.5F))

/* LETTERBOX-like viewport: device content rect offset (40,30), 1.5x device-per-layout size. */
#define VP_OFF_X 40.0F
#define VP_OFF_Y 30.0F
#define VP_SCALE 1.5F

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static nt_pointer_t make_pointer(float x, float y, bool is_down, bool is_pressed) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = is_down;
    p.buttons[NT_BUTTON_LEFT].is_pressed = is_pressed;
    return p;
}

static void declare_btn_element(void) {
    CLAY({.id = CLAY_ID("btn"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = BTN_X, .y = BTN_Y}}, .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}}}) {}
}

static nt_ui_viewport_t letterbox_vp(void) { return (nt_ui_viewport_t){.x = VP_OFF_X, .y = VP_OFF_Y, .w = SCREEN_W * VP_SCALE, .h = SCREEN_H * VP_SCALE}; }

/* Warm + step frame so btn enters the interactive registry; optionally set a viewport first.
 * Front-most arbitration reads the PREVIOUS frame's registry, so the widget reacts only from the
 * frame after its first step. */
static void warm_btn_frame(const nt_pointer_t *p, bool set_vp) {
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, p, 1);
    if (set_vp) {
        nt_ui_set_viewport(s_fx.ctx, letterbox_vp());
    }
    declare_btn_element();
    (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
}

static nt_ui_interaction_t query_btn_frame(const nt_pointer_t *p, bool set_vp) {
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, p, 1);
    if (set_vp) {
        nt_ui_set_viewport(s_fx.ctx, letterbox_vp());
    }
    declare_btn_element();
    nt_ui_interaction_t in = nt_ui_step_interaction(s_fx.ctx, nt_ui_id("btn"));
    nt_ui_end(s_fx.ctx);
    return in;
}

/* ---- 1: default viewport is identity (a LAYOUT-space pointer still hits, unscaled unchanged) ---- */
static void test_default_viewport_identity_hit(void) {
    nt_pointer_t f1 = make_pointer(BTN_CX, BTN_CY, false, false);
    warm_btn_frame(&f1, false);
    nt_pointer_t f2 = make_pointer(BTN_CX, BTN_CY, true, true);
    nt_ui_interaction_t in = query_btn_frame(&f2, false);
    TEST_ASSERT_TRUE(in.hovered);
    TEST_ASSERT_TRUE(in.pressed_now);
    /* pos echoes the converted (== device under identity) pointer in layout space. */
    TEST_ASSERT_TRUE(float_near(in.pos[0], BTN_CX, 0.5F));
    TEST_ASSERT_TRUE(float_near(in.pos[1], BTN_CY, 0.5F));
}

/* ---- 2: a scaled viewport maps a DEVICE pointer onto the widget's LAYOUT rect ---- */
static void test_scaled_viewport_device_pointer_hits(void) {
    /* Device coord of the widget's layout center: device = offset + layout * scale. */
    const float dev_cx = VP_OFF_X + (BTN_CX * VP_SCALE);
    const float dev_cy = VP_OFF_Y + (BTN_CY * VP_SCALE);

    nt_pointer_t f1 = make_pointer(dev_cx, dev_cy, false, false);
    warm_btn_frame(&f1, true);
    nt_pointer_t f2 = make_pointer(dev_cx, dev_cy, true, true);
    nt_ui_interaction_t in = query_btn_frame(&f2, true);
    TEST_ASSERT_TRUE(in.hovered);
    TEST_ASSERT_TRUE(in.pressed_now);
    /* Reported pos is in LAYOUT space (device converted back through the viewport). */
    TEST_ASSERT_TRUE(float_near(in.pos[0], BTN_CX, 0.5F));
    TEST_ASSERT_TRUE(float_near(in.pos[1], BTN_CY, 0.5F));
}

/* A device pointer OUTSIDE the widget's mapped rect does not hit. */
static void test_scaled_viewport_outside_misses(void) {
    const float dev_cx = VP_OFF_X + (BTN_CX * VP_SCALE);
    const float dev_cy = VP_OFF_Y + (BTN_CY * VP_SCALE);

    nt_pointer_t f1 = make_pointer(dev_cx, dev_cy, false, false);
    warm_btn_frame(&f1, true);
    /* Shift device pointer far past the widget's device extent (BTN_W*scale wide). */
    nt_pointer_t f2 = make_pointer(dev_cx + (BTN_W * VP_SCALE), dev_cy, true, true);
    nt_ui_interaction_t in = query_btn_frame(&f2, true);
    TEST_ASSERT_FALSE(in.hovered);
}

/* ---- 3: converter round-trip + default identity ---- */
static void test_converters_roundtrip_and_default_identity(void) {
    nt_pointer_t p = make_pointer(0.0F, 0.0F, false, false);

    /* Default (no set_viewport): screen_to_layout is identity. */
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &p, 1);
    const float screen0[2] = {BTN_CX, BTN_CY};
    float layout0[2];
    nt_ui_screen_to_layout(s_fx.ctx, screen0, layout0);
    TEST_ASSERT_TRUE(float_near(layout0[0], BTN_CX, 0.001F));
    TEST_ASSERT_TRUE(float_near(layout0[1], BTN_CY, 0.001F));
    nt_ui_end(s_fx.ctx);

    /* Scaled viewport: layout -> screen -> layout round-trips. */
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &p, 1);
    nt_ui_set_viewport(s_fx.ctx, letterbox_vp());
    const float layout_in[2] = {BTN_X, BTN_Y};
    float screen[2];
    float layout_back[2];
    nt_ui_layout_to_screen(s_fx.ctx, layout_in, screen);
    /* Forward maps to the expected device coord. */
    TEST_ASSERT_TRUE(float_near(screen[0], VP_OFF_X + (BTN_X * VP_SCALE), 0.01F));
    TEST_ASSERT_TRUE(float_near(screen[1], VP_OFF_Y + (BTN_Y * VP_SCALE), 0.01F));
    nt_ui_screen_to_layout(s_fx.ctx, screen, layout_back);
    TEST_ASSERT_TRUE(float_near(layout_back[0], BTN_X, 0.01F));
    TEST_ASSERT_TRUE(float_near(layout_back[1], BTN_Y, 0.01F));
    nt_ui_end(s_fx.ctx);
}

#if NT_UI_DEBUG_TOOLS
/* ---- 4: the inspector consumes the LAYOUT-space pointer under a scaled viewport ----
 * Geometry chosen so RAW vs LAYOUT flips BOTH inspector paths (would PASS on the layout path,
 * FAIL on the raw path):
 *   - widget layout center x = 300 (left of the 400-wide sidebar, panel edge at logical x=400)
 *   - device center x = VP_OFF_X + 300*VP_SCALE = 490 (>= 400, i.e. OVER the panel in raw coords)
 * Raw path: gate would consume the pointer (490 >= 400) AND Clay would hover-pick at device 490
 * (a miss vs the widget's layout rect 220..380) -> highlight != widget. Layout path: gate stays
 * open (300 < 400) and the hover-pick lands on the widget -> highlight == widget. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_scaled_inspector_uses_layout_pointer(void) {
    nt_ui_inspector_set_active(s_fx.ctx, true);

    const float ins_btn_w = 160.0F;
    const float ins_btn_h = 48.0F;
    const float ins_cx = 300.0F; /* layout center x: left of the sidebar (panel edge at 400) */
    const float ins_cy = 224.0F;
    const float ins_btn_x = ins_cx - (ins_btn_w * 0.5F); /* 220: rect spans layout 220..380 */
    const float ins_btn_y = ins_cy - (ins_btn_h * 0.5F);
    const float dev_cx = VP_OFF_X + (ins_cx * VP_SCALE); /* 490: over the panel in raw device coords */
    const float dev_cy = VP_OFF_Y + (ins_cy * VP_SCALE);

    /* Frame 1: declare widget on the scaled ctx so frame 2's pointer-over tests against this tree. */
    nt_pointer_t f1 = make_pointer(dev_cx, dev_cy, false, false);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &f1, 1);
    nt_ui_set_viewport(s_fx.ctx, letterbox_vp());
    CLAY({.id = CLAY_ID("insp_target"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = ins_btn_x, .y = ins_btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(ins_btn_w), CLAY_SIZING_FIXED(ins_btn_h)}}}) {}
    nt_ui_end(s_fx.ctx);

    /* Frame 2: same device pointer. The gate is recomputed in layout space at the hot resolve, so a
     * step here forces the conversion before reading the consumed flag. */
    nt_pointer_t f2 = make_pointer(dev_cx, dev_cy, false, false);
    nt_ui_begin(s_fx.ctx, SCREEN_W, SCREEN_H, 0.0F, &f2, 1);
    nt_ui_set_viewport(s_fx.ctx, letterbox_vp());
    CLAY({.id = CLAY_ID("insp_target"),
          .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = ins_btn_x, .y = ins_btn_y}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(ins_btn_w), CLAY_SIZING_FIXED(ins_btn_h)}}}) {
        (void)nt_ui_step_interaction(s_fx.ctx, nt_ui_id("insp_target"));
    }
    /* Layout-space gate: device 490 maps to layout 300 (< 400) -> NOT over the sidebar, so normal
     * interaction is not spuriously suppressed (the raw 490 >= 400 path would consume it). */
    TEST_ASSERT_FALSE(nt_ui_inspector_pointer_consumed(s_fx.ctx));
    nt_ui_end(s_fx.ctx);

    /* Hover-pick consumed the layout pointer (300,224) -> lands on the widget. The raw device point
     * (490,366) would miss the widget's layout rect, so this assert fails on the raw-pointer path. */
    TEST_ASSERT_EQUAL_UINT32(nt_ui_id("insp_target"), s_fx.ctx->inspector_highlight_id);
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_default_viewport_identity_hit);
    RUN_TEST(test_scaled_viewport_device_pointer_hits);
    RUN_TEST(test_scaled_viewport_outside_misses);
    RUN_TEST(test_converters_roundtrip_and_default_identity);
#if NT_UI_DEBUG_TOOLS
    RUN_TEST(test_scaled_inspector_uses_layout_pointer);
#endif
    return UNITY_END();
}
