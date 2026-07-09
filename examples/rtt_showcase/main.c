#include "app/nt_app.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "graphics/nt_gfx.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "math/nt_math.h"
#include "postfx/nt_postfx_blur.h"
#include "renderers/nt_shape_renderer.h"
#include "time/nt_time.h"
#include "window/nt_window.h"

#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

typedef struct {
    float position[2];
    float uv[2];
} rtt_quad_vertex_t;

_Static_assert(sizeof(rtt_quad_vertex_t) == 16, "rtt quad vertex size");

static const char *s_quad_vs_src = "precision mediump float;\n"
                                   "layout(location = 0) in vec2 a_position;\n"
                                   "layout(location = 3) in vec2 a_uv;\n"
                                   "out vec2 v_uv;\n"
                                   "void main() {\n"
                                   "    v_uv = a_uv;\n"
                                   "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
                                   "}\n";

static const char *s_quad_fs_src = "precision mediump float;\n"
                                   "uniform sampler2D u_texture;\n"
                                   "uniform vec4 u_tint;\n"
                                   "uniform int u_mode;\n"
                                   "uniform float u_zoom;\n"
                                   "in vec2 v_uv;\n"
                                   "out vec4 frag_color;\n"
                                   "void main() {\n"
                                   "    vec2 sample_uv = ((v_uv - vec2(0.5)) / max(u_zoom, 0.001)) + vec2(0.5);\n"
                                   "    vec4 sample_color = texture(u_texture, sample_uv);\n"
                                   "    if (u_mode == 1) {\n"
                                   "        float depth = 1.0 - sample_color.r;\n"
                                   "        frag_color = vec4(depth, depth, depth, 1.0);\n"
                                   "        return;\n"
                                   "    }\n"
                                   "    frag_color = sample_color * u_tint;\n"
                                   "}\n";

static const uint8_t s_white_pixel[4] = {255, 255, 255, 255};

static struct {
    nt_render_target_t scene;
    nt_render_target_t temp;
    nt_render_target_t blur;
    nt_texture_t scene_color;
    nt_texture_t scene_depth;
    nt_texture_t blur_color;
    nt_texture_t white;
    nt_shader_t quad_vs;
    nt_shader_t quad_fs;
    nt_pipeline_t quad_pipeline;
    nt_buffer_t quad_vbo;
    uint16_t rt_width;
    uint16_t rt_height;
    bool large_target;
    bool handles_stable;
    float sample_zoom;
    float blur_radius;
    int active_slider;
} s_demo;

enum {
    RTT_SLIDER_NONE = 0,
    RTT_SLIDER_ZOOM = 1,
    RTT_SLIDER_BLUR = 2,
};

static float clampf(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static bool mouse_ndc(float *out_x, float *out_y) {
    for (uint32_t i = 0; i < NT_INPUT_MAX_POINTERS; ++i) {
        const nt_pointer_t *ptr = &g_nt_input.pointers[i];
        if (ptr->active && ptr->type == NT_POINTER_MOUSE && g_nt_window.fb_width > 0U && g_nt_window.fb_height > 0U) {
            *out_x = ((ptr->x / (float)g_nt_window.fb_width) * 2.0F) - 1.0F;
            *out_y = 1.0F - ((ptr->y / (float)g_nt_window.fb_height) * 2.0F);
            return true;
        }
    }
    return false;
}

static bool point_in_rect(float x, float y, float x0, float y0, float x1, float y1) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }

static float slider_t(float x, float x0, float x1) { return clampf((x - x0) / (x1 - x0), 0.0F, 1.0F); }

