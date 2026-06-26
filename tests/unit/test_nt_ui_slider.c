/* Slider widget tests.
 *
 * Covers: value<->position map (float + int, clamped), step quantize, thumb-grab
 * (press on the thumb = relative drag, value unchanged that frame) vs track-jump
 * (press on the track = value jumps to the click point), and the exposed thumb
 * screen position. The synthetic nt_pointer_t drives the interaction headless. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_slider.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Track pinned at a fixed absolute position so the synthetic pointer lands inside.
 * Non-origin so a stray axis swap or sign flip is visible. */
#define SL_X 100.0F
#define SL_Y 200.0F
#define SL_W 200.0F
#define SL_H 20.0F
#define SL_TW 20.0F /* thumb_w */
#define SL_TH 24.0F /* thumb_h */
#define SL_CY (SL_Y + (SL_H * 0.5F))
#define SL_USABLE (SL_W - SL_TW) /* travel of the thumb LEFT edge */

/* value 0 maps to track_left + thumb_w/2; value 1 to track_left + track_w - thumb_w/2. */
#define SL_X_AT_FRAC(frac) (SL_X + (SL_TW * 0.5F) + ((frac) * SL_USABLE))

static nt_ui_slider_style_t s_style;

/* Unity float macros are excluded in this build (UNITY_EXCLUDE_FLOAT). */
static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static void init_style(void) {
    const nt_atlas_region_ref_t art = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    s_style = nt_ui_slider_style_defaults();
    for (int i = 0; i < 4; ++i) {
        s_style.states[i].track = art;
        s_style.states[i].fill = art;
        s_style.states[i].thumb = art;
    }
    s_style.track_w = SL_W;
    s_style.track_h = SL_H;
    s_style.thumb_w = SL_TW;
    s_style.thumb_h = SL_TH;
    /* 0 speeds = the eased fraction snaps to the value fraction every frame -> deterministic. */
    s_style.state_speed = 0.0F;
    s_style.value_speed = 0.0F;
}

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    init_style();
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static nt_pointer_t make_pointer(float x, float y, bool is_down, bool is_pressed, bool is_released) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = is_down;
    p.buttons[NT_BUTTON_LEFT].is_pressed = is_pressed;
    p.buttons[NT_BUTTON_LEFT].is_released = is_released;
    return p;
}

static const Clay_ElementDeclaration s_track_decl = {
    .layout = {.sizing = {CLAY_SIZING_FIXED(SL_W), CLAY_SIZING_FIXED(SL_H)}},
};

static bool slider_float_frame(const nt_pointer_t *p, float *value, float min, float max, float step, bool enabled) {
    bool changed = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = SL_X, .y = SL_Y}}}) {
        changed = nt_ui_slider_float(s_fx.ctx, NULL, 0, nt_ui_id("sl"), NULL, value, min, max, step, &s_style, &s_track_decl, enabled);
    }
    nt_ui_end(s_fx.ctx);
    return changed;
}

static bool slider_int_frame(const nt_pointer_t *p, int *value, int min, int max, int step, bool enabled) {
    bool changed = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = SL_X, .y = SL_Y}}}) {
        changed = nt_ui_slider_int(s_fx.ctx, NULL, 0, nt_ui_id("sl"), NULL, value, min, max, step, &s_style, &s_track_decl, enabled);
    }
    nt_ui_end(s_fx.ctx);
    return changed;
}

/* Warm-up frame so Clay caches the track bbox for next frame's hit-test + thumb rect. */
static void warmup_float(float *value, float min, float max) {
    nt_pointer_t f0 = make_pointer(SL_X - 50.0F, SL_CY, false, false, false);
    (void)slider_float_frame(&f0, value, min, max, 0.0F, true);
}

/* ---- Vertical slider fixture: narrow tall track, wide short thumb (overhang on left/right). ---- */
#define VT_X 100.0F
#define VT_Y 200.0F
#define VT_W 20.0F  /* narrow track */
#define VT_H 200.0F /* tall track */
#define VT_TW 24.0F /* wide thumb (overhangs the track by 2px each side) */
#define VT_TH 20.0F /* short thumb */
#define VT_CX (VT_X + (VT_W * 0.5F))
#define VT_USABLE (VT_H - VT_TH) /* travel of the thumb TOP edge */

/* Pointer-Y that maps to value fraction f. BOTTOM_UP: value-up = screen-up (inverted). */
#define VT_Y_AT_FRAC_BU(f) (VT_Y + ((1.0F - (f)) * VT_USABLE) + (VT_TH * 0.5F))
/* TOP_DOWN: value-up = screen-down (plain). */
#define VT_Y_AT_FRAC_TD(f) (VT_Y + ((f) * VT_USABLE) + (VT_TH * 0.5F))

static const Clay_ElementDeclaration s_vtrack_decl = {
    .layout = {.sizing = {CLAY_SIZING_FIXED(VT_W), CLAY_SIZING_FIXED(VT_H)}},
};

/* Same base art/speeds as init_style, then swap to a vertical axis with the given anchor. */
static void init_vstyle(nt_ui_fill_direction_t dir) {
    s_style.track_w = VT_W;
    s_style.track_h = VT_H;
    s_style.thumb_w = VT_TW;
    s_style.thumb_h = VT_TH;
    s_style.orientation = NT_UI_SLIDER_VERTICAL;
    s_style.fill_direction = dir;
}

