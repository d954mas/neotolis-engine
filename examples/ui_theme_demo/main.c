/*
 * UI Theme Demo -- Neotolis Engine
 *
 * Model D theme hot-swap demo: game owns per-widget styles via two
 * static-const palettes (g_dark + g_light), each grouping six pointers to
 * nt_ui_label_style_t instances. A single g_current palette pointer is
 * flipped on T-key press; the next frame's labels render with the new
 * palette. Engine ships no nt_ui_set_theme -- the swap is a pure game-side
 * pointer write.
 *
 * Six labels per frame exercise every nt_ui_label_style_t field at least once:
 *   h1            font_size + color
 *   body          font_size + color
 *   caption       align (CENTER)
 *   wrap          wrap_mode + line_height
 *   tracking      letter_tracking
 *   right         align (RIGHT)
 *
 * Build packs first:  build_ui_theme_demo_packs build/examples/ui_theme_demo
 */

// #region includes
#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "font/nt_font.h"
#include "fs/nt_fs.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "input/nt_input.h"
#include "log/nt_log.h"
#include "material/nt_material.h"
#include "render/nt_render_defs.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_label.h"
#include "window/nt_window.h"

#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "nt_pack_format.h"

#include "ui_theme_demo_assets.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

#include "clay.h"
// #endregion

// #region palette declarations
/* Per-widget styles -- one static-const per (role, palette) pair. Lives in
 * .rodata; any mutation crashes -- proves immutability for free. Color
 * channels are 0..255 floats per Clay's convention. */