static void update_sliders(void) {
    float mx = 0.0F;
    float my = 0.0F;
    bool has_mouse = mouse_ndc(&mx, &my);
    bool pressed = nt_input_mouse_is_pressed(NT_BUTTON_LEFT);
    bool down = nt_input_mouse_is_down(NT_BUTTON_LEFT);
    bool released = nt_input_mouse_is_released(NT_BUTTON_LEFT);

    if (released) {
        s_demo.active_slider = RTT_SLIDER_NONE;
    }
    if (!has_mouse) {
        return;
    }
    if (pressed) {
        if (point_in_rect(mx, my, -0.92F, 0.91F, -0.08F, 0.98F)) {
            s_demo.active_slider = RTT_SLIDER_ZOOM;
        } else if (point_in_rect(mx, my, 0.08F, 0.91F, 0.92F, 0.98F)) {
            s_demo.active_slider = RTT_SLIDER_BLUR;
        }
    }
    if (!down) {
        return;
    }
    if (s_demo.active_slider == RTT_SLIDER_ZOOM) {
        float t = slider_t(mx, -0.92F, -0.08F);
        s_demo.sample_zoom = 1.0F + (t * 1.5F);
    } else if (s_demo.active_slider == RTT_SLIDER_BLUR) {
        float t = slider_t(mx, 0.08F, 0.92F);
        s_demo.blur_radius = 2.0F + (t * 14.0F);
    }
}

static void destroy_quad_resources(void) {
    if (s_demo.quad_vbo.id != 0) {
        nt_gfx_destroy_buffer(s_demo.quad_vbo);
    }
    if (s_demo.quad_pipeline.id != 0) {
        nt_gfx_destroy_pipeline(s_demo.quad_pipeline);
    }
    if (s_demo.quad_fs.id != 0) {
        nt_gfx_destroy_shader(s_demo.quad_fs);
    }
    if (s_demo.quad_vs.id != 0) {
        nt_gfx_destroy_shader(s_demo.quad_vs);
    }
    if (s_demo.white.id != 0) {
        nt_gfx_destroy_texture(s_demo.white);
    }
    s_demo.quad_vbo = (nt_buffer_t){0};
    s_demo.quad_pipeline = (nt_pipeline_t){0};
    s_demo.quad_fs = (nt_shader_t){0};
    s_demo.quad_vs = (nt_shader_t){0};
    s_demo.white = (nt_texture_t){0};
}

static bool make_quad_resources(void) {
    s_demo.quad_vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_quad_vs_src, .label = "rtt_quad_vs"});
    s_demo.quad_fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_quad_fs_src, .label = "rtt_quad_fs"});
    if (s_demo.quad_vs.id == 0 || s_demo.quad_fs.id == 0) {
        return false;
    }

    s_demo.quad_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = s_demo.quad_vs,
        .fragment_shader = s_demo.quad_fs,
        .layout =
            {
                .stride = sizeof(rtt_quad_vertex_t),
                .attr_count = 2,
                .attrs =
                    {
                        {.location = NT_ATTR_POSITION, .format = NT_FORMAT_FLOAT2, .offset = 0},
                        {.location = NT_ATTR_TEXCOORD0, .format = NT_FORMAT_FLOAT2, .offset = 8},
                    },
            },
        .depth_test = false,
        .depth_write = false,
        .depth_func = NT_DEPTH_ALWAYS,
        .cull_mode = 0,
        .blend = false,
        .label = "rtt_quad_pipeline",
    });
    s_demo.quad_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_DYNAMIC,
        .size = 6U * (uint32_t)sizeof(rtt_quad_vertex_t),
        .label = "rtt_quad_vbo",
    });
    s_demo.white = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 1,
        .height = 1,
        .data = s_white_pixel,
        .format = NT_PIXEL_RGBA8,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .label = "rtt_white",
    });
    return s_demo.quad_pipeline.id != 0 && s_demo.quad_vbo.id != 0 && s_demo.white.id != 0;
}

static nt_render_target_t make_target(const char *label, uint16_t width, uint16_t height, nt_render_target_depth_t depth) {
    return nt_gfx_make_render_target(&(nt_render_target_desc_t){
        .width = width,
        .height = height,
        .color_format = NT_PIXEL_RGBA8,
        .depth = depth,
        .min_filter = NT_FILTER_LINEAR,
        .mag_filter = NT_FILTER_LINEAR,
        .wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .label = label,
    });
}