static bool slider_vfloat_frame(const nt_pointer_t *p, float *value, float min, float max, float step, bool enabled) {
    bool changed = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = VT_X, .y = VT_Y}}}) {
        changed = nt_ui_slider_float(s_fx.ctx, NULL, 0, nt_ui_id("vsl"), NULL, value, min, max, step, &s_style, &s_vtrack_decl, enabled);
    }
    nt_ui_end(s_fx.ctx);
    return changed;
}

static void warmup_vfloat(float *value, float min, float max) {
    nt_pointer_t f0 = make_pointer(VT_CX, VT_Y - 50.0F, false, false, false);
    (void)slider_vfloat_frame(&f0, value, min, max, 0.0F, true);
}

/* ---- Test 1: track-press jumps the value to the click point (left edge -> min,
 *      right edge -> max, midpoint -> mid). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_track_jump_value_map(void) {
    float value = 0.5F;
    warmup_float(&value, 0.0F, 1.0F);

    /* Press at the far left (thumb-center anchor) -> value 0. */
    nt_pointer_t pl = make_pointer(SL_X_AT_FRAC(0.0F), SL_CY, true, true, false);
    (void)slider_float_frame(&pl, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.0F, 0.02F));

    /* Release, re-warm, press at the far right -> value 1. */
    nt_pointer_t up = make_pointer(SL_X_AT_FRAC(0.0F), SL_CY, false, false, true);
    (void)slider_float_frame(&up, &value, 0.0F, 1.0F, 0.0F, true);
    nt_pointer_t pr = make_pointer(SL_X_AT_FRAC(1.0F), SL_CY, true, true, false);
    (void)slider_float_frame(&pr, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 1.0F, 0.02F));

    /* Release, press at the midpoint -> value 0.5. */
    nt_pointer_t up2 = make_pointer(SL_X_AT_FRAC(1.0F), SL_CY, false, false, true);
    (void)slider_float_frame(&up2, &value, 0.0F, 1.0F, 0.0F, true);
    nt_pointer_t pm = make_pointer(SL_X_AT_FRAC(0.5F), SL_CY, true, true, false);
    (void)slider_float_frame(&pm, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.02F));
}

/* ---- Test 2: a drag that overshoots the track ends clamps the value, not wraps.
 *      Press ON the track to grab, then drag the pointer far past each edge. ---- */
static void test_value_clamped(void) {
    float value = 0.5F;
    warmup_float(&value, 0.0F, 1.0F);

    /* Grab on the track midpoint, then drag the held pointer far LEFT -> clamps to 0. */
    nt_pointer_t grab = make_pointer(SL_X_AT_FRAC(0.5F), SL_CY, true, true, false);
    (void)slider_float_frame(&grab, &value, 0.0F, 1.0F, 0.0F, true);
    nt_pointer_t drag_l = make_pointer(SL_X - 200.0F, SL_CY, true, false, false);
    (void)slider_float_frame(&drag_l, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.0F, 0.001F));

    /* Same hold, drag far RIGHT -> clamps to 1. */
    nt_pointer_t drag_r = make_pointer(SL_X + SL_W + 200.0F, SL_CY, true, false, false);
    (void)slider_float_frame(&drag_r, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 1.0F, 0.001F));
}

/* ---- Test 3: non-unit [min,max] maps linearly (range 10..50, midpoint -> 30). ---- */
static void test_value_map_nonunit_range(void) {
    float value = 10.0F;
    warmup_float(&value, 10.0F, 50.0F);
    nt_pointer_t pm = make_pointer(SL_X_AT_FRAC(0.5F), SL_CY, true, true, false);
    (void)slider_float_frame(&pm, &value, 10.0F, 50.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 30.0F, 0.8F));
}

/* ---- Test 4: float step quantizes to the min + k*step grid (step 0.25). ---- */
static void test_float_step_quantize(void) {
    float value = 0.0F;
    warmup_float(&value, 0.0F, 1.0F);
    /* Press at ~0.6 fraction with step 0.25 -> snaps to 0.5. */
    nt_pointer_t p = make_pointer(SL_X_AT_FRAC(0.6F), SL_CY, true, true, false);
    (void)slider_float_frame(&p, &value, 0.0F, 1.0F, 0.25F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.001F));
}

/* ---- Test 5: int variant casts + quantizes (range 0..10, midpoint -> 5). ---- */
static void test_int_quantize_cast(void) {
    int value = 0;
    nt_pointer_t f0 = make_pointer(SL_X - 50.0F, SL_CY, false, false, false);
    (void)slider_int_frame(&f0, &value, 0, 10, 1, true); /* warm-up */
    nt_pointer_t pm = make_pointer(SL_X_AT_FRAC(0.5F), SL_CY, true, true, false);
    (void)slider_int_frame(&pm, &value, 0, 10, 1, true);
    TEST_ASSERT_EQUAL_INT(5, value);

    /* step 2 -> only even values; ~0.65 fraction (6.5) rounds to 6. */
    nt_pointer_t up = make_pointer(SL_X_AT_FRAC(0.5F), SL_CY, false, false, true);
    (void)slider_int_frame(&up, &value, 0, 10, 2, true);
    nt_pointer_t p2 = make_pointer(SL_X_AT_FRAC(0.65F), SL_CY, true, true, false);
    (void)slider_int_frame(&p2, &value, 0, 10, 2, true);
    TEST_ASSERT_EQUAL_INT(6, value);
}

