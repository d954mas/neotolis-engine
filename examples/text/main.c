/*
 * Text Rendering Demo -- Neotolis Engine
 *
 * End-to-end demo: builder -> pack -> font -> text renderer -> GPU.
 * 3D world-space text with trackball camera, multi-language text,
 * and progressive pack loading (Latin+Cyrillic base, CJK addon).
 *
 * Controls:
 *   LMB + drag -- rotate text (trackball)
 *   SCROLL     -- zoom in/out
 *   ESC        -- quit (native only)
 *
 * Build packs first:  build_text_packs build/examples/text
 */

#include "app/nt_app.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "font/nt_font.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "time/nt_time.h"
#include "window/nt_window.h"

#include "math/nt_math.h"
#include "nt_pack_format.h"
#include "text_assets.h"

#include <math.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

/* ---- Interaction constants ---- */

#define MOUSE_SENS 0.005F
#define ZOOM_MIN 3.0F
#define ZOOM_MAX 30.0F
#define ZOOM_SPEED 0.02F
#define FOV_DEG 70.0F
#define INERTIA_DECAY 0.95F
#define VEL_THRESHOLD 0.0001F

/* ---- Text content (D-24) ---- */

static const char *TEXT_EN = "Hello Slug!";
static const char *TEXT_RU = "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82 Slug!";       /* Privet Slug! */
static const char *TEXT_CN = "\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c";             /* ni hao shi jie */
static const char *TEXT_KR = "\xec\x95\x88\xeb\x85\x95\xed\x95\x98\xec\x84\xb8\xec\x9a\x94"; /* annyeonghaseyo */

/* ---- State ---- */

static float s_yaw;
static float s_pitch;
static float s_vel_yaw;
static float s_vel_pitch;
static float s_cam_dist = 12.0F;
static bool s_grabbed;

static nt_font_t s_font;
static nt_material_t s_text_material;
static nt_buffer_t s_frame_ubo;

static nt_hash32_t s_base_pack_id;
static nt_hash32_t s_cjk_pack_id;
static bool s_cjk_loading;

/* ---- Trackball: compose yaw/pitch into camera view ---- */

static void trackball_update(float dt) {
    /* Mouse drag rotation */
    if (nt_input_mouse_is_down(NT_BUTTON_LEFT)) {
        s_grabbed = true;
        float dx = g_nt_input.pointers[0].dx;
        float dy = g_nt_input.pointers[0].dy;
        float new_yaw = dx * MOUSE_SENS;
        float new_pitch = dy * MOUSE_SENS;
        s_vel_yaw = new_yaw * 0.6F + s_vel_yaw * 0.4F;
        s_vel_pitch = new_pitch * 0.6F + s_vel_pitch * 0.4F;
    }

    /* Apply velocity (active drag or inertia) */
    if (fabsf(s_vel_yaw) > VEL_THRESHOLD || fabsf(s_vel_pitch) > VEL_THRESHOLD) {
        s_yaw += s_vel_yaw;
        s_pitch += s_vel_pitch;

        /* Clamp pitch to avoid gimbal flip */
        if (s_pitch > 1.5F) {
            s_pitch = 1.5F;
        }
        if (s_pitch < -1.5F) {
            s_pitch = -1.5F;
        }

        /* Inertia decay (only when not dragging) */
        if (!nt_input_mouse_is_down(NT_BUTTON_LEFT)) {
            float decay = powf(INERTIA_DECAY, dt * 60.0F);
            s_vel_yaw *= decay;
            s_vel_pitch *= decay;
            if (fabsf(s_vel_yaw) < VEL_THRESHOLD) {
                s_vel_yaw = 0;
            }
            if (fabsf(s_vel_pitch) < VEL_THRESHOLD) {
                s_vel_pitch = 0;
            }
        }
    }

    /* Scroll zoom */
    float wheel = g_nt_input.pointers[0].wheel_dy;
    if (fabsf(wheel) > 0.001F) {
        s_cam_dist += wheel * ZOOM_SPEED;
        if (s_cam_dist < ZOOM_MIN) {
            s_cam_dist = ZOOM_MIN;
        }
        if (s_cam_dist > ZOOM_MAX) {
            s_cam_dist = ZOOM_MAX;
        }
    }
}

/* ---- Draw text strings at world positions ---- */