static void make_targets(uint16_t width, uint16_t height) {
    s_demo.scene = make_target("rtt_scene", width, height, NT_RT_DEPTH_TEXTURE);
    s_demo.temp = make_target("rtt_blur_temp", width, height, NT_RT_DEPTH_NONE);
    s_demo.blur = make_target("rtt_blur_dest", width, height, NT_RT_DEPTH_NONE);
    s_demo.scene_color = nt_gfx_render_target_color(s_demo.scene);
    s_demo.scene_depth = nt_gfx_render_target_depth(s_demo.scene);
    s_demo.blur_color = nt_gfx_render_target_color(s_demo.blur);
    NT_ASSERT(s_demo.scene.id != 0 && s_demo.temp.id != 0 && s_demo.blur.id != 0);
    NT_ASSERT(s_demo.scene_color.id != 0 && s_demo.scene_depth.id != 0 && s_demo.blur_color.id != 0);
    s_demo.rt_width = width;
    s_demo.rt_height = height;
    s_demo.handles_stable = true;
}

static void resize_targets(uint16_t width, uint16_t height) {
    nt_render_target_t old_scene = s_demo.scene;
    nt_render_target_t old_temp = s_demo.temp;
    nt_render_target_t old_blur = s_demo.blur;
    nt_texture_t old_scene_color = s_demo.scene_color;
    nt_texture_t old_scene_depth = s_demo.scene_depth;
    nt_texture_t old_blur_color = s_demo.blur_color;

    bool ok = nt_gfx_resize_render_target(s_demo.scene, width, height);
    ok = nt_gfx_resize_render_target(s_demo.temp, width, height) && ok;
    ok = nt_gfx_resize_render_target(s_demo.blur, width, height) && ok;

    s_demo.scene_color = nt_gfx_render_target_color(s_demo.scene);
    s_demo.scene_depth = nt_gfx_render_target_depth(s_demo.scene);
    s_demo.blur_color = nt_gfx_render_target_color(s_demo.blur);
    s_demo.handles_stable = ok && old_scene.id == s_demo.scene.id && old_temp.id == s_demo.temp.id && old_blur.id == s_demo.blur.id && old_scene_color.id == s_demo.scene_color.id &&
                            old_scene_depth.id == s_demo.scene_depth.id && old_blur_color.id == s_demo.blur_color.id;
    NT_ASSERT(s_demo.handles_stable && "render-target resize must preserve target/color/depth handles");
    s_demo.rt_width = width;
    s_demo.rt_height = height;
    nt_log_info("rtt_showcase resized targets to %ux%u, handles stable=%d", (unsigned)width, (unsigned)height, s_demo.handles_stable ? 1 : 0);
}

