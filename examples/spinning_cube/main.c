#include "app/nt_app.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "graphics/nt_gfx.h"
#include "input/nt_input.h"
#include "renderers/nt_shape_renderer.h"
#include "window/nt_window.h"

#include "math/nt_math.h"

#include <math.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

/* ---- Room dimensions ---- */

#define ROOM_W 20.0F
#define ROOM_H 10.0F
#define ROOM_D 20.0F
#define GRID_STEP 1.0F

/* ---- Interaction constants ---- */

#define MOUSE_SENS 0.005F
#define AUTO_SPIN_SPEED 0.5F
#define INERTIA_DECAY 0.95F
#define ZOOM_MIN 2.5F
#define ZOOM_MAX 9.0F
#define ZOOM_SPEED 0.01F
#define FOV_DEG 70.0F
#define VEL_THRESHOLD 0.0001F

/* ---- Shape types ---- */

enum { SHAPE_CUBE = 0, SHAPE_SPHERE, SHAPE_CYLINDER, SHAPE_CAPSULE, SHAPE_COUNT };

/* ---- Render modes ---- */

enum { MODE_SOLID_WIRE = 0, MODE_SOLID, MODE_WIRE, MODE_COUNT };

/* ---- State ---- */

static versor s_rotation = GLM_QUAT_IDENTITY_INIT;
static float s_vel_yaw;
static float s_vel_pitch;
static float s_cam_dist = 6.0F;
static int s_current_shape;
static int s_render_mode;
static bool s_grabbed;

/* ---- Shape colors ---- */

/* clang-format off */
static const float s_shape_colors[SHAPE_COUNT][4] = {
    {0.2F, 0.9F, 0.9F, 1.0F}, /* Cube: cyan */
    {0.7F, 0.3F, 1.0F, 1.0F}, /* Sphere: purple */
    {1.0F, 0.5F, 0.1F, 1.0F}, /* Cylinder: orange */
    {0.9F, 0.9F, 0.9F, 1.0F}, /* Capsule: white */
};
/* clang-format on */

static const float s_wire_color[4] = {0.0F, 0.0F, 0.0F, 1.0F};

/* ---- Shape position (center of room at eye level) ---- */

static const float s_shape_y = ROOM_H * 0.5F;

/* ---- draw_room ---- */

static void draw_room(void) {
    float hw = ROOM_W * 0.5F;
    float hd = ROOM_D * 0.5F;

    /* Floor: rect rotated 90 deg around X to lie flat */
    float floor_col[4] = {0.15F, 0.15F, 0.18F, 1.0F};
    float floor_pos[3] = {0, 0, 0};
    float floor_sz[2] = {ROOM_W, ROOM_D};
    float floor_rot[4] = {0.7071068F, 0, 0, 0.7071068F};
    nt_shape_renderer_rect_rot(floor_pos, floor_sz, floor_rot, floor_col);

    /* Grid lines on the floor */
    float grid_col[4] = {0.25F, 0.25F, 0.30F, 1.0F};
    int grid_nx = (int)(ROOM_W / GRID_STEP) + 1;
    int grid_nz = (int)(ROOM_D / GRID_STEP) + 1;
    for (int ix = 0; ix < grid_nx; ix++) {
        float x = -hw + ((float)ix * GRID_STEP);
        float a[3] = {x, 0.001F, -hd};
        float b[3] = {x, 0.001F, hd};
        nt_shape_renderer_line(a, b, grid_col);
    }
    for (int iz = 0; iz < grid_nz; iz++) {
        float z = -hd + ((float)iz * GRID_STEP);
        float a[3] = {-hw, 0.001F, z};
        float b[3] = {hw, 0.001F, z};
        nt_shape_renderer_line(a, b, grid_col);
    }

    /* Ceiling */
    float ceil_col[4] = {0.12F, 0.12F, 0.20F, 1.0F};
    float ceil_pos[3] = {0, ROOM_H, 0};
    nt_shape_renderer_rect_rot(ceil_pos, floor_sz, floor_rot, ceil_col);

    /* Walls */
    float wall_col[4] = {0.18F, 0.16F, 0.14F, 1.0F};

    /* Front wall (negative Z) */
    {
        float pos[3] = {0, ROOM_H * 0.5F, -hd};
        float sz[2] = {ROOM_W, ROOM_H};
        nt_shape_renderer_rect(pos, sz, wall_col);
    }
    /* Back wall (positive Z) */
    {
        float pos[3] = {0, ROOM_H * 0.5F, hd};
        float sz[2] = {ROOM_W, ROOM_H};
        nt_shape_renderer_rect(pos, sz, wall_col);
    }
    /* Left wall */
    {
        float pos[3] = {-hw, ROOM_H * 0.5F, 0};
        float sz[2] = {ROOM_D, ROOM_H};
        float rot[4] = {0, 0.7071068F, 0, 0.7071068F};
        nt_shape_renderer_rect_rot(pos, sz, rot, wall_col);
    }
    /* Right wall */
    {
        float pos[3] = {hw, ROOM_H * 0.5F, 0};
        float sz[2] = {ROOM_D, ROOM_H};
        float rot[4] = {0, 0.7071068F, 0, 0.7071068F};
        nt_shape_renderer_rect_rot(pos, sz, rot, wall_col);
    }
}

