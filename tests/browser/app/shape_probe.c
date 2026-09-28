#include "graphics/nt_gfx.h"
#include "renderers/nt_shape_renderer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define NT_TEST_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define NT_TEST_KEEPALIVE
#endif

enum { RT_W = 64, RT_H = 64 };

uint32_t nt_test_shape_stroke_probe(void);

static const float s_identity_vp[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static const uint8_t *pixel_at(const uint8_t *frame, int x, int y_top) { return &frame[(((size_t)y_top * RT_W) + (size_t)x) * 4U]; }

static bool pixel_is(const uint8_t *frame, int x, int y_top, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t *pixel = pixel_at(frame, x, y_top);
    int dr = (int)pixel[0] - (int)r;
    int dg = (int)pixel[1] - (int)g;
    int db = (int)pixel[2] - (int)b;
    return dr >= -1 && dr <= 1 && dg >= -1 && dg <= 1 && db >= -1 && db <= 1;
}

static uint32_t lit_column(const uint8_t *frame, int x, int begin, int end) {
    uint32_t count = 0;
    for (int y = begin; y < end; y++) {
        count += pixel_at(frame, x, y)[0] > 128;
    }
    return count;
}

static void begin_probe_pass(nt_render_target_t target) { nt_gfx_begin_pass(&(nt_pass_desc_t){.target = target, .clear_color = {0, 0, 0, 1}, .clear_depth = 1.0F}); }

static bool read_probe_pass(uint8_t frame[RT_W * RT_H * 4U]) {
    bool read_ok = nt_gfx_read_pixels(0, 0, RT_W, RT_H, frame, RT_W * RT_H * 4U);
    nt_gfx_end_pass();
    return read_ok;
}

static bool probe_multi_flush_ring(nt_render_target_t target) {
    const float red[4] = {1, 0, 0, 1};
    const float green[4] = {0, 1, 0, 1};
    const float blue[4] = {0, 0, 1, 1};
    nt_shape_renderer_set_vp(s_identity_vp);
    nt_shape_renderer_set_depth(false);
    begin_probe_pass(target);
    nt_shape_renderer_rect((float[3]){-0.5F, 0, 0}, (float[2]){1, 2}, red);
    nt_shape_renderer_cube((float[3]){0.5F, 0, 0}, (float[3]){0.5F, 0.5F, 0.5F}, green);
    nt_shape_renderer_flush();
    nt_shape_renderer_rect((float[3]){0.5F, 0.6F, 0}, (float[2]){0.6F, 0.4F}, blue);
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    return read_probe_pass(frame) && pixel_is(frame, 16, 32, 255, 0, 0) && pixel_is(frame, 48, 32, 0, 255, 0) && pixel_is(frame, 48, 13, 0, 0, 255) && pixel_is(frame, 56, 56, 0, 0, 0);
}

static bool probe_closed_circle(nt_render_target_t target) {
    nt_shape_renderer_set_vp(s_identity_vp);
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 5});
    nt_shape_renderer_set_line_width(0.3F);
    begin_probe_pass(target);
    nt_shape_renderer_circle_wire_rot((float[3]){0, 0, 0}, 0.5F, (float[4]){0.70710678F, 0, 0, 0.70710678F}, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    return read_probe_pass(frame) && pixel_is(frame, 32, 12, 255, 255, 255) && pixel_is(frame, 51, 32, 255, 255, 255) && pixel_is(frame, 32, 32, 0, 0, 0);
}

static bool probe_overlay_order(nt_render_target_t target) {
    nt_shape_renderer_set_vp(s_identity_vp);
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 5});
    nt_shape_renderer_set_line_width(0.3F);
    begin_probe_pass(target);
    nt_shape_renderer_rect_wire((float[3]){0, 0, 0}, (float[2]){1, 1}, (float[4]){1, 0, 0, 1});
    nt_shape_renderer_line((float[3]){-0.5F, 0.5F, 0}, (float[3]){0.5F, 0.5F, 0}, (float[4]){0, 1, 0, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    return read_probe_pass(frame) && pixel_is(frame, 32, 16, 0, 255, 0);
}

static bool probe_pixel_width_depth(nt_render_target_t target) {
    const float perspective[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1.22222222F, -1, 0, 0, -2.22222222F, 0};
    nt_shape_renderer_set_vp(perspective);
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 0});
    nt_shape_renderer_set_line_width_pixels(6, RT_W, RT_H);
    begin_probe_pass(target);
    nt_shape_renderer_line((float[3]){-0.75F, -0.5F, -2}, (float[3]){0.75F, -0.5F, -2}, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_line((float[3]){-1.5F, 1, -4}, (float[3]){1.5F, 1, -4}, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    return read_probe_pass(frame) && lit_column(frame, 32, 0, 32) == 6 && lit_column(frame, 32, 32, 64) == 6;
}

static bool probe_pixel_bevel(nt_render_target_t target) {
    const float points[][3] = {{-0.6F, -0.4F, 0}, {0, 0.4F, 0}, {-0.5F, -0.4F, 0}};
    nt_shape_renderer_set_vp(s_identity_vp);
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 5});
    nt_shape_renderer_set_line_width_pixels(8, RT_W, RT_H);
    begin_probe_pass(target);
    nt_shape_renderer_polyline(points, 3, false, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    return read_probe_pass(frame) && pixel_is(frame, 31, 19, 255, 255, 255) && pixel_is(frame, 32, 5, 0, 0, 0);
}

