/* Turkic Jam 2026 — game base template.
 * Engine bootstrap + render services + a scene loop. Scenes live in src/scenes
 * and only ever build UI; all engine init stays here. */

// #region includes
#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "devapi/nt_devapi.h"
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
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_scale.h"
#include "window/nt_window.h"

#include "nt_pack_format.h"
#include "turkic_jam_assets.h"

#include "config.h"
#include "game.h"
#include "i18n.h"
#include "rng.h"
#include "save.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

#include "clay.h"
// #endregion

#define TJ_UI_ARENA_SIZE ((size_t)2U * 1024U * 1024U)
#define TJ_SCRATCH_ARENA_SIZE ((size_t)256U * 1024U)

static NT_UI_DECLARE_ARENA(s_ui_arena, TJ_UI_ARENA_SIZE);

static game_ctx_t g;

static nt_buffer_t s_frame_ubo;
static nt_resource_t s_atlas_handle;
static nt_resource_t s_atlas_tex_handle;
static nt_resource_t s_sprite_vs;
static nt_resource_t s_sprite_fs;
static nt_resource_t s_text_vs;
static nt_resource_t s_text_fs;
static nt_resource_t s_font_resource;
static nt_material_t s_sprite_material;
static nt_material_t s_text_material;
static bool s_atlas_bound;
static bool s_font_bound;

void game_goto(game_ctx_t *gg, const scene_t *next) { gg->next = next; }

#if NT_DEVAPI_ENABLED
/* Game-registered devapi endpoint: confirms the loaded balance config. */
static int ep_game_config(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    return snprintf(o, (size_t)cap, "{\"path_cells\":%d,\"laps_to_win\":%d,\"start_stamina\":%d,\"tiles\":%d,\"heirs\":%d}", g_config.path_cells, g_config.laps_to_win, g_config.start_stamina,
                    g_config.tile_count, g_config.heir_count);
}
#endif

static void apply_transition(void) {
    if (!g.next) {
        return;
    }
    if (g.scene && g.scene->on_exit) {
        g.scene->on_exit(&g);
    }
    g.prev = g.scene;
    g.scene = g.next;
    g.next = NULL;
    if (g.scene->on_enter) {
        g.scene->on_enter(&g);
    }
}

static void bind_atlas(void) {
    g.atlas = s_atlas_handle;
    g.white_region = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS__WHITE.value);
    g.btn_blue = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_BUTTON_BLUE.value);
    g.btn_green = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_BUTTON_GREEN.value);
    g.btn_red = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_BUTTON_RED.value);
    NT_ASSERT(g.white_region != NT_ATLAS_INVALID_REGION);
    NT_ASSERT(g.btn_blue != NT_ATLAS_INVALID_REGION);
    NT_ASSERT(g.btn_green != NT_ATLAS_INVALID_REGION);
    NT_ASSERT(g.btn_red != NT_ATLAS_INVALID_REGION);
    nt_ui_set_atlas_white_region(g.ui, s_atlas_handle, g.white_region);
    s_atlas_bound = true;
    nt_log_info("turkic_jam: atlas bound");
}

static void bind_font(void) {
    nt_font_add(g.font, s_font_resource);
    nt_ui_set_font(g.ui, 0U, g.font);
    s_font_bound = true;
    nt_log_info("turkic_jam: font bound");
}

static void try_bind_resources(void) {
    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        bind_atlas();
    }
    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        bind_font();
    }
    g.resources_ready = s_atlas_bound && s_font_bound;
}