/* ---- draw_shape ---- */

static void draw_shape(void) {
    float pos[3] = {0, s_shape_y, 0};
    const float *col = s_shape_colors[s_current_shape];
    const float *wcol = s_wire_color;
    bool draw_solid = (s_render_mode == MODE_SOLID_WIRE) || (s_render_mode == MODE_SOLID);
    bool draw_wire = (s_render_mode == MODE_SOLID_WIRE) || (s_render_mode == MODE_WIRE);

    switch (s_current_shape) {
    case SHAPE_CUBE: {
        float sz[3] = {1.5F, 1.5F, 1.5F};
        if (draw_solid) {
            nt_shape_renderer_cube_rot(pos, sz, s_rotation, col);
        }
        if (draw_wire) {
            nt_shape_renderer_cube_wire_rot(pos, sz, s_rotation, wcol);
        }
        break;
    }
    case SHAPE_SPHERE: {
        float radius = 1.0F;
        if (draw_solid) {
            nt_shape_renderer_sphere_rot(pos, radius, s_rotation, col);
        }
        if (draw_wire) {
            nt_shape_renderer_sphere_wire_rot(pos, radius, s_rotation, wcol);
        }
        break;
    }
    case SHAPE_CYLINDER: {
        float radius = 0.6F;
        float height = 2.0F;
        if (draw_solid) {
            nt_shape_renderer_cylinder_rot(pos, radius, height, s_rotation, col);
        }
        if (draw_wire) {
            nt_shape_renderer_cylinder_wire_rot(pos, radius, height, s_rotation, wcol);
        }
        break;
    }
    case SHAPE_CAPSULE: {
        float radius = 0.4F;
        float height = 1.5F;
        if (draw_solid) {
            nt_shape_renderer_capsule_rot(pos, radius, height, s_rotation, col);
        }
        if (draw_wire) {
            nt_shape_renderer_capsule_wire_rot(pos, radius, height, s_rotation, wcol);
        }
        break;
    }
    default:
        break;
    }
}

/* ---- apply_rotation: compose yaw/pitch into accumulated quaternion ---- */

static void apply_rotation(float yaw, float pitch) {
    versor q_yaw;
    vec3 axis_y = {0, 1, 0};
    glm_quatv(q_yaw, yaw, axis_y);

    versor q_pitch;
    vec3 axis_x = {1, 0, 0};
    glm_quatv(q_pitch, pitch, axis_x);

    versor tmp;
    glm_quat_mul(q_yaw, s_rotation, tmp);
    glm_quat_mul(q_pitch, tmp, s_rotation);
    glm_quat_normalize(s_rotation);
}

/* ---- frame callback ---- */