static const nt_ui_label_style_t g_h1_dark = {
    .font_id = 0,
    .font_size = 28,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
};
static const nt_ui_label_style_t g_body_dark = {
    .font_id = 0,
    .font_size = 14,
    .color = {230.0F, 230.0F, 235.0F, 255.0F},
};
static const nt_ui_label_style_t g_caption_dark = {
    .font_id = 0,
    .font_size = 11,
    .color = {170.0F, 170.0F, 180.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};
static const nt_ui_label_style_t g_wrap_dark = {
    .font_id = 0,
    .font_size = 14,
    .color = {200.0F, 200.0F, 255.0F, 255.0F},
    .line_height = 22,
    .wrap_mode = CLAY_TEXT_WRAP_WORDS,
};
static const nt_ui_label_style_t g_tracking_dark = {
    .font_id = 0,
    .font_size = 18,
    .color = {255.0F, 220.0F, 150.0F, 255.0F},
    .letter_tracking = 8,
};
static const nt_ui_label_style_t g_right_dark = {
    .font_id = 0,
    .font_size = 14,
    .color = {230.0F, 230.0F, 235.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_RIGHT,
};

static const nt_ui_label_style_t g_h1_light = {
    .font_id = 0,
    .font_size = 28,
    .color = {0.0F, 0.0F, 0.0F, 255.0F},
};
static const nt_ui_label_style_t g_body_light = {
    .font_id = 0,
    .font_size = 14,
    .color = {20.0F, 20.0F, 28.0F, 255.0F},
};
static const nt_ui_label_style_t g_caption_light = {
    .font_id = 0,
    .font_size = 11,
    .color = {80.0F, 80.0F, 90.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};
static const nt_ui_label_style_t g_wrap_light = {
    .font_id = 0,
    .font_size = 14,
    .color = {40.0F, 40.0F, 100.0F, 255.0F},
    .line_height = 22,
    .wrap_mode = CLAY_TEXT_WRAP_WORDS,
};
static const nt_ui_label_style_t g_tracking_light = {
    .font_id = 0,
    .font_size = 18,
    .color = {140.0F, 80.0F, 0.0F, 255.0F},
    .letter_tracking = 8,
};
static const nt_ui_label_style_t g_right_light = {
    .font_id = 0,
    .font_size = 14,
    .color = {20.0F, 20.0F, 28.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_RIGHT,
};

typedef struct {
    const nt_ui_label_style_t *h1;
    const nt_ui_label_style_t *body;
    const nt_ui_label_style_t *caption;
    const nt_ui_label_style_t *wrap;
    const nt_ui_label_style_t *tracking;
    const nt_ui_label_style_t *right;
    Clay_Color bg;
    const char *name;
} ui_palette_t;

static const ui_palette_t g_dark = {
    .h1 = &g_h1_dark,
    .body = &g_body_dark,
    .caption = &g_caption_dark,
    .wrap = &g_wrap_dark,
    .tracking = &g_tracking_dark,
    .right = &g_right_dark,
    .bg = {18.0F, 18.0F, 22.0F, 255.0F},
    .name = "DARK",
};
static const ui_palette_t g_light = {
    .h1 = &g_h1_light,
    .body = &g_body_light,
    .caption = &g_caption_light,
    .wrap = &g_wrap_light,
    .tracking = &g_tracking_light,
    .right = &g_right_light,
    .bg = {245.0F, 245.0F, 250.0F, 255.0F},
    .name = "LIGHT",
};

/* The pointer write IS the hot-swap. No engine API involved. */
static const ui_palette_t *g_current = &g_dark;
// #endregion

// #region engine state
#define UI_ARENA_SIZE ((size_t)2U * 1024U * 1024U) /* 2 MB -- plenty for 6 labels */
#define SCRATCH_ARENA_SIZE ((size_t)256U * 1024U)

static NT_UI_DECLARE_ARENA(s_ui_arena, UI_ARENA_SIZE);

static nt_ui_context_t *s_ctx;
static nt_buffer_t s_frame_ubo;

static nt_hash32_t s_pack_id;
static nt_resource_t s_atlas_handle;
static nt_resource_t s_atlas_tex_handle;
static nt_resource_t s_sprite_vs_handle;
static nt_resource_t s_sprite_fs_handle;
static nt_resource_t s_text_vs_handle;
static nt_resource_t s_text_fs_handle;
static nt_resource_t s_font_resource;

static nt_material_t s_sprite_material;
static nt_material_t s_text_material;
static nt_font_t s_font;

static bool s_atlas_bound;
static bool s_font_bound;
static uint32_t s_white_region_idx;
// #endregion

// #region binding (run once resources are READY)
static void try_bind_resources(void) {
    if (s_atlas_bound && s_font_bound) {
        return;
    }

    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        uint32_t ridx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_THEME_DEMO_ATLAS__WHITE_PNG.value);
        NT_ASSERT(ridx != NT_ATLAS_INVALID_REGION && "ui_theme_demo: _white.png region missing in atlas");
        s_white_region_idx = ridx;
        nt_ui_set_atlas_white_region(s_ctx, s_atlas_handle, s_white_region_idx);
        s_atlas_bound = true;
        nt_log_info("ui_theme_demo: atlas bound, white_region_idx=%u", s_white_region_idx);
    }

    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        nt_font_add(s_font, s_font_resource);
        nt_ui_set_font(s_ctx, 0U, s_font);
        s_font_bound = true;
        nt_log_info("ui_theme_demo: font bound at slot 0");
    }
}
// #endregion

// #region frame
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void frame(void) {
    nt_window_poll();
    nt_input_poll();
    nt_mem_scratch_reset();

#ifndef NT_PLATFORM_WEB
    if (nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif

    nt_resource_step();
    nt_material_step();

    /* Drift-3 corrected: the verified API is nt_input_key_is_pressed; the legacy
     * planning draft used the wrong name (no _is_ infix) which would link-fail. */
    if (nt_input_key_is_pressed(NT_KEY_T)) {
        g_current = (g_current == &g_dark) ? &g_light : &g_dark;
        (void)printf("ui_theme_demo: swapped to %s palette\n", g_current->name);
    }

    try_bind_resources();

    const float w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);

    /* Frame ortho VP -- screen-space, bottom-left origin. */
    mat4 view_m;
    mat4 proj_m;
    mat4 vp;
    glm_mat4_identity(view_m);
    glm_ortho(0.0F, w, 0.0F, h, -1.0F, 1.0F, proj_m);
    glm_mat4_mul(proj_m, view_m, vp);

    nt_frame_uniforms_t uniforms = {0};
    memcpy(uniforms.view_proj, vp, 64);
    memcpy(uniforms.view, view_m, 64);
    memcpy(uniforms.proj, proj_m, 64);
    uniforms.resolution[0] = w;
    uniforms.resolution[1] = h;
    uniforms.resolution[2] = (w > 0.0F) ? (1.0F / w) : 0.0F;
    uniforms.resolution[3] = (h > 0.0F) ? (1.0F / h) : 0.0F;
    uniforms.near_far[0] = -1.0F;
    uniforms.near_far[1] = 1.0F;

    nt_gfx_begin_frame();
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
        .clear_color = {g_current->bg.r / 255.0F, g_current->bg.g / 255.0F, g_current->bg.b / 255.0F, 1.0F},
        .clear_depth = 1.0F,
    });

    nt_font_step();

    /* All bindings ready? Declare + walk the UI. */
    const nt_material_info_t *sprite_info = nt_material_get_info(s_sprite_material);
    const nt_material_info_t *text_info = nt_material_get_info(s_text_material);
    const bool can_render = s_atlas_bound && s_font_bound && sprite_info && sprite_info->ready && text_info && text_info->ready;

    if (can_render) {
        nt_gfx_update_buffer(s_frame_ubo, &uniforms, sizeof(uniforms));
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

        const nt_pointer_t *mouse = &g_nt_input.pointers[0];
        nt_ui_begin(s_ctx, w, h, mouse);

        // #region clay declaration: 6 labels per frame
        CLAY({.id = CLAY_ID("root"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(24), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 12},
              .backgroundColor = g_current->bg}) {
            nt_ui_label(s_ctx, "Theme Hot-Swap Demo (press T)", g_current->h1);
            nt_ui_label(s_ctx, "Body text -- flips between dark and light palettes.", g_current->body);
            nt_ui_label(s_ctx, "Caption (centered)", g_current->caption);
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(360), CLAY_SIZING_FIT(0)}}}) {
                nt_ui_label(s_ctx,
                            "This is a multi-line paragraph wrapped to a 360px container width to "
                            "demonstrate the wrap_mode field flowing through to Clay text layout.",
                            g_current->wrap);
            }
            nt_ui_label(s_ctx, "S T R E T C H E D   T R A C K I N G", g_current->tracking);
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) { nt_ui_label(s_ctx, "Right-aligned text", g_current->right); }
        }
        // #endregion

        nt_ui_end(s_ctx);

        nt_ui_target_t target = {.viewport = {0.0F, 0.0F, w, h}};
        nt_ui_walk(s_ctx, &target);
    }

    nt_gfx_end_pass();
    nt_gfx_end_frame();

    nt_window_swap_buffers();
}
// #endregion

