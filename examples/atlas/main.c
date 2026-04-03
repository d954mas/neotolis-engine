/*
 * Atlas Demo -- Neotolis Engine
 *
 * Displays atlas-packed sprite textures on spinning cubes.
 * The atlas page texture shows how the packer arranged sprites.
 *
 * Build packs first: build_atlas_packs <output_dir>
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
#include "time/nt_time.h"
#include "transform_comp/nt_transform_comp.h"
#include "window/nt_window.h"

#include "atlas_assets.h"
#include "math/nt_math.h"
#include "nt_pack_format.h"

#include <stdint.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

/* ---- Fallback checkerboard ---- */

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

static nt_hash32_t s_pack_id;

static nt_resource_t s_mesh_handle;
static nt_resource_t s_vs_handle;
static nt_resource_t s_fs_handle;
static nt_resource_t s_atlas_tex_handle;

/* ---- Material ---- */

static nt_material_t s_material;

/* ---- Entity ---- */

static nt_entity_t s_cube;
static nt_render_item_t s_sort_scratch[1];

/* ---- State ---- */

static bool s_pack_dumped;

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

    nt_resource_step();
    nt_material_step();

    /* Dump pack contents when ready */
    if (!s_pack_dumped && nt_resource_pack_state(s_pack_id) == NT_PACK_STATE_READY) {
        nt_log_info("======== ATLAS PACK READY ========");
        nt_resource_dump_pack(s_pack_id);
        s_pack_dumped = true;
    }

    /* Slowly rotate the cube */
    float angle = (float)nt_time_now() * 0.5F;
    float *rot = nt_transform_comp_rotation(s_cube);
    versor q_y;
    versor q_x;
    glm_quatv(q_y, angle, (vec3){0, 1, 0});
    glm_quatv(q_x, angle * 0.3F, (vec3){1, 0, 0});
    glm_quat_mul(q_y, q_x, rot);
    *nt_transform_comp_dirty(s_cube) = true;
    nt_transform_comp_update();

    /* Frame uniforms */
    float aspect = 1.0F;
    if (g_nt_window.fb_height > 0) {
        aspect = (float)g_nt_window.fb_width / (float)g_nt_window.fb_height;
    }

    mat4 view_m;
    mat4 proj_m;
    mat4 vp;
    glm_lookat((vec3){0.0F, 0.0F, 2.5F}, (vec3){0.0F, 0.0F, 0.0F}, (vec3){0.0F, 1.0F, 0.0F}, view_m);
    glm_perspective(glm_rad(60.0F), aspect, 0.1F, 10.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp);

    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, vp, 64);
    memcpy(uniforms.view, view_m, 64);
    memcpy(uniforms.proj, proj_m, 64);
    uniforms.camera_pos[0] = 0.0F;
    uniforms.camera_pos[1] = 0.0F;
    uniforms.camera_pos[2] = 2.5F;
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

    /* Render */
    const nt_material_info_t *mat_info = nt_material_get_info(s_material);
    bool can_render = mat_info && mat_info->ready && nt_resource_is_ready(s_mesh_handle);

    nt_gfx_begin_frame();

    if (g_nt_gfx.context_restored) {
        nt_resource_invalidate(NT_ASSET_SHADER_CODE);
        nt_resource_invalidate(NT_ASSET_MESH);
        nt_resource_invalidate(NT_ASSET_TEXTURE);

        nt_resource_register(nt_hash32_str("__fallback__"), nt_hash64_str("__fallback_checker__"), NT_ASSET_TEXTURE, s_fallback_texture.id);

        s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = NT_BUFFER_UNIFORM,
            .usage = NT_USAGE_DYNAMIC,
            .size = sizeof(nt_frame_uniforms_t),
            .label = "frame_uniforms",
        });
        nt_mesh_renderer_restore_gpu();
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_color = {0.1F, 0.1F, 0.15F, 1.0F}, .clear_depth = 1.0F});

    if (can_render) {
        nt_gfx_update_buffer(s_frame_ubo, &uniforms, sizeof(uniforms));
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

        uint32_t mesh_id = nt_resource_get(s_mesh_handle);
        *nt_mesh_comp_handle(s_cube) = (nt_mesh_t){.id = mesh_id};

        nt_render_item_t items[1];
        items[0].sort_key = nt_sort_key_opaque(s_material.id, mesh_id);
        items[0].entity = s_cube.id;
        items[0].batch_key = nt_batch_key(s_material.id, mesh_id);

        nt_sort_by_key(items, 1, s_sort_scratch);
        nt_mesh_renderer_draw_list(items, 1);
    }

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_window_swap_buffers();
}

int main(void) {
    nt_engine_config_t config = {0};
    config.app_name = "atlas_demo";
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
    nt_gfx_register_global_block("Globals", 0);

    nt_http_init();
    nt_fs_init();
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});

    nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture);
    nt_resource_set_activator(NT_ASSET_MESH, nt_gfx_activate_mesh, nt_gfx_deactivate_mesh);
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);

    s_pack_id = nt_hash32_str("atlas_demo");

    nt_material_desc_t mat_desc = nt_material_desc_defaults();
    nt_material_init(&mat_desc);

    nt_entity_init(&(nt_entity_desc_t){.max_entities = 8});
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 8});
    nt_mesh_comp_init(&(nt_mesh_comp_desc_t){.capacity = 8});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = 8});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 8});

    nt_mesh_renderer_desc_t mr_desc = nt_mesh_renderer_desc_defaults();
    nt_mesh_renderer_init(&mr_desc);

    /* Request resource handles */
    s_mesh_handle = nt_resource_request(ASSET_MESH_ASSETS_MESHES_CUBE_GLB, NT_ASSET_MESH);
    s_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_MESH_INST_VERT, NT_ASSET_SHADER_CODE);
    s_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_MESH_INST_FRAG, NT_ASSET_SHADER_CODE);
    /* Atlas page texture: "spineboy/tex0" */
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_SPINEBOY_TEX0, NT_ASSET_TEXTURE);

    /* Create material with atlas page texture */
    s_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_vs_handle,
        .fs = s_fs_handle,
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .attr_map = {{.stream_name = "position", .location = 0}, {.stream_name = "uv0", .location = 1}},
        .attr_map_count = 2,
        .depth_test = true,
        .depth_write = true,
        .cull_mode = NT_CULL_BACK,
        .color_mode = NT_COLOR_MODE_FLOAT4,
        .label = "atlas_cube",
    });

    /* Create a single cube entity */
    s_cube = nt_entity_create();
    nt_transform_comp_add(s_cube);
    nt_mesh_comp_add(s_cube);
    nt_material_comp_add(s_cube);
    nt_drawable_comp_add(s_cube);

    *nt_transform_comp_dirty(s_cube) = true;
    *nt_material_comp_handle(s_cube) = s_material;

    /* White tint */
    float *color = nt_drawable_comp_color(s_cube);
    color[0] = 1.0F;
    color[1] = 1.0F;
    color[2] = 1.0F;
    color[3] = 1.0F;

    /* Frame uniforms UBO */
    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    /* Fallback checkerboard */
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

    /* Load atlas pack */
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/atlas/atlas_demo.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/atlas_demo.ntpack");
#endif

    nt_resource_set_activate_time_budget(0);

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
    nt_material_destroy(s_material);
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