/* ---- Test 6: press ON the thumb keeps the value that frame (relative grab), then a
 *      relative drag moves it; press on the track (off the thumb) jumps. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_thumb_grab_vs_track_jump(void) {
    float value = 0.5F; /* thumb sits at the center */
    warmup_float(&value, 0.0F, 1.0F);

    /* Press exactly ON the thumb center -> value UNCHANGED this frame (grab, not jump). */
    const float thumb_center_x = SL_X_AT_FRAC(0.5F);
    nt_pointer_t grab = make_pointer(thumb_center_x, SL_CY, true, true, false);
    (void)slider_float_frame(&grab, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.02F));

    /* Drag right by a quarter of the usable width -> value moves +0.25 (relative). */
    nt_pointer_t drag = make_pointer(thumb_center_x + (SL_USABLE * 0.25F), SL_CY, true, false, false);
    (void)slider_float_frame(&drag, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.75F, 0.03F));

    /* Release; new press on the FAR LEFT track (off the thumb) -> jumps to ~0. */
    nt_pointer_t up = make_pointer(thumb_center_x + (SL_USABLE * 0.25F), SL_CY, false, false, true);
    (void)slider_float_frame(&up, &value, 0.0F, 1.0F, 0.0F, true);
    nt_pointer_t jump = make_pointer(SL_X_AT_FRAC(0.0F), SL_CY, true, true, false);
    (void)slider_float_frame(&jump, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.0F, 0.03F));
}

/* ---- Test 7: nt_ui_slider_thumb_pos returns the thumb screen center for the current
 *      value fraction (= track_left + frac*(track_w - thumb_w) + thumb_w/2). ---- */
static void test_thumb_pos_exposed(void) {
    float value = 0.5F;
    /* Two frames so the view cell + bbox are populated (thumb_pos reads prev-frame). */
    warmup_float(&value, 0.0F, 1.0F);
    nt_pointer_t idle = make_pointer(SL_X - 50.0F, SL_CY, false, false, false);
    (void)slider_float_frame(&idle, &value, 0.0F, 1.0F, 0.0F, true);

    const nt_ui_slider_thumb_t t = nt_ui_slider_thumb_pos(s_fx.ctx, nt_ui_id("sl"));
    TEST_ASSERT_TRUE(t.found);
    TEST_ASSERT_TRUE(float_near(t.x, SL_X_AT_FRAC(0.5F), 1.0F));
    TEST_ASSERT_TRUE(float_near(t.y, SL_CY, 1.0F));
}

/* ---- Test 8: thumb_pos for an id never declared returns found=false. ---- */
static void test_thumb_pos_unknown_id(void) {
    const nt_ui_slider_thumb_t t = nt_ui_slider_thumb_pos(s_fx.ctx, nt_ui_id("never_declared"));
    TEST_ASSERT_FALSE(t.found);
}

/* ---- Test 9: disabled slider ignores a track press (value unchanged) but still emits. ---- */
static void test_disabled_no_drag(void) {
    float value = 0.5F;
    warmup_float(&value, 0.0F, 1.0F);
    nt_pointer_t p = make_pointer(SL_X_AT_FRAC(0.0F), SL_CY, true, true, false);
    TEST_ASSERT_FALSE(slider_float_frame(&p, &value, 0.0F, 1.0F, 0.0F, false));
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.001F)); /* untouched */
}

/* ---- Test 9b: disabling mid-drag clears the drag cell, so re-enabling with the
 *      pointer still held does NOT resume the stale grab and jump the value. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_disabled_mid_drag_no_resume_jump(void) {
    float value = 0.5F; /* thumb at center */
    warmup_float(&value, 0.0F, 1.0F);

    /* Grab ON the thumb center: value unchanged, drag cell now active with a grab offset. */
    const float thumb_center_x = SL_X_AT_FRAC(0.5F);
    nt_pointer_t grab = make_pointer(thumb_center_x, SL_CY, true, true, false);
    (void)slider_float_frame(&grab, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.02F));

    /* Disable while the pointer is held FAR from the thumb: a stale grab cell would map
     * this pointer to the value on re-enable. The value must stay put while disabled. */
    nt_pointer_t held_far = make_pointer(SL_X_AT_FRAC(0.0F), SL_CY, true, false, false);
    TEST_ASSERT_FALSE(slider_float_frame(&held_far, &value, 0.0F, 1.0F, 0.0F, false));
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.001F)); /* untouched while disabled */

    /* Re-enable with the pointer still held (no fresh press_now): the cleared cell means
     * no live drag, so the value must NOT snap to the held pointer position. */
    nt_pointer_t held_still = make_pointer(SL_X_AT_FRAC(0.0F), SL_CY, true, false, false);
    TEST_ASSERT_FALSE(slider_float_frame(&held_still, &value, 0.0F, 1.0F, 0.0F, true));
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.02F)); /* no stale-grab jump */
}

