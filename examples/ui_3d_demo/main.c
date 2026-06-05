/* ui_3d_demo — walkable space with a central rotating primitive + HUD hints. Phase 1 of the
 * 3D-UI demo (no world-space UI panels yet; keys cycle shape/speed). Validates FPS camera +
 * shape renderer + text HUD. PASS 2 will replace the keyboard controls with two world-mounted
 * UI panels via nt_ui + XFORM. PASS 3 adds the 2D-vs-3D input bench.
 *
 * Controls:
 *   WASD            walk along yaw (no collisions per scope)
 *   RMB-drag        mouselook (yaw + pitch)
 *   Q / E           yaw keys (alternate to mouse)
 *   R               reset player + shape rotation
 *   1 / 2 / 3       shape = cube / sphere / capsule
 *   4 / 5 / 6 / 7   speed = stop / slow / medium / fast
 *   F1              toggle debug overlay (player pose + fps)
 *   Esc             quit (native) */

// #region includes
#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
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
#include "material/nt_material.h"
#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_shape_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "stats/nt_stats.h"
#include "time/nt_time.h"
#include "window/nt_window.h"

#include "nt_pack_format.h"

#include "ui_buttons_demo_assets.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif
// #endregion

// #region constants
#define ROOM_W 20.0F
#define ROOM_H 10.0F
#define ROOM_D 20.0F
#define GRID_STEP 1.0F

#define MOVE_SPEED 5.0F
#define ROT_SPEED 1.8F
#define MOUSE_SENS 0.005F
#define PITCH_LIMIT 1.2F
#define FOV_DEG 65.0F
#define EYE_HEIGHT 1.7F

#define SCRATCH_ARENA_SIZE ((size_t)256 * 1024)

#define HUD_SIZE 16.0F
#define HUD_TITLE_SIZE 20.0F

enum { SHAPE_CUBE = 0, SHAPE_SPHERE, SHAPE_CAPSULE, SHAPE_COUNT };
enum { SPEED_STOP = 0, SPEED_SLOW, SPEED_MED, SPEED_FAST, SPEED_COUNT };
// #endregion

// #region tables
/* clang-format off */
static const float s_speed_table[SPEED_COUNT] = { 0.0F, 0.4F, 1.2F, 3.5F };
static const float s_shape_colors[SHAPE_COUNT][4] = {
    {0.25F, 0.85F, 0.95F, 1.0F},
    {0.95F, 0.45F, 1.00F, 1.0F},
    {1.00F, 0.75F, 0.20F, 1.0F},
};
/* clang-format on */
static const char *const s_shape_labels[SHAPE_COUNT] = {"CUBE", "SPHERE", "CAPSULE"};
static const char *const s_speed_labels[SPEED_COUNT] = {"STOP", "SLOW", "MEDIUM", "FAST"};
// #endregion

// #region state
static float s_player_pos[3] = {0.0F, EYE_HEIGHT, 7.0F};
static float s_player_yaw;
static float s_player_pitch;

static int s_shape_kind = SHAPE_CUBE;
static int s_speed_kind = SPEED_SLOW;
static float s_shape_yaw;

static bool s_debug_overlay;

/* Resources. */
static nt_hash32_t s_pack_id;
static nt_resource_t s_text_vs_handle;
static nt_resource_t s_text_fs_handle;
static nt_resource_t s_font_resource;
static nt_material_t s_text_material;
static nt_font_t s_font;
static bool s_font_bound;
static nt_buffer_t s_frame_ubo;
// #endregion

// #region helpers
static void player_reset(void) {
    s_player_pos[0] = 0.0F;
    s_player_pos[1] = EYE_HEIGHT;
    s_player_pos[2] = 7.0F;
    s_player_yaw = 0.0F;
    s_player_pitch = 0.0F;
    s_shape_yaw = 0.0F;
}