// #region main + init
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    nt_engine_config_t config = {0};
    config.app_name = "ui_theme_demo";
    config.version = 1;

    if (nt_engine_init(&config) != NT_OK) {
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
    nt_mem_scratch_init(SCRATCH_ARENA_SIZE);

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
    s_ctx = nt_ui_create_context(s_ui_arena, sizeof s_ui_arena, &ui_desc);
    NT_ASSERT(s_ctx != NULL && "ui_theme_demo: failed to create UI context");

    g_nt_app.target_dt = 0.0F;

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    s_pack_id = nt_hash32_str("ui_theme_demo");
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/ui_theme_demo/ui_theme_demo.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/ui_theme_demo.ntpack");
#endif

    s_sprite_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_UI_THEME_DEMO_ATLAS, NT_ASSET_ATLAS);
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_UI_THEME_DEMO_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_UI_THEME_DEMO_FONT, NT_ASSET_FONT);

    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_sprite_vs_handle,
        .fs = s_sprite_fs_handle,
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "ui_theme_demo_sprite",
    });
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_text_vs_handle,
        .fs = s_text_fs_handle,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "ui_theme_demo_text",
    });

    nt_ui_set_sprite_material(s_ctx, s_sprite_material);
    nt_ui_set_text_material(s_ctx, s_text_material);

    s_font = nt_font_create(&(nt_font_create_desc_t){
        .curve_texture_width = 1024,
        .curve_texture_height = 512,
        .band_texture_height = 256,
        .band_count = 8,
        .measure_cache_size = 256,
    });

    nt_resource_set_activate_time_budget(0);

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_log_info("ui_theme_demo: starting in DARK palette; press T to toggle");

    nt_app_run(frame);

#ifndef NT_PLATFORM_WEB
    nt_ui_destroy_context(s_ctx);
    nt_ui_module_shutdown();
    nt_text_renderer_shutdown();
    nt_sprite_renderer_shutdown();
    nt_font_destroy(s_font);
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