static void draw_text_scene(void) {
    mat4 model;

    /* Large "Hello Slug!" centered at Y=3.0, white */
    {
        float white[4] = {1.0F, 1.0F, 1.0F, 1.0F};
        glm_mat4_identity(model);
        glm_translate(model, (vec3){-5.0F, 3.0F, 0.0F});
        nt_text_renderer_draw(TEXT_EN, (const float *)model, 2.0F, white);
    }

    /* Medium Russian text at Y=1.0, light blue */
    {
        float blue[4] = {0.6F, 0.8F, 1.0F, 1.0F};
        glm_mat4_identity(model);
        glm_translate(model, (vec3){-5.0F, 1.0F, 0.0F});
        nt_text_renderer_draw(TEXT_RU, (const float *)model, 1.5F, blue);
    }

    /* Chinese text at Y=-1.0, light green */
    {
        float green[4] = {0.6F, 1.0F, 0.6F, 1.0F};
        glm_mat4_identity(model);
        glm_translate(model, (vec3){-3.0F, -1.0F, 0.0F});
        nt_text_renderer_draw(TEXT_CN, (const float *)model, 1.5F, green);
    }

    /* Korean text at Y=-3.0, light yellow */
    {
        float yellow[4] = {1.0F, 1.0F, 0.6F, 1.0F};
        glm_mat4_identity(model);
        glm_translate(model, (vec3){-4.0F, -3.0F, 0.0F});
        nt_text_renderer_draw(TEXT_KR, (const float *)model, 1.5F, yellow);
    }

    /* Small size reference at bottom */
    {
        float gray[4] = {0.7F, 0.7F, 0.7F, 1.0F};

        glm_mat4_identity(model);
        glm_translate(model, (vec3){-8.0F, -5.5F, 0.0F});
        nt_text_renderer_draw(TEXT_EN, (const float *)model, 0.5F, gray);

        glm_mat4_identity(model);
        glm_translate(model, (vec3){-2.0F, -5.5F, 0.0F});
        nt_text_renderer_draw(TEXT_RU, (const float *)model, 0.5F, gray);

        glm_mat4_identity(model);
        glm_translate(model, (vec3){4.0F, -5.5F, 0.0F});
        nt_text_renderer_draw(TEXT_CN, (const float *)model, 0.5F, gray);

        glm_mat4_identity(model);
        glm_translate(model, (vec3){8.0F, -5.5F, 0.0F});
        nt_text_renderer_draw(TEXT_KR, (const float *)model, 0.5F, gray);
    }
}

/* ---- Frame callback ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void frame(void) {
    nt_window_poll();
    nt_input_poll();
    float dt = g_nt_app.dt;

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif

    /* Step resource + material systems */
    nt_resource_step();
    nt_material_step();

    /* Progressive loading: start CJK pack after base pack is ready */
    if (!s_cjk_loading && nt_resource_pack_state(s_base_pack_id) == NT_PACK_STATE_READY) {
        s_cjk_loading = true;
#ifdef NT_PLATFORM_WEB
        nt_platform_web_loading_complete();
#endif
        nt_log_info("Base pack ready, starting CJK download...");
        nt_resource_load_auto(s_cjk_pack_id, "assets/text_cjk.ntpack");
    }

    /* Trackball camera update */
    trackball_update(dt);

    /* VP matrix construction */
    float aspect = 1.0F;
    if (g_nt_window.fb_height > 0) {
        aspect = (float)g_nt_window.fb_width / (float)g_nt_window.fb_height;
    }

    /* Camera position from spherical coordinates */
    float cx = s_cam_dist * cosf(s_pitch) * sinf(s_yaw);
    float cy = s_cam_dist * sinf(s_pitch);
    float cz = s_cam_dist * cosf(s_pitch) * cosf(s_yaw);

    vec3 eye = {cx, cy, cz};
    vec3 center = {0, 0, 0};
    vec3 up = {0, 1, 0};

    mat4 view_m;
    mat4 proj_m;
    mat4 vp;
    glm_lookat(eye, center, up, view_m);
    glm_perspective(glm_rad(FOV_DEG), aspect, 0.1F, 100.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp);

    /* Frame uniforms */
    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, vp, 64);
    memcpy(uniforms.view, view_m, 64);
    memcpy(uniforms.proj, proj_m, 64);
    uniforms.camera_pos[0] = cx;
    uniforms.camera_pos[1] = cy;
    uniforms.camera_pos[2] = cz;
    uniforms.time[0] = (float)nt_time_now();
    uniforms.time[1] = dt;
    if (g_nt_window.fb_width > 0) {
        uniforms.resolution[0] = (float)g_nt_window.fb_width;
        uniforms.resolution[1] = (float)g_nt_window.fb_height;
        uniforms.resolution[2] = 1.0F / (float)g_nt_window.fb_width;
        uniforms.resolution[3] = 1.0F / (float)g_nt_window.fb_height;
    }
    uniforms.near_far[0] = 0.1F;
    uniforms.near_far[1] = 100.0F;

    /* ---- Render ---- */

    nt_gfx_begin_frame();

    /* Restore GPU resources after WebGL context loss */
    if (g_nt_gfx.context_restored) {
        nt_resource_invalidate(NT_ASSET_SHADER_CODE);
        nt_resource_invalidate(NT_ASSET_FONT);

        s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = NT_BUFFER_UNIFORM,
            .usage = NT_USAGE_DYNAMIC,
            .size = sizeof(nt_frame_uniforms_t),
            .label = "frame_uniforms",
        });
        nt_text_renderer_restore_gpu();
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .clear_color = {0.05F, 0.05F, 0.1F, 1.0F},
        .clear_depth = 1.0F,
    });

    /* Upload and bind frame UBO (slot 0) */
    nt_gfx_update_buffer(s_frame_ubo, &uniforms, sizeof(uniforms));
    nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

    /* Step font system -- resolves pending resources, uploads GPU data */
    nt_font_step();

    /* Draw text */
    nt_text_renderer_set_material(s_text_material);
    nt_text_renderer_set_font(s_font);

    draw_text_scene();

    nt_text_renderer_flush();

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_window_swap_buffers();
}