static bool probe_near_clip(nt_render_target_t target) {
    const float perspective[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1.22222222F, -1, 0, 0, -2.22222222F, 0};
    nt_shape_renderer_set_vp(perspective);
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 0});
    nt_shape_renderer_set_line_width_pixels(6, RT_W, RT_H);
    begin_probe_pass(target);
    nt_shape_renderer_line((float[3]){-0.5F, 0, 0.2F}, (float[3]){0.75F, 0, -3}, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_line((float[3]){-1, 0.5F, 0.2F}, (float[3]){1, 0.5F, -0.5F}, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    return read_probe_pass(frame) && lit_column(frame, 35, 0, 64) == 6 && pixel_is(frame, 16, 16, 0, 0, 0) && pixel_is(frame, 48, 48, 0, 0, 0);
}

static bool probe_outer_corner(nt_render_target_t target) {
    const float points[][3] = {{-0.5F, -0.5F, 0}, {0, -0.5F, 0}, {0, 0.5F, 0}};
    const float offscreen[][3] = {{-5, -5, 0}, {-4, -5, 0}};
    nt_shape_renderer_set_vp(s_identity_vp);
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 5});
    nt_shape_renderer_set_line_width(0.3F);
    begin_probe_pass(target);
    for (uint32_t i = 1; i < NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS; i++) {
        nt_shape_renderer_polyline(offscreen, 2, false, (float[4]){1, 1, 1, 1});
    }
    nt_shape_renderer_polyline(points, 3, false, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    return read_probe_pass(frame) && pixel_is(frame, 35, 51, 255, 255, 255) && pixel_is(frame, 32, 14, 0, 0, 0);
}

static bool probe_active_viewport(nt_render_target_t target) {
    nt_shape_renderer_set_vp(s_identity_vp);
    nt_shape_renderer_set_cam_pos((float[3]){0, 0, 5});
    nt_shape_renderer_set_line_width_pixels(6, RT_W, RT_H / 2);
    begin_probe_pass(target);
    nt_gfx_set_viewport(0, 0, RT_W, RT_H / 2);
    nt_shape_renderer_line((float[3]){-0.5F, 0, 0}, (float[3]){0.5F, 0, 0}, (float[4]){1, 1, 1, 1});
    nt_shape_renderer_flush();
    uint8_t frame[RT_W * RT_H * 4U] = {0};
    return read_probe_pass(frame) && lit_column(frame, 32, 0, RT_H) == 6 && pixel_is(frame, 32, 48, 255, 255, 255);
}

NT_TEST_KEEPALIVE uint32_t nt_test_shape_stroke_probe(void) {
    nt_gfx_begin_frame();
    nt_texture_t color = nt_gfx_make_texture(&(nt_texture_desc_t){.width = RT_W, .height = RT_H, .format = NT_TEXTURE_FORMAT_RGBA8});
    nt_texture_t depth = nt_gfx_make_texture(&(nt_texture_desc_t){.width = RT_W, .height = RT_H, .format = NT_TEXTURE_FORMAT_DEPTH24});
    nt_render_target_t target = nt_gfx_make_render_target(&(nt_render_target_desc_t){.color = color, .depth = depth, .label = "shape_stroke_probe"});
    if (!nt_gfx_render_target_valid(target)) {
        nt_gfx_destroy_texture(depth);
        nt_gfx_destroy_texture(color);
        return 0;
    }

    uint32_t mask = 0;
    mask |= probe_multi_flush_ring(target) ? 1U << 0U : 0;
    mask |= probe_closed_circle(target) ? 1U << 1U : 0;
    mask |= probe_overlay_order(target) ? 1U << 2U : 0;
    mask |= probe_pixel_width_depth(target) ? 1U << 3U : 0;
    mask |= probe_pixel_bevel(target) ? 1U << 4U : 0;
    mask |= probe_near_clip(target) ? 1U << 5U : 0;
    mask |= probe_outer_corner(target) ? 1U << 6U : 0;
    mask |= probe_active_viewport(target) ? 1U << 7U : 0;

    nt_gfx_destroy_render_target(target);
    nt_gfx_destroy_texture(depth);
    nt_gfx_destroy_texture(color);
    nt_gfx_begin_frame();
    return mask;
}
