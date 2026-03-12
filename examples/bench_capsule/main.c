/* Capsule instancing benchmark.
   Tests GPU-side capsule via hemisphere-tagged vertices vs CPU batch.

   Template: unit capsule (radius=1, body_half=0). Vertices are vec4:
     xyz = unit sphere position, w = hemisphere sign (+1 top, -1 bottom).
   Two equator rings: one tagged +1 (top edge of cylinder), one tagged -1 (bottom edge).

   Shader: p = a_pos.xyz * radius;  p.y += a_pos.w * body_half;
   No branching — just a multiply + add.

   Instance data (44 bytes, same as shape_renderer):
     center[3], scale[3] (x=radius, y=body_half, z=unused), rot[4], color_u8[4].
*/

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

#define SEGS 32
#define HALF_RINGS (SEGS / 4)                         /* 8 */
#define TOTAL_SECTIONS (2 * HALF_RINGS + 1)           /* 17 */
#define TPL_VERTS ((TOTAL_SECTIONS + 1) * (SEGS + 1)) /* 18 * 33 = 594 */
#define TPL_INDICES (TOTAL_SECTIONS * SEGS * 6)       /* 17 * 32 * 6 = 3264 */

#define MAX_INSTANCES 4096
#define BATCH_START 500

/* ---- Instance struct (matches shape_renderer layout) ---- */

typedef struct {
    float center[3];
    float scale[3]; /* x=radius, y=body_half, z=unused */
    float rot[4];
    uint8_t color[4];
} capsule_instance_t;

_Static_assert(sizeof(capsule_instance_t) == 44, "instance size");

/* ---- Capsule instancing vertex shader ---- */

static const char *s_cap_vs = "layout(location = 0) in vec4 a_pos_tag;\n" /* xyz=unit sphere, w=hemisphere sign */
                              "layout(location = 1) in vec3 i_center;\n"
                              "layout(location = 2) in vec3 i_scale;\n" /* x=radius, y=body_half */
                              "layout(location = 3) in vec4 i_rot;\n"
                              "layout(location = 4) in vec4 i_color;\n"
                              "uniform mat4 u_vp;\n"
                              "out vec4 v_color;\n"
                              "void main() {\n"
                              "    float radius = i_scale.x;\n"
                              "    float body_half = i_scale.y;\n"
                              "    vec3 p = a_pos_tag.xyz * radius;\n"
                              "    p.y += a_pos_tag.w * body_half;\n"
                              "    vec3 t = 2.0 * cross(i_rot.xyz, p);\n"
                              "    vec3 r = p + i_rot.w * t + cross(i_rot.xyz, t);\n"
                              "    v_color = i_color;\n"
                              "    gl_Position = u_vp * vec4(i_center + r, 1.0);\n"
                              "}\n";

static const char *s_cap_fs = "in vec4 v_color;\n"
                              "out vec4 frag_color;\n"
                              "void main() {\n"
                              "    frag_color = v_color;\n"
                              "}\n";

/* ---- Module state ---- */

static struct {
    /* Instanced capsule resources */
    nt_shader_t vs;
    nt_shader_t fs;
    nt_pipeline_t pip;
    nt_buffer_t tpl_vbo;
    nt_buffer_t tpl_ibo;
    nt_buffer_t inst_buf;
    uint32_t tpl_num_vertices;
    uint32_t tpl_num_indices;

    /* Pre-built instance data */
    capsule_instance_t instances[MAX_INSTANCES];
    int count;

    /* Benchmark state */
    int mode; /* 0 = instanced, 1 = CPU batch (shape_renderer) */
    nt_accumulator_t acc;
    float dt_sum;
    float dt_max;
    float render_sum;
    float render_max;
    int dt_count;
    float log_timer;

    /* Camera */
    float cam_pos[3];
    float cam_yaw;
    float cam_pitch;

    /* Trig LUT */
    float sin_lut[SEGS + 1];
    float cos_lut[SEGS + 1];
} s_bench;

/* ---- Build capsule template mesh (vec4 vertices with hemisphere tag) ---- */

