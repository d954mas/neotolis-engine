/*
 * Sponza Demo -- Neotolis Engine
 *
 * Full asset pipeline demo:
 *   Builder packs -> resource loading -> material creation -> entity/components
 *   -> render items -> nt_mesh_renderer_draw_list -> GPU
 *
 * Shows: progressive pack stacking (base + full quality), Blinn-Phong shading
 * with normal mapping via Lighting UBO, scene manifest loading, 3 shader
 * permutations (full/diffuse/alpha), FPS camera with WASD + mouse look.
 *
 * Packs:
 *   sponza_base.ntpack -- compressed quality (float16 pos, int8 normals)
 *   sponza_full.ntpack -- full quality (float32, full-res textures)
 *
 * Controls:
 *   WASD       -- move (fly mode, follows pitch)
 *   SPACE      -- move up
 *   LSHIFT     -- move down
 *   LMB + drag -- look around
 *   SCROLL     -- adjust move speed
 *   ENTER      -- toggle base/full quality
 *   ESC        -- quit (native only)
 *
 * Build packs first:  build_sponza_packs  (from project root)
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
#include "render/nt_render_util.h"
#include "renderers/nt_mesh_renderer.h"
#include "resource/nt_resource.h"
#include "time/nt_time.h"
#include "transform_comp/nt_transform_comp.h"
#include "window/nt_window.h"

#include "math/nt_math.h"
#include "nt_pack_format.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

/* ---- Constants ---- */

#define MAX_SCENE_NODES 256
#define MOUSE_SENSITIVITY 0.003F
#define MOVE_SPEED_DEFAULT 3.0F
#define MOVE_SPEED_MIN 0.5F
#define MOVE_SPEED_MAX 20.0F

/* ---- Lighting UBO (game-defined, slot 1) ---- */

typedef struct {
    float light_dir[4];   /* xyz = direction towards light, w = unused */
    float light_color[4]; /* rgb = color, a = intensity */
    float ambient[4];     /* rgb = color, a = intensity */
} nt_lighting_t;

_Static_assert(sizeof(nt_lighting_t) == 48, "lighting UBO 48 bytes");

#include "sponza_manifest.h"

/* ---- Fallback 4x4 checkerboard ---- */

/* clang-format off */
static const uint8_t s_checker_4x4[4 * 4 * 4] = {
    255,255,255,255,  80,80,80,255,    255,255,255,255,  80,80,80,255,
    80,80,80,255,     255,255,255,255, 80,80,80,255,     255,255,255,255,
    255,255,255,255,  80,80,80,255,    255,255,255,255,  80,80,80,255,
    80,80,80,255,     255,255,255,255, 80,80,80,255,     255,255,255,255,
};
/* clang-format on */

/* ---- Camera state ---- */

static float s_cam_pos[3] = {-8.0F, 4.0F, 0.0F};
static float s_cam_yaw;   /* radians */
static float s_cam_pitch; /* radians */
static float s_move_speed = MOVE_SPEED_DEFAULT;

/* ---- GFX handles ---- */

static nt_buffer_t s_frame_ubo;
static nt_buffer_t s_light_ubo;
static nt_texture_t s_fallback_texture;

/* ---- Resource handles ---- */

static nt_hash32_t s_base_pack_id;
static nt_hash32_t s_full_pack_id;

static nt_resource_t s_manifest_handle;
static nt_resource_t s_mesh_handles[MAX_SCENE_NODES];
static nt_resource_t s_tex_handles[MAX_SCENE_NODES * 3]; /* diffuse, normal, specular per node */

/* ---- Shader resource handles (6 shaders, 3 permutations) ---- */

static nt_resource_t s_vs_full;
static nt_resource_t s_fs_full;
static nt_resource_t s_vs_diffuse;
static nt_resource_t s_fs_diffuse;
static nt_resource_t s_vs_alpha;
static nt_resource_t s_fs_alpha;

/* ---- Materials and entities ---- */