static void player_update(float dt) {
    if (nt_input_mouse_is_down(NT_BUTTON_RIGHT)) {
        s_player_yaw += g_nt_input.pointers[0].dx * MOUSE_SENS;
        s_player_pitch -= g_nt_input.pointers[0].dy * MOUSE_SENS;
    }
    if (nt_input_key_is_down(NT_KEY_Q)) {
        s_player_yaw -= ROT_SPEED * dt;
    }
    if (nt_input_key_is_down(NT_KEY_E)) {
        s_player_yaw += ROT_SPEED * dt;
    }
    if (s_player_pitch > PITCH_LIMIT) {
        s_player_pitch = PITCH_LIMIT;
    }
    if (s_player_pitch < -PITCH_LIMIT) {
        s_player_pitch = -PITCH_LIMIT;
    }

    const float cy = cosf(s_player_yaw);
    const float sy = sinf(s_player_yaw);
    const float fwd_x = sy;
    const float fwd_z = -cy;
    const float right_x = cy;
    const float right_z = sy;

    float mx = 0.0F;
    float mz = 0.0F;
    if (nt_input_key_is_down(NT_KEY_W)) {
        mx += fwd_x;
        mz += fwd_z;
    }
    if (nt_input_key_is_down(NT_KEY_S)) {
        mx -= fwd_x;
        mz -= fwd_z;
    }
    if (nt_input_key_is_down(NT_KEY_A)) {
        mx -= right_x;
        mz -= right_z;
    }
    if (nt_input_key_is_down(NT_KEY_D)) {
        mx += right_x;
        mz += right_z;
    }
    const float len = sqrtf((mx * mx) + (mz * mz));
    if (len > 0.0001F) {
        mx /= len;
        mz /= len;
        const float step = MOVE_SPEED * dt;
        s_player_pos[0] += mx * step;
        s_player_pos[2] += mz * step;
    }
    const float inset = 0.5F;
    const float hw = (ROOM_W * 0.5F) - inset;
    const float hd = (ROOM_D * 0.5F) - inset;
    if (s_player_pos[0] < -hw) {
        s_player_pos[0] = -hw;
    }
    if (s_player_pos[0] > hw) {
        s_player_pos[0] = hw;
    }
    if (s_player_pos[2] < -hd) {
        s_player_pos[2] = -hd;
    }
    if (s_player_pos[2] > hd) {
        s_player_pos[2] = hd;
    }
}

static void compute_perspective_vp(mat4 out_vp, float aspect) {
    const float cy = cosf(s_player_yaw);
    const float sy = sinf(s_player_yaw);
    const float cp = cosf(s_player_pitch);
    const float sp = sinf(s_player_pitch);
    vec3 eye = {s_player_pos[0], s_player_pos[1], s_player_pos[2]};
    vec3 fwd = {sy * cp, sp, -cy * cp};
    vec3 center = {eye[0] + fwd[0], eye[1] + fwd[1], eye[2] + fwd[2]};
    vec3 up = {0.0F, 1.0F, 0.0F};
    mat4 view;
    mat4 proj;
    glm_lookat(eye, center, up, view);
    glm_perspective(glm_rad(FOV_DEG), aspect, 0.1F, 100.0F, proj);
    glm_mat4_mul(proj, view, out_vp);
}
// #endregion

