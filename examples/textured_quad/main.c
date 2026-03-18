/*
 * Textured Cube Demo — Neotolis Engine
 *
 * Full asset pipeline demo: mesh, shaders, and textures all from .ntpack packs.
 *
 * Packs:
 *   base.ntpack         — cube mesh + shaders (loaded on start)
 *   lenna_pixel.ntpack  — 8x8 pixel art lenna
 *   lenna_hires.ntpack  — full resolution lenna
 *
 * Controls:
 *   SPACE — cycle: load pixel → load hires → unload both
 *   ENTER — swap texture pack priorities
 *   ESC   — quit (native only)
 *
 * Build packs first:  build_tq_packs  (from project root)
 */

#include "app/nt_app.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "resource/nt_resource.h"
#include "time/nt_time.h"
#include "window/nt_window.h"

#include "math/nt_math.h"
#include "nt_pack_format.h"

#include <stdint.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

/* ---- Fallback 4x4 checkerboard (shown when no texture pack loaded) ---- */

/* clang-format off */
static const uint8_t s_checker_4x4[4 * 4 * 4] = {
    255,255,255,255,  80,80,80,255,    255,255,255,255,  80,80,80,255,
    80,80,80,255,     255,255,255,255, 80,80,80,255,     255,255,255,255,
    255,255,255,255,  80,80,80,255,    255,255,255,255,  80,80,80,255,
    80,80,80,255,     255,255,255,255, 80,80,80,255,     255,255,255,255,
};
/* clang-format on */

/* ---- GFX handles ---- */

static nt_pipeline_t s_pipeline;
static nt_texture_t s_fallback_texture;

/* ---- Resource handles ---- */

static uint32_t s_base_pack_id;
static uint32_t s_pixel_pack_id;
static uint32_t s_hires_pack_id;

static nt_resource_t s_mesh_handle;
static nt_resource_t s_vs_handle;
static nt_resource_t s_fs_handle;
static nt_resource_t s_lenna_handle;

static int16_t s_pixel_prio = 10;
static int16_t s_hires_prio = 20;

/* ---- State ---- */

enum { STATE_EMPTY = 0, STATE_PIXEL = 1, STATE_BOTH = 2 };
static int s_load_state = STATE_EMPTY;
static bool s_pipeline_ready;
static bool s_base_dumped;
static bool s_pixel_dumped;
static bool s_hires_dumped;

/* ---- Status print: show current pack priorities ---- */

static void print_status(void) {
    char buf[256];
    (void)snprintf(buf, sizeof(buf), "  base   prio=100  state=%d", (int)nt_resource_pack_state(s_base_pack_id));
    nt_log_info(buf);
    if (s_load_state >= STATE_PIXEL) {
        (void)snprintf(buf, sizeof(buf), "  pixel  prio=%d  state=%d", (int)s_pixel_prio, (int)nt_resource_pack_state(s_pixel_pack_id));
        nt_log_info(buf);
    }
    if (s_load_state >= STATE_BOTH) {
        (void)snprintf(buf, sizeof(buf), "  hires  prio=%d  state=%d", (int)s_hires_prio, (int)nt_resource_pack_state(s_hires_pack_id));
        nt_log_info(buf);
    }
    if (s_load_state == STATE_EMPTY) {
        nt_log_info("  Texture: (fallback)");
    } else if (s_load_state == STATE_PIXEL) {
        nt_log_info("  Texture winner: pixel (only texture pack)");
    } else {
        (void)snprintf(buf, sizeof(buf), "  Texture winner: %s", s_pixel_prio > s_hires_prio ? "pixel" : "hires");
        nt_log_info(buf);
    }
}