/* ---- Test 10: style defaults are a valid baseline that renders with supplied art. ---- */
static void test_style_defaults_valid_baseline(void) {
    nt_ui_slider_style_t s = nt_ui_slider_style_defaults();
    TEST_ASSERT_TRUE(s.track_w > 0.0F && s.track_h > 0.0F);
    TEST_ASSERT_EQUAL_INT(NT_UI_FILL_STRETCH, s.fill_mode);
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(s.states[i].opacity > 0.0F);
    }
}

/* ---- Test 11: fill rect MUST stay inside the track rect at every ratio (LTR STRETCH).
 *      Pins the emit geometry: fill left == track left, fill width == ratio*track_w, fill
 *      right <= track right. A regression here = the visual "fill pokes past the track end". ---- */
/* The track is the registered slider id (image cmd at the track bbox); the fill is the only
 * other track-tall image whose left edge == track left and width < track width. */
static bool find_track_and_fill(float *track_left, float *track_right, float *fill_left, float *fill_right, bool *has_fill) {
    const Clay_RenderCommand *track_cmd = NULL;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE && c->id == nt_ui_id("sl")) {
            track_cmd = c;
            break;
        }
    }
    if (track_cmd == NULL) {
        return false;
    }
    *track_left = track_cmd->boundingBox.x;
    *track_right = track_cmd->boundingBox.x + track_cmd->boundingBox.width;
    /* Fill = the image whose left edge matches the track and width is <= the track width but it
     * is NOT the track itself (no id) and NOT the thumb (thumb is taller / left==track only at 0). */
    *has_fill = false;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType != CLAY_RENDER_COMMAND_TYPE_IMAGE || c == track_cmd) {
            continue;
        }
        const Clay_BoundingBox b = c->boundingBox;
        const bool track_tall = float_near(b.height, SL_H, 0.5F); /* fill is track-tall; thumb is SL_TH */
        if (track_tall && float_near(b.x, *track_left, 0.5F)) {
            *fill_left = b.x;
            *fill_right = b.x + b.width;
            *has_fill = true;
            return true;
        }
    }
    return true; /* no fill (ratio 0) is valid */
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_fill_inside_track_all_ratios(void) {
    const float ratios[] = {0.0F, 0.05F, 0.18F, 0.5F, 0.95F, 1.0F};
    for (size_t ri = 0; ri < sizeof ratios / sizeof ratios[0]; ++ri) {
        float value = ratios[ri];
        warmup_float(&value, 0.0F, 1.0F);
        nt_pointer_t idle = make_pointer(SL_X - 50.0F, SL_CY, false, false, false);
        (void)slider_float_frame(&idle, &value, 0.0F, 1.0F, 0.0F, true);

        float tl = 0.0F;
        float tr = 0.0F;
        float fl = 0.0F;
        float fr = 0.0F;
        bool has_fill = false;
        TEST_ASSERT_TRUE(find_track_and_fill(&tl, &tr, &fl, &fr, &has_fill));
        TEST_ASSERT_TRUE(float_near(tl, SL_X, 0.5F));
        TEST_ASSERT_TRUE(float_near(tr, SL_X + SL_W, 0.5F));

        TEST_ASSERT_TRUE(has_fill);
        /* Left edge anchored to the track; right edge == THUMB CENTER (fill meets the knob,
         * never fraction*track_w — the thumb travels inset by thumb_w/2); never past the track. */
        TEST_ASSERT_TRUE(float_near(fl, tl, 0.5F));
        TEST_ASSERT_TRUE(float_near(fr, SL_X_AT_FRAC(ratios[ri]), 0.5F));
        TEST_ASSERT_TRUE(fl >= tl - 0.5F);
        TEST_ASSERT_TRUE(fr <= tr + 0.5F);
    }
}

/* ---- Test 12: hit pad makes a press ABOVE the track (within the pad) start a drag.
 *      Without the pad the press would miss the thin track entirely. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_hit_pad_above_track_drags(void) {
    float value = 0.0F;
    /* 12px top/bottom pad -> effective height 20 + 24 = 44px (touch-target reference). */
    s_style.hit_padding_lrtb[2] = 12;
    s_style.hit_padding_lrtb[3] = 12;
    warmup_float(&value, 0.0F, 1.0F);

    /* Press 10px ABOVE the track top, at the horizontal midpoint. Inside the 12px top pad,
     * outside the bare track -> a drag must start and jump the value to ~0.5. */
    const float above_y = SL_Y - 10.0F;
    TEST_ASSERT_TRUE(above_y < SL_Y); /* genuinely outside the visual track */
    nt_pointer_t p = make_pointer(SL_X_AT_FRAC(0.5F), above_y, true, true, false);
    (void)slider_float_frame(&p, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.03F));
}

/* ---- Test 13: thumb overhang (thumb_h > track_h) is clickable even at ZERO style pad —
 *      the effective vertical pad auto-grows to (thumb_h - track_h)/2. ---- */
static void test_thumb_overhang_auto_pad_drags(void) {
    float value = 0.0F;
    /* No style pad; thumb_h 24 > track_h 20 -> 2px overhang each side auto-padded. */
    s_style.hit_padding_lrtb[2] = 0;
    s_style.hit_padding_lrtb[3] = 0;
    warmup_float(&value, 0.0F, 1.0F);

    /* Press 1px above the track top (inside the 2px auto-grown overhang pad). */
    nt_pointer_t p = make_pointer(SL_X_AT_FRAC(0.5F), SL_Y - 1.0F, true, true, false);
    (void)slider_float_frame(&p, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.03F));
}

