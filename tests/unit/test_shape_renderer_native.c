/* Real-GL proof that shapes drawn from frame storage and the shared templates land where the
 * draws read: several flushes of one frame render pixel-correct.
 * Fixed-size offscreen target — the window framebuffer scales with host DPI. */

#include "color/nt_color.h"
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
    desc.frame_capacity[NT_GFX_FRAME_VERTEX] = 512U * 1024U;
    nt_gfx_init(&desc);
    nt_gfx_begin_frame();
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);

    /* nt_gfx_shutdown releases the attachment textures. */
    s_target = nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .color = nt_gfx_make_texture(&(nt_texture_desc_t){.width = RT_W, .height = RT_H, .format = NT_TEXTURE_FORMAT_RGBA8}),
        .depth = nt_gfx_make_texture(&(nt_texture_desc_t){.width = RT_W, .height = RT_H, .format = NT_TEXTURE_FORMAT_DEPTH24}),
        .label = "shape_rt",
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

/* Every flush of the frame appends to frame storage; each draw reads its own range of it and of the
 * shared template buffers, including the capsule's hemisphere offset in the shared fill program. */
static void test_several_flushes_of_one_frame_render_in_place(void) {
    const uint32_t red = NT_RGBA8(255, 0, 0, 255);
    const uint32_t green = NT_RGBA8(0, 255, 0, 255);
    const uint32_t blue = NT_RGBA8(0, 0, 255, 255);
    const uint32_t yellow = NT_RGBA8(255, 255, 0, 255);
    const uint32_t magenta = NT_RGBA8(255, 0, 255, 255);

    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});

    /* Flush 1: red rect over the left half; green cube center-right (the circle
     * template lies in XZ — edge-on under the identity VP — so cube it is). */
    nt_shape_renderer_rect((float[3]){-0.5F, 0.0F, 0.0F}, (float[2]){1.0F, 2.0F}, NULL, red);
    nt_shape_renderer_cube((float[3]){0.5F, 0.0F, 0.0F}, (float[3]){0.5F, 0.5F, 0.5F}, NULL, green);
    nt_shape_renderer_flush();

    /* Flush 2: blue rect in the top-right quadrant (clip x [0.2,0.8], y [0.4,0.8]). */
    nt_shape_renderer_rect((float[3]){0.5F, 0.6F, 0.0F}, (float[2]){0.6F, 0.4F}, NULL, blue);
    nt_shape_renderer_flush();

    /* Flush 3: a batch triangle low on the right and a vertical capsule at the right edge
     * (radius 0.1, half body 0.2: clip y [-0.3, 0.3]). */
    nt_shape_renderer_triangle((float[3]){0.2F, -0.9F, 0}, (float[3]){0.5F, -0.9F, 0}, (float[3]){0.35F, -0.5F, 0}, yellow);
    nt_shape_renderer_capsule((float[3]){0.85F, 0.0F, 0.0F}, 0.1F, 0.6F, NULL, magenta);
    nt_shape_renderer_flush();

    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);

    assert_pixel(frame, 16, 32, 255, 0, 0);   /* left half: red rect */
    assert_pixel(frame, 48, 32, 0, 255, 0);   /* cube center: green */
    assert_pixel(frame, 48, 13, 0, 0, 255);   /* top-right quadrant: blue rect (second flush) */
    assert_pixel(frame, 43, 56, 255, 255, 0); /* triangle centroid: batch from frame storage */
    assert_pixel(frame, 59, 32, 255, 0, 255); /* capsule body */
    assert_pixel(frame, 59, 24, 255, 0, 255); /* capsule top hemisphere, moved up by the half body */
    assert_pixel(frame, 59, 20, 0, 0, 0);     /* above the capsule: background */
    assert_pixel(frame, 60, 60, 0, 0, 0);     /* bottom-right corner: background */
}