static nt_material_t s_materials[MAX_SCENE_NODES];
static nt_entity_t s_entities[MAX_SCENE_NODES];
static uint32_t s_entity_count;
static bool s_scene_loaded;
static bool s_full_quality; /* true = full pack has higher priority */

/* ---- Pitch clamp (89 degrees in radians) ---- */

#define PITCH_LIMIT 1.5533F /* ~89 degrees */

/* ---- FPS camera update ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void camera_update(float dt) {
    /* Mouse look (LMB held) */
    if (nt_input_mouse_is_down(NT_BUTTON_LEFT)) {
        float mx = g_nt_input.pointers[0].dx;
        float my = g_nt_input.pointers[0].dy;
        s_cam_yaw -= mx * MOUSE_SENSITIVITY;
        s_cam_pitch -= my * MOUSE_SENSITIVITY;
    }

    /* Clamp pitch */
    if (s_cam_pitch > PITCH_LIMIT) {
        s_cam_pitch = PITCH_LIMIT;
    }
    if (s_cam_pitch < -PITCH_LIMIT) {
        s_cam_pitch = -PITCH_LIMIT;
    }

    /* Forward (fly-style: includes pitch) and right vectors */
    float forward[3] = {
        sinf(s_cam_yaw) * cosf(s_cam_pitch),
        sinf(s_cam_pitch),
        cosf(s_cam_yaw) * cosf(s_cam_pitch),
    };
    float right[3] = {-cosf(s_cam_yaw), 0.0F, sinf(s_cam_yaw)};

    /* WASD movement */
    float speed = s_move_speed * dt;
    if (nt_input_key_is_down(NT_KEY_W)) {
        s_cam_pos[0] += forward[0] * speed;
        s_cam_pos[1] += forward[1] * speed;
        s_cam_pos[2] += forward[2] * speed;
    }
    if (nt_input_key_is_down(NT_KEY_S)) {
        s_cam_pos[0] -= forward[0] * speed;
        s_cam_pos[1] -= forward[1] * speed;
        s_cam_pos[2] -= forward[2] * speed;
    }
    if (nt_input_key_is_down(NT_KEY_A)) {
        s_cam_pos[0] -= right[0] * speed;
        s_cam_pos[1] -= right[1] * speed;
        s_cam_pos[2] -= right[2] * speed;
    }
    if (nt_input_key_is_down(NT_KEY_D)) {
        s_cam_pos[0] += right[0] * speed;
        s_cam_pos[1] += right[1] * speed;
        s_cam_pos[2] += right[2] * speed;
    }
    if (nt_input_key_is_down(NT_KEY_SPACE)) {
        s_cam_pos[1] += speed;
    }
    if (nt_input_key_is_down(NT_KEY_LSHIFT)) {
        s_cam_pos[1] -= speed;
    }

    /* Scroll wheel adjusts speed */
    float wheel = g_nt_input.pointers[0].wheel_dy;
    if (fabsf(wheel) > 0.001F) {
        s_move_speed *= (1.0F + wheel * 0.1F);
        if (s_move_speed < MOVE_SPEED_MIN) {
            s_move_speed = MOVE_SPEED_MIN;
        }
        if (s_move_speed > MOVE_SPEED_MAX) {
            s_move_speed = MOVE_SPEED_MAX;
        }
    }
}