/* ---- Test 14: stepped drag SNAPS the thumb visually — the emitted thumb center sits on the
 *      nearest tick fraction, not the raw pointer fraction. int slider 0..10, drag between ticks. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_step_thumb_visual_snap(void) {
    int value = 0;
    nt_pointer_t f0 = make_pointer(SL_X - 50.0F, SL_CY, false, false, false);
    (void)slider_int_frame(&f0, &value, 0, 10, 1, true); /* warm-up */

    /* Press at fraction 0.63 (between tick 0.6 and 0.7 on a 0..10 grid). Value snaps to 6. */
    nt_pointer_t p = make_pointer(SL_X_AT_FRAC(0.63F), SL_CY, true, true, false);
    (void)slider_int_frame(&p, &value, 0, 10, 1, true);
    TEST_ASSERT_EQUAL_INT(6, value);

    /* A second frame so the view cell + bbox latch the snapped fraction for thumb_pos. */
    nt_pointer_t hold = make_pointer(SL_X_AT_FRAC(0.63F), SL_CY, true, false, false);
    (void)slider_int_frame(&hold, &value, 0, 10, 1, true);

    /* The emitted thumb center sits on the 0.6 tick fraction, NOT the 0.63 pointer fraction. */
    const nt_ui_slider_thumb_t t = nt_ui_slider_thumb_pos(s_fx.ctx, nt_ui_id("sl"));
    TEST_ASSERT_TRUE(t.found);
    TEST_ASSERT_TRUE(float_near(t.x, SL_X_AT_FRAC(0.6F), 0.6F));
    /* And NOT at the raw pointer fraction (would be ~3px off at this width — guards the regression). */
    TEST_ASSERT_FALSE(float_near(t.x, SL_X_AT_FRAC(0.63F), 0.6F));
}

/* ---- Test 15: an out-of-range incoming *value is clamped AND written back (returns true)
 *      on the first frame, with no pointer interaction. Unity property-clamp semantics. ---- */
static void test_out_of_range_writeback(void) {
    /* Above max: clamps to 1.0, write-back true. */
    float hi = 5.0F;
    nt_pointer_t idle = make_pointer(SL_X - 50.0F, SL_CY, false, false, false);
    const bool changed_hi = slider_float_frame(&idle, &hi, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(changed_hi);
    TEST_ASSERT_TRUE(float_near(hi, 1.0F, 0.001F));

    /* Below min: clamps to 0.0, write-back true. */
    float lo = -3.0F;
    const bool changed_lo = slider_float_frame(&idle, &lo, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(changed_lo);
    TEST_ASSERT_TRUE(float_near(lo, 0.0F, 0.001F));

    /* In-range, untouched -> no write-back. */
    float ok = 0.5F;
    const bool changed_ok = slider_float_frame(&idle, &ok, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_FALSE(changed_ok);
    TEST_ASSERT_TRUE(float_near(ok, 0.5F, 0.001F));
}

/* ---- Test 16: a slider inside a scroll container scrolled under the clip edge must have its
 *      floating THUMB clipped to the scroll viewport. Clay emits clipTo=ATTACHED_PARENT as a
 *      SCISSOR_START (both clip axes false marker) carrying the attached clip rect; with
 *      CLIP_TO_NONE that scissor is absent and the thumb leaks past the container clip. ----
 *
 * Scroll container 200x200 at SCROLL_Y; tall content holds the slider near the bottom so a
 * scroll-down pushes the thumb past the container's bottom clip edge. We assert the thumb IMAGE
 * command sits inside a both-axes-false SCISSOR whose bbox == the scroll viewport, and that the
 * viewport actually clips the thumb (thumb bottom extends past the viewport bottom). */
#define SCROLL_SL_ID 0x5C5117U
#define SCROLL_X 60.0F
#define SCROLL_Y 40.0F
#define SCROLL_DIM 200.0F
#define SCROLL_CONTENT_H 2000.0F
#define SCROLL_SL_PAD_TOP 1700.0F /* slider sits low in the content so scrolling drives it off-screen */

static nt_ui_scroll_style_t scroll_y_style(void) {
    nt_ui_scroll_style_t st = nt_ui_scroll_style_defaults();
    st.scroll_x = false;
    st.scroll_y = true;
    st.bar_visibility = NT_UI_SCROLLBAR_AUTO; /* no bar art -> no extra floating images to confuse the scan */
    st.wheel_ease_speed = 0.0F;
    return st;
}

/* One frame: scroll container holding the slider low in tall content. */
static void slider_in_scroll_frame(const nt_pointer_t *p, float *value, const nt_ui_scroll_style_t *scroll_style) {
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, p, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = SCROLL_X, .y = SCROLL_Y}}}) {
        nt_ui_scroll_begin(s_fx.ctx, NULL, SCROLL_SL_ID, scroll_style, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(SCROLL_DIM), CLAY_SIZING_FIXED(SCROLL_DIM)}}});
        {
            CLAY({.id = CLAY_ID("scroll-content"),
                  .layout = {.sizing = {CLAY_SIZING_FIXED(SCROLL_DIM), CLAY_SIZING_FIXED(SCROLL_CONTENT_H)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = {.top = (uint16_t)SCROLL_SL_PAD_TOP}}}) {
                (void)nt_ui_slider_float(s_fx.ctx, NULL, 0, SCROLL_SL_ID + 1U, NULL, value, 0.0F, 1.0F, 0.0F, &s_style, &s_track_decl, true);
            }
        }
        nt_ui_scroll_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

/* The thumb IMAGE command: SL_TH tall (taller than the SL_H track + fill), the only such image. */
static const Clay_RenderCommand *find_thumb_image(int32_t *out_index) {
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE && float_near(c->boundingBox.height, SL_TH, 0.5F)) {
            if (out_index != NULL) {
                *out_index = i;
            }
            return c;
        }
    }
    return NULL;
}