static void build_template(void) {
    float verts[TPL_VERTS * 4]; /* vec4 per vertex */
    uint16_t indices[TPL_INDICES];

    /* Build trig LUT */
    float inv = 2.0F * NT_PI / (float)SEGS;
    for (int i = 0; i <= SEGS; i++) {
        float theta = inv * (float)i;
        s_bench.sin_lut[i] = sinf(theta);
        s_bench.cos_lut[i] = cosf(theta);
    }

    /* Unit capsule template: radius=1, body_half=0 (it's a sphere).
       Rows 0..HALF_RINGS: top hemisphere (w=+1)
       Row HALF_RINGS: top equator (w=+1)
       Row HALF_RINGS+1: bottom equator (w=-1)
       Rows HALF_RINGS+1..TOTAL_SECTIONS: bottom hemisphere (w=-1) */
    int vi = 0;
    for (int row = 0; row <= TOTAL_SECTIONS; row++) {
        float tag;
        float phi;

        if (row <= HALF_RINGS) {
            /* Top hemisphere: pole (row=0) to equator (row=HALF_RINGS) */
            tag = 1.0F;
            phi = NT_PI * 0.5F * (float)(HALF_RINGS - row) / (float)HALF_RINGS; /* PI/2 -> 0 */
        } else {
            /* Bottom hemisphere: equator (row=HALF_RINGS+1) to pole */
            tag = -1.0F;
            int bot_row = row - HALF_RINGS - 1;                      /* 0..HALF_RINGS */
            phi = NT_PI * 0.5F * (float)bot_row / (float)HALF_RINGS; /* 0 -> PI/2 */
        }

        float sp = (row <= HALF_RINGS) ? sinf(phi) : -sinf(phi);
        float cp = cosf(phi);

        for (int seg = 0; seg <= SEGS; seg++) {
            /* Unit sphere position */
            verts[vi++] = cp * s_bench.cos_lut[seg]; /* x */
            verts[vi++] = sp;                        /* y */
            verts[vi++] = cp * s_bench.sin_lut[seg]; /* z */
            verts[vi++] = tag;                       /* w: hemisphere sign */
        }
    }

    /* Indices: same as any lat/lon mesh */
    int ii = 0;
    for (int row = 0; row < TOTAL_SECTIONS; row++) {
        for (int seg = 0; seg < SEGS; seg++) {
            uint16_t a = (uint16_t)(row * (SEGS + 1) + seg);
            uint16_t b = (uint16_t)(a + 1);
            uint16_t c = (uint16_t)(a + SEGS + 1);
            uint16_t d = (uint16_t)(c + 1);
            indices[ii++] = a;
            indices[ii++] = c;
            indices[ii++] = b;
            indices[ii++] = b;
            indices[ii++] = c;
            indices[ii++] = d;
        }
    }

    s_bench.tpl_num_vertices = TPL_VERTS;
    s_bench.tpl_num_indices = TPL_INDICES;
    s_bench.tpl_vbo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = verts,
        .size = sizeof(verts),
        .label = "cap_tpl_vbo",
    });
    s_bench.tpl_ibo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_INDEX,
        .usage = NT_USAGE_IMMUTABLE,
        .data = indices,
        .size = sizeof(indices),
        .label = "cap_tpl_ibo",
    });
}

/* ---- Build pipeline ---- */

static void build_pipeline(void) {
    s_bench.vs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_VERTEX, .source = s_cap_vs, .label = "cap_inst_vs"});
    s_bench.fs = nt_gfx_make_shader(&(nt_shader_desc_t){.type = NT_SHADER_FRAGMENT, .source = s_cap_fs, .label = "cap_inst_fs"});

    s_bench.pip = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
        .vertex_shader = s_bench.vs,
        .fragment_shader = s_bench.fs,
        .layout =
            {
                .attr_count = 1,
                .stride = (uint16_t)(4 * sizeof(float)), /* vec4 */
                .attrs =
                    {
                        {.location = 0, .format = NT_FORMAT_FLOAT4, .offset = 0},
                    },
            },
        .instance_layout =
            {
                .attr_count = 4,
                .stride = (uint16_t)sizeof(capsule_instance_t),
                .attrs =
                    {
                        {.location = 1, .format = NT_FORMAT_FLOAT3, .offset = 0},   /* center */
                        {.location = 2, .format = NT_FORMAT_FLOAT3, .offset = 12},  /* scale */
                        {.location = 3, .format = NT_FORMAT_FLOAT4, .offset = 24},  /* rot */
                        {.location = 4, .format = NT_FORMAT_UBYTE4N, .offset = 40}, /* color */
                    },
            },
        .depth_test = true,
        .depth_write = true,
        .depth_func = NT_DEPTH_LEQUAL,
        .cull_face = false,
        .label = "cap_inst_pip",
    });

    s_bench.inst_buf = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_VERTEX,
        .usage = NT_USAGE_STREAM,
        .size = MAX_INSTANCES * (uint32_t)sizeof(capsule_instance_t),
        .label = "cap_inst_buf",
    });
}