// #region drawing
static void draw_room(void) {
    const float hw = ROOM_W * 0.5F;
    const float hd = ROOM_D * 0.5F;
    const float floor_col[4] = {0.13F, 0.13F, 0.16F, 1.0F};
    const float ceil_col[4] = {0.10F, 0.10F, 0.16F, 1.0F};
    const float wall_col[4] = {0.20F, 0.18F, 0.16F, 1.0F};
    const float grid_col[4] = {0.30F, 0.30F, 0.36F, 1.0F};
    const float floor_pos[3] = {0.0F, 0.0F, 0.0F};
    const float floor_sz[2] = {ROOM_W, ROOM_D};
    const float floor_rot[4] = {0.7071068F, 0.0F, 0.0F, 0.7071068F};
    const float ceil_pos[3] = {0.0F, ROOM_H, 0.0F};
    nt_shape_renderer_rect_rot(floor_pos, floor_sz, floor_rot, floor_col);
    nt_shape_renderer_rect_rot(ceil_pos, floor_sz, floor_rot, ceil_col);

    const int nx = (int)(ROOM_W / GRID_STEP) + 1;
    const int nz = (int)(ROOM_D / GRID_STEP) + 1;
    for (int ix = 0; ix < nx; ++ix) {
        const float x = -hw + ((float)ix * GRID_STEP);
        const float a[3] = {x, 0.002F, -hd};
        const float b[3] = {x, 0.002F, hd};
        nt_shape_renderer_line(a, b, grid_col);
    }
    for (int iz = 0; iz < nz; ++iz) {
        const float z = -hd + ((float)iz * GRID_STEP);
        const float a[3] = {-hw, 0.002F, z};
        const float b[3] = {hw, 0.002F, z};
        nt_shape_renderer_line(a, b, grid_col);
    }

    const float wall_sz_fb[2] = {ROOM_W, ROOM_H};
    const float front_pos[3] = {0.0F, ROOM_H * 0.5F, -hd};
    nt_shape_renderer_rect(front_pos, wall_sz_fb, wall_col);
    const float back_pos[3] = {0.0F, ROOM_H * 0.5F, hd};
    nt_shape_renderer_rect(back_pos, wall_sz_fb, wall_col);

    const float side_rot[4] = {0.0F, 0.7071068F, 0.0F, 0.7071068F};
    const float wall_sz_lr[2] = {ROOM_D, ROOM_H};
    const float left_pos[3] = {-hw, ROOM_H * 0.5F, 0.0F};
    nt_shape_renderer_rect_rot(left_pos, wall_sz_lr, side_rot, wall_col);
    const float right_pos[3] = {hw, ROOM_H * 0.5F, 0.0F};
    nt_shape_renderer_rect_rot(right_pos, wall_sz_lr, side_rot, wall_col);
}

static void draw_shape(void) {
    const float pos[3] = {0.0F, ROOM_H * 0.5F, 0.0F};
    versor q;
    vec3 axis = {0.0F, 1.0F, 0.0F};
    glm_quatv(q, s_shape_yaw, axis);
    const float rot[4] = {q[0], q[1], q[2], q[3]};
    const float *col = s_shape_colors[s_shape_kind];
    const float wire_col[4] = {0.0F, 0.0F, 0.0F, 1.0F};
    switch (s_shape_kind) {
    case SHAPE_CUBE: {
        const float sz[3] = {1.6F, 1.6F, 1.6F};
        nt_shape_renderer_cube_rot(pos, sz, rot, col);
        nt_shape_renderer_cube_wire_rot(pos, sz, rot, wire_col);
        break;
    }
    case SHAPE_SPHERE:
        nt_shape_renderer_sphere_rot(pos, 1.0F, rot, col);
        nt_shape_renderer_sphere_wire_rot(pos, 1.0F, rot, wire_col);
        break;
    case SHAPE_CAPSULE:
        nt_shape_renderer_capsule_rot(pos, 0.5F, 2.0F, rot, col);
        nt_shape_renderer_capsule_wire_rot(pos, 0.5F, 2.0F, rot, wire_col);
        break;
    default:
        break;
    }
}
// #endregion

// #region hud
static void draw_hud_block(const char *text, float x, float y, float size, const float color[4]) {
    mat4 model;
    glm_mat4_identity(model);
    glm_translate(model, (vec3){x, y, 0.0F});
    nt_text_renderer_draw(text, (const float *)model, size, color, 0.0F, 0.0F);
}