/* Walk the frozen commands up to thumb_index, tracking the innermost active both-axes-false
 * (floating-clipTo) SCISSOR bbox. Returns true + the clip rect if the thumb is enclosed by one. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool active_floating_clip_at(int32_t thumb_index, Clay_BoundingBox *out_clip) {
    Clay_BoundingBox stack[16];
    bool is_floating_marker[16];
    int depth = 0;
    bool found = false;
    for (int32_t i = 0; i < thumb_index; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
            const bool both_false = !c->renderData.clip.horizontal && !c->renderData.clip.vertical;
            if (depth < 16) {
                stack[depth] = c->boundingBox;
                is_floating_marker[depth] = both_false;
                ++depth;
            }
        } else if (c->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
            if (depth > 0) {
                --depth;
            }
        }
    }
    /* Innermost floating-clip marker still open at the thumb. */
    for (int d = depth - 1; d >= 0; --d) {
        if (is_floating_marker[d]) {
            if (out_clip != NULL) {
                *out_clip = stack[d];
            }
            found = true;
            break;
        }
    }
    return found;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_thumb_clipped_in_scroll(void) {
    nt_ui_scroll_style_t scroll_style = scroll_y_style();
    float value = 0.5F;
    nt_pointer_t idle = make_pointer(0.0F, 0.0F, false, false, false);

    /* Two frames so Clay solves the scroll dims, then scroll so the slider track (local y ~=
     * SCROLL_SL_PAD_TOP) straddles the container's BOTTOM clip edge: place the track top a little
     * above the viewport bottom so the SL_TH-tall thumb pokes past the clip but stays partly visible. */
    slider_in_scroll_frame(&idle, &value, &scroll_style);
    slider_in_scroll_frame(&idle, &value, &scroll_style);
    const float target = -(SCROLL_SL_PAD_TOP - (SCROLL_DIM - (SL_TH * 0.5F))); /* track top ~ viewport bottom - thumb/2 */
    nt_ui_scroll_to(s_fx.ctx, SCROLL_SL_ID, 0.0F, target);
    slider_in_scroll_frame(&idle, &value, &scroll_style);
    slider_in_scroll_frame(&idle, &value, &scroll_style);

    int32_t thumb_index = -1;
    const Clay_RenderCommand *thumb = find_thumb_image(&thumb_index);
    TEST_ASSERT_NOT_NULL(thumb);

    /* The thumb must be enclosed by a floating-clipTo SCISSOR (the clipTo=ATTACHED_PARENT marker). */
    Clay_BoundingBox clip = {0};
    const bool clipped = active_floating_clip_at(thumb_index, &clip);
    TEST_ASSERT_TRUE(clipped); /* FAILS if clipTo reverts to CLAY_CLIP_TO_NONE (no scissor emitted) */

    /* The clip rect is the scroll viewport (200x200 at the scroll container origin). */
    TEST_ASSERT_TRUE(float_near(clip.height, SCROLL_DIM, 1.0F));
    const float clip_top = clip.y;
    const float clip_bottom = clip.y + clip.height;

    /* The thumb actually pokes past the viewport bottom -> the clip is doing real work (the leak
     * the fix prevents). The visible thumb is the IMAGE bbox intersected with this clip rect. */
    const float thumb_top = thumb->boundingBox.y;
    const float thumb_bottom = thumb->boundingBox.y + thumb->boundingBox.height;
    TEST_ASSERT_TRUE(thumb_bottom > clip_bottom + 0.5F); /* leaks past the bottom clip edge without clipping */
    TEST_ASSERT_TRUE(thumb_top < clip_bottom);           /* but still partially inside (a real partial clip) */
    TEST_ASSERT_TRUE(thumb_bottom > clip_top);           /* sanity: overlaps the viewport */
}