int main(void) {
    /* 1. Engine init */
    nt_engine_config_t config = {0};
    config.app_name = "text_demo";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        return 1;
    }

    /* 2. Window init */
    g_nt_window.width = 800;
    g_nt_window.height = 600;
    nt_window_init();

    /* 3. Input init */
    nt_input_init();

    /* 4. GFX init */
    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    nt_gfx_init(&gfx_desc);

    /* Register global UBO block (slot 0 for Globals: view_proj etc.) */
    nt_gfx_register_global_block("Globals", 0);

    /* 5. I/O init */
    nt_http_init();
    nt_fs_init();
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});

    /* 6. Register activators */
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);

    /* 7. Font init */
    nt_font_init(&(nt_font_desc_t){.max_fonts = 4});

    /* 8. Material init */
    nt_material_init(&(nt_material_desc_t){.max_materials = 8});

    /* 9. Text renderer init */
    nt_text_renderer_init();

    /* 10. Create frame uniforms UBO */
    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    /* 11. Mount packs and start base pack loading */
    s_base_pack_id = nt_hash32_str("text_base");
    nt_resource_mount(s_base_pack_id, 10);
    nt_resource_load_auto(s_base_pack_id, "assets/text_base.ntpack");

    s_cjk_pack_id = nt_hash32_str("text_cjk");
    nt_resource_mount(s_cjk_pack_id, 20);
    /* CJK pack loaded progressively in frame() after base is ready */

    /* 12. Request shader resources */
    nt_resource_t vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    nt_resource_t fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);

    /* 13. Create text material (shader pair, alpha blend for Slug) */
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = vs,
        .fs = fs,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = true,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "slug_text",
    });

    /* 14. Create font and add font resources */
    s_font = nt_font_create(&(nt_font_create_desc_t){
        .curve_texture_width = 1024,
        .curve_texture_height = 512,
        .band_texture_height = 256,
        .band_count = 8,
    });

    /* Request font resources (resolved when packs arrive) */
    nt_resource_t font_base_res = nt_resource_request(ASSET_FONT_TEXT_FONT_BASE, NT_ASSET_FONT);
    nt_font_add(s_font, font_base_res);

#ifdef ASSET_FONT_TEXT_FONT_CJK
    /* CJK font resource -- resolved when CJK pack loads (progressive) */
    nt_resource_t font_cjk_res = nt_resource_request(ASSET_FONT_TEXT_FONT_CJK, NT_ASSET_FONT);
    nt_font_add(s_font, font_cjk_res);
#endif

    /* 15. Unlimited activation budget during loading (0 = no time limit) */
    nt_resource_set_activate_time_budget(0);

    /* 16. Run main loop */
    nt_app_run(frame);

    /* 17. Shutdown (native only) */
#ifndef NT_PLATFORM_WEB
    nt_text_renderer_shutdown();
    nt_font_destroy(s_font);
    nt_font_shutdown();
    nt_material_destroy(s_text_material);
    nt_material_shutdown();
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_hash_shutdown();
    nt_gfx_destroy_buffer(s_frame_ubo);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif

    return 0;
}
