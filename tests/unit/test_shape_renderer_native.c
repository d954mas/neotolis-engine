/* Real-GL proof that shapes drawn from frame storage and the shared templates land where the
 * draws read: several flushes of one frame render pixel-correct.
 * Fixed-size offscreen target — the window framebuffer scales with host DPI. */

#include "color/nt_color.h"
#include "graphics/nt_gfx.h"
#include "renderers/nt_shape_renderer.h"
#include "unity.h"
#include "window/nt_window.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

enum { RT_W = 64, RT_H = 64 };

static const float k_identity_vp[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static nt_render_target_t s_target;

/* The renderer's programs finish linking at begin_frames, several with parallel compile, and a flush
 * draws nothing until all of them are ready: a probe shape that draws marks them linked. */
static void wait_shape_links(void) {
    const time_t start = time(NULL);
    for (;;) {
        nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_depth = 1.0F});
        nt_shape_renderer_triangle((const float[3]){0, 0, 0}, (const float[3]){1, 0, 0}, (const float[3]){0, 1, 0}, NT_RGBA8(255, 255, 255, 255));
        nt_shape_renderer_flush();
        const bool drawn = nt_gfx_draw_calls(&g_nt_gfx.counters) != 0;
        nt_gfx_end_pass();
        nt_gfx_end_frame();
        nt_gfx_begin_frame();
        if (drawn) {
            return;
        }
        TEST_ASSERT_TRUE_MESSAGE(time(NULL) - start < 10, "shape renderer programs did not link");
    }
}

void setUp(void) {
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.frame_capacity[NT_GFX_FRAME_VERTEX] = 512U * 1024U;
    desc.frame_capacity[NT_GFX_FRAME_UNIFORM] = 16U * 1024U; /* a Draw block per settings change and pass */
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
    wait_shape_links();
}

void tearDown(void) {
    nt_shape_renderer_shutdown();
    nt_gfx_destroy_render_target(s_target);
    nt_gfx_shutdown();
}

/* Sample one pixel from a top-left-oriented full-frame readback. */
static const uint8_t *pixel_at(const uint8_t *frame, int x, int y_top) { return &frame[(((size_t)y_top * RT_W) + (size_t)x) * 4U]; }

/* Ends the pass and the frame, then reads the whole target top-left first. */
static void end_frame_and_read(uint8_t *frame) {
    nt_gfx_end_pass();
    nt_gfx_end_frame();
    TEST_ASSERT_TRUE(nt_gfx_read_pixels(s_target, 0, 0, RT_W, RT_H, frame, RT_W * RT_H * 4U));
}

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
    end_frame_and_read(frame);

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
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_shape_renderer_circle_wire((float[3]){0, 0, 0}, 0.5F, (float[4]){0.70710678F, 0, 0, 0.70710678F}, 0.3F, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
    assert_pixel(frame, 32, 12, 255, 255, 255);
    assert_pixel(frame, 51, 32, 255, 255, 255);
    assert_pixel(frame, 32, 32, 0, 0, 0);
}

/* The sphere wire sits after the circle in the shared wire buffer: its draw starts at a nonzero
 * index offset. Its XY ring faces the identity camera. */