static void draw_hud(float fb_w, float fb_h) {
    if (!s_font_bound) {
        return;
    }
    nt_text_renderer_set_material(s_text_material);
    nt_text_renderer_set_font(s_font);

    const float white[4] = {0.95F, 0.95F, 0.98F, 1.0F};
    const float accent[4] = {1.00F, 0.85F, 0.30F, 1.0F};
    const float dim[4] = {0.75F, 0.78F, 0.82F, 1.0F};
    (void)fb_w;

    /* Top-left: title + controls. Each line is its own draw to keep things simple
     * (nt_text_renderer doesn't auto-wrap, lines are positioned manually). */
    const float left_x = 12.0F;
    float y = fb_h - HUD_TITLE_SIZE - 4.0F;
    draw_hud_block("UI 3D DEMO  (PASS 1)", left_x, y, HUD_TITLE_SIZE, accent);
    y -= HUD_TITLE_SIZE + 6.0F;

    const char *lines[] = {
        "WASD          walk",
        "RMB drag      mouselook",
        "Q / E         yaw left / right",
        "R             reset position",
        "1 / 2 / 3     shape  (cube / sphere / capsule)",
        "4 / 5 / 6 / 7 speed  (stop / slow / medium / fast)",
        "F1            debug overlay",
        "Esc           quit",
    };
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
        draw_hud_block(lines[i], left_x, y, HUD_SIZE, white);
        y -= HUD_SIZE + 2.0F;
    }

    /* Top-right: current shape + speed status. */
    char status[64];
    (void)snprintf(status, sizeof status, "shape: %-7s   speed: %s", s_shape_labels[s_shape_kind], s_speed_labels[s_speed_kind]);
    /* Right-align approximation: place at fixed x near right edge. */
    draw_hud_block(status, fb_w - 360.0F, fb_h - HUD_SIZE - 6.0F, HUD_SIZE, accent);

    /* Bottom-left: debug overlay when toggled. */
    if (s_debug_overlay) {
        char dbg[256];
        const float yaw_deg = s_player_yaw * 57.29578F;
        const float pitch_deg = s_player_pitch * 57.29578F;
        (void)snprintf(dbg, sizeof dbg, "pos: (%+.2f, %+.2f, %+.2f)   yaw: %+.1f°   pitch: %+.1f°", (double)s_player_pos[0], (double)s_player_pos[1], (double)s_player_pos[2], (double)yaw_deg,
                       (double)pitch_deg);
        draw_hud_block(dbg, left_x, 32.0F, HUD_SIZE, dim);

        mat4 stats_model;
        glm_mat4_identity(stats_model);
        glm_translate(stats_model, (vec3){left_x, 12.0F, 0.0F});
        const float stats_color[4] = {0.8F, 0.9F, 0.8F, 1.0F};
        nt_stats_draw(s_text_material, s_font, (const float *)stats_model, HUD_SIZE - 2.0F, stats_color);
    }

    nt_text_renderer_flush();
}

static void try_bind_resources(void) {
    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        nt_font_add(s_font, s_font_resource);
        s_font_bound = true;
        nt_log_info("ui_3d_demo: font bound");
    }
}
// #endregion

