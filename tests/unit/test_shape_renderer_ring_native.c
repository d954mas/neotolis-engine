/* Real-GL proof that ring-allocated instance uploads land where the draws
 * read: multi-flush frames write disjoint ranges yet render pixel-correct.
 * Fixed-size offscreen target — the window framebuffer scales with host DPI. */

#include "graphics/nt_gfx.h"
#include "renderers/nt_shape_renderer.h"
#include "unity.h"
#include "window/nt_window.h"

#include <stddef.h>
#include <stdint.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

enum { RT_W = 64, RT_H = 64 };

static const float k_identity_vp[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static nt_render_target_t s_target;

void setUp(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){
        .max_dpr = 1.0F,
        .resizable = false,
        .width = RT_W,
        .height = RT_H,
    };
    nt_window_init();

    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);

    s_target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = RT_W,
        .height = RT_H,
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_NEAREST,
        .color_mag_filter = NT_FILTER_NEAREST,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = NT_RT_DEPTH_BUFFER,
        .depth_format = NT_TEXTURE_FORMAT_DEPTH24,
        .label = "shape_ring_rt",
    });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, s_target.id);
    TEST_ASSERT_TRUE(nt_gfx_render_target_ready(s_target));

    nt_shape_renderer_init();
    nt_shape_renderer_set_vp(k_identity_vp);
    nt_shape_renderer_set_depth(false);
}

void tearDown(void) {
    nt_shape_renderer_shutdown();
    nt_gfx_destroy_render_target(s_target);
    nt_gfx_shutdown();
    nt_window_shutdown();
}

/* Sample one pixel from a top-left-oriented full-frame readback. */
static const uint8_t *pixel_at(const uint8_t *frame, int x, int y_top) { return &frame[(((size_t)y_top * RT_W) + (size_t)x) * 4U]; }

static void assert_pixel(const uint8_t *frame, int x, int y_top, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t *p = pixel_at(frame, x, y_top);
    TEST_ASSERT_UINT8_WITHIN(1, r, p[0]);
    TEST_ASSERT_UINT8_WITHIN(1, g, p[1]);
    TEST_ASSERT_UINT8_WITHIN(1, b, p[2]);
}

static void test_multi_flush_ring_offsets_render_correctly(void) {
    const float red[4] = {1, 0, 0, 1};
    const float green[4] = {0, 1, 0, 1};
    const float blue[4] = {0, 0, 1, 1};

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});

    /* Flush 1: two instance types -> two ring writes within one flush.
     * Red rect covers the left half; green cube sits center-right (the circle
     * template lies in XZ — edge-on under the identity VP — so cube it is). */
    nt_shape_renderer_rect((float[3]){-0.5F, 0.0F, 0.0F}, (float[2]){1.0F, 2.0F}, red);
    nt_shape_renderer_cube((float[3]){0.5F, 0.0F, 0.0F}, (float[3]){0.5F, 0.5F, 0.5F}, green);
    nt_shape_renderer_flush();

    /* Flush 2: rect again — its upload starts at a nonzero ring offset.
     * Blue rect in the top-right quadrant (clip x [0.2,0.8], y [0.4,0.8]). */
    nt_shape_renderer_rect((float[3]){0.5F, 0.6F, 0.0F}, (float[2]){0.6F, 0.4F}, blue);
    nt_shape_renderer_flush();

    uint8_t frame[RT_W * RT_H * 4U] = {0};
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame)));

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    assert_pixel(frame, 16, 32, 255, 0, 0); /* left half: red rect (flush 1, write 1) */
    assert_pixel(frame, 48, 32, 0, 255, 0); /* cube center: green (flush 1, write 2 at nonzero offset) */
    assert_pixel(frame, 48, 13, 0, 0, 255); /* top-right quadrant: blue rect (flush 2 at nonzero offset) */
    assert_pixel(frame, 56, 56, 0, 0, 0);   /* bottom-right corner untouched: background */
}

/* Wrap: many flushes exceed instance-buffer capacity; after the cursor wraps
 * to 0 the newest shape must still render (no stale data drawn). */
static void test_ring_wrap_still_renders(void) {
    const float red[4] = {1, 0, 0, 1};
    const float green[4] = {0, 1, 0, 1};

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});

    /* Flush count derived from the actual capacity: >= 2 wraps at any
     * configured size (per-flush count clamped so small caps still cycle). */
    uint32_t cap = nt_shape_renderer_test_instance_capacity();
    uint32_t per = cap < 256U ? cap : 256U;
    uint32_t flushes = ((cap / per) * 2U) + 1U;
    for (uint32_t i = 0; i < flushes; i++) {
        for (uint32_t j = 0; j < per; j++) {
            nt_shape_renderer_rect((float[3]){-0.5F, 0.0F, 0.0F}, (float[2]){1.0F, 2.0F}, red);
        }
        nt_shape_renderer_flush();
    }
    nt_shape_renderer_rect((float[3]){0.5F, 0.0F, 0.0F}, (float[2]){1.0F, 2.0F}, green);
    nt_shape_renderer_flush();

    uint8_t frame[RT_W * RT_H * 4U] = {0};
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame)));

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    assert_pixel(frame, 16, 32, 255, 0, 0); /* left half still red */
    assert_pixel(frame, 48, 32, 0, 255, 0); /* post-wrap green rect renders */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_multi_flush_ring_offsets_render_correctly);
    RUN_TEST(test_ring_wrap_still_renders);
    return UNITY_END();
}