static void test_wire_template_at_an_index_offset_renders(void) {
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_shape_renderer_sphere_wire((float[3]){0, 0, 0}, 0.5F, NULL, 0.2F, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
    /* Diagonals: only the XY ring passes there; the edge-on XZ and YZ rings are bands along the axes. */
    assert_pixel(frame, 43, 20, 255, 255, 255); /* the XY ring at 45 degrees */
    assert_pixel(frame, 20, 43, 255, 255, 255); /* and at 225 degrees */
    assert_pixel(frame, 38, 26, 0, 0, 0);       /* inside the ring, off the axis bands */
}

/* Within one flush the line stays above a later fill; the next flush draws over both. */
static void test_overlay_strokes_draw_over_fills_until_flush(void) {
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F});
    nt_shape_renderer_line((float[3]){-0.5F, 0, 0}, (float[3]){0.5F, 0, 0}, 0.3F, NT_RGBA8(0, 255, 0, 255));
    nt_shape_renderer_rect((float[3]){0, 0, 0}, (float[2]){1, 1}, NULL, NT_RGBA8(255, 0, 0, 255));
    nt_shape_renderer_flush();
    nt_shape_renderer_rect((float[3]){-0.25F, 0, 0}, (float[2]){0.5F, 0.5F}, NULL, NT_RGBA8(0, 0, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
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
    nt_shape_renderer_set_width_units_pixels(RT_W, RT_H);
    nt_shape_renderer_restore_gpu();
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_line((float[3]){-0.75F, -0.5F, -2}, (float[3]){0.75F, -0.5F, -2}, 6, white);
    nt_shape_renderer_line((float[3]){-1.5F, 1, -4}, (float[3]){1.5F, 1, -4}, 6, white);
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
    TEST_ASSERT_EQUAL_UINT32(6, lit_column(frame, 32, 0, 32));
    TEST_ASSERT_EQUAL_UINT32(6, lit_column(frame, 32, 32, 64));
}

/* Each stroke kind reads its width from its own instance layout: a wrong attribute would read w = 1. */
static void test_pixel_widths_share_a_flush_in_every_stroke_kind(void) {
    const float quarter_turn_x[4] = {0.70710678F, 0, 0, 0.70710678F};
    const float widths[2] = {6, 10};
    nt_shape_renderer_set_width_units_pixels(RT_W, RT_H);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    for (int i = 0; i < 2; i++) {
        const float y = i == 0 ? 0.75F : -0.25F;
        const float path[3][3] = {{-0.3F, y, 0}, {-0.1F, y, 0}, {0.1F, y, 0}};
        nt_shape_renderer_line((float[3]){-0.9F, y, 0}, (float[3]){-0.5F, y, 0}, widths[i], NT_RGBA8(255, 255, 255, 255));
        nt_shape_renderer_polyline(path, 3, false, widths[i], NT_RGBA8(255, 255, 255, 255));
        nt_shape_renderer_circle_wire((float[3]){0.6F, y - 0.3F, 0}, 0.3F, quarter_turn_x, widths[i], NT_RGBA8(255, 255, 255, 255));
    }
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
    for (int i = 0; i < 2; i++) {
        const int top = i * 32;
        const uint32_t expected = (uint32_t)widths[i];
        TEST_ASSERT_EQUAL_UINT32(expected, lit_column(frame, 10, top, top + 32));
        TEST_ASSERT_EQUAL_UINT32(expected, lit_column(frame, 26, top, top + 32));
        /* Column 49 crosses the ring's top segment mid-way, tilted 11.25 degrees: one pixel of slack. */
        TEST_ASSERT_UINT32_WITHIN(1, expected, lit_column(frame, 49, top, top + 18));
    }
}

/* Interleaved types draw grouped by type, each instance with its own data: rect and
 * cube wires take a non-uniform scale, the cube its depth, the capsule its half body apart from the width,
 * the cylinder its height. */
static void test_interleaved_types_keep_their_instances(void) {
    /* A quarter turn about Y narrows a rect to 0.14 and widens a cube to 0.28: the templates differ on screen. */
    const float turn_y[4] = {0, 0.38268343F, 0, 0.92387953F};
    nt_shape_renderer_set_width_units_pixels(RT_W, RT_H);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_cube((float[3]){-0.75F, -0.75F, 0}, (float[3]){0.2F, 0.2F, 0.2F}, turn_y, NT_RGBA8(255, 255, 0, 255));
    nt_shape_renderer_rect_wire((float[3]){-0.5F, 0.5F, 0}, (float[2]){0.5F, 0.25F}, NULL, 4, NT_RGBA8(255, 0, 0, 255));
    nt_shape_renderer_capsule_wire((float[3]){0, -0.5F, 0}, 0.25F, 0.75F, NULL, 4, NT_RGBA8(0, 0, 255, 255));
    nt_shape_renderer_rect((float[3]){0.75F, -0.75F, 0}, (float[2]){0.2F, 0.2F}, turn_y, NT_RGBA8(255, 0, 255, 255));
    nt_shape_renderer_cube((float[3]){-0.75F, -0.25F, 0}, (float[3]){0.2F, 0.2F, 0.2F}, turn_y, NT_RGBA8(0, 255, 255, 255));
    nt_shape_renderer_cylinder_wire((float[3]){0.5F, -0.25F, 0}, 0.125F, 0.25F, NULL, 2, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_cube_wire((float[3]){0.5F, 0.5F, 0}, (float[3]){0.5F, 0.25F, 0.5F}, turn_y, 4, NT_RGBA8(0, 255, 0, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
    /* Rect and cube outlines: horizontal edges at rows 12 and 20, 4 px each. The turned cube spans
     * x 0.15..0.85; without its depth it would span 0.32..0.68 and miss column 39. */
    TEST_ASSERT_EQUAL_UINT32(8, lit_column(frame, 16, 0, 32));
    assert_pixel(frame, 16, 11, 255, 0, 0);
    assert_pixel(frame, 16, 16, 0, 0, 0);
    assert_pixel(frame, 52, 11, 0, 255, 0);
    assert_pixel(frame, 52, 19, 0, 255, 0);
    assert_pixel(frame, 52, 16, 0, 0, 0);
    assert_pixel(frame, 39, 11, 0, 255, 0);
    /* Cylinder rings at its height's ends, rows 36 and 44, 2 px each. */
    assert_pixel(frame, 46, 35, 255, 255, 255);
    assert_pixel(frame, 46, 40, 0, 0, 0);
    assert_pixel(frame, 46, 43, 255, 255, 255);
    /* Capsule equator rings, moved by the half body 0.125 to rows 44 and 52; meridian tip at radius + half body, row 36. */
    assert_pixel(frame, 32, 35, 0, 0, 255);
    assert_pixel(frame, 34, 43, 0, 0, 255);
    assert_pixel(frame, 34, 48, 0, 0, 0);
    assert_pixel(frame, 34, 51, 0, 0, 255);
    /* Column 4 lies inside the turned cubes and outside a turned rect at their place. */
    assert_pixel(frame, 4, 56, 255, 255, 0);
    assert_pixel(frame, 4, 40, 0, 255, 255);
    assert_pixel(frame, 56, 56, 255, 0, 255);
    assert_pixel(frame, 52, 56, 0, 0, 0);
}

static void test_pixel_join_bevel_is_bounded(void) {
    const float points[][3] = {{-0.6F, -0.4F, 0}, {0, 0.4F, 0}, {-0.5F, -0.4F, 0}};
    nt_shape_renderer_set_width_units_pixels(RT_W, RT_H);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_polyline(points, 3, false, 8, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
    assert_pixel(frame, 31, 19, 255, 255, 255);
    assert_pixel(frame, 32, 5, 0, 0, 0);
}

static void test_pixel_line_clips_at_near_plane(void) {
    const float perspective[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1.22222222F, -1, 0, 0, -2.22222222F, 0};
    nt_shape_renderer_set_vp(perspective);
    nt_shape_renderer_set_width_units_pixels(RT_W, RT_H);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_line((float[3]){-0.5F, 0, 0.2F}, (float[3]){0.75F, 0, -3}, 6, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_line((float[3]){-1, 0.5F, 0.2F}, (float[3]){1, 0.5F, -0.5F}, 6, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
    TEST_ASSERT_EQUAL_UINT32(6, lit_column(frame, 35, 0, 64));
    assert_pixel(frame, 16, 16, 0, 0, 0);
    assert_pixel(frame, 48, 48, 0, 0, 0);
}

/* Far off-axis in a wide orthographic view, a world-width stroke still keeps its full width. */
static void test_ortho_world_width_off_axis(void) {
    const float ortho[16] = {0.01F, 0, 0, 0, 0, 0.01F, 0, 0, 0, 0, -0.02F, 0, 0, 0, -0.8F, 1};
    nt_shape_renderer_set_vp(ortho);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_line((float[3]){-90, 80, 0}, (float[3]){90, 80, 0}, 25, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
    TEST_ASSERT_EQUAL_UINT32(8, lit_column(frame, 32, 0, 32));
}

/* The join at the visible vertex must use the near-clipped neighbor, not its w<0 projection. */
static void test_pixel_join_clips_hidden_neighbor(void) {
    const float perspective[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1.22222222F, -1, 0, 0, -2.22222222F, 0};
    const float points[][3] = {{-0.05F, 0, 1}, {0.5F, 0, -2}, {0.5F, 1, -2}};
    nt_shape_renderer_set_vp(perspective);
    nt_shape_renderer_set_width_units_pixels(RT_W, RT_H);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_shape_renderer_polyline(points, 3, false, 6, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
    assert_pixel(frame, 37, 31, 255, 255, 255);
    assert_pixel(frame, 37, 32, 255, 255, 255);
    TEST_ASSERT_EQUAL_UINT32(0, lit_column(frame, 10, 0, 64));
}

static void test_polyline_outer_corner_and_butt_end(void) {
    const float points[][3] = {{-0.5F, -0.5F, 0}, {0, -0.5F, 0}, {0, 0.5F, 0}};
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    const float offscreen[][3] = {{-5, -5, 0}, {-4, -5, 0}};
    for (uint32_t i = 1; i < NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS; i++) {
        nt_shape_renderer_polyline(offscreen, 2, false, 0.3F, NT_RGBA8(255, 255, 255, 255));
    }
    nt_shape_renderer_polyline(points, 3, false, 0.3F, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
    assert_pixel(frame, 35, 51, 255, 255, 255);
    assert_pixel(frame, 32, 14, 0, 0, 0);
}

static void test_pixel_width_uses_active_viewport_height(void) {
    nt_shape_renderer_set_width_units_pixels(RT_W, RT_H / 2);
    nt_gfx_end_frame();
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.target = s_target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1});
    nt_gfx_set_viewport(0, 0, RT_W, RT_H / 2);
    nt_shape_renderer_line((float[3]){-0.5F, 0, 0}, (float[3]){0.5F, 0, 0}, 6, NT_RGBA8(255, 255, 255, 255));
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    end_frame_and_read(frame);
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
    RUN_TEST(test_pixel_widths_share_a_flush_in_every_stroke_kind);
    RUN_TEST(test_interleaved_types_keep_their_instances);
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