static void draw_scene_contents(void) {
    float aspect = (float)s_demo.rt_width / (float)s_demo.rt_height;
    mat4 view;
    mat4 proj;
    mat4 vp;
    glm_lookat((vec3){0.0F, 2.2F, 5.2F}, (vec3){0.0F, 0.65F, 0.0F}, (vec3){0.0F, 1.0F, 0.0F}, view);
    glm_perspective(glm_rad(55.0F), aspect, 0.1F, 20.0F, proj);
    glm_mat4_mul(proj, view, vp);

    float cam_pos[3] = {0.0F, 2.2F, 5.2F};
    nt_shape_renderer_set_vp((float *)vp);
    nt_shape_renderer_set_cam_pos(cam_pos);
    nt_shape_renderer_set_depth(true);

    float t = (float)nt_time_now();
    versor cube_rot;
    glm_quatv(cube_rot, t * 0.7F, (vec3){0.3F, 1.0F, 0.1F});
    float cube_pos[3] = {-1.15F, 0.85F, 0.0F};
    float cube_size[3] = {0.85F, 1.55F, 0.65F};
    float red[4] = {0.95F, 0.15F, 0.10F, 1.0F};
    nt_shape_renderer_cube_rot(cube_pos, cube_size, cube_rot, red);

    float sphere_pos[3] = {1.05F, 0.65F, -0.55F};
    float teal[4] = {0.05F, 0.85F, 0.95F, 1.0F};
    nt_shape_renderer_sphere(sphere_pos, 0.55F, teal);

    float cyl_pos[3] = {0.25F, 1.55F, 0.75F};
    float yellow[4] = {1.0F, 0.82F, 0.15F, 1.0F};
    nt_shape_renderer_cylinder(cyl_pos, 0.26F, 1.25F, yellow);

    float floor_pos[3] = {0.0F, -0.02F, 0.0F};
    float floor_size[2] = {5.8F, 3.2F};
    float floor_rot[4] = {0.7071068F, 0.0F, 0.0F, 0.7071068F};
    float floor_color[4] = {0.12F, 0.14F, 0.18F, 1.0F};
    nt_shape_renderer_rect_rot(floor_pos, floor_size, floor_rot, floor_color);

    float line_color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    nt_shape_renderer_line((float[3]){-2.7F, 0.04F, -1.4F}, (float[3]){2.4F, 0.04F, 1.25F}, line_color);
    nt_shape_renderer_line((float[3]){-2.4F, 0.04F, 1.15F}, (float[3]){2.8F, 0.04F, -1.25F}, line_color);
    nt_shape_renderer_flush();
}

static void draw_textured_quad(nt_texture_t texture, float x0, float y0, float x1, float y1, int mode, const float tint[4]) {
    rtt_quad_vertex_t verts[6] = {
        {{x0, y0}, {0.0F, 0.0F}}, {{x1, y0}, {1.0F, 0.0F}}, {{x1, y1}, {1.0F, 1.0F}}, {{x0, y0}, {0.0F, 0.0F}}, {{x1, y1}, {1.0F, 1.0F}}, {{x0, y1}, {0.0F, 1.0F}},
    };
    nt_gfx_update_buffer(s_demo.quad_vbo, verts, sizeof(verts));
    nt_gfx_bind_pipeline(s_demo.quad_pipeline);
    nt_gfx_bind_vertex_buffer(s_demo.quad_vbo);
    nt_gfx_bind_texture(texture, 0);
    nt_gfx_set_uniform_int("u_texture", 0);
    nt_gfx_set_uniform_int("u_mode", mode);
    nt_gfx_set_uniform_float("u_zoom", mode == 1 ? 1.0F : s_demo.sample_zoom);
    nt_gfx_set_uniform_vec4("u_tint", tint);
    nt_gfx_draw(0, 6);
}

static void draw_solid_quad(float x0, float y0, float x1, float y1, const float color[4]) { draw_textured_quad(s_demo.white, x0, y0, x1, y1, 0, color); }

static void draw_slider(float x0, float y0, float x1, float y1, float t, const float fill[4]) {
    float track[4] = {0.12F, 0.14F, 0.18F, 1.0F};
    float thumb[4] = {0.92F, 0.96F, 1.0F, 1.0F};
    float mid_y = (y0 + y1) * 0.5F;
    float thumb_x = x0 + ((x1 - x0) * clampf(t, 0.0F, 1.0F));
    draw_solid_quad(x0, mid_y - 0.010F, x1, mid_y + 0.010F, track);
    draw_solid_quad(x0, mid_y - 0.015F, thumb_x, mid_y + 0.015F, fill);
    draw_solid_quad(thumb_x - 0.018F, y0, thumb_x + 0.018F, y1, thumb);
}

