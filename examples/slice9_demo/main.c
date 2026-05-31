/* Slice9 Demo -- visual smoke test for slice9 rendering + transform animations.
 * Shows same panel at 3 sizes (corner non-stretch proof) + animated panel
 * with scale/opacity/offset/rotation transform inheritance to children.
 * Keys: S scale | O opacity | P position | R rotation | A all | D debug
 * Build packs: slice9_demo_build_packs build/examples/slice9_demo */

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
#include "stats/nt_stats.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_panel.h"
#include "ui/nt_ui_scale.h"
#include "window/nt_window.h"

#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "nt_pack_format.h"

#include "slice9_demo_assets.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

#include "clay.h"
// #endregion

// #region styles
static const nt_ui_image_style_t g_panel_style = {
    .color_packed = 0xFFFFFFFF, /* no tint */
};

static const nt_ui_label_style_t g_status_style = {
    .font_id = 0,
    .font_size = 16,
    .color = {200.0F, 200.0F, 210.0F, 255.0F},
};

static const nt_ui_label_style_t g_title_style = {
    .font_id = 0,
    .font_size = 28,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

static const nt_ui_label_style_t g_panel_label_style = {
    .font_id = 0,
    .font_size = 20,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

static const nt_ui_label_style_t g_child_label_style = {
    .font_id = 0,
    .font_size = 18,
    .color = {255.0F, 240.0F, 200.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};
// #endregion

// #region engine state
#define UI_ARENA_SIZE ((size_t)2U * 1024U * 1024U)
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
static uint32_t s_panel_beige_idx;
static uint32_t s_panel_blue_idx;
static uint32_t s_panel_brown_idx;
static uint32_t s_button_blue_idx;
static uint32_t s_button_green_idx;

#define LAYER_BG 0
#define LAYER_IMG 1
#define LAYER_TEXT 2

#define UI_REF_W 960.0F
#define UI_REF_H 640.0F

/* Animation toggles (D-54-21) — start ON for visual verification */
static bool s_anim_scale = true;
static bool s_anim_opacity = true;
static bool s_anim_position = true;
static bool s_anim_rotation = true;
static float s_time;
// #endregion

// #region binding
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void try_bind_resources(void) {
    if (s_atlas_bound && s_font_bound) {
        return;
    }

    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        s_white_region_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_SLICE9_DEMO_ATLAS__WHITE.value);
        NT_ASSERT(s_white_region_idx != NT_ATLAS_INVALID_REGION);

        s_panel_beige_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_SLICE9_DEMO_ATLAS_PANEL_BEIGE.value);
        s_panel_blue_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_SLICE9_DEMO_ATLAS_PANEL_BLUE.value);
        s_panel_brown_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_SLICE9_DEMO_ATLAS_PANEL_BROWN.value);
        s_button_blue_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_SLICE9_DEMO_ATLAS_BUTTON_BLUE.value);
        s_button_green_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_SLICE9_DEMO_ATLAS_BUTTON_GREEN.value);
        NT_ASSERT(s_panel_beige_idx != NT_ATLAS_INVALID_REGION);
        NT_ASSERT(s_button_blue_idx != NT_ATLAS_INVALID_REGION);

        nt_ui_set_atlas_white_region(s_ctx, s_atlas_handle, s_white_region_idx);
        s_atlas_bound = true;
        nt_log_info("slice9_demo: atlas bound (5 Kenney regions + white)");
    }

    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        nt_font_add(s_font, s_font_resource);
        nt_ui_set_font(s_ctx, 0U, s_font);
        s_font_bound = true;
        nt_log_info("slice9_demo: font bound at slot 0");
    }
}
// #endregion

