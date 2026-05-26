/* Unit tests for nt_ui_scale math (Phase 53 Plan 05 adaptive UI helper).
 *
 * Note on floats: Unity is compiled with UNITY_EXCLUDE_FLOAT (see
 * deps/unity/CMakeLists.txt). All values here are integer-valued or
 * scaled to integers before comparing. Same pattern as test_nt_ui_label. */

#include <stdbool.h>
#include <stdint.h>

#include "test_helpers/nt_assert_trap.h"
#include "ui/nt_ui_scale.h"
#include "unity.h"

void setUp(void) { nt_test_assert_install(); }
void tearDown(void) {}

/* Compare scaled-by-1000 to avoid float assertion. Tolerance = 1 = 0.001. */
static void assert_int_eq_scaled(int32_t expected_x1000, float actual, int32_t tolerance) {
    int32_t actual_x1000 = (int32_t)((actual * 1000.0F) + (actual >= 0.0F ? 0.5F : -0.5F));
    int32_t diff = actual_x1000 - expected_x1000;
    if (diff < 0) {
        diff = -diff;
    }
    TEST_ASSERT_LESS_OR_EQUAL_INT32(tolerance, diff);
}

/* === STRETCH === */

/* 1:1 scale, dims unchanged, no offset. */
static void test_stretch_identity(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_STRETCH};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 800.0F, 600.0F);
    assert_int_eq_scaled(800000, s.logical_w, 1);
    assert_int_eq_scaled(600000, s.logical_h, 1);
    assert_int_eq_scaled(1000, s.scale_x, 1);
    assert_int_eq_scaled(1000, s.scale_y, 1);
    assert_int_eq_scaled(0, s.offset_x, 1);
    assert_int_eq_scaled(0, s.offset_y, 1);
}

/* Different aspect ratios stretch independently per axis. */
static void test_stretch_anisotropic(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_STRETCH};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 600.0F); /* wider only */
    assert_int_eq_scaled(800000, s.logical_w, 1);
    assert_int_eq_scaled(600000, s.logical_h, 1);
    assert_int_eq_scaled(2000, s.scale_x, 1); /* 1600/800 = 2.0 */
    assert_int_eq_scaled(1000, s.scale_y, 1); /* 600/600 = 1.0 */
}

/* === LETTERBOX === */

/* Wider window than ref: uniform scale from y axis, x bars on left/right. */
static void test_letterbox_wider_window(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_LETTERBOX};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 600.0F);
    /* fit-inside: scale = min(2.0, 1.0) = 1.0 */
    assert_int_eq_scaled(1000, s.scale_x, 1);
    assert_int_eq_scaled(1000, s.scale_y, 1);
    assert_int_eq_scaled(800000, s.logical_w, 1);
    assert_int_eq_scaled(600000, s.logical_h, 1);
    /* bars on x: (1600 - 800*1) / 2 = 400 */
    assert_int_eq_scaled(400000, s.offset_x, 1);
    assert_int_eq_scaled(0, s.offset_y, 1);
}

/* Taller window: y bars top/bottom. */
static void test_letterbox_taller_window(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_LETTERBOX};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 800.0F, 1200.0F);
    assert_int_eq_scaled(1000, s.scale_x, 1); /* min(1.0, 2.0) = 1.0 */
    assert_int_eq_scaled(0, s.offset_x, 1);
    assert_int_eq_scaled(300000, s.offset_y, 1); /* (1200 - 600) / 2 = 300 */
}

/* === CROP === */

/* Wider window: uniform scale from x axis, content clipped vertically.
 * offset_y goes negative (logical TL pushed above physical TL). */
static void test_crop_wider_window(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_CROP};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 600.0F);
    /* fit-outside: scale = max(2.0, 1.0) = 2.0 */
    assert_int_eq_scaled(2000, s.scale_x, 1);
    assert_int_eq_scaled(2000, s.scale_y, 1);
    assert_int_eq_scaled(0, s.offset_x, 1);
    /* (600 - 600*2) / 2 = -300 (top/bottom cropped) */
    int32_t off_y_x1000 = (int32_t)((s.offset_y * 1000.0F) - 0.5F);
    TEST_ASSERT_EQUAL_INT32(-300000, off_y_x1000);
}

/* === EXPAND === */

/* Wider window: scale by min (matching y), logical_w grows past ref_w. */
static void test_expand_wider_window(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_EXPAND};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 600.0F);
    assert_int_eq_scaled(1000, s.scale_x, 1); /* min(2.0, 1.0) = 1.0 */
    assert_int_eq_scaled(1000, s.scale_y, 1);
    assert_int_eq_scaled(1600000, s.logical_w, 1); /* 1600/1 = 1600 (grew) */
    assert_int_eq_scaled(600000, s.logical_h, 1);
    assert_int_eq_scaled(0, s.offset_x, 1);
    assert_int_eq_scaled(0, s.offset_y, 1);
}

