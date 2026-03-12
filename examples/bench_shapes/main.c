#include "app/nt_app.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "graphics/nt_gfx.h"
#include "input/nt_input.h"
#include "renderers/nt_shape_renderer.h"
#include "time/nt_time.h"
#include "window/nt_window.h"

#include "math/nt_math.h"

#include <stdio.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

#define BATCH 50

/* Room dimensions */
#define ROOM_W 20.0F
#define ROOM_H 8.0F
#define ROOM_D 20.0F
#define GRID_STEP 1.0F
#define MIN_DIST 2.0F

/* Shape storage */
#define MAX_SHAPES_PER_TYPE 4096
#define MAX_SHAPES (MAX_SHAPES_PER_TYPE * 8)

/* Shape types */
enum { BENCH_LINE, BENCH_RECT, BENCH_TRI, BENCH_CIRCLE, BENCH_CUBE, BENCH_SPHERE, BENCH_CYL, BENCH_CAP };

typedef struct {
    int type;
    int variant; /* 0=fill, 1=wire, 2=fill_rot/col, 3=wire_rot/col */
    float a[3];
    float b[3];
    float c[3];
    float size[3];
    float rot[4];
    float col[4];
    float col_b[4];
    float col_c[4];
} bench_shape_t;

static bench_shape_t s_shapes[MAX_SHAPES];
static int s_shape_count;

static nt_accumulator_t s_acc;
static int s_mul = 1;

/* timing stats */
static float s_dt_max;
static float s_dt_sum;
static int s_dt_count;
static float s_log_timer;
static float s_render_max;
static float s_render_sum;

/* FPS camera */
static float s_cam_pos[3];
static float s_cam_yaw;
static float s_cam_pitch;
#define MOVE_SPEED 5.0F
#define MOUSE_SENS 0.003F

/* ---- deterministic pseudo-random ---- */

static uint32_t hash_int(int i, int salt) {
    uint32_t h = ((uint32_t)i * 2654435761U) + ((uint32_t)salt * 2246822519U);
    h ^= h >> 16;
    h *= 0x45d9f3bU;
    return h;
}

static float rand_range(int i, int salt, float lo, float hi) {
    uint32_t h = hash_int(i, salt);
    return lo + (((float)(h & 0xFFFFU) / 65535.0F) * (hi - lo));
}

static void rand_pos(int i, int salt, float out[3]) {
    for (int attempt = 0; attempt < 8; attempt++) {
        int s = salt + (attempt * 100);
        out[0] = rand_range(i, s, -ROOM_W * 0.45F, ROOM_W * 0.45F);
        out[1] = rand_range(i, s + 1, 0.3F, ROOM_H - 0.3F);
        out[2] = rand_range(i, s + 2, -ROOM_D * 0.45F, ROOM_D * 0.45F);
        float dy = out[1] - (ROOM_H * 0.5F);
        float d2 = (out[0] * out[0]) + (dy * dy) + (out[2] * out[2]);
        if (d2 > MIN_DIST * MIN_DIST) {
            return;
        }
    }
}

static void rand_rot(int i, int salt, float rot[4]) {
    float angle = rand_range(i, salt, 0.0F, 2.0F * NT_PI);
    rot[0] = 0.0F;
    rot[1] = sinf(angle * 0.5F);
    rot[2] = 0.0F;
    rot[3] = cosf(angle * 0.5F);
}

static void set4(float dst[4], float r, float g, float b, float a2) {
    dst[0] = r;
    dst[1] = g;
    dst[2] = b;
    dst[3] = a2;
}

/* ---- Build shape array (runs once on start + each F press) ---- */

