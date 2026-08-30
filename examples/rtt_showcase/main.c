#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "clay.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "font/nt_font.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "material/nt_program_ref.h"
#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "nt_pack_format.h"
#include "postfx/nt_postfx_blur.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_shape_renderer.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "time/nt_time.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_slider.h"
#include "window/nt_window.h"

#include "rtt_showcase_assets.h"

#include <stdio.h>
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

#define UI_ARENA_SIZE ((size_t)2U * 1024U * 1024U)
#define SCRATCH_ARENA_SIZE ((size_t)128U * 1024U)

#define LAYER_UI_BG 0
#define LAYER_UI_TEXT 1

static NT_UI_DECLARE_ARENA(s_ui_arena, UI_ARENA_SIZE);

static nt_ui_context_t *s_ui_ctx;
static nt_buffer_t s_frame_ubo;
static nt_hash32_t s_pack_id;
static nt_resource_t s_atlas_handle;
static nt_resource_t s_atlas_tex_handle;
static nt_resource_t s_font_resource;
static nt_material_t s_sprite_material;
static nt_material_t s_text_material;
static nt_program_ref_t s_sprite_program;
static nt_program_ref_t s_text_program;

/* Links each pair once both its stages are ready. The programs are ours:
 * materials only borrow the handles, and context loss forces a relink. */
static void link_programs(void) {
    if (nt_program_ref_update(&s_sprite_program)) {
        nt_material_set_program(s_sprite_material, s_sprite_program.program);
    }
    if (nt_program_ref_update(&s_text_program)) {
        nt_material_set_program(s_text_material, s_text_program.program);
    }
}
static nt_font_t s_font;
static nt_atlas_region_ref_t s_white_ref;
static bool s_atlas_bound;
static bool s_font_bound;

static const nt_ui_label_style_t s_title_style = {
    .font_id = 0,
    .font_size = 16.0F,
    .color = {232.0F, 238.0F, 248.0F, 255.0F},
};

static const nt_ui_label_style_t s_value_style = {
    .font_id = 0,
    .font_size = 13.0F,
    .color = {166.0F, 177.0F, 194.0F, 255.0F},
};

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
    nt_program_t quad_program;
    nt_pipeline_t quad_pipeline;
    nt_buffer_t quad_vbo;
    uint16_t rt_width;
    uint16_t rt_height;
    bool large_target;
    bool handles_stable;
    bool render_resources_ready;
    float sample_zoom;
    float blur_radius;
} s_demo;

typedef enum {
    RTT_RESIZE_UNUSABLE,
    RTT_RESIZE_ROLLED_BACK,
    RTT_RESIZE_COMMITTED,
} rtt_resize_result_t;

static void destroy_quad_resources(void) {
    if (s_demo.quad_vbo.id != 0) {
        nt_gfx_destroy_buffer(s_demo.quad_vbo);
    }
    if (s_demo.quad_pipeline.id != 0) {
        nt_gfx_destroy_pipeline(s_demo.quad_pipeline);
    }
    if (s_demo.quad_program.id != 0) {
        nt_gfx_destroy_program(s_demo.quad_program);
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
    s_demo.quad_program = NT_PROGRAM_INVALID;
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
    s_demo.quad_program = nt_gfx_make_program(s_demo.quad_vs, s_demo.quad_fs);
    if (!nt_gfx_program_ready(s_demo.quad_program)) {
        return false;
    }

    s_demo.quad_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .program = s_demo.quad_program,
        .layout =
            {
                .stride = sizeof(rtt_quad_vertex_t),
                .attr_count = 2,
                .attrs =
                    {
                        {.location = NT_ATTR_POSITION, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 0},
                        {.location = NT_ATTR_TEXCOORD0, .type = NT_VERTEX_FLOAT, .count = 2, .offset = 8},
                    },
            },
        .depth_test = false,
        .depth_write = false,
        .depth_func = NT_DEPTH_ALWAYS,
        .cull_mode = 0,
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
        .format = NT_TEXTURE_FORMAT_RGBA8,
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
        .color_format = NT_TEXTURE_FORMAT_RGBA8,
        .color_min_filter = NT_FILTER_LINEAR,
        .color_mag_filter = NT_FILTER_LINEAR,
        .color_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .color_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .depth_storage = depth,
        .depth_format = depth == NT_RT_DEPTH_NONE ? NT_TEXTURE_FORMAT_INVALID : NT_TEXTURE_FORMAT_DEPTH24,
        .depth_texture_min_filter = NT_FILTER_NEAREST,
        .depth_texture_mag_filter = NT_FILTER_NEAREST,
        .depth_texture_wrap_u = NT_WRAP_CLAMP_TO_EDGE,
        .depth_texture_wrap_v = NT_WRAP_CLAMP_TO_EDGE,
        .label = label,
    });
}