// #region declare_static_panels
/* Kenney panels at 3 sizes — corners must not stretch. */
static void declare_static_panels(void) {
    CLAY({.id = CLAY_ID("static-title"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Kenney panels at 3 sizes (corners stay rounded)", &g_title_style);
    }

    CLAY({.id = CLAY_ID("static-row"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 16, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {

        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(60)}}}) { nt_ui_image(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_atlas_handle, s_panel_beige_idx, &g_panel_style); }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(70)}}}) { nt_ui_image(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_atlas_handle, s_panel_blue_idx, &g_panel_style); }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(350), CLAY_SIZING_FIXED(80)}}}) { nt_ui_image(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_atlas_handle, s_panel_brown_idx, &g_panel_style); }
    }

    /* Kenney buttons */
    CLAY({.id = CLAY_ID("btn-row"),
          .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 12, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(40)}}}) { nt_ui_image(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_atlas_handle, s_button_blue_idx, &g_panel_style); }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(250), CLAY_SIZING_FIXED(40)}}}) { nt_ui_image(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_atlas_handle, s_button_green_idx, &g_panel_style); }
    }
}
// #endregion

// #region declare_animated_panel
/* Animated panel: scale, opacity, offset, rotation with child label inheritance. */
static void declare_animated_panel(void) {
    CLAY({.id = CLAY_ID("anim-title"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Animated panel (toggle S/O/P/R/A)", &g_title_style);
    }

    /* Compute animated transform */
    nt_ui_transform_t t = nt_ui_transform_defaults();
    float opacity = 1.0F;

    if (s_anim_scale) {
        t.scale_x = t.scale_y = 0.8F + (0.2F * (sinf(s_time * 2.0F) + 1.0F));
    }
    if (s_anim_position) {
        t.offset_x = sinf(s_time * 1.0F) * 50.0F;
    }
    if (s_anim_rotation) {
        t.rotation = sinf(s_time * 3.0F) * 0.15F;
    }
    if (s_anim_opacity) {
        opacity = (sinf(s_time * 1.5F) + 1.0F) * 0.5F;
    }

    CLAY({.id = CLAY_ID("anim-container"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {

        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(300), CLAY_SIZING_FIXED(100)},
                         .padding = CLAY_PADDING_ALL(16),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 8,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            nt_ui_push_transform(s_ctx, &t);
            nt_ui_push_opacity(s_ctx, opacity);
            nt_ui_panel_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_atlas_handle, s_panel_brown_idx, &g_panel_style);
            {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Animated Panel", &g_panel_label_style);
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Hello World", &g_child_label_style);
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Children inherit transform", &g_child_label_style);
            }
            nt_ui_panel_end(s_ctx);
            nt_ui_pop_opacity(s_ctx);
            nt_ui_pop_transform(s_ctx);
        }
    }
}
// #endregion

// #region declare_nested_panels
/* Nested panels: outer(offset+scale) > middle(offset+opacity) > inner(scale).
 * Verifies transforms accumulate hierarchically. */
static void declare_nested_panels(void) {
    CLAY({.id = CLAY_ID("nested-title"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Nested: outer(offset+scale) > mid(opacity) > inner", &g_status_style);
    }

    /* Outer: sliding + scale pulse */
    nt_ui_transform_t outer_t = nt_ui_transform_defaults();
    if (s_anim_position) {
        outer_t.offset_x = sinf(s_time * 1.0F) * 40.0F;
    }
    if (s_anim_scale) {
        outer_t.scale_x = outer_t.scale_y = 0.9F + (0.1F * (sinf(s_time * 2.5F) + 1.0F));
    }

    /* Middle: fading */
    float mid_opacity = 1.0F;
    if (s_anim_opacity) {
        mid_opacity = (sinf(s_time * 2.0F) + 1.0F) * 0.5F;
    }

    /* Inner: no transform — inherits outer scale + middle opacity */

    CLAY({.id = CLAY_ID("nested-wrap"), .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        /* Outer beige panel */
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(500), CLAY_SIZING_FIXED(140)}, .padding = CLAY_PADDING_ALL(12), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            nt_ui_push_transform(s_ctx, &outer_t);
            nt_ui_panel_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_atlas_handle, s_panel_beige_idx, &g_panel_style);
            {
                /* Middle blue panel */
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(10), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                    nt_ui_push_opacity(s_ctx, mid_opacity);
                    nt_ui_panel_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_atlas_handle, s_panel_blue_idx, &g_panel_style);
                    {
                        // clang-format off
                        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(8), .childGap = 4, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                            nt_ui_panel_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_atlas_handle, s_panel_brown_idx, &g_panel_style);
                            { nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Nested child", &g_child_label_style); }
                            nt_ui_panel_end(s_ctx);
                        }
                        // clang-format on
                    }
                    nt_ui_panel_end(s_ctx);
                    nt_ui_pop_opacity(s_ctx);
                }
            }
            nt_ui_panel_end(s_ctx);
            nt_ui_pop_transform(s_ctx);
        }
    }
}
// #endregion