/* ---- Color packing: float [0,1] → uint8 [0,255] ---- */

static inline uint8_t float_to_u8(float v) {
    if (v <= 0.0F) {
        return 0;
    }
    if (v >= 1.0F) {
        return 255;
    }
    return (uint8_t)(v * 255.0F + 0.5F);
}

static void pack_color(uint8_t out[4], float r, float g, float b, float a) {
    out[0] = float_to_u8(r);
    out[1] = float_to_u8(g);
    out[2] = float_to_u8(b);
    out[3] = float_to_u8(a);
}

/* ---- Pseudo-random helpers ---- */

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

/* ---- Build instance data ---- */

static void rebuild_instances(void) {
    s_bench.count = BATCH_START;
    if (s_bench.count > MAX_INSTANCES) {
        s_bench.count = MAX_INSTANCES;
    }

    for (int i = 0; i < s_bench.count; i++) {
        capsule_instance_t *inst = &s_bench.instances[i];

        /* Random position in a 20x8x20 box */
        inst->center[0] = rand_range(i, 0, -9.0F, 9.0F);
        inst->center[1] = rand_range(i, 1, 0.5F, 7.5F);
        inst->center[2] = rand_range(i, 2, -9.0F, 9.0F);

        /* Random capsule params */
        float radius = rand_range(i, 3, 0.08F, 0.25F);
        float height = rand_range(i, 4, 0.3F, 0.8F);
        float body_half = (height - 2.0F * radius) * 0.5F;
        if (body_half < 0.0F) {
            body_half = 0.0F;
        }
        inst->scale[0] = radius;
        inst->scale[1] = body_half;
        inst->scale[2] = 0.0F;

        /* Random Y-axis rotation */
        float angle = rand_range(i, 5, 0.0F, 2.0F * NT_PI);
        inst->rot[0] = 0.0F;
        inst->rot[1] = sinf(angle * 0.5F);
        inst->rot[2] = 0.0F;
        inst->rot[3] = cosf(angle * 0.5F);

        /* Color based on index */
        float t = (float)i / (float)s_bench.count;
        pack_color(inst->color, 0.3F + t * 0.7F, 1.0F - t * 0.5F, 0.5F, 1.0F);
    }
}

/* ---- Draw instanced capsules ---- */

static void draw_instanced(const float vp[16]) {
    nt_gfx_update_buffer(s_bench.inst_buf, s_bench.instances, (uint32_t)s_bench.count * (uint32_t)sizeof(capsule_instance_t));
    nt_gfx_bind_pipeline(s_bench.pip);
    nt_gfx_bind_vertex_buffer(s_bench.tpl_vbo);
    nt_gfx_bind_index_buffer(s_bench.tpl_ibo);
    nt_gfx_bind_instance_buffer(s_bench.inst_buf);
    nt_gfx_set_uniform_mat4("u_vp", vp);
    nt_gfx_draw_indexed_instanced(0, s_bench.tpl_num_indices, s_bench.tpl_num_vertices, (uint32_t)s_bench.count);
}

/* ---- Draw CPU-batched capsules (via shape_renderer) ---- */

static void draw_cpu_batch(void) {
    for (int i = 0; i < s_bench.count; i++) {
        capsule_instance_t *inst = &s_bench.instances[i];
        float radius = inst->scale[0];
        float body_half = inst->scale[1];
        float height = (2.0F * radius) + (2.0F * body_half);
        float col[4] = {(float)inst->color[0] / 255.0F, (float)inst->color[1] / 255.0F, (float)inst->color[2] / 255.0F, (float)inst->color[3] / 255.0F};

        if (inst->rot[3] < 0.9999F) {
            nt_shape_renderer_capsule_rot(inst->center, radius, height, inst->rot, col);
        } else {
            nt_shape_renderer_capsule(inst->center, radius, height, col);
        }
    }
    nt_shape_renderer_flush();
}