static void draw_default_frame(void) {
    float white[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    float frame[4] = {0.08F, 0.10F, 0.13F, 1.0F};
    float stable[4] = {0.05F, 0.85F, 0.30F, 1.0F};
    float unstable[4] = {0.95F, 0.10F, 0.05F, 1.0F};
    float zoom_fill[4] = {0.22F, 0.55F, 1.0F, 1.0F};
    float blur_fill[4] = {1.0F, 0.70F, 0.12F, 1.0F};
    draw_solid_quad(-0.96F, -0.76F, -0.08F, 0.78F, frame);
    draw_solid_quad(0.08F, -0.76F, 0.96F, 0.78F, frame);
    draw_textured_quad(s_demo.scene_color, -0.92F, -0.62F, -0.12F, 0.70F, 0, white);
    draw_textured_quad(s_demo.blur_color, 0.12F, -0.62F, 0.92F, 0.70F, 0, white);
    draw_textured_quad(s_demo.scene_depth, -0.44F, -0.95F, 0.44F, -0.78F, 1, white);
    draw_solid_quad(-0.92F, 0.82F, 0.92F, 0.89F, s_demo.handles_stable ? stable : unstable);
    draw_slider(-0.92F, 0.91F, -0.08F, 0.98F, (s_demo.sample_zoom - 1.0F) / 1.5F, zoom_fill);
    draw_slider(0.08F, 0.91F, 0.92F, 0.98F, (s_demo.blur_radius - 2.0F) / 14.0F, blur_fill);
}

static void frame(void) {
    nt_window_poll();
    nt_input_poll();

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif
    if (nt_input_key_is_pressed(NT_KEY_R)) {
        s_demo.large_target = !s_demo.large_target;
        if (s_demo.large_target) {
            resize_targets(768, 432);
        } else {
            resize_targets(512, 288);
        }
    }
    update_sliders();

    nt_gfx_begin_frame();
    if (g_nt_gfx.context_lost) {
        nt_window_swap_buffers();
        return;
    }
    if (g_nt_gfx.context_restored) {
        nt_shape_renderer_restore_gpu();
        nt_postfx_blur_restore_gpu();
        destroy_quad_resources();
        bool ok = make_quad_resources();
        NT_ASSERT(ok && "rtt_showcase: failed to restore quad GPU resources");
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = s_demo.scene,
        .clear_color = {0.02F, 0.025F, 0.035F, 1.0F},
        .clear_depth = 1.0F,
    });
    draw_scene_contents();
    nt_gfx_end_pass();

    bool blurred = nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = s_demo.scene_color,
        .temp = s_demo.temp,
        .dest = s_demo.blur,
        .radius = s_demo.blur_radius,
        .sigma = 0.0F,
    });
    NT_ASSERT(blurred && "rtt_showcase: blur pass failed");

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.015F, 0.018F, 0.025F, 1.0F}, .clear_depth = 1.0F});
    draw_default_frame();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_window_swap_buffers();
}

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "rtt_showcase";
    config.version = 1;

    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }

    g_nt_window.width = 960;
    g_nt_window.height = 540;
    g_nt_window.title = "Neotolis RTT Showcase";
    nt_window_init();
    nt_input_init();

    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    gfx_desc.max_render_targets = 8;
    gfx_desc.max_textures = 16;
    nt_gfx_init(&gfx_desc);
    memset(&s_demo, 0, sizeof(s_demo));
    s_demo.sample_zoom = 1.0F;
    s_demo.blur_radius = 8.0F;
    nt_shape_renderer_init();
    if (nt_postfx_blur_init() != NT_OK) {
        return 1;
    }
    bool quad_ok = make_quad_resources();
    NT_ASSERT(quad_ok && "rtt_showcase: failed to create quad resources");
    if (!quad_ok) {
        return 1;
    }
    make_targets(512, 288);

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_gfx_destroy_render_target(s_demo.blur);
    nt_gfx_destroy_render_target(s_demo.temp);
    nt_gfx_destroy_render_target(s_demo.scene);
    destroy_quad_resources();
    nt_postfx_blur_shutdown();
    nt_shape_renderer_shutdown();
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