static void rebuild_shapes(void) {
    int n = s_mul * BATCH;
    s_shape_count = 0;

    for (int i = 0; i < n && s_shape_count + 8 <= MAX_SHAPES; i++) {
        int v = i & 3;
        bench_shape_t *s;

        /* Line */
        s = &s_shapes[s_shape_count++];
        s->type = BENCH_LINE;
        s->variant = (i & 1) ? 0 : 1;
        rand_pos(i, 0, s->a);
        s->b[0] = s->a[0] + 0.5F;
        s->b[1] = s->a[1] + 0.5F;
        s->b[2] = s->a[2];
        set4(s->col, 1.0F, 0.3F, 0.2F, 1.0F);
        set4(s->col_b, 0.3F, 0.5F, 1.0F, 1.0F);

        /* Rect */
        s = &s_shapes[s_shape_count++];
        s->type = BENCH_RECT;
        s->variant = v;
        rand_pos(i, 1000, s->a);
        s->size[0] = 0.4F;
        s->size[1] = 0.4F;
        rand_rot(i, 1000, s->rot);
        set4(s->col, 0.2F, 1.0F, 0.3F, 1.0F);

        /* Triangle */
        s = &s_shapes[s_shape_count++];
        s->type = BENCH_TRI;
        s->variant = v;
        {
            float tc[3];
            rand_pos(i, 2000, tc);
            s->a[0] = tc[0];
            s->a[1] = tc[1] + 0.3F;
            s->a[2] = tc[2];
            s->b[0] = tc[0] - 0.25F;
            s->b[1] = tc[1] - 0.2F;
            s->b[2] = tc[2];
            s->c[0] = tc[0] + 0.25F;
            s->c[1] = tc[1] - 0.2F;
            s->c[2] = tc[2];
        }
        set4(s->col, 0.3F, 0.5F, 1.0F, 1.0F);
        set4(s->col_b, 0.2F, 1.0F, 0.3F, 1.0F);
        set4(s->col_c, 1.0F, 0.3F, 0.2F, 1.0F);

        /* Circle */
        s = &s_shapes[s_shape_count++];
        s->type = BENCH_CIRCLE;
        s->variant = v;
        rand_pos(i, 3000, s->a);
        s->size[0] = 0.3F;
        rand_rot(i, 3000, s->rot);
        set4(s->col, 1.0F, 0.9F, 0.2F, 1.0F);

        /* Cube */
        s = &s_shapes[s_shape_count++];
        s->type = BENCH_CUBE;
        s->variant = v;
        rand_pos(i, 4000, s->a);
        s->size[0] = 0.3F;
        s->size[1] = 0.3F;
        s->size[2] = 0.3F;
        rand_rot(i, 4000, s->rot);
        set4(s->col, 0.2F, 0.9F, 0.9F, 1.0F);

        /* Sphere */
        s = &s_shapes[s_shape_count++];
        s->type = BENCH_SPHERE;
        s->variant = (i & 1) ? 0 : 1;
        rand_pos(i, 5000, s->a);
        s->size[0] = 0.2F;
        set4(s->col, 0.7F, 0.3F, 1.0F, 1.0F);

        /* Cylinder */
        s = &s_shapes[s_shape_count++];
        s->type = BENCH_CYL;
        s->variant = v;
        rand_pos(i, 6000, s->a);
        s->size[0] = 0.15F;
        s->size[1] = 0.5F;
        rand_rot(i, 6000, s->rot);
        set4(s->col, 1.0F, 0.5F, 0.1F, 1.0F);

        /* Capsule */
        s = &s_shapes[s_shape_count++];
        s->type = BENCH_CAP;
        s->variant = v;
        rand_pos(i, 7000, s->a);
        s->size[0] = 0.1F;
        s->size[1] = 0.4F;
        rand_rot(i, 7000, s->rot);
        set4(s->col, 0.9F, 0.9F, 0.9F, 1.0F);
    }
}

/* ---- Dispatch one shape to the renderer ---- */