// #region frame
static void frame(void) {
    nt_window_poll();
    nt_input_poll();
    nt_devapi_apply_pending(); /* inject queued synthetic input (no-op unless devapi on) */
    nt_devapi_net_poll();      /* serve debug/bot commands */
    nt_mem_scratch_reset();

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif

    apply_transition();

    nt_resource_step();
    nt_material_step();
    try_bind_resources();

    tj_shake_update(&g.shake, g_nt_app.dt);

    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);

    nt_ui_scale_desc_t scale_desc = {.ref_w = TJ_REF_W, .ref_h = TJ_REF_H, .mode = NT_UI_SCALE_EXPAND};
    nt_ui_scale_t scale = nt_ui_compute_scale(&scale_desc, fb_w, fb_h);
    nt_ui_scale_ortho_t ortho = nt_ui_scale_ortho(&scale);
    g.logical_w = scale.logical_w;
    g.logical_h = scale.logical_h;
    nt_devapi_set_view(fb_w, fb_h, scale.logical_w, scale.logical_h);

    mat4 view_m;
    mat4 proj_m;
    mat4 vp;
    glm_mat4_identity(view_m);
    glm_ortho(ortho.left, ortho.right, ortho.bottom, ortho.top, -1.0F, 1.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp);

    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, vp, 64);
    memcpy(uniforms.view, view_m, 64);
    memcpy(uniforms.proj, proj_m, 64);
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
        nt_sprite_renderer_restore_gpu();
        nt_text_renderer_restore_gpu();
        s_atlas_bound = false;
        s_font_bound = false;
    }

    nt_gfx_begin_pass(&(nt_pass_desc_t){
        .clear_color = {0.04F, 0.05F, 0.08F, 1.0F},
        .clear_depth = 1.0F,
    });

    nt_font_step();

    const nt_material_info_t *sprite_info = nt_material_get_info(s_sprite_material);
    const nt_material_info_t *text_info = nt_material_get_info(s_text_material);
    const bool can_render = s_atlas_bound && s_font_bound && sprite_info && sprite_info->ready && text_info && text_info->ready;

    if (can_render) {
        nt_gfx_update_buffer(s_frame_ubo, &uniforms, sizeof(uniforms));
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

        const nt_pointer_t mouse_logical = nt_ui_scale_apply_pointer(&scale, g_nt_input.pointers[0]);
        nt_ui_begin(g.ui, scale.logical_w, scale.logical_h, g_nt_app.dt, &mouse_logical, 1);

        /* Screen-shake (juice): trauma -> small offset+rotation on the card. */
        nt_ui_transform_t shake_xform = nt_ui_transform_defaults();
        {
            float sdx;
            float sdy;
            float sdeg;
            tj_shake_sample(&g.shake, 14.0F, 1.5F, &sdx, &sdy, &sdeg);
            shake_xform.offset_x = sdx;
            shake_xform.offset_y = sdy;
            shake_xform.rotation_z = sdeg * 0.017453292F; /* deg -> rad */
        }

        /* Shared shell: dark backdrop + centered card. Scenes fill the card. */
        CLAY({.id = CLAY_ID("root"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(24), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {10.0F, 12.0F, 20.0F, 255.0F}}) {
            CLAY({.id = CLAY_ID("card"),
                  .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)},
                             .padding = CLAY_PADDING_ALL(48),
                             .layoutDirection = CLAY_TOP_TO_BOTTOM,
                             .childGap = 22,
                             .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                  .backgroundColor = {26.0F, 30.0F, 46.0F, 240.0F},
                  .cornerRadius = CLAY_CORNER_RADIUS(28),
                  .userData = (void *)NT_UI_DATA_XFORM(0U, &shake_xform, 1.0F)}) {
                if (g.scene && g.scene->on_update) {
                    g.scene->on_update(&g, g_nt_app.dt);
                }
            }
        }

        nt_ui_end(g.ui);

        nt_ui_target_t target = nt_ui_scale_make_target(&scale);
        nt_ui_walk(g.ui, &target);
    }

    nt_gfx_end_pass();
    nt_gfx_end_segment();
    nt_gfx_end_frame();
    nt_window_swap_buffers();
}
// #endregion