/* ---- Frame callback ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void frame(void) {
    nt_window_poll();
    nt_input_poll();

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif

    /* SPACE: cycle texture load states */
    if (nt_input_key_is_pressed(NT_KEY_SPACE)) {
        if (s_load_state == STATE_EMPTY) {
            nt_log_info("======== LOAD PIXEL PACK ========");
            nt_resource_mount(s_pixel_pack_id, s_pixel_prio);
            nt_resource_load_auto(s_pixel_pack_id, "assets/lenna_pixel.ntpack");
            s_pixel_dumped = false;
            s_load_state = STATE_PIXEL;
            print_status();
        } else if (s_load_state == STATE_PIXEL) {
            nt_log_info("======== LOAD HIRES PACK ========");
            nt_resource_mount(s_hires_pack_id, s_hires_prio);
            nt_resource_load_auto(s_hires_pack_id, "assets/lenna_hires.ntpack");
            s_hires_dumped = false;
            s_load_state = STATE_BOTH;
            print_status();
        } else {
            nt_log_info("======== UNLOAD ALL ========");
            nt_resource_unmount(s_pixel_pack_id);
            nt_resource_unmount(s_hires_pack_id);
            s_load_state = STATE_EMPTY;
            print_status();
        }
    }

    /* ENTER: swap priorities */
    if (nt_input_key_is_pressed(NT_KEY_ENTER)) {
        int16_t tmp = s_pixel_prio;
        s_pixel_prio = s_hires_prio;
        s_hires_prio = tmp;
        if (s_load_state >= STATE_PIXEL) {
            nt_resource_set_priority(s_pixel_pack_id, s_pixel_prio);
        }
        if (s_load_state >= STATE_BOTH) {
            nt_resource_set_priority(s_hires_pack_id, s_hires_prio);
        }
        nt_log_info("======== SWAP PRIORITIES ========");
        print_status();
    }

    /* Step resource system */
    nt_resource_step();

    /* Dump pack contents when they become READY */
    if (!s_base_dumped && nt_resource_pack_state(s_base_pack_id) == NT_PACK_STATE_READY) {
        nt_log_info("======== BASE PACK READY ========");
        nt_resource_dump_pack(s_base_pack_id);
        s_base_dumped = true;
    }
    if (!s_pixel_dumped && s_load_state >= STATE_PIXEL && nt_resource_pack_state(s_pixel_pack_id) == NT_PACK_STATE_READY) {
        nt_log_info("======== PIXEL PACK READY ========");
        nt_resource_dump_pack(s_pixel_pack_id);
        print_status();
        s_pixel_dumped = true;
    }
    if (!s_hires_dumped && s_load_state >= STATE_BOTH && nt_resource_pack_state(s_hires_pack_id) == NT_PACK_STATE_READY) {
        nt_log_info("======== HIRES PACK READY ========");
        nt_resource_dump_pack(s_hires_pack_id);
        print_status();
        s_hires_dumped = true;
    }

    /* Create pipeline once shaders are ready */
    if (!s_pipeline_ready && nt_resource_is_ready(s_vs_handle) && nt_resource_is_ready(s_fs_handle)) {
        nt_shader_t vs = {.id = nt_resource_get(s_vs_handle)};
        nt_shader_t fs = {.id = nt_resource_get(s_fs_handle)};

        /* Layout matches cube.glb: position(float3) + uv0(float2), stride 20 */
        s_pipeline = nt_gfx_make_pipeline(&(nt_pipeline_desc_t){
            .vertex_shader = vs,
            .fragment_shader = fs,
            .layout =
                {
                    .attrs =
                        {
                            {.location = NT_ATTR_POSITION, .format = NT_FORMAT_FLOAT3, .offset = 0}, {.location = 1, .format = NT_FORMAT_FLOAT2, .offset = 12}, /* mesh.vert: layout(location=1) a_uv */
                        },
                    .attr_count = 2,
                    .stride = 20,
                },
            .depth_test = true,
            .depth_write = true,
            .depth_func = NT_DEPTH_LEQUAL,
            .cull_face = true,
            .label = "cube_pipeline",
        });
        s_pipeline_ready = true;
        nt_log_info(">> Pipeline created from pack shaders");
    }

    /* Resolve texture: resource handle → GFX texture, fallback if not ready */
    nt_texture_t active_tex = s_fallback_texture;
    if (s_load_state > STATE_EMPTY && nt_resource_is_ready(s_lenna_handle)) {
        uint32_t tex_id = nt_resource_get(s_lenna_handle);
        if (tex_id != 0) {
            active_tex.id = tex_id;
        }
    }

    /* Build MVP */
    float aspect = 1.0F;
    if (g_nt_window.fb_height > 0) {
        aspect = (float)g_nt_window.fb_width / (float)g_nt_window.fb_height;
    }

    float angle = (float)nt_time_now() * 0.7F;
    mat4 model;
    mat4 proj;
    mat4 view;
    mat4 mvp;
    mat4 tmp;

    glm_mat4_identity(model);
    glm_rotate_y(model, angle, model);
    glm_rotate_x(model, angle * 0.6F, model);
    glm_perspective(glm_rad(60.0F), aspect, 0.1F, 10.0F, proj);
    glm_lookat((vec3){0.0F, 0.0F, 3.0F}, (vec3){0.0F, 0.0F, 0.0F}, (vec3){0.0F, 1.0F, 0.0F}, view);
    glm_mat4_mul(proj, view, tmp);
    glm_mat4_mul(tmp, model, mvp);

    /* Render */
    nt_gfx_begin_frame();
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.15F, 0.15F, 0.2F, 1.0F}, .clear_depth = 1.0F});

    if (s_pipeline_ready && nt_resource_is_ready(s_mesh_handle)) {
        uint32_t mesh_handle = nt_resource_get(s_mesh_handle);
        const nt_gfx_mesh_info_t *mesh = nt_gfx_get_mesh_info(mesh_handle);

        if (mesh) {
            nt_gfx_bind_pipeline(s_pipeline);
            nt_gfx_bind_vertex_buffer(mesh->vbo);
            nt_gfx_bind_index_buffer(mesh->ibo);
            nt_gfx_bind_texture(active_tex, 0);
            nt_gfx_set_uniform_mat4("u_mvp", (float *)mvp);
            nt_gfx_set_uniform_int("u_texture", 0);
            nt_gfx_draw_indexed(0, mesh->index_count, mesh->vertex_count);
        }
    }

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_window_swap_buffers();
}

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "textured_cube";
    config.version = 1;

    nt_result_t result = nt_engine_init(&config);
    if (result != NT_OK) {
        return 1;
    }

    g_nt_window.width = 800;
    g_nt_window.height = 600;
    nt_window_init();
    nt_input_init();

    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    nt_gfx_init(&gfx_desc);

    /* Init I/O and resource systems */
    nt_http_init();
    nt_fs_init();
    nt_resource_init(&(nt_resource_desc_t){0});

    /* Register GFX activators */
    nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture);
    nt_resource_set_activator(NT_ASSET_MESH, nt_gfx_activate_mesh, nt_gfx_deactivate_mesh);
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);

    /* Compute pack IDs */
    s_base_pack_id = nt_resource_hash("base");
    s_pixel_pack_id = nt_resource_hash("lenna_pixel");
    s_hires_pack_id = nt_resource_hash("lenna_hires");

    /* Request resource handles */
    s_mesh_handle = nt_resource_request(nt_resource_hash("assets/meshes/cube.glb"), NT_ASSET_MESH);
    s_vs_handle = nt_resource_request(nt_resource_hash("assets/shaders/mesh.vert"), NT_ASSET_SHADER_CODE);
    s_fs_handle = nt_resource_request(nt_resource_hash("assets/shaders/mesh.frag"), NT_ASSET_SHADER_CODE);
    s_lenna_handle = nt_resource_request(nt_resource_hash("textures/lenna"), NT_ASSET_TEXTURE);

    /* Fallback checkerboard texture */
    s_fallback_texture = nt_gfx_make_texture(&(nt_texture_desc_t){
        .width = 4,
        .height = 4,
        .data = s_checker_4x4,
        .min_filter = NT_FILTER_NEAREST,
        .mag_filter = NT_FILTER_NEAREST,
        .wrap_u = NT_WRAP_REPEAT,
        .wrap_v = NT_WRAP_REPEAT,
        .label = "fallback_checker",
    });

    /* Load base pack (cube mesh + shaders — always present) */
    nt_resource_mount(s_base_pack_id, 100);
    nt_resource_load_auto(s_base_pack_id, "assets/base.ntpack");

    /* Bump activation budget so base pack activates in first frame */
    nt_resource_set_activate_budget(64);

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    if (s_pipeline_ready) {
        nt_gfx_destroy_pipeline(s_pipeline);
    }
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_gfx_destroy_texture(s_fallback_texture);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
