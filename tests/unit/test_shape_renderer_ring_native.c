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
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);

    /* nt_gfx_shutdown releases the attachment textures. */
    s_target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .color = nt_gfx_make_texture(&(nt_texture_desc_t){.width = RT_W, .height = RT_H, .format = NT_TEXTURE_FORMAT_RGBA8}),
        .depth = nt_gfx_make_texture(&(nt_texture_desc_t){.width = RT_W, .height = RT_H, .format = NT_TEXTURE_FORMAT_DEPTH24}),
        .label = "shape_ring_rt",
    });
    TEST_ASSERT_TRUE(nt_gfx_render_target_valid(s_target));

    nt_shape_renderer_init();
    nt_shape_renderer_set_vp(k_identity_vp);
    nt_shape_renderer_set_depth(false);
}

void tearDown(void) {
    nt_shape_renderer_shutdown();
    nt_gfx_destroy_render_target(s_target);
    nt_gfx_shutdown();
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
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);

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
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);

    assert_pixel(frame, 16, 32, 255, 0, 0); /* left half still red */
    assert_pixel(frame, 48, 32, 0, 255, 0); /* post-wrap green rect renders */
}

static void test_wire_circle_has_closed_outer_joins(void) {
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 5});
    nt_shape_renderer_set_line_width(0.3F);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_shape_renderer_circle_wire_rot((float[3]){0, 0, 0}, 0.5F, (float[4]){0.70710678F, 0, 0, 0.70710678F}, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    assert_pixel(frame, 32, 12, 255, 255, 255);
    assert_pixel(frame, 51, 32, 255, 255, 255);
    assert_pixel(frame, 32, 32, 0, 0, 0);
}

static void test_overlay_wires_keep_submission_order(void) {
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 5});
    nt_shape_renderer_set_line_width(0.3F);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_shape_renderer_rect_wire((float[3]){0, 0, 0}, (float[2]){1, 1}, (float[4]){1, 0, 0, 1});
    nt_shape_renderer_line((float[3]){-0.5F, 0.5F, 0}, (float[3]){0.5F, 0.5F, 0}, (float[4]){0, 1, 0, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    assert_pixel(frame, 32, 16, 0, 255, 0);
}

static uint32_t lit_column(const uint8_t *frame, int x, int begin, int end) {
    uint32_t count = 0;
    for (int y = begin; y < end; y++) {
        count += pixel_at(frame, x, y)[0] > 128;
    }
    return count;
}

static void test_pixel_width_is_constant_across_depth_and_restore(void) {
    const float perspective[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1.22222222F, -1, 0, 0, -2.22222222F, 0};
    const float white[4] = {1, 1, 1, 1};
    nt_shape_renderer_set_vp(perspective);
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 0});
    nt_shape_renderer_set_line_width_pixels(6, RT_W, RT_H);
    nt_shape_renderer_restore_gpu();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_line((float[3]){-0.75F, -0.5F, -2}, (float[3]){0.75F, -0.5F, -2}, white);
    nt_shape_renderer_line((float[3]){-1.5F, 1, -4}, (float[3]){1.5F, 1, -4}, white);
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    TEST_ASSERT_EQUAL_UINT32(6, lit_column(frame, 32, 0, 32));
    TEST_ASSERT_EQUAL_UINT32(6, lit_column(frame, 32, 32, 64));
}

static void test_pixel_join_bevel_is_bounded(void) {
    const float points[][3] = {{-0.6F, -0.4F, 0}, {0, 0.4F, 0}, {-0.5F, -0.4F, 0}};
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 5});
    nt_shape_renderer_set_line_width_pixels(8, RT_W, RT_H);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_polyline(points, 3, false, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    assert_pixel(frame, 31, 19, 255, 255, 255);
    assert_pixel(frame, 32, 5, 0, 0, 0);
}

static void test_pixel_line_clips_at_near_plane(void) {
    const float perspective[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1.22222222F, -1, 0, 0, -2.22222222F, 0};
    nt_shape_renderer_set_vp(perspective);
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 0});
    nt_shape_renderer_set_line_width_pixels(6, RT_W, RT_H);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_line((float[3]){-0.5F, 0, 0.2F}, (float[3]){0.75F, 0, -3}, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_line((float[3]){-1, 0.5F, 0.2F}, (float[3]){1, 0.5F, -0.5F}, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    TEST_ASSERT_EQUAL_UINT32(6, lit_column(frame, 35, 0, 64));
    assert_pixel(frame, 16, 16, 0, 0, 0);
    assert_pixel(frame, 48, 48, 0, 0, 0);
}

static void test_polyline_outer_corner_and_butt_end(void) {
    const float points[][3] = {{-0.5F, -0.5F, 0}, {0, -0.5F, 0}, {0, 0.5F, 0}};
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 5});
    nt_shape_renderer_set_line_width(0.3F);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    const float offscreen[][3] = {{-5, -5, 0}, {-4, -5, 0}};
    for (uint32_t i = 1; i < NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS; i++) {
        nt_shape_renderer_polyline(offscreen, 2, false, (float[4]){1, 1, 1, 1});
    }
    nt_shape_renderer_polyline(points, 3, false, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    assert_pixel(frame, 35, 51, 255, 255, 255);
    assert_pixel(frame, 32, 14, 0, 0, 0);
}

static void test_pixel_width_uses_active_viewport_height(void) {
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 5});
    nt_shape_renderer_set_line_width_pixels(6, RT_W, RT_H / 2);
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_gfx_set_viewport(0, 0, RT_W, RT_H / 2);
    nt_shape_renderer_line((float[3]){-0.5F, 0, 0}, (float[3]){0.5F, 0, 0}, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    TEST_ASSERT_EQUAL_UINT32(6, lit_column(frame, 32, 0, RT_H));
    assert_pixel(frame, 32, 48, 255, 255, 255);
}

int main(void) {
    /* One hidden window and GL context serve every test; setUp/tearDown reset only engine state. */
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){
        .max_dpr = 1.0F,
        .resizable = false,
        .width = RT_W,
        .height = RT_H,
    };
    nt_window_init();
    UNITY_BEGIN();
    RUN_TEST(test_polyline_outer_corner_and_butt_end);
    RUN_TEST(test_pixel_width_uses_active_viewport_height);
    RUN_TEST(test_pixel_width_is_constant_across_depth_and_restore);
    RUN_TEST(test_pixel_join_bevel_is_bounded);
    RUN_TEST(test_pixel_line_clips_at_near_plane);
    RUN_TEST(test_wire_circle_has_closed_outer_joins);
    RUN_TEST(test_overlay_wires_keep_submission_order);
    RUN_TEST(test_multi_flush_ring_offsets_render_correctly);
    RUN_TEST(test_ring_wrap_still_renders);
    int failures = UNITY_END();
    nt_window_shutdown();
    return failures;
}