/* 2x scale window: logical = ref, scale = 2.0 */
static void test_expand_uniform_2x(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_EXPAND};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 1200.0F);
    assert_int_eq_scaled(2000, s.scale_x, 1);
    assert_int_eq_scaled(800000, s.logical_w, 1);
    assert_int_eq_scaled(600000, s.logical_h, 1);
}

/* === Pointer translation === */

/* Letterbox 2x window: pointer at physical center -> logical center. */
static void test_pointer_letterbox(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_LETTERBOX};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 1200.0F); /* uniform 2x, no bars */
    nt_pointer_t p = {0};
    p.x = 800.0F;
    p.y = 600.0F;
    nt_pointer_t out = nt_ui_scale_apply_pointer(&s, p);
    /* 800/2 = 400 (logical center of 800 wide) */
    assert_int_eq_scaled(400000, out.x, 1);
    assert_int_eq_scaled(300000, out.y, 1);
}

/* Letterbox with bars: pointer inside the bar maps to negative logical. */
static void test_pointer_letterbox_in_bar(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_LETTERBOX};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 600.0F); /* x bars, offset_x=400 */
    nt_pointer_t p = {0};
    p.x = 200.0F; /* inside left bar */
    p.y = 100.0F;
    nt_pointer_t out = nt_ui_scale_apply_pointer(&s, p);
    /* (200 - 400) / 1.0 = -200 (clearly outside logical area) */
    int32_t out_x_x1000 = (int32_t)((out.x * 1000.0F) - 0.5F);
    TEST_ASSERT_EQUAL_INT32(-200000, out_x_x1000);
    assert_int_eq_scaled(100000, out.y, 1);
}

/* Pointer state (buttons, dx, wheel) passes through unchanged. */
static void test_pointer_preserves_state(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_STRETCH};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 800.0F, 600.0F);
    nt_pointer_t p = {0};
    p.x = 100.0F;
    p.y = 50.0F;
    p.dx = 5.0F;
    p.wheel_dy = 1.0F;
    p.buttons[NT_BUTTON_LEFT].is_down = true;
    p.active = true;
    p.id = 42U;
    nt_pointer_t out = nt_ui_scale_apply_pointer(&s, p);
    TEST_ASSERT_EQUAL_UINT32(42U, out.id);
    TEST_ASSERT_TRUE(out.active);
    TEST_ASSERT_TRUE(out.buttons[NT_BUTTON_LEFT].is_down);
    assert_int_eq_scaled(5000, out.dx, 1); /* dx unchanged */
    assert_int_eq_scaled(1000, out.wheel_dy, 1);
}

/* === Edge case: zero framebuffer === */

static void test_zero_framebuffer_safe(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_EXPAND};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 0.0F, 600.0F);
    /* No-op: logical = ref, scale = 1 */
    assert_int_eq_scaled(800000, s.logical_w, 1);
    assert_int_eq_scaled(600000, s.logical_h, 1);
    assert_int_eq_scaled(1000, s.scale_x, 1);
    assert_int_eq_scaled(1000, s.scale_y, 1);
}

/* === Ortho bounds === */

/* EXPAND: no offset, ortho = [0, logical_w] x [0, logical_h]. */
static void test_ortho_expand(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_EXPAND};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 600.0F);
    nt_ui_scale_ortho_t o = nt_ui_scale_ortho(&s);
    assert_int_eq_scaled(0, o.left, 1);
    assert_int_eq_scaled(1600000, o.right, 1); /* logical_w grew to 1600 */
    assert_int_eq_scaled(0, o.bottom, 1);
    assert_int_eq_scaled(600000, o.top, 1);
}

/* LETTERBOX/CROP/STRETCH: ortho ALWAYS [0..logical] regardless of margins;
 * walker viewport already accounts for fb_offset, so margins must not be
 * applied twice. Bars stay clear-color (outside viewport). */
static void test_ortho_letterbox(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_LETTERBOX};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 600.0F);
    nt_ui_scale_ortho_t o = nt_ui_scale_ortho(&s);
    assert_int_eq_scaled(0, o.left, 1);
    assert_int_eq_scaled(800000, o.right, 1);
    assert_int_eq_scaled(0, o.bottom, 1);
    assert_int_eq_scaled(600000, o.top, 1);
}

static void test_ortho_crop(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_CROP};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 600.0F, 1200.0F);
    nt_ui_scale_ortho_t o = nt_ui_scale_ortho(&s);
    assert_int_eq_scaled(0, o.left, 1);
    assert_int_eq_scaled(800000, o.right, 1);
    assert_int_eq_scaled(0, o.bottom, 1);
    assert_int_eq_scaled(600000, o.top, 1);
}

/* STRETCH: no offset, logical = ref. */
static void test_ortho_stretch(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_STRETCH};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 1200.0F);
    nt_ui_scale_ortho_t o = nt_ui_scale_ortho(&s);
    assert_int_eq_scaled(0, o.left, 1);
    assert_int_eq_scaled(800000, o.right, 1);
    assert_int_eq_scaled(0, o.bottom, 1);
    assert_int_eq_scaled(600000, o.top, 1);
}

