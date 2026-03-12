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

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

#define BATCH 50

static nt_accumulator_t s_acc;
static int s_mul = 1;

/* timing stats — reset every second */
static float s_dt_max;
static float s_dt_sum;
static int s_dt_count;
static float s_log_timer;
static float s_orbit;

/* ---- deterministic pseudo-random scatter ---- */

static float scatter(int i, int axis) {
    uint32_t h = (uint32_t)i * 2654435761u + (uint32_t)axis * 2246822519u;
    return ((float)(h & 0xFFFFu) / 65535.0F) * 20.0F - 10.0F;
}

/* ---- draw all shapes ---- */

static void draw_shapes(float t) {
    int n = s_mul * BATCH;

    float angle = t * 0.5F;
    float sa = sinf(angle * 0.5F);
    float ca = cosf(angle * 0.5F);
    float rot[4] = {0, sa, 0, ca};

    float red[4] = {1.0F, 0.3F, 0.2F, 1.0F};
    float grn[4] = {0.2F, 1.0F, 0.3F, 1.0F};
    float blu[4] = {0.3F, 0.5F, 1.0F, 1.0F};
    float yel[4] = {1.0F, 0.9F, 0.2F, 1.0F};
    float cyn[4] = {0.2F, 0.9F, 0.9F, 1.0F};
    float pur[4] = {0.7F, 0.3F, 1.0F, 1.0F};
    float org[4] = {1.0F, 0.5F, 0.1F, 1.0F};
    float wht[4] = {0.9F, 0.9F, 0.9F, 1.0F};

    /* Lines */
    for (int i = 0; i < n; i++) {
        float a[3] = {scatter(i, 0), scatter(i, 1), scatter(i, 2)};
        float b[3] = {a[0] + 0.5F, a[1] + 0.5F, a[2]};
        if (i & 1)
            nt_shape_renderer_line(a, b, red);
        else
            nt_shape_renderer_line_col(a, b, red, blu);
    }

    /* Rects: fill / wire / fill+rot / wire+rot */
    for (int i = 0; i < n; i++) {
        float pos[3] = {scatter(i + 1000, 0), scatter(i + 1000, 1), scatter(i + 1000, 2)};
        float sz[2] = {0.4F, 0.4F};
        switch (i & 3) {
        case 0:
            nt_shape_renderer_rect(pos, sz, grn);
            break;
        case 1:
            nt_shape_renderer_rect_wire(pos, sz, grn);
            break;
        case 2:
            nt_shape_renderer_rect_rot(pos, sz, rot, grn);
            break;
        case 3:
            nt_shape_renderer_rect_wire_rot(pos, sz, rot, grn);
            break;
        }
    }

    /* Triangles: fill / wire / per-vert / wire per-vert */
    for (int i = 0; i < n; i++) {
        float cx = scatter(i + 2000, 0);
        float cy = scatter(i + 2000, 1);
        float cz = scatter(i + 2000, 2);
        float a[3] = {cx, cy + 0.3F, cz};
        float b[3] = {cx - 0.25F, cy - 0.2F, cz};
        float c[3] = {cx + 0.25F, cy - 0.2F, cz};
        switch (i & 3) {
        case 0:
            nt_shape_renderer_triangle(a, b, c, blu);
            break;
        case 1:
            nt_shape_renderer_triangle_wire(a, b, c, blu);
            break;
        case 2:
            nt_shape_renderer_triangle_col(a, b, c, red, grn, blu);
            break;
        case 3:
            nt_shape_renderer_triangle_wire_col(a, b, c, red, grn, blu);
            break;
        }
    }

    /* Circles: fill / wire / fill+rot / wire+rot */
    for (int i = 0; i < n; i++) {
        float ctr[3] = {scatter(i + 3000, 0), scatter(i + 3000, 1), scatter(i + 3000, 2)};
        switch (i & 3) {
        case 0:
            nt_shape_renderer_circle(ctr, 0.3F, yel);
            break;
        case 1:
            nt_shape_renderer_circle_wire(ctr, 0.3F, yel);
            break;
        case 2:
            nt_shape_renderer_circle_rot(ctr, 0.3F, rot, yel);
            break;
        case 3:
            nt_shape_renderer_circle_wire_rot(ctr, 0.3F, rot, yel);
            break;
        }
    }

    /* Cubes: fill / wire / fill+rot / wire+rot */
    for (int i = 0; i < n; i++) {
        float ctr[3] = {scatter(i + 4000, 0), scatter(i + 4000, 1), scatter(i + 4000, 2)};
        float sz[3] = {0.3F, 0.3F, 0.3F};
        switch (i & 3) {
        case 0:
            nt_shape_renderer_cube(ctr, sz, cyn);
            break;
        case 1:
            nt_shape_renderer_cube_wire(ctr, sz, cyn);
            break;
        case 2:
            nt_shape_renderer_cube_rot(ctr, sz, rot, cyn);
            break;
        case 3:
            nt_shape_renderer_cube_wire_rot(ctr, sz, rot, cyn);
            break;
        }
    }

    /* Spheres: fill / wire */
    for (int i = 0; i < n; i++) {
        float ctr[3] = {scatter(i + 5000, 0), scatter(i + 5000, 1), scatter(i + 5000, 2)};
        if (i & 1)
            nt_shape_renderer_sphere(ctr, 0.2F, pur);
        else
            nt_shape_renderer_sphere_wire(ctr, 0.2F, pur);
    }

    /* Cylinders: fill / wire / fill+rot / wire+rot */
    for (int i = 0; i < n; i++) {
        float ctr[3] = {scatter(i + 6000, 0), scatter(i + 6000, 1), scatter(i + 6000, 2)};
        switch (i & 3) {
        case 0:
            nt_shape_renderer_cylinder(ctr, 0.15F, 0.5F, org);
            break;
        case 1:
            nt_shape_renderer_cylinder_wire(ctr, 0.15F, 0.5F, org);
            break;
        case 2:
            nt_shape_renderer_cylinder_rot(ctr, 0.15F, 0.5F, rot, org);
            break;
        case 3:
            nt_shape_renderer_cylinder_wire_rot(ctr, 0.15F, 0.5F, rot, org);
            break;
        }
    }

    /* Capsules: fill / wire / fill+rot / wire+rot */
    for (int i = 0; i < n; i++) {
        float ctr[3] = {scatter(i + 7000, 0), scatter(i + 7000, 1), scatter(i + 7000, 2)};
        switch (i & 3) {
        case 0:
            nt_shape_renderer_capsule(ctr, 0.1F, 0.4F, wht);
            break;
        case 1:
            nt_shape_renderer_capsule_wire(ctr, 0.1F, 0.4F, wht);
            break;
        case 2:
            nt_shape_renderer_capsule_rot(ctr, 0.1F, 0.4F, rot, wht);
            break;
        case 3:
            nt_shape_renderer_capsule_wire_rot(ctr, 0.1F, 0.4F, rot, wht);
            break;
        }
    }
}