// #region frame
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void frame(void) {
    nt_stats_frame_begin();
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

    // #region input handling
    if (nt_input_key_is_pressed(NT_KEY_S)) {
        s_anim_scale = !s_anim_scale;
        nt_log_info("slice9_demo: scale %s", s_anim_scale ? "ON" : "OFF");
    }
    if (nt_input_key_is_pressed(NT_KEY_O)) {
        s_anim_opacity = !s_anim_opacity;
        nt_log_info("slice9_demo: opacity %s", s_anim_opacity ? "ON" : "OFF");
    }
    if (nt_input_key_is_pressed(NT_KEY_P)) {
        s_anim_position = !s_anim_position;
        nt_log_info("slice9_demo: position %s", s_anim_position ? "ON" : "OFF");
    }
    if (nt_input_key_is_pressed(NT_KEY_R)) {
        s_anim_rotation = !s_anim_rotation;
        nt_log_info("slice9_demo: rotation %s", s_anim_rotation ? "ON" : "OFF");
    }
    if (nt_input_key_is_pressed(NT_KEY_A)) {
        const bool all_on = s_anim_scale && s_anim_opacity && s_anim_position && s_anim_rotation;
        s_anim_scale = !all_on;
        s_anim_opacity = !all_on;
        s_anim_position = !all_on;
        s_anim_rotation = !all_on;
        nt_log_info("slice9_demo: all animations %s", !all_on ? "ON" : "OFF");
    }
    if (nt_input_key_is_pressed(NT_KEY_D)) {
        const bool now_on = !nt_ui_get_debug_overlay(s_ctx);
        nt_ui_set_debug_overlay(s_ctx, now_on);
        nt_log_info("slice9_demo: debug overlay %s", now_on ? "ON" : "OFF");
    }
    // #endregion

    s_time += g_nt_app.dt;

    try_bind_resources();

    const float fb_w = (float)(g_nt_window.fb_width > 0 ? g_nt_window.fb_width : 800);
    const float fb_h = (float)(g_nt_window.fb_height > 0 ? g_nt_window.fb_height : 600);

    nt_ui_scale_desc_t scale_desc = {.ref_w = UI_REF_W, .ref_h = UI_REF_H, .mode = NT_UI_SCALE_EXPAND};
    nt_ui_scale_t scale = nt_ui_compute_scale(&scale_desc, fb_w, fb_h);
    nt_ui_scale_ortho_t ortho = nt_ui_scale_ortho(&scale);

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
    /* nt_stats reads frame total via segment named "frame" by convention. */
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
        .clear_color = {0.07F, 0.07F, 0.09F, 1.0F},
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
        nt_ui_begin(s_ctx, scale.logical_w, scale.logical_h, g_nt_app.dt, &mouse_logical, 1);

        // #region status line
        char status_text[128];
        (void)snprintf(status_text, sizeof status_text, "S:%s  O:%s  P:%s  R:%s   [A]toggle all  [D]debug", s_anim_scale ? "on" : "off", s_anim_opacity ? "on" : "off", s_anim_position ? "on" : "off",
                       s_anim_rotation ? "on" : "off");
        // #endregion

        // #region clay declaration
        CLAY({.id = CLAY_ID("root"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .padding = CLAY_PADDING_ALL(20),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 16,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
              .backgroundColor = {18.0F, 18.0F, 22.0F, 255.0F}}) {
            nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), status_text, &g_status_style);
            declare_static_panels();
            declare_animated_panel();
            declare_nested_panels();
        }
        // #endregion

        nt_ui_end(s_ctx);

        nt_ui_target_t target = nt_ui_scale_make_target(&scale);
        nt_ui_walk(s_ctx, &target);

        // #region metrics bridge
        nt_stats_count("ui_draw_calls", (uint64_t)nt_ui_get_last_walk_draw_calls(s_ctx));
        nt_stats_count("ui_commands", (uint64_t)nt_ui_get_last_walk_command_count(s_ctx));
        nt_stats_count("ui_rect_cmds", (uint64_t)nt_ui_get_last_walk_rect_command_count(s_ctx));
        nt_stats_count("ui_image_cmds", (uint64_t)nt_ui_get_last_walk_image_command_count(s_ctx));
        nt_stats_count("ui_text_cmds", (uint64_t)nt_ui_get_last_walk_text_command_count(s_ctx));
        nt_stats_count("ui_border_cmds", (uint64_t)nt_ui_get_last_walk_border_command_count(s_ctx));
        nt_stats_count("ui_scissor_cmds", (uint64_t)nt_ui_get_last_walk_scissor_command_count(s_ctx));
        nt_stats_count("ui_layout_us", (uint64_t)(nt_ui_get_last_layout_ms(s_ctx) * 1000.0F));
        nt_stats_count("ui_walk_us", (uint64_t)(nt_ui_get_last_walk_ms(s_ctx) * 1000.0F));
        // #endregion

        // #region stats overlay
        {
            mat4 stats_model;
            glm_mat4_identity(stats_model);
            glm_translate(stats_model, (vec3){10.0F, scale.logical_h - 20.0F, 0.0F});
            const float stats_color[4] = {0.8F, 0.9F, 0.8F, 1.0F};
            nt_stats_draw(s_text_material, s_font, (const float *)stats_model, 14.0F, stats_color);
            /* nt_stats_draw only stages text; flush before end_pass so the
             * overlay lands in THIS frame, not the next walk's flush. */
            nt_text_renderer_flush();
        }
        // #endregion
    }

    nt_gfx_end_pass();
    nt_gfx_end_segment();
    nt_gfx_end_frame();
    nt_stats_frame_end();

    nt_window_swap_buffers();
}
// #endregion

// #region main + init
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    nt_engine_config_t config = {0};
    config.app_name = "slice9_demo";
    config.version = 1;

    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }

    g_nt_window.width = 960;
    g_nt_window.height = 640;
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
    NT_ASSERT(s_ctx != NULL && "slice9_demo: failed to create UI context");

    g_nt_app.target_dt = 0.0F;

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    s_pack_id = nt_hash32_str("slice9_demo");
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/slice9_demo/slice9_demo.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/slice9_demo.ntpack");
#endif

    s_sprite_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_SLICE9_DEMO_ATLAS, NT_ASSET_ATLAS);
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_SLICE9_DEMO_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_SLICE9_DEMO_FONT, NT_ASSET_FONT);

    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_sprite_vs_handle,
        .fs = s_sprite_fs_handle,
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "slice9_demo_sprite",
    });
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_text_vs_handle,
        .fs = s_text_fs_handle,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "slice9_demo_text",
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

    nt_stats_desc_t stats_desc = nt_stats_desc_defaults();
    nt_stats_init(&stats_desc);

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_log_info("slice9_demo: starting (all animations OFF; press A to toggle all)");

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
    nt_stats_shutdown();
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