/* ---- Test V1: vertical track-jump value map, BOTTOM_UP (top edge -> max, bottom -> min). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_vertical_track_jump_bottom_up(void) {
    init_vstyle(NT_UI_FILL_BOTTOM_UP);
    float value = 0.5F;
    warmup_vfloat(&value, 0.0F, 1.0F);

    /* Press at the TOP edge -> value 1 (up = more under BOTTOM_UP). */
    nt_pointer_t pt = make_pointer(VT_CX, VT_Y_AT_FRAC_BU(1.0F), true, true, false);
    (void)slider_vfloat_frame(&pt, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 1.0F, 0.02F));

    /* Release, press at the BOTTOM edge -> value 0. */
    nt_pointer_t up = make_pointer(VT_CX, VT_Y_AT_FRAC_BU(1.0F), false, false, true);
    (void)slider_vfloat_frame(&up, &value, 0.0F, 1.0F, 0.0F, true);
    nt_pointer_t pb = make_pointer(VT_CX, VT_Y_AT_FRAC_BU(0.0F), true, true, false);
    (void)slider_vfloat_frame(&pb, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.0F, 0.02F));

    /* Release, press at the MIDDLE -> value 0.5. */
    nt_pointer_t up2 = make_pointer(VT_CX, VT_Y_AT_FRAC_BU(0.0F), false, false, true);
    (void)slider_vfloat_frame(&up2, &value, 0.0F, 1.0F, 0.0F, true);
    nt_pointer_t pm = make_pointer(VT_CX, VT_Y_AT_FRAC_BU(0.5F), true, true, false);
    (void)slider_vfloat_frame(&pm, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.02F));
}

/* ---- Test V2: vertical track-jump value map, TOP_DOWN (top edge -> min, bottom -> max). ---- */
static void test_vertical_track_jump_top_down(void) {
    init_vstyle(NT_UI_FILL_TOP_DOWN);
    float value = 0.5F;
    warmup_vfloat(&value, 0.0F, 1.0F);

    /* Press at the TOP edge -> value 0 (down = more under TOP_DOWN). */
    nt_pointer_t pt = make_pointer(VT_CX, VT_Y_AT_FRAC_TD(0.0F), true, true, false);
    (void)slider_vfloat_frame(&pt, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.0F, 0.02F));

    /* Release, press at the BOTTOM edge -> value 1. */
    nt_pointer_t up = make_pointer(VT_CX, VT_Y_AT_FRAC_TD(0.0F), false, false, true);
    (void)slider_vfloat_frame(&up, &value, 0.0F, 1.0F, 0.0F, true);
    nt_pointer_t pb = make_pointer(VT_CX, VT_Y_AT_FRAC_TD(1.0F), true, true, false);
    (void)slider_vfloat_frame(&pb, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 1.0F, 0.02F));
}

/* ---- Test V3a: vertical thumb-grab relative drag under BOTTOM_UP. Dragging the pointer UP
 *      INCREASES the value (screen-Y inversion: up = more). ---- */
static void test_vertical_thumb_grab_bottom_up(void) {
    init_vstyle(NT_UI_FILL_BOTTOM_UP);
    float value = 0.5F;
    warmup_vfloat(&value, 0.0F, 1.0F);
    const float thumb_center = VT_Y_AT_FRAC_BU(0.5F);
    nt_pointer_t grab = make_pointer(VT_CX, thumb_center, true, true, false);
    (void)slider_vfloat_frame(&grab, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.02F)); /* grab keeps the value this frame */
    nt_pointer_t up = make_pointer(VT_CX, thumb_center - (VT_USABLE * 0.25F), true, false, false);
    (void)slider_vfloat_frame(&up, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.75F, 0.03F)); /* up = more */
}

/* ---- Test V3b: vertical thumb-grab relative drag under TOP_DOWN. Dragging the pointer UP
 *      DECREASES the value (plain mapping: down = more). ---- */
static void test_vertical_thumb_grab_top_down(void) {
    init_vstyle(NT_UI_FILL_TOP_DOWN);
    float value = 0.5F;
    warmup_vfloat(&value, 0.0F, 1.0F);
    const float thumb_center = VT_Y_AT_FRAC_TD(0.5F);
    nt_pointer_t grab = make_pointer(VT_CX, thumb_center, true, true, false);
    (void)slider_vfloat_frame(&grab, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.02F)); /* grab keeps the value this frame */
    nt_pointer_t up = make_pointer(VT_CX, thumb_center - (VT_USABLE * 0.25F), true, false, false);
    (void)slider_vfloat_frame(&up, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.25F, 0.03F)); /* up = less */
}

/* ---- Test V4: a wide thumb (thumb_w > track_w) auto-grows the hit pad on LEFT/RIGHT for a
 *      vertical slider; a press just LEFT of the thin track (inside the overhang pad) drags. ---- */
static void test_vertical_hit_pad_left_right(void) {
    init_vstyle(NT_UI_FILL_BOTTOM_UP);
    s_style.hit_padding_lrtb[0] = 0; /* no style pad: overhang = (24-20)/2 = 2px auto-grown each side */
    s_style.hit_padding_lrtb[1] = 0;
    float value = 0.0F;
    warmup_vfloat(&value, 0.0F, 1.0F);

    /* Press 1px LEFT of the track left edge (inside the 2px auto-grown left pad), at the vertical
     * middle -> a drag must start and jump the value to ~0.5. Misses the bare thin track. */
    const float left_x = VT_X - 1.0F;
    TEST_ASSERT_TRUE(left_x < VT_X); /* genuinely outside the visual track */
    nt_pointer_t p = make_pointer(left_x, VT_Y_AT_FRAC_BU(0.5F), true, true, false);
    (void)slider_vfloat_frame(&p, &value, 0.0F, 1.0F, 0.0F, true);
    TEST_ASSERT_TRUE(float_near(value, 0.5F, 0.03F));
}