/* === make_target === */

/* EXPAND target: viewport = logical, fb_size = physical, offset = 0. */
static void test_target_expand(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_EXPAND};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 600.0F);
    nt_ui_target_t t = nt_ui_scale_make_target(&s);
    TEST_ASSERT_TRUE(t.fb_size[0] > 0.0F);
    assert_int_eq_scaled(0, t.viewport[0], 1);
    assert_int_eq_scaled(0, t.viewport[1], 1);
    assert_int_eq_scaled(1600000, t.viewport[2], 1); /* logical_w grew */
    assert_int_eq_scaled(600000, t.viewport[3], 1);
    assert_int_eq_scaled(1600000, t.fb_size[0], 1);
    assert_int_eq_scaled(600000, t.fb_size[1], 1);
    assert_int_eq_scaled(0, t.fb_offset[0], 1);
    assert_int_eq_scaled(0, t.fb_offset[1], 1);
}

/* LETTERBOX target: viewport = logical ref dims, fb_offset = bar margin. */
static void test_target_letterbox(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_LETTERBOX};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 600.0F);
    nt_ui_target_t t = nt_ui_scale_make_target(&s);
    assert_int_eq_scaled(800000, t.viewport[2], 1); /* logical = ref */
    assert_int_eq_scaled(600000, t.viewport[3], 1);
    assert_int_eq_scaled(1600000, t.fb_size[0], 1);
    assert_int_eq_scaled(400000, t.fb_offset[0], 1);
    assert_int_eq_scaled(0, t.fb_offset[1], 1);
}

/* T3: CROP target has negative fb_offset. */
static void test_target_crop(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_CROP};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 600.0F, 1200.0F);
    nt_ui_target_t t = nt_ui_scale_make_target(&s);
    TEST_ASSERT_TRUE(t.fb_size[0] > 0.0F);
    assert_int_eq_scaled(800000, t.viewport[2], 1);
    assert_int_eq_scaled(600000, t.viewport[3], 1);
    assert_int_eq_scaled(600000, t.fb_size[0], 1);
    assert_int_eq_scaled(1200000, t.fb_size[1], 1);
    /* offset_x = (600 - (800*2))/2 = -500 */
    int32_t ox_x1000 = (int32_t)((t.fb_offset[0] * 1000.0F) - 0.5F);
    TEST_ASSERT_EQUAL_INT32(-500000, ox_x1000);
}

/* T3: STRETCH target has no offset, viewport = ref. */
static void test_target_stretch(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_STRETCH};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 1600.0F, 1200.0F);
    nt_ui_target_t t = nt_ui_scale_make_target(&s);
    TEST_ASSERT_TRUE(t.fb_size[0] > 0.0F);
    assert_int_eq_scaled(800000, t.viewport[2], 1);
    assert_int_eq_scaled(600000, t.viewport[3], 1);
    assert_int_eq_scaled(0, t.fb_offset[0], 1);
    assert_int_eq_scaled(0, t.fb_offset[1], 1);
}

/* T4: pointer in CROP mode -- negative offset → positive logical. */
static void test_pointer_crop(void) {
    nt_ui_scale_desc_t desc = {.ref_w = 800.0F, .ref_h = 600.0F, .mode = NT_UI_SCALE_CROP};
    nt_ui_scale_t s = nt_ui_compute_scale(&desc, 600.0F, 1200.0F);
    /* scale=2, offset_x=-500, offset_y=0. Physical (0,0) → logical (250, 0). */
    nt_pointer_t p = {.x = 0.0F, .y = 0.0F};
    nt_pointer_t logical = nt_ui_scale_apply_pointer(&s, p);
    assert_int_eq_scaled(250000, logical.x, 1);
    assert_int_eq_scaled(0, logical.y, 1);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stretch_identity);
    RUN_TEST(test_stretch_anisotropic);
    RUN_TEST(test_letterbox_wider_window);
    RUN_TEST(test_letterbox_taller_window);
    RUN_TEST(test_crop_wider_window);
    RUN_TEST(test_expand_wider_window);
    RUN_TEST(test_expand_uniform_2x);
    RUN_TEST(test_pointer_letterbox);
    RUN_TEST(test_pointer_letterbox_in_bar);
    RUN_TEST(test_pointer_preserves_state);
    RUN_TEST(test_pointer_crop);
    RUN_TEST(test_zero_framebuffer_safe);
    RUN_TEST(test_ortho_expand);
    RUN_TEST(test_ortho_letterbox);
    RUN_TEST(test_ortho_crop);
    RUN_TEST(test_ortho_stretch);
    RUN_TEST(test_target_expand);
    RUN_TEST(test_target_letterbox);
    RUN_TEST(test_target_crop);
    RUN_TEST(test_target_stretch);
    return UNITY_END();
}