static bool make_targets(uint16_t width, uint16_t height) {
    s_demo.scene = make_target("rtt_scene", width, height, NT_RT_DEPTH_TEXTURE);
    s_demo.temp = make_target("rtt_blur_temp", width, height, NT_RT_DEPTH_NONE);
    s_demo.blur = make_target("rtt_blur_dest", width, height, NT_RT_DEPTH_NONE);
    if (s_demo.scene.id == 0 || s_demo.temp.id == 0 || s_demo.blur.id == 0) {
        if (s_demo.blur.id != 0) {
            nt_gfx_destroy_render_target(s_demo.blur);
        }
        if (s_demo.temp.id != 0) {
            nt_gfx_destroy_render_target(s_demo.temp);
        }
        if (s_demo.scene.id != 0) {
            nt_gfx_destroy_render_target(s_demo.scene);
        }
        s_demo.scene = NT_RENDER_TARGET_INVALID;
        s_demo.temp = NT_RENDER_TARGET_INVALID;
        s_demo.blur = NT_RENDER_TARGET_INVALID;
        return false;
    }
    s_demo.scene_color = nt_gfx_render_target_color(s_demo.scene);
    s_demo.scene_depth = nt_gfx_render_target_depth(s_demo.scene);
    s_demo.blur_color = nt_gfx_render_target_color(s_demo.blur);
    NT_ASSERT(s_demo.scene.id != 0 && s_demo.temp.id != 0 && s_demo.blur.id != 0);
    NT_ASSERT(s_demo.scene_color.id != 0 && s_demo.scene_depth.id != 0 && s_demo.blur_color.id != 0);
    s_demo.rt_width = width;
    s_demo.rt_height = height;
    s_demo.handles_stable = true;
    return true;
}

static rtt_resize_result_t resize_targets(uint16_t width, uint16_t height) {
    nt_render_target_t old_scene = s_demo.scene;
    nt_render_target_t old_temp = s_demo.temp;
    nt_render_target_t old_blur = s_demo.blur;
    nt_texture_t old_scene_color = s_demo.scene_color;
    nt_texture_t old_scene_depth = s_demo.scene_depth;
    nt_texture_t old_blur_color = s_demo.blur_color;

    bool scene_resized = nt_gfx_resize_render_target(s_demo.scene, width, height);
    bool temp_resized = scene_resized && nt_gfx_resize_render_target(s_demo.temp, width, height);
    bool blur_resized = temp_resized && nt_gfx_resize_render_target(s_demo.blur, width, height);
    if (!blur_resized) {
        bool rollback_ok = true;
        if (temp_resized) {
            rollback_ok = nt_gfx_resize_render_target(s_demo.temp, s_demo.rt_width, s_demo.rt_height) && rollback_ok;
        }
        if (scene_resized) {
            rollback_ok = nt_gfx_resize_render_target(s_demo.scene, s_demo.rt_width, s_demo.rt_height) && rollback_ok;
        }
        if (!rollback_ok) {
            nt_log_error("rtt_showcase: render-target resize rollback failed");
            return RTT_RESIZE_UNUSABLE;
        }
        nt_log_error("rtt_showcase: render-target resize failed; previous size restored");
        return RTT_RESIZE_ROLLED_BACK;
    }

    s_demo.scene_color = nt_gfx_render_target_color(s_demo.scene);
    s_demo.scene_depth = nt_gfx_render_target_depth(s_demo.scene);
    s_demo.blur_color = nt_gfx_render_target_color(s_demo.blur);
    s_demo.handles_stable = old_scene.id == s_demo.scene.id && old_temp.id == s_demo.temp.id && old_blur.id == s_demo.blur.id && old_scene_color.id == s_demo.scene_color.id &&
                            old_scene_depth.id == s_demo.scene_depth.id && old_blur_color.id == s_demo.blur_color.id;
    NT_ASSERT(s_demo.handles_stable && "render-target resize must preserve target/color/depth handles");
    s_demo.rt_width = width;
    s_demo.rt_height = height;
    nt_log_info("rtt_showcase resized targets to %ux%u, handles stable=%d", (unsigned)width, (unsigned)height, s_demo.handles_stable ? 1 : 0);
    return RTT_RESIZE_COMMITTED;
}

static bool render_targets_ready(void) {
    return nt_gfx_render_target_ready(s_demo.scene) && nt_gfx_render_target_ready(s_demo.temp) && nt_gfx_render_target_ready(s_demo.blur) && nt_gfx_texture_ready(s_demo.scene_color) &&
           nt_gfx_texture_ready(s_demo.scene_depth) && nt_gfx_texture_ready(s_demo.blur_color);
}