// #region frame
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void frame(void) {
    nt_stats_frame_begin();
    nt_window_poll();
    nt_input_poll();
    nt_mem_scratch_reset();

    const float dt = g_nt_app.dt;

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif
    if (nt_input_key_is_pressed(NT_KEY_R)) {
        player_reset();
    }
    if (nt_input_key_is_pressed(NT_KEY_F1)) {
        s_debug_overlay = !s_debug_overlay;
    }
    if (nt_input_key_is_pressed(NT_KEY_1)) {
        s_shape_kind = SHAPE_CUBE;
    }
    if (nt_input_key_is_pressed(NT_KEY_2)) {
        s_shape_kind = SHAPE_SPHERE;
    }
    if (nt_input_key_is_pressed(NT_KEY_3)) {
        s_shape_kind = SHAPE_CAPSULE;
    }
    if (nt_input_key_is_pressed(NT_KEY_4)) {
        s_speed_kind = SPEED_STOP;
    }
    if (nt_input_key_is_pressed(NT_KEY_5)) {
        s_speed_kind = SPEED_SLOW;
    }
    if (nt_input_key_is_pressed(NT_KEY_6)) {
        s_speed_kind = SPEED_MED;
    }
    if (nt_input_key_is_pressed(NT_KEY_7)) {
        s_speed_kind = SPEED_FAST;
    }

    player_update(dt);
    s_shape_yaw += s_speed_table[s_speed_kind] * dt;

    nt_resource_step();
    nt_material_step();
    try_bind_resources();

    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);
    const float aspect = (fb_h > 0.0F) ? (fb_w / fb_h) : 1.0F;

    mat4 vp_3d;
    compute_perspective_vp(vp_3d, aspect);

    /* Frame uniforms drive the text renderer; use ortho VP so HUD anchors at screen pixels. */
    mat4 view_m;
    mat4 proj_m;
    mat4 vp_ortho;
    glm_mat4_identity(view_m);
    glm_ortho(0.0F, fb_w, 0.0F, fb_h, -1.0F, 1.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp_ortho);

    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, vp_ortho, 64);
    memcpy(uniforms.view, view_m, 64);
    memcpy(uniforms.proj, proj_m, 64);
    uniforms.time[0] = (float)nt_time_now();
    uniforms.time[1] = dt;
    uniforms.resolution[0] = fb_w;
    uniforms.resolution[1] = fb_h;
    uniforms.resolution[2] = (fb_w > 0.0F) ? (1.0F / fb_w) : 0.0F;
    uniforms.resolution[3] = (fb_h > 0.0F) ? (1.0F / fb_h) : 0.0F;
    uniforms.near_far[0] = -1.0F;
    uniforms.near_far[1] = 1.0F;

    nt_gfx_begin_frame();
    nt_gfx_begin_segment("frame");
    if (g_nt_gfx.context_restored) {
        nt_resource_invalidate(NT_ASSET_SHADER_CODE);
        nt_resource_invalidate(NT_ASSET_TEXTURE);
        nt_resource_invalidate(NT_ASSET_FONT);
        nt_gfx_destroy_buffer(s_frame_ubo);
        s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = NT_BUFFER_UNIFORM,
            .usage = NT_USAGE_DYNAMIC,
            .size = sizeof(nt_frame_uniforms_t),
            .label = "frame_uniforms",
        });
        nt_shape_renderer_restore_gpu();
        nt_text_renderer_restore_gpu();
        s_font_bound = false;
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.06F, 0.07F, 0.10F, 1.0F}, .clear_depth = 1.0F});
    nt_font_step();

    nt_gfx_update_buffer(s_frame_ubo, &uniforms, sizeof(uniforms));
    nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

    /* 3D pass: shape renderer drives its own VP, ignores frame_uniforms.view_proj. */
    const float cam_pos[3] = {s_player_pos[0], s_player_pos[1], s_player_pos[2]};
    nt_shape_renderer_set_vp((const float *)vp_3d);
    nt_shape_renderer_set_cam_pos(cam_pos);
    nt_shape_renderer_set_depth(true);
    draw_room();
    draw_shape();
    nt_shape_renderer_flush();

    /* HUD: text reads ortho VP from frame_uniforms — depth_test=false on the material. */
    const nt_material_info_t *text_info = nt_material_get_info(s_text_material);
    if (text_info && text_info->ready) {
        draw_hud(fb_w, fb_h);
    }

    nt_gfx_end_pass();
    nt_gfx_end_segment();
    nt_gfx_end_frame();
    nt_stats_frame_end();
    nt_window_swap_buffers();
}
// #endregion

// #region main
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    nt_engine_config_t config = {0};
    config.app_name = "ui_3d_demo";
    config.version = 1;
    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }

    g_nt_window.width = 1280;
    g_nt_window.height = 800;
    nt_window_init();
    nt_input_init();

    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    gfx_desc.depth = true;
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
    nt_font_init(&(nt_font_desc_t){.max_fonts = 2});

    nt_shape_renderer_init();
    nt_text_renderer_init();

    g_nt_app.target_dt = 0.0F;

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    /* Reuse ui_buttons_demo pack for slug shaders + font. */
    s_pack_id = nt_hash32_str("ui_buttons_demo");
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/ui_buttons_demo/ui_buttons_demo.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/ui_buttons_demo.ntpack");
#endif

    s_text_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_font_resource = nt_resource_request(ASSET_FONT_UI_BUTTONS_DEMO_FONT, NT_ASSET_FONT);

    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_text_vs_handle,
        .fs = s_text_fs_handle,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "ui_3d_demo_text",
    });

    s_font = nt_font_create(&(nt_font_create_desc_t){
        .curve_texture_width = 1024,
        .curve_texture_height = 512,
        .band_texture_height = 256,
        .band_count = 8,
        .measure_cache_size = 256,
    });

    nt_resource_set_activate_time_budget(0);
    nt_stats_desc_t stats_desc = nt_stats_desc_defaults();
    nt_stats_init(&stats_desc);

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_log_info("ui_3d_demo: PASS 1 — WASD walk, RMB look, Q/E yaw, 1-3 shape, 4-7 speed, F1 debug, R reset, Esc quit");

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_text_renderer_shutdown();
    nt_shape_renderer_shutdown();
    nt_font_destroy(s_font);
    nt_font_shutdown();
    nt_material_destroy(s_text_material);
    nt_material_shutdown();
    nt_stats_shutdown();
    nt_mem_scratch_shutdown();
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
// #endregion