// #region main + init
int main(int argc, char *argv[]) {
    int devapi_port = 0;
    const char *config_dir = "config";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--devapi") == 0 && i + 1 < argc) {
            devapi_port = (int)strtol(argv[i + 1], NULL, 10);
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_dir = argv[i + 1];
        }
    }

    nt_engine_config_t config = {0};
    config.app_name = "turkic_jam";
    config.version = 1;
    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }

    g_nt_window.width = 1280;
    g_nt_window.height = 720;
    nt_window_init();
    nt_input_init();

    nt_gfx_desc_t gfx_desc = nt_gfx_desc_defaults();
    nt_gfx_init(&gfx_desc);
    nt_gfx_register_global_block("Globals", 0);

    nt_http_init();
    nt_fs_init();
    nt_hash_init(&(nt_hash_desc_t){0});
    nt_resource_init(&(nt_resource_desc_t){0});
    nt_mem_scratch_init(TJ_SCRATCH_ARENA_SIZE);

    nt_resource_set_activator(NT_ASSET_TEXTURE, nt_gfx_activate_texture, nt_gfx_deactivate_texture);
    nt_resource_set_activator(NT_ASSET_SHADER_CODE, nt_gfx_activate_shader, nt_gfx_deactivate_shader);
    nt_atlas_init();

    nt_material_init(&(nt_material_desc_t){.max_materials = 4});
    nt_font_init(&(nt_font_desc_t){.max_fonts = 2});

    nt_sprite_renderer_desc_t sr_desc = nt_sprite_renderer_desc_defaults();
    nt_sprite_renderer_init(&sr_desc);
    nt_text_renderer_init();

    nt_ui_module_init();
    const nt_ui_create_desc_t ui_desc = nt_ui_create_desc_defaults();
    g.ui = nt_ui_create_context(s_ui_arena, sizeof s_ui_arena, &ui_desc);
    NT_ASSERT(g.ui != NULL && "turkic_jam: failed to create UI context");

    g_nt_app.target_dt = 0.0F;

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    const nt_hash32_t pack_id = nt_hash32_str("turkic_jam");
    nt_resource_mount(pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(pack_id, NT_CDN_URL "/turkic_jam/turkic_jam.ntpack");
#else
    nt_resource_load_auto(pack_id, "assets/turkic_jam.ntpack");
#endif

    s_sprite_vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_vs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_fs = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_TURKIC_JAM_ATLAS, NT_ASSET_ATLAS);
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_TURKIC_JAM_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_TURKIC_JAM_FONT, NT_ASSET_FONT);

    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_sprite_vs,
        .fs = s_sprite_fs,
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "turkic_jam_sprite",
    });
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_text_vs,
        .fs = s_text_fs,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "turkic_jam_text",
    });

    nt_ui_set_sprite_material(g.ui, s_sprite_material);
    nt_ui_set_text_material(g.ui, s_text_material);

    g.font = nt_font_create(&(nt_font_create_desc_t){
        .curve_texture_width = 1024,
        .curve_texture_height = 512,
        .band_texture_height = 256,
        .band_count = 8,
        .measure_cache_size = 256,
    });

    nt_resource_set_activate_time_budget(0);

    /* Persistence + restore last language before the first frame. */
    rng_seed(0xC0FFEEU);
    save_init();
    i18n_set((i18n_lang_t)save_get_int("lang", LANG_EN));

    tj_config_load(config_dir);
    nt_log_info("turkic_jam: config '%s' -> %d tiles, %d heirs, %d cells", config_dir, g_config.tile_count, g_config.heir_count, g_config.path_cells);

    /* Debug/automation command bus (no-op unless NT_DEVAPI_ENABLED). */
    nt_devapi_init();
    nt_devapi_register_builtins();
    nt_devapi_set_ui_context(g.ui);
#if NT_DEVAPI_ENABLED
    nt_devapi_register("game.config", ep_game_config, NULL);
#endif

    /* Initial scene; its on_enter may register more endpoints (e.g. game.run),
     * so it must run AFTER nt_devapi_init() clears the registry. */
    g.scene = g_config.start_in_game ? &SCENE_GAME : &SCENE_MENU;
    if (g.scene->on_enter) {
        g.scene->on_enter(&g);
    }

    /* Start the TCP server last — after endpoints + the first scene are ready. */
    if (devapi_port > 0) {
        nt_devapi_net_start((uint16_t)devapi_port);
    }

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_log_info("turkic_jam: starting (scene=%s)", g.scene->name);

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_devapi_net_stop();
    nt_devapi_shutdown();
    nt_ui_destroy_context(g.ui);
    nt_ui_module_shutdown();
    nt_text_renderer_shutdown();
    nt_sprite_renderer_shutdown();
    nt_font_destroy(g.font);
    nt_font_shutdown();
    nt_material_destroy(s_sprite_material);
    nt_material_destroy(s_text_material);
    nt_material_shutdown();
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