/* ---- Frame ---- */

#define MOVE_SPEED 5.0F
#define MOUSE_SENS 0.003F

static void frame(void) {
    nt_window_poll();
    nt_input_poll();
    float dt = g_nt_app.dt;
    nt_accumulator_update(&s_bench.acc, dt);

    /* M = toggle mode */
    if (nt_input_key_is_pressed(NT_KEY_M)) {
        s_bench.mode = 1 - s_bench.mode;
        printf("[bench_capsule] mode: %s\n", s_bench.mode == 0 ? "INSTANCED" : "CPU BATCH");
    }

    /* F = +100 capsules */
    if (nt_input_key_is_pressed(NT_KEY_F)) {
        s_bench.count += 100;
        if (s_bench.count > MAX_INSTANCES) {
            s_bench.count = MAX_INSTANCES;
        }
        /* Extend instance array */
        for (int i = s_bench.count - 100; i < s_bench.count; i++) {
            capsule_instance_t *inst = &s_bench.instances[i];
            inst->center[0] = rand_range(i, 0, -9.0F, 9.0F);
            inst->center[1] = rand_range(i, 1, 0.5F, 7.5F);
            inst->center[2] = rand_range(i, 2, -9.0F, 9.0F);
            float radius = rand_range(i, 3, 0.08F, 0.25F);
            float height = rand_range(i, 4, 0.3F, 0.8F);
            float body_half = (height - 2.0F * radius) * 0.5F;
            if (body_half < 0.0F) {
                body_half = 0.0F;
            }
            inst->scale[0] = radius;
            inst->scale[1] = body_half;
            inst->scale[2] = 0.0F;
            float angle = rand_range(i, 5, 0.0F, 2.0F * NT_PI);
            inst->rot[0] = 0.0F;
            inst->rot[1] = sinf(angle * 0.5F);
            inst->rot[2] = 0.0F;
            inst->rot[3] = cosf(angle * 0.5F);
            float t = (float)i / (float)s_bench.count;
            pack_color(inst->color, 0.3F + t * 0.7F, 1.0F - t * 0.5F, 0.5F, 1.0F);
        }
        printf("[bench_capsule] count: %d\n", s_bench.count);
    }

    /* Timing */
    if (dt > s_bench.dt_max) {
        s_bench.dt_max = dt;
    }
    s_bench.dt_sum += dt;
    s_bench.dt_count++;
    s_bench.log_timer += dt;

    if (s_bench.log_timer >= 1.0F) {
        float avg = s_bench.dt_sum / (float)s_bench.dt_count;
        float render_avg = s_bench.render_sum / (float)s_bench.dt_count;
        nt_gfx_frame_stats_t stats = g_nt_gfx.frame_stats;
        uint32_t batch_dc = stats.draw_calls - stats.draw_calls_instanced;
        uint32_t tris = stats.indices / 3;
        printf("[bench_capsule] %s  n=%-5d avg=%.2fms  max=%.2fms  render=%.2f/%.2fms  fps=%.0f\n"
               "                dc=%u (batch=%u inst=%u)  obj=%u  verts=%u  tris=%u  idx=%u\n",
               s_bench.mode == 0 ? "INST" : "CPU ", s_bench.count, (double)(avg * 1000.0F), (double)(s_bench.dt_max * 1000.0F), (double)render_avg, (double)s_bench.render_max, (double)(1.0F / avg),
               stats.draw_calls, batch_dc, stats.draw_calls_instanced, stats.instances, stats.vertices, tris, stats.indices);
        s_bench.dt_max = 0.0F;
        s_bench.dt_sum = 0.0F;
        s_bench.dt_count = 0;
        s_bench.render_max = 0.0F;
        s_bench.render_sum = 0.0F;
        s_bench.log_timer -= 1.0F;
    }

    /* Camera */
    if (nt_input_mouse_is_down(NT_BUTTON_LEFT) || nt_input_mouse_is_down(NT_BUTTON_RIGHT)) {
        s_bench.cam_yaw -= g_nt_input.pointers[0].dx * MOUSE_SENS;
        s_bench.cam_pitch -= g_nt_input.pointers[0].dy * MOUSE_SENS;
        if (s_bench.cam_pitch > 1.5F) {
            s_bench.cam_pitch = 1.5F;
        }
        if (s_bench.cam_pitch < -1.5F) {
            s_bench.cam_pitch = -1.5F;
        }
    }
    float cp = cosf(s_bench.cam_pitch);
    float sp = sinf(s_bench.cam_pitch);
    float fwd_x = sinf(s_bench.cam_yaw) * cp;
    float fwd_y = sp;
    float fwd_z = cosf(s_bench.cam_yaw) * cp;
    float rgt_x = -cosf(s_bench.cam_yaw);
    float rgt_z = sinf(s_bench.cam_yaw);
    float speed = MOVE_SPEED * dt;
    if (nt_input_key_is_down(NT_KEY_W)) {
        s_bench.cam_pos[0] += fwd_x * speed;
        s_bench.cam_pos[1] += fwd_y * speed;
        s_bench.cam_pos[2] += fwd_z * speed;
    }
    if (nt_input_key_is_down(NT_KEY_S)) {
        s_bench.cam_pos[0] -= fwd_x * speed;
        s_bench.cam_pos[1] -= fwd_y * speed;
        s_bench.cam_pos[2] -= fwd_z * speed;
    }
    if (nt_input_key_is_down(NT_KEY_D)) {
        s_bench.cam_pos[0] += rgt_x * speed;
        s_bench.cam_pos[2] += rgt_z * speed;
    }
    if (nt_input_key_is_down(NT_KEY_A)) {
        s_bench.cam_pos[0] -= rgt_x * speed;
        s_bench.cam_pos[2] -= rgt_z * speed;
    }
    if (nt_input_key_is_down(NT_KEY_SPACE)) {
        s_bench.cam_pos[1] += speed;
    }
    if (nt_input_key_is_down(NT_KEY_LSHIFT)) {
        s_bench.cam_pos[1] -= speed;
    }

    vec3 eye = {s_bench.cam_pos[0], s_bench.cam_pos[1], s_bench.cam_pos[2]};
    vec3 center = {s_bench.cam_pos[0] + fwd_x, s_bench.cam_pos[1] + fwd_y, s_bench.cam_pos[2] + fwd_z};
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

    /* Render */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.05F, 0.05F, 0.08F, 1.0F}, .clear_depth = 1.0F});

    double t_start = nt_time_now();

    if (s_bench.mode == 0) {
        draw_instanced((float *)vp);
    } else {
        nt_shape_renderer_set_vp((float *)vp);
        nt_shape_renderer_set_cam_pos(s_bench.cam_pos);
        nt_shape_renderer_set_depth(true);
        draw_cpu_batch();
    }

    double t_end = nt_time_now();
    float render_ms = (float)(t_end - t_start) * 1000.0F;
    s_bench.render_sum += render_ms;
    if (render_ms > s_bench.render_max) {
        s_bench.render_max = render_ms;
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
    config.app_name = "bench_capsule";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        printf("Failed to init: %d\n", result);
        return 1;
    }

    nt_window_init();
    nt_input_init();
    nt_gfx_init(&(nt_gfx_desc_t){.max_shaders = 32, .max_pipelines = 16, .max_buffers = 128, .depth = true});
    nt_shape_renderer_init();

    build_template();
    build_pipeline();
    rebuild_instances();

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    s_bench.cam_pos[0] = 0.0F;
    s_bench.cam_pos[1] = 4.0F;
    s_bench.cam_pos[2] = 0.0F;

    printf("Capsule Instancing Benchmark\n");
    printf("  M = toggle INSTANCED / CPU BATCH\n");
    printf("  F = +100 capsules | WASD = move | Mouse+LMB = look\n");
    printf("  Starting: %d capsules, mode: INSTANCED\n", s_bench.count);

    nt_accumulator_init(&s_bench.acc, 1.0F / 60.0F, 4);
    g_nt_app.target_dt = 0;

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_shape_renderer_shutdown();
    nt_gfx_destroy_buffer(s_bench.inst_buf);
    nt_gfx_destroy_buffer(s_bench.tpl_ibo);
    nt_gfx_destroy_buffer(s_bench.tpl_vbo);
    nt_gfx_destroy_pipeline(s_bench.pip);
    nt_gfx_destroy_shader(s_bench.fs);
    nt_gfx_destroy_shader(s_bench.vs);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