static void frame(void) {
    nt_window_poll();
    float dt = g_nt_app.dt;

    /* ---- Input: shape cycling and controls ---- */

    if (nt_input_key_is_pressed(NT_KEY_A)) {
        s_current_shape = (s_current_shape + SHAPE_COUNT - 1) % SHAPE_COUNT;
    }
    if (nt_input_key_is_pressed(NT_KEY_D)) {
        s_current_shape = (s_current_shape + 1) % SHAPE_COUNT;
    }
    if (nt_input_key_is_pressed(NT_KEY_W)) {
        s_render_mode = (s_render_mode + 1) % MODE_COUNT;
    }
    if (nt_input_key_is_pressed(NT_KEY_R)) {
        glm_quat_identity(s_rotation);
        s_vel_yaw = 0;
        s_vel_pitch = 0;
    }
#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif

    /* ---- Trackball rotation ---- */

    if (nt_input_mouse_is_down(NT_BUTTON_LEFT)) {
        s_grabbed = true;
        float dx = g_nt_input.pointers[0].dx;
        float dy = g_nt_input.pointers[0].dy;
        float new_yaw = dx * MOUSE_SENS;
        float new_pitch = dy * MOUSE_SENS;
        /* Blend with previous velocity so brief pauses don't kill momentum */
        s_vel_yaw = new_yaw * 0.6F + s_vel_yaw * 0.4F;
        s_vel_pitch = new_pitch * 0.6F + s_vel_pitch * 0.4F;
        apply_rotation(s_vel_yaw, s_vel_pitch);
    } else if (fabsf(s_vel_yaw) > VEL_THRESHOLD || fabsf(s_vel_pitch) > VEL_THRESHOLD) {
        /* Inertia: apply decaying velocity */
        apply_rotation(s_vel_yaw, s_vel_pitch);
        float decay = powf(INERTIA_DECAY, dt * 60.0F);
        s_vel_yaw *= decay;
        s_vel_pitch *= decay;
        if (fabsf(s_vel_yaw) < VEL_THRESHOLD) {
            s_vel_yaw = 0;
        }
        if (fabsf(s_vel_pitch) < VEL_THRESHOLD) {
            s_vel_pitch = 0;
        }
    } else if (!s_grabbed) {
        /* Auto-spin until first grab */
        apply_rotation(AUTO_SPIN_SPEED * dt, 0);
    }

    /* ---- Scroll zoom ---- */

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

    /* ---- VP matrix construction ---- */

    float aspect = 1.0F;
    if (g_nt_window.fb_height > 0) {
        aspect = (float)g_nt_window.fb_width / (float)g_nt_window.fb_height;
    }

    vec3 eye = {0, s_shape_y + 0.5F, s_cam_dist};
    vec3 center = {0, s_shape_y, 0};
    vec3 up = {0, 1, 0};

    mat4 view;
    mat4 proj;
    mat4 vp;
    glm_lookat(eye, center, up, view);
    glm_perspective(glm_rad(FOV_DEG), aspect, 0.1F, 50.0F, proj);
    glm_mat4_mul(proj, view, vp);

    /* ---- Render ---- */

    float cam_pos[3] = {eye[0], eye[1], eye[2]};

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.05F, 0.05F, 0.08F, 1.0F}, .clear_depth = 1.0F});

    nt_shape_renderer_set_vp((float *)vp);
    nt_shape_renderer_set_cam_pos(cam_pos);
    nt_shape_renderer_set_depth(true);

    draw_room();
    draw_shape();

    nt_shape_renderer_flush();
    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_window_swap_buffers();
}

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "spinning_cube";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        return 1;
    }

    g_nt_window.width = 800;
    g_nt_window.height = 600;
    nt_window_init();
    nt_input_init();
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_pipelines = 16, .max_buffers = 128, .depth = true});
    nt_shape_renderer_init();

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    /* Initialize state */
    glm_quat_identity(s_rotation);
    s_vel_yaw = 0;
    s_vel_pitch = 0;
    s_cam_dist = 6.0F;
    s_current_shape = SHAPE_CUBE;
    s_render_mode = MODE_SOLID_WIRE;
    s_grabbed = false;

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