/* ---- Death tests (NT_ASSERT_FULL only) ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* axis mismatch (VERTICAL + LTR fill) asserts AND hard-coerces fill_direction to the axis
 * default (BOTTOM_UP) in place, so the no-assert/shipping path never maps the wrong axis. */
static void test_vertical_axis_mismatch_coerce(void) {
    init_vstyle(NT_UI_FILL_LTR); /* LTR is a HORIZONTAL anchor -> mismatch for VERTICAL */
    float value = 0.0F;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_slider_float(s_fx.ctx, NULL, 0, nt_ui_id("vsl"), NULL, &value, 0.0F, 1.0F, 0.0F, &s_style, &s_vtrack_decl, true)); }
    nt_ui_end(s_fx.ctx);
    /* The coerce ran before the assert longjmp: fill_direction snapped to the vertical default. */
    TEST_ASSERT_EQUAL_INT(NT_UI_FILL_BOTTOM_UP, s_style.fill_direction);
}

/* track_w == 0 -> assert. */
static void test_assert_track_w_zero(void) {
    float value = 0.5F;
    nt_ui_slider_style_t bad = s_style;
    bad.track_w = 0.0F;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_slider_float(s_fx.ctx, NULL, 0, nt_ui_id("sl"), NULL, &value, 0.0F, 1.0F, 0.0F, &bad, &s_track_decl, true)); }
    nt_ui_end(s_fx.ctx);
}

/* min == max -> assert. */
static void test_assert_min_eq_max(void) {
    float value = 0.5F;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_slider_float(s_fx.ctx, NULL, 0, nt_ui_id("sl"), NULL, &value, 1.0F, 1.0F, 0.0F, &s_style, &s_track_decl, true)); }
    nt_ui_end(s_fx.ctx);
}

/* min > max (reversed range) -> assert, both float and int wrappers (inverted clamps). */
static void test_assert_min_gt_max(void) {
    float fvalue = 0.5F;
    int ivalue = 5;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        NT_TEST_EXPECT_ASSERT((void)nt_ui_slider_float(s_fx.ctx, NULL, 0, nt_ui_id("slf"), NULL, &fvalue, 1.0F, 0.0F, 0.0F, &s_style, &s_track_decl, true));
        NT_TEST_EXPECT_ASSERT((void)nt_ui_slider_int(s_fx.ctx, NULL, 0, nt_ui_id("sli"), NULL, &ivalue, 10, 0, 0, &s_style, &s_track_decl, true));
    }
    nt_ui_end(s_fx.ctx);
}

/* data->flags must NOT set HAS_TRANSFORM/HAS_OPACITY -- the widget owns those. */
static void test_assert_data_flags_transform(void) {
    float value = 0.5F;
    nt_ui_transform_t t = nt_ui_transform_defaults();
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        const nt_ui_element_data_t *bad = NT_UI_DATA_XFORM(0U, &t, 1.0F); /* sets HAS_TRANSFORM|HAS_OPACITY */
        NT_TEST_EXPECT_ASSERT((void)nt_ui_slider_float(s_fx.ctx, bad, 0, nt_ui_id("sl"), NULL, &value, 0.0F, 1.0F, 0.0F, &s_style, &s_track_decl, true));
    }
    nt_ui_end(s_fx.ctx);
}

/* negative step -> assert. */
static void test_assert_negative_step(void) {
    float value = 0.5F;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT((void)nt_ui_slider_float(s_fx.ctx, NULL, 0, nt_ui_id("sl"), NULL, &value, 0.0F, 1.0F, -1.0F, &s_style, &s_track_decl, true)); }
    nt_ui_end(s_fx.ctx);
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_track_jump_value_map);
    RUN_TEST(test_value_clamped);
    RUN_TEST(test_value_map_nonunit_range);
    RUN_TEST(test_float_step_quantize);
    RUN_TEST(test_int_quantize_cast);
    RUN_TEST(test_thumb_grab_vs_track_jump);
    RUN_TEST(test_thumb_pos_exposed);
    RUN_TEST(test_thumb_pos_unknown_id);
    RUN_TEST(test_disabled_no_drag);
    RUN_TEST(test_disabled_mid_drag_no_resume_jump);
    RUN_TEST(test_style_defaults_valid_baseline);
    RUN_TEST(test_fill_inside_track_all_ratios);
    RUN_TEST(test_hit_pad_above_track_drags);
    RUN_TEST(test_thumb_overhang_auto_pad_drags);
    RUN_TEST(test_step_thumb_visual_snap);
    RUN_TEST(test_out_of_range_writeback);
    RUN_TEST(test_thumb_clipped_in_scroll);
    RUN_TEST(test_vertical_track_jump_bottom_up);
    RUN_TEST(test_vertical_track_jump_top_down);
    RUN_TEST(test_vertical_thumb_grab_bottom_up);
    RUN_TEST(test_vertical_thumb_grab_top_down);
    RUN_TEST(test_vertical_hit_pad_left_right);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_vertical_axis_mismatch_coerce);
    RUN_TEST(test_assert_track_w_zero);
    RUN_TEST(test_assert_min_eq_max);
    RUN_TEST(test_assert_min_gt_max);
    RUN_TEST(test_assert_data_flags_transform);
    RUN_TEST(test_assert_negative_step);
#endif
    return UNITY_END();
}