/* ---- Scene loading from manifest ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void load_scene_from_manifest(void) {
    uint32_t manifest_size = 0;
    const uint8_t *blob = nt_resource_get_blob(s_manifest_handle, &manifest_size);
    if (!blob || manifest_size < sizeof(SponzaManifestHeader)) {
        nt_log_info("Manifest blob not available or too small");
        return;
    }

    const SponzaManifestHeader *hdr = (const SponzaManifestHeader *)blob;
    uint32_t node_count = hdr->node_count;
    if (node_count > MAX_SCENE_NODES) {
        node_count = MAX_SCENE_NODES;
    }

    if (manifest_size < sizeof(SponzaManifestHeader) + node_count * sizeof(SponzaManifestNode)) {
        nt_log_info("Manifest blob truncated");
        return;
    }

    const SponzaManifestNode *nodes = (const SponzaManifestNode *)(blob + sizeof(SponzaManifestHeader));

    for (uint32_t i = 0; i < node_count; i++) {
        const SponzaManifestNode *mn = &nodes[i];

        /* Request mesh resource */
        s_mesh_handles[i] = nt_resource_request((nt_hash64_t){.value = mn->mesh_rid}, NT_ASSET_MESH);

        /* Request texture resources */
        uint32_t tex_base = i * 3;
        if (mn->diffuse_rid != 0) {
            s_tex_handles[tex_base + 0] = nt_resource_request((nt_hash64_t){.value = mn->diffuse_rid}, NT_ASSET_TEXTURE);
        }
        if (mn->normal_rid != 0) {
            s_tex_handles[tex_base + 1] = nt_resource_request((nt_hash64_t){.value = mn->normal_rid}, NT_ASSET_TEXTURE);
        }
        if (mn->specular_rid != 0) {
            s_tex_handles[tex_base + 2] = nt_resource_request((nt_hash64_t){.value = mn->specular_rid}, NT_ASSET_TEXTURE);
        }

        /* Select shader pair based on shader_type */
        nt_resource_t vs_handle;
        nt_resource_t fs_handle;

        switch (mn->shader_type) {
        case SPONZA_SHADER_FULL:
            vs_handle = s_vs_full;
            fs_handle = s_fs_full;
            break;
        case SPONZA_SHADER_ALPHA:
            vs_handle = s_vs_alpha;
            fs_handle = s_fs_alpha;
            break;
        default: /* SPONZA_SHADER_DIFFUSE */
            vs_handle = s_vs_diffuse;
            fs_handle = s_fs_diffuse;
            break;
        }

        /* Create material descriptor */
        nt_material_create_desc_t mat_desc = {
            .vs = vs_handle,
            .fs = fs_handle,
            .depth_test = true,
            .depth_write = true,
            .cull_mode = NT_CULL_BACK,
        };

        /* Textures and attr_map depend on shader type */
        if (mn->shader_type == SPONZA_SHADER_FULL) {
            /* Full: diffuse + normal + specular, 4 streams */
            mat_desc.texture_count = 0;
            if (mn->diffuse_rid != 0) {
                mat_desc.textures[mat_desc.texture_count].name = "u_diffuse";
                mat_desc.textures[mat_desc.texture_count].resource = s_tex_handles[tex_base + 0];
                mat_desc.texture_count++;
            }
            if (mn->normal_rid != 0) {
                mat_desc.textures[mat_desc.texture_count].name = "u_normal";
                mat_desc.textures[mat_desc.texture_count].resource = s_tex_handles[tex_base + 1];
                mat_desc.texture_count++;
            }
            if (mn->specular_rid != 0) {
                mat_desc.textures[mat_desc.texture_count].name = "u_specular";
                mat_desc.textures[mat_desc.texture_count].resource = s_tex_handles[tex_base + 2];
                mat_desc.texture_count++;
            }
            mat_desc.attr_map[0] = (nt_material_attr_desc_t){.stream_name = "position", .location = 0};
            mat_desc.attr_map[1] = (nt_material_attr_desc_t){.stream_name = "normal", .location = 1};
            mat_desc.attr_map[2] = (nt_material_attr_desc_t){.stream_name = "uv0", .location = 2};
            mat_desc.attr_map[3] = (nt_material_attr_desc_t){.stream_name = "tangent", .location = 3};
            mat_desc.attr_map_count = 4;
        } else if (mn->shader_type == SPONZA_SHADER_ALPHA) {
            /* Alpha: diffuse only, 3 streams, double-sided */
            mat_desc.texture_count = 0;
            if (mn->diffuse_rid != 0) {
                mat_desc.textures[mat_desc.texture_count].name = "u_diffuse";
                mat_desc.textures[mat_desc.texture_count].resource = s_tex_handles[tex_base + 0];
                mat_desc.texture_count++;
            }
            mat_desc.attr_map[0] = (nt_material_attr_desc_t){.stream_name = "position", .location = 0};
            mat_desc.attr_map[1] = (nt_material_attr_desc_t){.stream_name = "normal", .location = 1};
            mat_desc.attr_map[2] = (nt_material_attr_desc_t){.stream_name = "uv0", .location = 2};
            mat_desc.attr_map_count = 3;
            mat_desc.params[0] = (nt_material_param_desc_t){.name = "u_alpha_cutoff", .value = {(float)mn->alpha_cutoff_x100 / 100.0F}};
            mat_desc.param_count = 1;
            mat_desc.blend_mode = NT_BLEND_MODE_OPAQUE;
            mat_desc.cull_mode = NT_CULL_NONE; /* double-sided for foliage */
        } else {
            /* Diffuse: diffuse only, 3 streams */
            mat_desc.texture_count = 0;
            if (mn->diffuse_rid != 0) {
                mat_desc.textures[mat_desc.texture_count].name = "u_diffuse";
                mat_desc.textures[mat_desc.texture_count].resource = s_tex_handles[tex_base + 0];
                mat_desc.texture_count++;
            }
            mat_desc.attr_map[0] = (nt_material_attr_desc_t){.stream_name = "position", .location = 0};
            mat_desc.attr_map[1] = (nt_material_attr_desc_t){.stream_name = "normal", .location = 1};
            mat_desc.attr_map[2] = (nt_material_attr_desc_t){.stream_name = "uv0", .location = 2};
            mat_desc.attr_map_count = 3;
        }

        mat_desc.label = "sponza_material";

        s_materials[i] = nt_material_create(&mat_desc);

        /* Create entity with all 4 components */
        s_entities[i] = nt_entity_create();
        nt_transform_comp_add(s_entities[i]);
        nt_mesh_comp_add(s_entities[i]);
        nt_material_comp_add(s_entities[i]);
        nt_drawable_comp_add(s_entities[i]);

        /* Decompose manifest 4x4 world matrix into TRS for transform component */
        {
            mat4 m;
            mat4 rot_m;
            vec4 t;
            vec3 s;
            versor q;
            memcpy(m, mn->transform, sizeof(mat4));
            glm_decompose(m, t, rot_m, s);
            glm_mat4_quat(rot_m, q);
            memcpy(nt_transform_comp_position(s_entities[i]), t, sizeof(float) * 3);
            memcpy(nt_transform_comp_rotation(s_entities[i]), q, sizeof(versor));
            memcpy(nt_transform_comp_scale(s_entities[i]), s, sizeof(float) * 3);
            *nt_transform_comp_dirty(s_entities[i]) = true;
        }

        /* Set material component */
        *nt_material_comp_handle(s_entities[i]) = s_materials[i];

        /* Set drawable color from manifest base_color */
        float *color = nt_drawable_comp_color(s_entities[i]);
        color[0] = mn->base_color[0];
        color[1] = mn->base_color[1];
        color[2] = mn->base_color[2];
        color[3] = mn->base_color[3];
    }

    s_entity_count = node_count;
    s_scene_loaded = true;

    char buf[128];
    (void)snprintf(buf, sizeof(buf), "Sponza scene loaded: %u entities, %u materials", node_count, node_count);
    nt_log_info(buf);
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

    /* Enter toggles pack priority (base <-> full quality) */
    if (nt_input_key_is_pressed(NT_KEY_ENTER)) {
        s_full_quality = !s_full_quality;
        if (s_full_quality) {
            nt_resource_set_priority(s_base_pack_id, 10);
            nt_resource_set_priority(s_full_pack_id, 20);
            nt_log_info(">> Switched to FULL quality");
        } else {
            nt_resource_set_priority(s_base_pack_id, 20);
            nt_resource_set_priority(s_full_pack_id, 10);
            nt_log_info(">> Switched to BASE quality");
        }
    }

    /* Step resource + material systems */
    nt_resource_step();
    nt_material_step();

    /* Check if manifest is ready and scene not yet loaded */
    if (!s_scene_loaded && nt_resource_is_ready(s_manifest_handle)) {
        load_scene_from_manifest();
    }

    /* Camera update */
    camera_update(g_nt_app.dt);

    /* Transform update */
    nt_transform_comp_update();

    /* Build frame uniforms */
    float aspect = 1.0F;
    if (g_nt_window.fb_height > 0) {
        aspect = (float)g_nt_window.fb_width / (float)g_nt_window.fb_height;
    }

    vec3 target = {
        s_cam_pos[0] + (sinf(s_cam_yaw) * cosf(s_cam_pitch)),
        s_cam_pos[1] + sinf(s_cam_pitch),
        s_cam_pos[2] + (cosf(s_cam_yaw) * cosf(s_cam_pitch)),
    };

    mat4 view_m;
    mat4 proj_m;
    mat4 vp;
    glm_lookat(s_cam_pos, target, (vec3){0.0F, 1.0F, 0.0F}, view_m);
    glm_perspective(glm_rad(60.0F), aspect, 0.01F, 100.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp);

    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, vp, 64);
    memcpy(uniforms.view, view_m, 64);
    memcpy(uniforms.proj, proj_m, 64);
    uniforms.camera_pos[0] = s_cam_pos[0];
    uniforms.camera_pos[1] = s_cam_pos[1];
    uniforms.camera_pos[2] = s_cam_pos[2];
    uniforms.time[0] = (float)nt_time_now();
    uniforms.time[1] = g_nt_app.dt;
    if (g_nt_window.fb_width > 0) {
        uniforms.resolution[0] = (float)g_nt_window.fb_width;
        uniforms.resolution[1] = (float)g_nt_window.fb_height;
        uniforms.resolution[2] = 1.0F / (float)g_nt_window.fb_width;
        uniforms.resolution[3] = 1.0F / (float)g_nt_window.fb_height;
    }
    uniforms.near_far[0] = 0.01F;
    uniforms.near_far[1] = 100.0F;

    /* Build render items (only if scene loaded) */
    nt_render_item_t items[MAX_SCENE_NODES];
    uint32_t item_count = 0;

    if (s_scene_loaded) {
        static bool s_diag_logged;
        uint32_t skip_vis = 0;
        uint32_t skip_mat = 0;
        uint32_t skip_mesh = 0;

        for (uint32_t i = 0; i < s_entity_count; i++) {
            if (!nt_render_is_visible(s_entities[i])) {
                skip_vis++;
                continue;
            }

            const nt_material_info_t *mat_info = nt_material_get_info(s_materials[i]);
            if (!mat_info || !mat_info->ready) {
                skip_mat++;
                continue;
            }

            if (!nt_resource_is_ready(s_mesh_handles[i])) {
                skip_mesh++;
                continue;
            }

            /* Update mesh component handle */
            uint32_t mesh_id = nt_resource_get(s_mesh_handles[i]);
            *nt_mesh_comp_handle(s_entities[i]) = (nt_mesh_t){.id = mesh_id};

            items[item_count].sort_key = nt_sort_key_opaque(s_materials[i].id, mesh_id);
            items[item_count].entity = s_entities[i].id;
            items[item_count].batch_key = nt_batch_key(s_materials[i].id, mesh_id);
            item_count++;
        }

        if (!s_diag_logged && item_count > 0 && item_count < s_entity_count) {
            char dbuf[256];
            (void)snprintf(dbuf, sizeof(dbuf), ">> DIAG: %u/%u rendered, skipped: vis=%u mat=%u mesh=%u", item_count, s_entity_count, skip_vis, skip_mat, skip_mesh);
            nt_log_info(dbuf);
            s_diag_logged = true;
        }

        /* Sort items by sort_key */
        nt_sort_by_key(items, item_count);
    }

    /* ---- Render ---- */

    nt_gfx_begin_frame();

    /* Restore GPU resources after WebGL context loss */
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
        s_light_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
            .type = NT_BUFFER_UNIFORM,
            .usage = NT_USAGE_DYNAMIC,
            .size = sizeof(nt_lighting_t),
            .label = "lighting",
        });
        nt_mesh_renderer_restore_gpu();
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .clear_color = {0.529F, 0.808F, 0.922F, 1.0F}, /* sky blue */
        .clear_depth = 1.0F,
    });

    if (item_count > 0) {
        /* Upload and bind frame UBO (slot 0) */
        nt_gfx_update_buffer(s_frame_ubo, &uniforms, sizeof(uniforms));
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

        /* Upload and bind lighting UBO (slot 1) */
        nt_lighting_t lighting = {
            .light_dir = {0.5F, 0.7F, 0.5F, 0.0F},    /* angled sunlight */
            .light_color = {1.0F, 0.95F, 0.9F, 1.2F}, /* warm white, intensity 1.2 */
            .ambient = {0.6F, 0.65F, 0.8F, 0.3F},     /* cool ambient, intensity 0.3 */
        };
        nt_gfx_update_buffer(s_light_ubo, &lighting, sizeof(lighting));
        nt_gfx_bind_uniform_buffer(s_light_ubo, 1);

        /* Draw all render items */
        nt_mesh_renderer_draw_list(items, item_count);

        /* Per-second FPS + render stats */
        {
            static double s_stats_accum;
            static uint32_t s_stats_frames;
            static float s_stats_max_dt;
            s_stats_accum += (double)g_nt_app.dt;
            s_stats_frames++;
            if (g_nt_app.dt > s_stats_max_dt) {
                s_stats_max_dt = g_nt_app.dt;
            }
            if (s_stats_accum >= 1.0) {
                float avg_fps = (float)s_stats_frames / (float)s_stats_accum;
                float min_fps = (s_stats_max_dt > 0.0F) ? (1.0F / s_stats_max_dt) : 0.0F;
                char buf[256];
                (void)snprintf(buf, sizeof(buf), "FPS avg=%.1f min=%.1f dt=%.4f spd=%.0f | draws=%u inst=%u verts=%u tris=%u items=%u/%u", (double)avg_fps, (double)min_fps, (double)g_nt_app.dt,
                               (double)s_move_speed, g_nt_gfx.frame_stats.draw_calls, g_nt_gfx.frame_stats.instances, g_nt_gfx.frame_stats.vertices, g_nt_gfx.frame_stats.indices / 3, item_count,
                               s_entity_count);
                nt_log_info(buf);
                s_stats_accum = 0.0;
                s_stats_frames = 0;
                s_stats_max_dt = 0.0F;
            }
        }
    }

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_window_swap_buffers();
}