static void init_ui_refs(void) { s_white_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_RTT_SHOWCASE_UI_ATLAS__WHITE.value); }

static void try_bind_ui_resources(void) {
    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        uint32_t white_region = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_RTT_SHOWCASE_UI_ATLAS__WHITE.value);
        NT_ASSERT(white_region != NT_ATLAS_INVALID_REGION);
        if (white_region == NT_ATLAS_INVALID_REGION) {
            return;
        }
        nt_ui_set_atlas_white_region(s_ui_ctx, s_atlas_handle, white_region);
        s_atlas_bound = true;
        nt_log_info("rtt_showcase: UI atlas white region bound");
    }

    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        nt_font_add(s_font, s_font_resource);
        nt_ui_set_font(s_ui_ctx, 0U, s_font);
        s_font_bound = true;
        nt_log_info("rtt_showcase: UI font bound at slot 0");
    }
}

static nt_ui_slider_style_t make_slider_style(uint32_t fill_tint) {
    nt_ui_slider_style_t style = nt_ui_slider_style_defaults();
    style.track_w = 300.0F;
    style.track_h = 8.0F;
    style.thumb_w = 18.0F;
    style.thumb_h = 24.0F;
    style.value_speed = 0.0F;
    style.states[NT_UI_SLIDER_IDLE].track = s_white_ref;
    style.states[NT_UI_SLIDER_IDLE].fill = s_white_ref;
    style.states[NT_UI_SLIDER_IDLE].thumb = s_white_ref;
    style.states[NT_UI_SLIDER_IDLE].track_tint = 0xFF2A3240U;
    style.states[NT_UI_SLIDER_IDLE].fill_tint = fill_tint;
    style.states[NT_UI_SLIDER_IDLE].thumb_tint = 0xFFEAF1FFU;
    style.states[NT_UI_SLIDER_HOVER].thumb_tint = 0xFFFFFFFFU;
    style.states[NT_UI_SLIDER_PRESSED].thumb_tint = 0xFFFFFFFFU;
    return style;
}

static void declare_slider_control(const char *title, const char *value_text, uint32_t id, float *value, float min_value, float max_value, uint32_t fill_tint) {
    nt_ui_slider_style_t slider = make_slider_style(fill_tint);
    static const Clay_ElementDeclaration slider_decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(324), CLAY_SIZING_FIXED(34)}},
    };

    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(340), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            nt_ui_label(s_ui_ctx, NT_UI_DATA_LAYER(LAYER_UI_TEXT), title, &s_title_style);
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1)}}}) {}
            nt_ui_label(s_ui_ctx, NT_UI_DATA_LAYER(LAYER_UI_TEXT), value_text, &s_value_style);
        }
        (void)nt_ui_slider_float(s_ui_ctx, NT_UI_DATA_LAYER(LAYER_UI_BG), LAYER_UI_TEXT, id, NULL, value, min_value, max_value, 0.0F, &slider, &slider_decl, true);
    }
}

static bool ui_ready(void) {
    const nt_material_info_t *sprite_info = nt_material_get_info(s_sprite_material);
    const nt_material_info_t *text_info = nt_material_get_info(s_text_material);
    return s_atlas_bound && s_font_bound && sprite_info != NULL && nt_gfx_program_ready(sprite_info->program) && text_info != NULL && nt_gfx_program_ready(text_info->program);
}

