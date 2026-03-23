/*
 * Textured Cube Demo -- Neotolis Engine
 *
 * Full mesh rendering pipeline demo:
 *   Entity/components → render items → nt_mesh_renderer_draw_list → GPU
 *
 * Shows: asset packs, material system, entity system, instanced mesh rendering,
 * UBO frame uniforms, texture hot-swap via resource priorities.
 *
 * Packs:
 *   base.ntpack         -- cube mesh + instanced shaders (loaded on start)
 *   lenna_pixel.ntpack  -- 64x64 pixel art lenna
 *   lenna_hires.ntpack  -- full resolution lenna
 *
 * Controls:
 *   SPACE -- cycle: load pixel -> load hires -> unload both
 *   ENTER -- swap texture pack priorities
 *   ESC   -- quit (native only)
 *
 * Build packs first:  build_tq_packs  (from project root)
 */

#include "app/nt_app.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "material_comp/nt_material_comp.h"
#include "mesh_comp/nt_mesh_comp.h"
#include "render/nt_render_defs.h"
#include "render/nt_render_items.h"
#include "renderers/nt_mesh_renderer.h"
#include "resource/nt_resource.h"
#include "sort/nt_sort.h"
#include "time/nt_time.h"
#include "transform_comp/nt_transform_comp.h"
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

static nt_texture_t s_fallback_texture;
static nt_buffer_t s_frame_ubo;

/* ---- Resource handles ---- */

static nt_hash32_t s_base_pack_id;
static nt_hash32_t s_pixel_pack_id;
static nt_hash32_t s_hires_pack_id;

static nt_resource_t s_mesh_handle;
static nt_resource_t s_vs_handle;
static nt_resource_t s_fs_handle;
static nt_resource_t s_lenna_handle;

/* ---- Material ---- */

static nt_material_t s_cube_material;

static int16_t s_pixel_prio = 10;
static int16_t s_hires_prio = 20;

/* ---- Entities ---- */

#define NUM_CUBES 5

static nt_entity_t s_cubes[NUM_CUBES];
static nt_render_item_t s_sort_scratch[NUM_CUBES];

/* clang-format off */
static const float s_cube_positions[NUM_CUBES][3] = {
    { 0.0F,  0.0F,  0.0F},
    {-2.0F,  0.5F, -1.0F},
    { 2.0F, -0.5F, -1.0F},
    {-1.0F, -1.0F,  1.5F},
    { 1.0F,  1.0F,  1.5F},
};
static const float s_cube_colors[NUM_CUBES][4] = {
    {1.0F, 1.0F, 1.0F, 1.0F}, /* white — full texture */
    {1.0F, 0.6F, 0.6F, 1.0F}, /* red tint */
    {0.6F, 1.0F, 0.6F, 1.0F}, /* green tint */
    {0.6F, 0.6F, 1.0F, 1.0F}, /* blue tint */
    {1.0F, 1.0F, 0.6F, 1.0F}, /* yellow tint */
};
/* clang-format on */

/* ---- State ---- */

enum { STATE_EMPTY = 0, STATE_PIXEL = 1, STATE_BOTH = 2 };
static int s_load_state = STATE_EMPTY;
static bool s_base_dumped;
static bool s_pixel_dumped;
static bool s_hires_dumped;

/* ---- Status print: show current pack priorities ---- */