static void dispatch_shape(const bench_shape_t *s) {
    switch (s->type) {
    case BENCH_LINE:
        nt_shape_renderer_line(s->a, s->b, s->col);
        break;
    case BENCH_RECT:
        switch (s->variant) {
        case 0:
            nt_shape_renderer_rect(s->a, s->size, s->col);
            break;
        case 1:
            nt_shape_renderer_rect_wire(s->a, s->size, s->col);
            break;
        case 2:
            nt_shape_renderer_rect_rot(s->a, s->size, s->rot, s->col);
            break;
        case 3:
            nt_shape_renderer_rect_wire_rot(s->a, s->size, s->rot, s->col);
            break;
        default:
            break;
        }
        break;
    case BENCH_TRI:
        switch (s->variant) {
        case 0:
            nt_shape_renderer_triangle(s->a, s->b, s->c, s->col);
            break;
        case 1:
            nt_shape_renderer_triangle_wire(s->a, s->b, s->c, s->col);
            break;
        case 2:
            nt_shape_renderer_triangle_col(s->a, s->b, s->c, s->col, s->col_b, s->col_c);
            break;
        case 3:
            nt_shape_renderer_triangle_wire(s->a, s->b, s->c, s->col);
            break;
        default:
            break;
        }
        break;
    case BENCH_CIRCLE:
        switch (s->variant) {
        case 0:
            nt_shape_renderer_circle(s->a, s->size[0], s->col);
            break;
        case 1:
            nt_shape_renderer_circle_wire(s->a, s->size[0], s->col);
            break;
        case 2:
            nt_shape_renderer_circle_rot(s->a, s->size[0], s->rot, s->col);
            break;
        case 3:
            nt_shape_renderer_circle_wire_rot(s->a, s->size[0], s->rot, s->col);
            break;
        default:
            break;
        }
        break;
    case BENCH_CUBE:
        switch (s->variant) {
        case 0:
            nt_shape_renderer_cube(s->a, s->size, s->col);
            break;
        case 1:
            nt_shape_renderer_cube_wire(s->a, s->size, s->col);
            break;
        case 2:
            nt_shape_renderer_cube_rot(s->a, s->size, s->rot, s->col);
            break;
        case 3:
            nt_shape_renderer_cube_wire_rot(s->a, s->size, s->rot, s->col);
            break;
        default:
            break;
        }
        break;
    case BENCH_SPHERE:
        if (s->variant == 0) {
            nt_shape_renderer_sphere(s->a, s->size[0], s->col);
        } else {
            nt_shape_renderer_sphere_wire(s->a, s->size[0], s->col);
        }
        break;
    case BENCH_CYL:
        switch (s->variant) {
        case 0:
            nt_shape_renderer_cylinder(s->a, s->size[0], s->size[1], s->col);
            break;
        case 1:
            nt_shape_renderer_cylinder_wire(s->a, s->size[0], s->size[1], s->col);
            break;
        case 2:
            nt_shape_renderer_cylinder_rot(s->a, s->size[0], s->size[1], s->rot, s->col);
            break;
        case 3:
            nt_shape_renderer_cylinder_wire_rot(s->a, s->size[0], s->size[1], s->rot, s->col);
            break;
        default:
            break;
        }
        break;
    case BENCH_CAP:
        switch (s->variant) {
        case 0:
            nt_shape_renderer_capsule(s->a, s->size[0], s->size[1], s->col);
            break;
        case 1:
            nt_shape_renderer_capsule_wire(s->a, s->size[0], s->size[1], s->col);
            break;
        case 2:
            nt_shape_renderer_capsule_rot(s->a, s->size[0], s->size[1], s->rot, s->col);
            break;
        case 3:
            nt_shape_renderer_capsule_wire_rot(s->a, s->size[0], s->size[1], s->rot, s->col);
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

/* ---- stroke font: draw a letter on the floor (XZ plane, y=0.002) ---- */

static void draw_letter(float ox, float oz, float scale, const float *strokes, int count, const float color[4]) {
    for (int i = 0; i < count; i++) {
        int si = i * 4;
        float x0 = ox + (strokes[si] * scale);
        float z0 = oz - (strokes[si + 1] * scale);
        float x1 = ox + (strokes[si + 2] * scale);
        float z1 = oz - (strokes[si + 3] * scale);
        float a[3] = {x0, 0.002F, z0};
        float b[3] = {x1, 0.002F, z1};
        nt_shape_renderer_line(a, b, color);
    }
}

/* clang-format off */
static const float s_N[] = {0,1.4F, 0,0, 0,1.4F, 1,0, 1,0, 1,1.4F};
static const float s_E[] = {0,0, 0,1.4F, 0,1.4F, 1,1.4F, 0,0.7F, 0.8F,0.7F, 0,0, 1,0};
static const float s_O[] = {0,0, 1,0, 1,0, 1,1.4F, 1,1.4F, 0,1.4F, 0,1.4F, 0,0};
static const float s_T[] = {0,1.4F, 1,1.4F, 0.5F,1.4F, 0.5F,0};
static const float s_L[] = {0,1.4F, 0,0, 0,0, 1,0};
static const float s_I[] = {0.5F,1.4F, 0.5F,0};
static const float s_S[] = {1,1.4F, 0,1.4F, 0,1.4F, 0,0.7F, 0,0.7F, 1,0.7F, 1,0.7F, 1,0, 1,0, 0,0};
static const float s_G[] = {1,1.4F, 0,1.4F, 0,1.4F, 0,0, 0,0, 1,0, 1,0, 1,0.7F, 1,0.7F, 0.5F,0.7F};
/* clang-format on */

static void draw_floor_text(void) {
    float color[4] = {0.6F, 0.7F, 1.0F, 1.0F};
    nt_shape_renderer_set_line_width(0.05F);
    float scale = 1.2F;
    float gap = 0.3F;
    float cell_w = scale + gap;

    float line1_w = (8.0F * cell_w) - gap;
    float line2_w = (6.0F * cell_w) - gap;
    float x1_start = -line1_w * 0.5F;
    float x2_start = -line2_w * 0.5F;
    float z_line1 = -1.0F;
    float z_line2 = 1.0F;

    struct {
        const float *strokes;
        int count;
    } word1[] = {
        {s_N, 3}, {s_E, 4}, {s_O, 4}, {s_T, 2}, {s_O, 4}, {s_L, 2}, {s_I, 1}, {s_S, 5},
    };
    for (int i = 0; i < 8; i++) {
        draw_letter(x1_start + ((float)i * cell_w), z_line1, scale, word1[i].strokes, word1[i].count, color);
    }

    struct {
        const float *strokes;
        int count;
    } word2[] = {
        {s_E, 4}, {s_N, 3}, {s_G, 5}, {s_I, 1}, {s_N, 3}, {s_E, 4},
    };
    for (int i = 0; i < 6; i++) {
        draw_letter(x2_start + ((float)i * cell_w), z_line2, scale, word2[i].strokes, word2[i].count, color);
    }
    nt_shape_renderer_set_line_width(0.02F);
}

/* ---- draw room ---- */

static void draw_room(void) {
    float hw = ROOM_W * 0.5F;
    float hd = ROOM_D * 0.5F;

    float floor_col[4] = {0.15F, 0.15F, 0.18F, 1.0F};
    float floor_pos[3] = {0, 0, 0};
    float floor_sz[2] = {ROOM_W, ROOM_D};
    float floor_rot[4] = {0.7071068F, 0, 0, 0.7071068F};
    nt_shape_renderer_rect_rot(floor_pos, floor_sz, floor_rot, floor_col);

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

    float ceil_col[4] = {0.12F, 0.12F, 0.20F, 1.0F};
    float ceil_pos[3] = {0, ROOM_H, 0};
    nt_shape_renderer_rect_rot(ceil_pos, floor_sz, floor_rot, ceil_col);

    float wall_col[4] = {0.18F, 0.16F, 0.14F, 1.0F};
    {
        float pos[3] = {0, ROOM_H * 0.5F, -hd};
        float sz[2] = {ROOM_W, ROOM_H};
        nt_shape_renderer_rect(pos, sz, wall_col);
    }
    {
        float pos[3] = {0, ROOM_H * 0.5F, hd};
        float sz[2] = {ROOM_W, ROOM_H};
        nt_shape_renderer_rect(pos, sz, wall_col);
    }
    {
        float pos[3] = {-hw, ROOM_H * 0.5F, 0};
        float sz[2] = {ROOM_D, ROOM_H};
        float rot[4] = {0, 0.7071068F, 0, 0.7071068F};
        nt_shape_renderer_rect_rot(pos, sz, rot, wall_col);
    }
    {
        float pos[3] = {hw, ROOM_H * 0.5F, 0};
        float sz[2] = {ROOM_D, ROOM_H};
        float rot[4] = {0, 0.7071068F, 0, 0.7071068F};
        nt_shape_renderer_rect_rot(pos, sz, rot, wall_col);
    }
}

/* ---- Draw pre-built shapes ---- */

static void draw_shapes(void) {
    for (int i = 0; i < s_shape_count; i++) {
        dispatch_shape(&s_shapes[i]);
    }
}

/* ---- frame callback ---- */

static void frame(void) {
    nt_window_poll();
    nt_input_poll();
    float dt = g_nt_app.dt;
    nt_accumulator_update(&s_acc, dt);

    if (nt_input_key_is_pressed(NT_KEY_F)) {
        s_mul++;
        rebuild_shapes();
        printf("[bench] +50 each => %d per type, %d total shapes\n", s_mul * BATCH, s_mul * BATCH * 8);
    }

    if (dt > s_dt_max) {
        s_dt_max = dt;
    }
    s_dt_sum += dt;
    s_dt_count++;
    s_log_timer += dt;

    if (s_log_timer >= 1.0F) {
        float avg = s_dt_sum / (float)s_dt_count;
        float render_avg = s_render_sum / (float)s_dt_count;
        nt_gfx_frame_stats_t stats = g_nt_gfx.frame_stats;
        printf("[bench] shapes=%-6d avg=%.2fms  max=%.2fms  render=%.2f/%.2fms  fps=%.0f  dc=%u  inst=%u  verts=%u  idx=%u\n", s_shape_count, (double)(avg * 1000.0F), (double)(s_dt_max * 1000.0F),
               (double)render_avg, (double)s_render_max, (double)(1.0F / avg), stats.draw_calls, stats.instances, stats.vertices, stats.indices);
        s_dt_max = 0.0F;
        s_dt_sum = 0.0F;
        s_dt_count = 0;
        s_render_max = 0.0F;
        s_render_sum = 0.0F;
        s_log_timer -= 1.0F;
    }

    /* FPS camera */
    if (nt_input_mouse_is_down(NT_BUTTON_LEFT) || nt_input_mouse_is_down(NT_BUTTON_RIGHT)) {
        s_cam_yaw -= g_nt_input.pointers[0].dx * MOUSE_SENS;
        s_cam_pitch -= g_nt_input.pointers[0].dy * MOUSE_SENS;
        if (s_cam_pitch > 1.5F) {
            s_cam_pitch = 1.5F;
        }
        if (s_cam_pitch < -1.5F) {
            s_cam_pitch = -1.5F;
        }
    }

    float cp = cosf(s_cam_pitch);
    float sp = sinf(s_cam_pitch);
    float fwd_x = sinf(s_cam_yaw) * cp;
    float fwd_y = sp;
    float fwd_z = cosf(s_cam_yaw) * cp;
    float rgt_x = -cosf(s_cam_yaw);
    float rgt_z = sinf(s_cam_yaw);

    float speed = MOVE_SPEED * dt;
    if (nt_input_key_is_down(NT_KEY_W)) {
        s_cam_pos[0] += fwd_x * speed;
        s_cam_pos[1] += fwd_y * speed;
        s_cam_pos[2] += fwd_z * speed;
    }
    if (nt_input_key_is_down(NT_KEY_S)) {
        s_cam_pos[0] -= fwd_x * speed;
        s_cam_pos[1] -= fwd_y * speed;
        s_cam_pos[2] -= fwd_z * speed;
    }
    if (nt_input_key_is_down(NT_KEY_D)) {
        s_cam_pos[0] += rgt_x * speed;
        s_cam_pos[2] += rgt_z * speed;
    }
    if (nt_input_key_is_down(NT_KEY_A)) {
        s_cam_pos[0] -= rgt_x * speed;
        s_cam_pos[2] -= rgt_z * speed;
    }
    if (nt_input_key_is_down(NT_KEY_SPACE)) {
        s_cam_pos[1] += speed;
    }
    if (nt_input_key_is_down(NT_KEY_LSHIFT)) {
        s_cam_pos[1] -= speed;
    }

    vec3 eye = {s_cam_pos[0], s_cam_pos[1], s_cam_pos[2]};
    vec3 center = {s_cam_pos[0] + fwd_x, s_cam_pos[1] + fwd_y, s_cam_pos[2] + fwd_z};
    vec3 up = {0, 1, 0};

    float aspect = 1.0F;
    if (g_nt_window.fb_height > 0) {
        aspect = (float)g_nt_window.fb_width / (float)g_nt_window.fb_height;
    }

    mat4 view;
    mat4 proj;
    mat4 vp;
    glm_lookat(eye, center, up, view);
    glm_perspective(glm_rad(75.0F), aspect, 0.1F, 50.0F, proj);
    glm_mat4_mul(proj, view, vp);

    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.05F, 0.05F, 0.08F, 1.0F}, .clear_depth = 1.0F});

    nt_shape_renderer_set_vp((float *)vp);
    nt_shape_renderer_set_cam_pos(s_cam_pos);
    nt_shape_renderer_set_depth(true);

    double t_render_start = nt_time_now();
    draw_room();
    draw_floor_text();
    draw_shapes();
    nt_shape_renderer_flush();
    double t_render_end = nt_time_now();
    float render_ms = (float)(t_render_end - t_render_start) * 1000.0F;
    s_render_sum += render_ms;
    if (render_ms > s_render_max) {
        s_render_max = render_ms;
    }

    nt_gfx_end_pass();
    nt_gfx_end_frame();

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif
}

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "bench_shapes";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        printf("Failed to initialize engine: error %d\n", result);
        return 1;
    }

    nt_window_init();
    nt_input_init();
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_pipelines = 16, .max_buffers = 128, .depth = true});
    nt_shape_renderer_init();

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    s_cam_pos[0] = 0.0F;
    s_cam_pos[1] = ROOM_H * 0.5F;
    s_cam_pos[2] = 0.0F;

    rebuild_shapes();

    printf("Shape Renderer Benchmark\n");
    printf("  WASD = move | Space/Shift = up/down | Mouse+LMB/RMB = look\n");
    printf("  F = +50 shapes per type | ESC = quit\n");
    printf("  Starting: %d per type, %d total\n", BATCH, BATCH * 8);

    nt_accumulator_init(&s_acc, 1.0F / 60.0F, 4);
    g_nt_app.target_dt = 0;

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_shape_renderer_shutdown();
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