/* ---- frame callback ---- */

static void frame(void) {
    nt_window_poll();
    nt_input_poll();
    float dt = g_nt_app.dt;
    nt_accumulator_update(&s_acc, dt);

    /* Space = +50 of each shape type */
    if (nt_input_key_is_pressed(NT_KEY_SPACE)) {
        s_mul++;
        printf("[bench] +50 each => %d per type, %d total shapes\n", s_mul * BATCH, s_mul * BATCH * 8);
    }

    /* Track timing */
    if (dt > s_dt_max) {
        s_dt_max = dt;
    }
    s_dt_sum += dt;
    s_dt_count++;
    s_log_timer += dt;

    if (s_log_timer >= 1.0F) {
        float avg = s_dt_sum / (float)s_dt_count;
        printf("[bench] shapes=%-6d avg=%.2fms  max=%.2fms  fps=%.0f\n", s_mul * BATCH * 8, (double)(avg * 1000.0F), (double)(s_dt_max * 1000.0F), (double)(1.0F / avg));
        s_dt_max = 0.0F;
        s_dt_sum = 0.0F;
        s_dt_count = 0;
        s_log_timer -= 1.0F;
    }

    /* Camera orbit */
    s_orbit += dt * 0.3F;
    float dist = 15.0F;
    float pitch = 0.4F;
    float cp = cosf(pitch);
    vec3 eye = {dist * cp * sinf(s_orbit), dist * sinf(pitch), dist * cp * cosf(s_orbit)};
    vec3 center = {0, 0, 0};
    vec3 up = {0, 1, 0};

    float aspect = 1.0F;
    if (g_nt_window.fb_height > 0) {
        aspect = (float)g_nt_window.fb_width / (float)g_nt_window.fb_height;
    }

    mat4 view, proj, vp;
    glm_lookat(eye, center, up, view);
    glm_perspective(glm_rad(60.0F), aspect, 0.1F, 100.0F, proj);
    glm_mat4_mul(proj, view, vp);

    /* Render */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.08F, 0.08F, 0.1F, 1.0F}, .clear_depth = 1.0F});

    nt_shape_renderer_set_vp((float *)vp);
    nt_shape_renderer_set_depth(true);

    draw_shapes(g_nt_app.time);

    nt_shape_renderer_flush();
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

    printf("Shape Renderer Benchmark\n");
    printf("  SPACE = +50 shapes per type | ESC = quit\n");
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