static void draw_ui_overlay(void) {
    nt_font_step();
    if (!ui_ready()) {
        return;
    }

    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 960);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 540);

    mat4 view_m;
    mat4 proj_m;
    mat4 vp;
    glm_mat4_identity(view_m);
    glm_ortho(0.0F, fb_w, 0.0F, fb_h, -1.0F, 1.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp);

    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, vp, 64);
    memcpy(uniforms.view, view_m, 64);
    memcpy(uniforms.proj, proj_m, 64);
    uniforms.resolution[0] = fb_w;
    uniforms.resolution[1] = fb_h;
    uniforms.resolution[2] = (fb_w > 0.0F) ? (1.0F / fb_w) : 0.0F;
    uniforms.resolution[3] = (fb_h > 0.0F) ? (1.0F / fb_h) : 0.0F;
    uniforms.near_far[0] = -1.0F;
    uniforms.near_far[1] = 1.0F;
    nt_gfx_update_buffer(s_frame_ubo, 0, &uniforms, sizeof(uniforms));
    nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

    char zoom_text[32];
    char blur_text[32];
    (void)snprintf(zoom_text, sizeof zoom_text, "%.2fx", (double)s_demo.sample_zoom);
    (void)snprintf(blur_text, sizeof blur_text, "%.1f px", (double)s_demo.blur_radius);

    nt_ui_begin(s_ui_ctx, fb_w, fb_h, g_nt_app.dt, g_nt_input.pointers, NT_INPUT_MAX_POINTERS);
    CLAY({.id = CLAY_ID("rtt_ui_root"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = {.left = 18, .right = 18, .top = 14, .bottom = 0}, .layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
        CLAY({.id = CLAY_ID("rtt_controls"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {.left = 18, .right = 18, .top = 12, .bottom = 12},
                         .layoutDirection = CLAY_LEFT_TO_RIGHT,
                         .childGap = 28,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {9.0F, 13.0F, 20.0F, 224.0F}}) {
            declare_slider_control("Sample zoom", zoom_text, nt_ui_id("rtt_sample_zoom"), &s_demo.sample_zoom, 1.0F, 2.5F, 0xFFFF8E38U);
            declare_slider_control("Blur radius", blur_text, nt_ui_id("rtt_blur_radius"), &s_demo.blur_radius, 2.0F, 16.0F, 0xFF2EE4A6U);
        }
    }
    nt_ui_end(s_ui_ctx);

    nt_ui_target_t target = {.viewport = {0.0F, 0.0F, fb_w, fb_h}};
    nt_ui_walk(s_ui_ctx, &target);
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
    nt_gfx_update_buffer(s_demo.quad_vbo, 0, verts, sizeof(verts));
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

static void draw_default_frame(void) {
    float white[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    float frame[4] = {0.08F, 0.10F, 0.13F, 1.0F};
    float stable[4] = {0.05F, 0.85F, 0.30F, 1.0F};
    float unstable[4] = {0.95F, 0.10F, 0.05F, 1.0F};
    draw_solid_quad(-0.96F, -0.76F, -0.08F, 0.78F, frame);
    draw_solid_quad(0.08F, -0.76F, 0.96F, 0.78F, frame);
    draw_textured_quad(s_demo.scene_color, -0.92F, -0.62F, -0.12F, 0.70F, 0, white);
    draw_textured_quad(s_demo.blur_color, 0.12F, -0.62F, 0.92F, 0.70F, 0, white);
    draw_textured_quad(s_demo.scene_depth, -0.44F, -0.95F, 0.44F, -0.78F, 1, white);
    draw_solid_quad(-0.92F, 0.82F, 0.92F, 0.89F, s_demo.handles_stable ? stable : unstable);
}

static void frame(void) {
    nt_window_poll();
    nt_input_poll();
    nt_mem_scratch_reset();

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif
    if (nt_input_key_is_pressed(NT_KEY_R)) {
        bool make_large = !s_demo.large_target;
        rtt_resize_result_t resize_result = make_large ? resize_targets(768, 432) : resize_targets(512, 288);
        s_demo.render_resources_ready = resize_result != RTT_RESIZE_UNUSABLE;
        if (resize_result == RTT_RESIZE_COMMITTED) {
            s_demo.large_target = make_large;
        }
    }
    nt_resource_step();
    nt_material_step();
    link_programs();
    try_bind_ui_resources();

    nt_gfx_begin_frame();
    if (g_nt_gfx.context_lost) {
        nt_window_swap_buffers();
        return;
    }
    if (g_nt_gfx.context_restored) {
        /* Order does not matter here: nothing draws between these calls, and the
         * materials keep their handles -- a destroyed program reads as not ready,
         * so every renderer skips until the gate below relinks and re-assigns. */
        nt_shape_renderer_restore_gpu();
        bool restored = nt_postfx_blur_restore_gpu() == NT_OK;
        destroy_quad_resources();
        restored = make_quad_resources() && restored;
        nt_resource_invalidate(NT_ASSET_TEXTURE);
        nt_resource_invalidate(NT_ASSET_FONT);
        nt_gfx_destroy_buffer(s_frame_ubo);
        s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = NT_BUFFER_UNIFORM,
            .usage = NT_USAGE_DYNAMIC,
            .size = sizeof(nt_frame_uniforms_t),
            .label = "rtt_frame_uniforms",
        });
        restored = s_frame_ubo.id != 0 && restored;
        nt_sprite_renderer_restore_gpu();
        nt_text_renderer_restore_gpu();
        nt_program_ref_drop(&s_sprite_program);
        nt_program_ref_drop(&s_text_program);
        nt_resource_invalidate(NT_ASSET_SHADER_CODE);
        s_atlas_bound = false;
        s_font_bound = false;
        s_demo.render_resources_ready = restored && render_targets_ready();
        if (!s_demo.render_resources_ready) {
            nt_log_error("rtt_showcase: GPU resources are not ready after context restore");
        }
        /* Everything decided before begin_frame described the dead context, so
         * this frame draws nothing -- the next one is built from scratch. */
        nt_gfx_end_frame();
        nt_window_swap_buffers();
        return;
    }
    if (!s_demo.render_resources_ready || !render_targets_ready()) {
        nt_gfx_end_frame();
        nt_window_swap_buffers();
        return;
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .target = s_demo.scene,
        .clear_color = {0.02F, 0.025F, 0.035F, 1.0F},
        .clear_depth = 1.0F,
    });
    draw_scene_contents();
    nt_gfx_end_pass();

    nt_postfx_blur_gaussian(&(nt_postfx_blur_pass_t){
        .source = s_demo.scene_color,
        .temp = s_demo.temp,
        .dest = s_demo.blur,
        .radius = s_demo.blur_radius,
        .sigma = 0.0F,
    });

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.015F, 0.018F, 0.025F, 1.0F}, .clear_depth = 1.0F});
    draw_default_frame();
    draw_ui_overlay();
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
    gfx_desc.max_textures = 32;
    gfx_desc.max_pipelines = 32;
    nt_gfx_init(&gfx_desc);
    nt_gfx_register_global_block("Globals", 0);
    nt_http_init();
    nt_fs_init();
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_mem_scratch_init(SCRATCH_ARENA_SIZE);
    nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture);
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);
    nt_atlas_init();
    nt_material_init(&(nt_material_desc_t){.max_materials = 4});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 1});
    nt_sprite_renderer_desc_t sprite_desc = nt_sprite_renderer_desc_defaults();
    nt_sprite_renderer_init(&sprite_desc);
    nt_text_renderer_init();
    nt_ui_module_init();
    const nt_ui_create_desc_t ui_desc = nt_ui_create_desc_defaults();
    s_ui_ctx = nt_ui_create_context(s_ui_arena, sizeof s_ui_arena, &ui_desc);
    NT_ASSERT(s_ui_ctx != NULL && "rtt_showcase: failed to create UI context");

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "rtt_frame_uniforms",
    });

    s_pack_id = nt_hash32_str("rtt_showcase");
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/rtt_showcase/rtt_showcase.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/rtt_showcase.ntpack");
#endif

    s_sprite_program.vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_program.fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_program.vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_program.fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_RTT_SHOWCASE_UI_ATLAS, NT_ASSET_ATLAS);
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_RTT_SHOWCASE_UI_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_RTT_SHOWCASE_FONT, NT_ASSET_FONT);
    init_ui_refs();

    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .blend = nt_blend_alpha_premultiplied(),
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "rtt_showcase_ui_sprite",
    });
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .blend = nt_blend_alpha_premultiplied(),
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .params[0] = {.name = "u_alpha_cutoff", .value = {NT_TEXT_ALPHA_CUTOFF_DEFAULT}},
        .param_count = 1,
        .label = "rtt_showcase_ui_text",
    });
    nt_ui_set_sprite_material(s_ui_ctx, s_sprite_material);
    nt_ui_set_text_material(s_ui_ctx, s_text_material);
    s_font = nt_font_create(&(nt_font_create_desc_t){
        .curve_texture_width = 1024,
        .curve_texture_height = 512,
        .band_texture_height = 256,
        .band_count = 8,
        .measure_cache_size = 256,
    });
    nt_resource_set_activate_time_budget(0);

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
    if (!make_targets(512, 288)) {
        return 1;
    }
    s_demo.render_resources_ready = true;

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_gfx_destroy_render_target(s_demo.blur);
    nt_gfx_destroy_render_target(s_demo.temp);
    nt_gfx_destroy_render_target(s_demo.scene);
    destroy_quad_resources();
    nt_ui_destroy_context(s_ui_ctx);
    nt_ui_module_shutdown();
    nt_text_renderer_shutdown();
    nt_sprite_renderer_shutdown();
    nt_font_destroy(s_font);
    nt_font_shutdown();
    nt_material_destroy(s_sprite_material);
    nt_material_destroy(s_text_material);
    nt_program_ref_drop(&s_sprite_program);
    nt_program_ref_drop(&s_text_program);
    nt_material_shutdown();
    nt_mem_scratch_shutdown();
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_hash_shutdown();
    nt_gfx_destroy_buffer(s_frame_ubo);
    nt_postfx_blur_shutdown();
    nt_shape_renderer_shutdown();
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