static void test_wire_circle_has_closed_outer_joins(void) {
    nt_shape_renderer_set_line_width(0.3F);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_shape_renderer_circle_wire((float[3]){0, 0, 0}, 0.5F, (float[4]){0.70710678F, 0, 0, 0.70710678F}, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    assert_pixel(frame, 32, 12, 255, 255, 255);
    assert_pixel(frame, 51, 32, 255, 255, 255);
    assert_pixel(frame, 32, 32, 0, 0, 0);
}

/* The sphere wire sits after the circle in the shared wire buffer: its draw starts at a nonzero
 * index offset. Its XY ring faces the identity camera. */
static void test_wire_template_at_an_index_offset_renders(void) {
    nt_shape_renderer_set_line_width(0.2F);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_shape_renderer_sphere_wire((float[3]){0, 0, 0}, 0.5F, NULL, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    /* Diagonals: only the XY ring passes there; the edge-on XZ and YZ rings are bands along the axes. */
    assert_pixel(frame, 43, 20, 255, 255, 255); /* the XY ring at 45 degrees */
    assert_pixel(frame, 20, 43, 255, 255, 255); /* and at 225 degrees */
    assert_pixel(frame, 38, 26, 0, 0, 0);       /* inside the ring, off the axis bands */
}

/* Within one flush the line stays above a later fill; the next flush draws over both. */
static void test_overlay_strokes_draw_over_fills_until_flush(void) {
    nt_shape_renderer_set_line_width(0.3F);
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_shape_renderer_line((float[3]){-0.5F, 0, 0}, (float[3]){0.5F, 0, 0}, NT_RGBA8(0, 255, 0, 255));
    nt_shape_renderer_rect((float[3]){0, 0, 0}, (float[2]){1, 1}, NULL, NT_RGBA8(255, 0, 0, 255));
    nt_shape_renderer_flush();
    nt_shape_renderer_rect((float[3]){-0.25F, 0, 0}, (float[2]){0.5F, 0.5F}, NULL, NT_RGBA8(0, 0, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    assert_pixel(frame, 40, 32, 0, 255, 0);
    assert_pixel(frame, 40, 20, 255, 0, 0);
    assert_pixel(frame, 24, 32, 0, 0, 255);
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
    const uint32_t white = NT_RGBA8(255, 255, 255, 255);
    nt_shape_renderer_set_vp(perspective);
    nt_shape_renderer_set_line_width_pixels(6, RT_W, RT_H);
    nt_shape_renderer_restore_gpu();
    nt_gfx_end_frame();
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
    nt_shape_renderer_set_line_width_pixels(8, RT_W, RT_H);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_polyline(points, 3, false, NT_RGBA8(255, 255, 255, 255));
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
    nt_shape_renderer_set_line_width_pixels(6, RT_W, RT_H);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_line((float[3]){-0.5F, 0, 0.2F}, (float[3]){0.75F, 0, -3}, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_line((float[3]){-1, 0.5F, 0.2F}, (float[3]){1, 0.5F, -0.5F}, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    TEST_ASSERT_EQUAL_UINT32(6, lit_column(frame, 35, 0, 64));
    assert_pixel(frame, 16, 16, 0, 0, 0);
    assert_pixel(frame, 48, 48, 0, 0, 0);
}

/* Far off-axis in a wide orthographic view, a world-width stroke still keeps its full width. */
static void test_ortho_world_width_off_axis(void) {
    const float ortho[16] = {0.01F, 0, 0, 0, 0, 0.01F, 0, 0, 0, 0, -0.02F, 0, 0, 0, -0.8F, 1};
    nt_shape_renderer_set_vp(ortho);
    nt_shape_renderer_set_line_width(25);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_line((float[3]){-90, 80, 0}, (float[3]){90, 80, 0}, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    TEST_ASSERT_EQUAL_UINT32(8, lit_column(frame, 32, 0, 32));
}

/* The join at the visible vertex must use the near-clipped neighbor, not its w<0 projection. */
static void test_pixel_join_clips_hidden_neighbor(void) {
    const float perspective[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1.22222222F, -1, 0, 0, -2.22222222F, 0};
    const float points[][3] = {{-0.05F, 0, 1}, {0.5F, 0, -2}, {0.5F, 1, -2}};
    nt_shape_renderer_set_vp(perspective);
    nt_shape_renderer_set_line_width_pixels(6, RT_W, RT_H);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_polyline(points, 3, false, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    assert_pixel(frame, 37, 31, 255, 255, 255);
    assert_pixel(frame, 37, 32, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT32(0, lit_column(frame, 10, 0, 64));
}

static void test_polyline_outer_corner_and_butt_end(void) {
    const float points[][3] = {{-0.5F, -0.5F, 0}, {0, -0.5F, 0}, {0, 0.5F, 0}};
    nt_shape_renderer_set_line_width(0.3F);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    const float offscreen[][3] = {{-5, -5, 0}, {-4, -5, 0}};
    for (uint32_t i = 1; i < NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS; i++) {
        nt_shape_renderer_polyline(offscreen, 2, false, NT_RGBA8(255, 255, 255, 255));
    }
    nt_shape_renderer_polyline(points, 3, false, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, sizeof(frame));
    nt_gfx_end_pass();
    TEST_ASSERT_TRUE(read_ok);
    assert_pixel(frame, 35, 51, 255, 255, 255);
    assert_pixel(frame, 32, 14, 0, 0, 0);
}

static void test_pixel_width_uses_active_viewport_height(void) {
    nt_shape_renderer_set_line_width_pixels(6, RT_W, RT_H / 2);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_gfx_set_viewport(0, 0, RT_W, RT_H / 2);
    nt_shape_renderer_line((float[3]){-0.5F, 0, 0}, (float[3]){0.5F, 0, 0}, NT_RGBA8(255, 255, 255, 255));
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
    RUN_TEST(test_ortho_world_width_off_axis);
    RUN_TEST(test_pixel_join_clips_hidden_neighbor);
    RUN_TEST(test_polyline_outer_corner_and_butt_end);
    RUN_TEST(test_pixel_width_uses_active_viewport_height);
    RUN_TEST(test_pixel_width_is_constant_across_depth_and_restore);
    RUN_TEST(test_pixel_join_bevel_is_bounded);
    RUN_TEST(test_pixel_line_clips_at_near_plane);
    RUN_TEST(test_wire_circle_has_closed_outer_joins);
    RUN_TEST(test_overlay_strokes_draw_over_fills_until_flush);
    RUN_TEST(test_several_flushes_of_one_frame_render_in_place);
    RUN_TEST(test_wire_template_at_an_index_offset_renders);
    int failures = UNITY_END();
    nt_window_shutdown();
    return failures;
}