int main(void) {
    /* 1. Engine init */
    nt_engine_config_t config = {0};
    config.app_name = "sponza";
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

    /* 4. GFX init -- Sponza needs larger pools (69 textures, 103 meshes) */
    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    gfx_desc.max_textures = 256;
    gfx_desc.max_buffers = 512;
    gfx_desc.max_meshes = 256;
    nt_gfx_init(&gfx_desc);

    /* Register global UBO blocks */
    nt_gfx_register_global_block("Globals", 0);
    nt_gfx_register_global_block("Lighting", 1);

    /* 5. I/O init */
    nt_http_init();
    nt_fs_init();
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});

    /* 6. Register GFX activators */
    nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture);
    nt_resource_set_activator(NT_ASSET_MESH, nt_gfx_activate_mesh, nt_gfx_deactivate_mesh);
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);

    /* 7. Material init */
    nt_material_desc_t mat_desc = {.max_materials = 256};
    nt_material_init(&mat_desc);

    /* 8. Entity init */
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 256});

    /* 9. Component inits */
    nt_transform_comp_init(&(nt_transform_comp_desc_t){.capacity = 256});
    nt_mesh_comp_init(&(nt_mesh_comp_desc_t){.capacity = 256});
    nt_material_comp_init(&(nt_material_comp_desc_t){.capacity = 256});
    nt_drawable_comp_init(&(nt_drawable_comp_desc_t){.capacity = 256});

    /* 10. Mesh renderer init */
    nt_mesh_renderer_desc_t mr_desc = nt_mesh_renderer_desc_defaults();
    nt_mesh_renderer_init(&mr_desc);

    /* 11. Request shader resource handles (6 shaders, 3 permutations) */
    s_vs_full = nt_resource_request(nt_hash64_str("assets/shaders/sponza_full.vert"), NT_ASSET_SHADER_CODE);
    s_fs_full = nt_resource_request(nt_hash64_str("assets/shaders/sponza_full.frag"), NT_ASSET_SHADER_CODE);
    s_vs_diffuse = nt_resource_request(nt_hash64_str("assets/shaders/sponza_diffuse.vert"), NT_ASSET_SHADER_CODE);
    s_fs_diffuse = nt_resource_request(nt_hash64_str("assets/shaders/sponza_diffuse.frag"), NT_ASSET_SHADER_CODE);
    s_vs_alpha = nt_resource_request(nt_hash64_str("assets/shaders/sponza_alpha.vert"), NT_ASSET_SHADER_CODE);
    s_fs_alpha = nt_resource_request(nt_hash64_str("assets/shaders/sponza_alpha.frag"), NT_ASSET_SHADER_CODE);

    /* 12. Request manifest resource handle */
    s_manifest_handle = nt_resource_request(nt_hash64_str("sponza/manifest"), NT_ASSET_BLOB);

    /* 13-14. Create fallback checkerboard texture and set as placeholder */
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

    /* Create frame uniforms UBO (updated each frame) */
    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    /* Create lighting UBO (updated each frame) */
    s_light_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_lighting_t),
        .label = "lighting",
    });

    /* 15. Mount and load packs (base first, full in background) */
    s_base_pack_id = nt_hash32_str("sponza_base");
    nt_resource_mount(s_base_pack_id, 20); /* base starts as primary */
    nt_resource_load_auto(s_base_pack_id, "assets/sponza_base.ntpack");

    s_full_pack_id = nt_hash32_str("sponza_full");
    nt_resource_mount(s_full_pack_id, 10); /* full loads but lower priority */
    nt_resource_load_auto(s_full_pack_id, "assets/sponza_full.ntpack");

    /* 16. Sponza needs ~400 activation units total — set budget high enough */
    nt_resource_set_activate_budget(1024);

    /* 17. Platform web loading complete */
#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    /* 18. Run main loop */
    nt_app_run(frame);

    /* 19. Shutdown (native only) */
#ifndef NT_PLATFORM_WEB
    nt_mesh_renderer_shutdown();
    nt_drawable_comp_shutdown();
    nt_material_comp_shutdown();
    nt_mesh_comp_shutdown();
    nt_transform_comp_shutdown();
    nt_entity_shutdown();
    for (uint32_t i = 0; i < s_entity_count; i++) {
        nt_material_destroy(s_materials[i]);
    }
    nt_material_shutdown();
    nt_resource_shutdown();
    nt_fs_shutdown();
    nt_http_shutdown();
    nt_hash_shutdown();
    nt_gfx_destroy_buffer(s_light_ubo);
    nt_gfx_destroy_buffer(s_frame_ubo);
    nt_gfx_destroy_texture(s_fallback_texture);
    nt_gfx_shutdown();
    nt_input_shutdown();
    nt_window_shutdown();
    nt_engine_shutdown();
#endif

    return 0;
}