static void print_status(void) {
    nt_log_info("  base   prio=100  state=%d", (int)nt_resource_pack_state(s_base_pack_id));
    if (s_load_state >= STATE_PIXEL) {
        nt_log_info("  pixel  prio=%d  state=%d", (int)s_pixel_prio, (int)nt_resource_pack_state(s_pixel_pack_id));
    }
    if (s_load_state >= STATE_BOTH) {
        nt_log_info("  hires  prio=%d  state=%d", (int)s_hires_prio, (int)nt_resource_pack_state(s_hires_pack_id));
    }
    if (s_load_state == STATE_EMPTY) {
        nt_log_info("  Texture: (fallback)");
    } else if (s_load_state == STATE_PIXEL) {
        nt_log_info("  Texture winner: pixel (only texture pack)");
    } else {
        nt_log_info("  Texture winner: %s", s_pixel_prio > s_hires_prio ? "pixel" : "hires");
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
#ifdef NT_CDN_URL
            nt_resource_load_auto(s_pixel_pack_id, NT_CDN_URL "/textured_quad/lenna_pixel.ntpack");
#else
            nt_resource_load_auto(s_pixel_pack_id, "assets/lenna_pixel.ntpack");
#endif
            s_pixel_dumped = false;
            s_load_state = STATE_PIXEL;
            print_status();
        } else if (s_load_state == STATE_PIXEL) {
            nt_log_info("======== LOAD HIRES PACK ========");
            nt_resource_mount(s_hires_pack_id, s_hires_prio);
#ifdef NT_CDN_URL
            nt_resource_load_auto(s_hires_pack_id, NT_CDN_URL "/textured_quad/lenna_hires.ntpack");
#else
            nt_resource_load_auto(s_hires_pack_id, "assets/lenna_hires.ntpack");
#endif
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

    /* Step resource + material systems */
    nt_resource_step();
    nt_material_step();

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

    /* ---- Update entity transforms ---- */

    float angle = (float)nt_time_now() * 0.7F;

    for (int i = 0; i < NUM_CUBES; i++) {
        float *rot = nt_transform_comp_rotation(s_cubes[i]);

        /* Each cube spins at a different rate */
        float speed = 1.0F + ((float)i * 0.3F);
        versor q_y;
        versor q_x;
        glm_quatv(q_y, angle * speed, (vec3){0, 1, 0});
        glm_quatv(q_x, angle * speed * 0.6F, (vec3){1, 0, 0});
        glm_quat_mul(q_y, q_x, rot);
        *nt_transform_comp_dirty(s_cubes[i]) = true;
    }

    nt_transform_comp_update();

    /* ---- Build frame uniforms ---- */

    float aspect = 1.0F;
    if (g_nt_window.fb_height > 0) {
        aspect = (float)g_nt_window.fb_width / (float)g_nt_window.fb_height;
    }

    mat4 view_m;
    mat4 proj_m;
    mat4 vp;
    glm_lookat((vec3){0.0F, 0.0F, 3.0F}, (vec3){0.0F, 0.0F, 0.0F}, (vec3){0.0F, 1.0F, 0.0F}, view_m);
    glm_perspective(glm_rad(60.0F), aspect, 0.1F, 10.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp);

    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, vp, 64);
    memcpy(uniforms.view, view_m, 64);
    memcpy(uniforms.proj, proj_m, 64);
    uniforms.camera_pos[0] = 0.0F;
    uniforms.camera_pos[1] = 0.0F;
    uniforms.camera_pos[2] = 3.0F;
    uniforms.time[0] = (float)nt_time_now();
    uniforms.time[1] = g_nt_app.dt;
    if (g_nt_window.fb_width > 0) {
        uniforms.resolution[0] = (float)g_nt_window.fb_width;
        uniforms.resolution[1] = (float)g_nt_window.fb_height;
        uniforms.resolution[2] = 1.0F / (float)g_nt_window.fb_width;
        uniforms.resolution[3] = 1.0F / (float)g_nt_window.fb_height;
    }
    uniforms.near_far[0] = 0.1F;
    uniforms.near_far[1] = 10.0F;

    /* ---- Build render items ---- */

    const nt_material_info_t *mat_info = nt_material_get_info(s_cube_material);
    bool can_render = mat_info && mat_info->ready && nt_resource_is_ready(s_mesh_handle);

    /* ---- Render ---- */

    nt_gfx_begin_frame();

    /* Restore GPU resources after WebGL context loss */
    if (g_nt_gfx.context_restored) {
        /* Invalidate all GFX-backed resources so they re-activate from blobs */
        nt_resource_invalidate(NT_ASSET_SHADER_CODE);
        nt_resource_invalidate(NT_ASSET_MESH);
        nt_resource_invalidate(NT_ASSET_TEXTURE);

        /* Re-register virtual pack resources (invalidate skips virtual packs) */
        nt_resource_register(nt_hash32_str("__fallback__"), nt_hash64_str("__fallback_checker__"), NT_ASSET_TEXTURE, s_fallback_texture.id);

        /* Recreate game-owned GPU resources */
        s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = NT_BUFFER_UNIFORM,
            .usage = NT_USAGE_DYNAMIC,
            .size = sizeof(nt_frame_uniforms_t),
            .label = "frame_uniforms",
        });
        nt_mesh_renderer_restore_gpu();
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.15F, 0.15F, 0.2F, 1.0F}, .clear_depth = 1.0F});

    if (can_render) {
        /* Upload frame uniforms and bind to slot 0 */
        nt_gfx_update_buffer(s_frame_ubo, &uniforms, sizeof(uniforms));
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

        uint32_t mesh_id = nt_resource_get(s_mesh_handle);

        /* ---- Collect: build render items from entities ---- */
        nt_render_item_t items[NUM_CUBES];
        uint32_t item_count = 0;

        for (int i = 0; i < NUM_CUBES; i++) {
            /* Update mesh handle (may change when resource resolves) */
            *nt_mesh_comp_handle(s_cubes[i]) = (nt_mesh_t){.id = mesh_id};

            items[item_count].sort_key = nt_sort_key_opaque(s_cube_material.id, mesh_id);
            items[item_count].entity = s_cubes[i].id;
            items[item_count].batch_key = nt_batch_key(s_cube_material.id, mesh_id);
            item_count++;
        }

        /* ---- Sort: order by sort_key (material+mesh grouping) ---- */
        nt_sort_by_key(items, item_count, s_sort_scratch);

        /* ---- Draw: mesh renderer handles pipeline, instancing, batching ---- */
        nt_mesh_renderer_draw_list(items, item_count);

        /* One-time log to verify batching */
        static bool s_stats_logged;
        if (!s_stats_logged) {
            nt_log_info(">> Render stats: %u draw calls, %u instanced, %u instances (from %u items)", g_nt_gfx.frame_stats.draw_calls, g_nt_gfx.frame_stats.draw_calls_instanced,
                        g_nt_gfx.frame_stats.instances, item_count);
            s_stats_logged = true;
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

    /* Register global UBO blocks (required after Plan 02 removed auto-bind) */
    nt_gfx_register_global_block("Globals", 0);

    /* Init I/O and resource systems */
    nt_http_init();
    nt_fs_init();
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});

    /* Register GFX activators */
    nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture);
    nt_resource_set_activator(NT_ASSET_MESH, nt_gfx_activate_mesh, nt_gfx_deactivate_mesh);
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);

    /* Compute pack IDs */
    s_base_pack_id = nt_hash32_str("base");
    s_pixel_pack_id = nt_hash32_str("lenna_pixel");
    s_hires_pack_id = nt_hash32_str("lenna_hires");

    /* Init material system */
    nt_material_desc_t mat_desc = nt_material_desc_defaults();
    nt_material_init(&mat_desc);

    /* Init entity / component systems */
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 64});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 64});
    nt_mesh_comp_init(&(nt_mesh_comp_desc_t){.capacity = 64});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = 64});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 64});

    /* Init mesh renderer */
    nt_mesh_renderer_desc_t mr_desc = nt_mesh_renderer_desc_defaults();
    nt_mesh_renderer_init(&mr_desc);

    /* Request resource handles (instanced shaders) */
    s_mesh_handle = nt_resource_request(nt_hash64_str("assets/meshes/cube.glb"), NT_ASSET_MESH);
    s_vs_handle = nt_resource_request(nt_hash64_str("assets/shaders/mesh_inst.vert"), NT_ASSET_SHADER_CODE);
    s_fs_handle = nt_resource_request(nt_hash64_str("assets/shaders/mesh_inst.frag"), NT_ASSET_SHADER_CODE);
    s_lenna_handle = nt_resource_request(nt_hash64_str("textures/lenna"), NT_ASSET_TEXTURE);

    /* Create material from resource handles */
    s_cube_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_vs_handle,
        .fs = s_fs_handle,
        .textures = {{.name = "u_texture", .resource = s_lenna_handle}},
        .texture_count = 1,
        .attr_map = {{.stream_name = "position", .location = 0}, {.stream_name = "uv0", .location = 1}},
        .attr_map_count = 2,
        .depth_test = true,
        .depth_write = true,
        .cull_mode = NT_CULL_BACK,
        .label = "cube_lenna_instanced",
    });

    /* Create cube entities with all components */
    for (int i = 0; i < NUM_CUBES; i++) {
        s_cubes[i] = nt_entity_create();
        nt_transform_comp_add(s_cubes[i]);
        nt_mesh_comp_add(s_cubes[i]);
        nt_material_comp_add(s_cubes[i]);
        nt_drawable_comp_add(s_cubes[i]);

        /* Set position */
        float *p = nt_transform_comp_position(s_cubes[i]);
        p[0] = s_cube_positions[i][0];
        p[1] = s_cube_positions[i][1];
        p[2] = s_cube_positions[i][2];

        /* Set scale (smaller for orbiting cubes) */
        float *scl = nt_transform_comp_scale(s_cubes[i]);
        float s = (i == 0) ? 1.0F : 0.5F;
        scl[0] = s;
        scl[1] = s;
        scl[2] = s;

        *nt_transform_comp_dirty(s_cubes[i]) = true;

        /* Set material */
        *nt_material_comp_handle(s_cubes[i]) = s_cube_material;

        /* Set tint color */
        float *color = nt_drawable_comp_color(s_cubes[i]);
        color[0] = s_cube_colors[i][0];
        color[1] = s_cube_colors[i][1];
        color[2] = s_cube_colors[i][2];
        color[3] = s_cube_colors[i][3];
    }

    /* Create frame uniforms UBO (updated each frame) */
    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

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
    nt_hash64_t checker_rid = nt_hash64_str("__fallback_checker__");
    nt_hash32_t checker_pid = nt_hash32_str("__fallback__");
    nt_resource_create_pack(checker_pid, 0);
    nt_resource_register(checker_pid, checker_rid, NT_ASSET_TEXTURE, s_fallback_texture.id);
    nt_resource_set_placeholder_texture(checker_rid);

    /* Load base pack (cube mesh + instanced shaders -- always present) */
    nt_resource_mount(s_base_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_base_pack_id, NT_CDN_URL "/textured_quad/base.ntpack");
#else
    nt_resource_load_auto(s_base_pack_id, "assets/base.ntpack");
#endif

    /* Bump activation budget so base pack activates in first frame */
    nt_resource_set_activate_budget(64);

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_mesh_renderer_shutdown();
    nt_drawable_comp_shutdown();
    nt_material_comp_shutdown();
    nt_mesh_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
    nt_material_destroy(s_cube_material);
    nt_material_shutdown();
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_hash_shutdown();
    nt_gfx_destroy_buffer(s_frame_ubo);
    nt_gfx_destroy_texture(s_fallback_texture);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif
    return 0;
}
