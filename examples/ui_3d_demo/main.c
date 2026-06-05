/* ui_3d_demo — walkable space with a central rotating primitive. Phase 1 of the 3D-UI demo
 * (no UI panels yet; keys cycle shape/speed). Validates FPS camera + shape renderer + future
 * groundwork for two world-mounted UI panels (Phase 2) and a 2D-vs-3D input bench (Phase 3).
 *
 * Scene: 20×10×20 room (floor grid + 4 walls + ceiling). One primitive rotating at the
 * room center. Shape and speed cycle through keyboard for now; UI follows.
 *
 * Controls:
 *   WASD            walk (forward/back, strafe) along yaw
 *   RMB-drag        mouselook (yaw + pitch)
 *   Q / E           yaw left / right (when RMB unavailable)
 *   R               reset player + shape rotation
 *   1 / 2 / 3       shape = cube / sphere / capsule
 *   4 / 5 / 6 / 7   speed = stop / slow / medium / fast
 *   Esc             quit (native) */

// #region includes
#include "app/nt_app.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "graphics/nt_gfx.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "math/nt_math.h"
#include "renderers/nt_shape_renderer.h"
#include "window/nt_window.h"

#include <math.h>
#include <stdint.h>

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
// #endregion

// #region state
static float s_player_pos[3] = {0.0F, EYE_HEIGHT, 7.0F};
static float s_player_yaw;
static float s_player_pitch;

static int s_shape_kind = SHAPE_CUBE;
static int s_speed_kind = SPEED_SLOW;
static float s_shape_yaw;
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
    /* Mouselook on RMB; cursor stays free so future UI clicks still work. */
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

    /* Clamp inside the room with a small inset. */
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

static void compute_vp(mat4 out_vp, float aspect) {
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

    /* Front wall (-Z) — facing inside via +Z normal. */
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

// #region frame
static void frame(void) {
    nt_window_poll();
    nt_input_poll();
    const float dt = g_nt_app.dt;

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif
    if (nt_input_key_is_pressed(NT_KEY_R)) {
        player_reset();
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

    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);
    const float aspect = (fb_h > 0.0F) ? (fb_w / fb_h) : 1.0F;

    mat4 vp;
    compute_vp(vp, aspect);

    nt_gfx_begin_frame();
    if (g_nt_gfx.context_restored) {
        nt_shape_renderer_restore_gpu();
    }
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.06F, 0.07F, 0.10F, 1.0F}, .clear_depth = 1.0F});

    const float cam_pos[3] = {s_player_pos[0], s_player_pos[1], s_player_pos[2]};
    nt_shape_renderer_set_vp((const float *)vp);
    nt_shape_renderer_set_cam_pos(cam_pos);
    nt_shape_renderer_set_depth(true);

    draw_room();
    draw_shape();
    nt_shape_renderer_flush();

    nt_gfx_end_pass();
    nt_gfx_end_frame();
    nt_window_swap_buffers();
}
// #endregion

// #region main
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
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_pipelines = 16, .max_buffers = 128, .max_textures = 16, .max_meshes = 64, .depth = true});
    nt_shape_renderer_init();

    g_nt_app.target_dt = 0.0F;

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_log_info("ui_3d_demo: PASS 1 — WASD walk, RMB look, Q/E yaw, 1-3 shape, 4-7 speed, R reset, Esc quit");

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_shape_renderer_shutdown();
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
// #endregion
