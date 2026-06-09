/* UI Stateful Demo — settings-menu worked example for checkbox/toggle/radio.
 * Value lives in the GAME (Model D): the engine stores no logical value, only the
 * transient eased pop/slide. Keys: Esc quit (native), D toggle inspector.
 * Build packs: build_ui_stateful_demo_packs build/examples/ui_stateful_demo */

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
#include "ui/nt_ui_checkbox.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_scale.h"
#include "window/nt_window.h"

#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "nt_pack_format.h"

#include "ui_stateful_demo_assets.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

#include "clay.h"
// #endregion

// #region text styles
static const nt_ui_label_style_t g_title_style = {
    .font_id = 0,
    .font_size = 34,
    .color = {235.0F, 238.0F, 245.0F, 255.0F},
};

static const nt_ui_label_style_t g_section_style = {
    .font_id = 0,
    .font_size = 20,
    .color = {150.0F, 200.0F, 240.0F, 255.0F},
};

static const nt_ui_label_style_t g_status_style = {
    .font_id = 0,
    .font_size = 22,
    .color = {200.0F, 205.0F, 215.0F, 255.0F},
};

static const nt_ui_label_style_t g_help_style = {
    .font_id = 0,
    .font_size = 18,
    .color = {150.0F, 160.0F, 172.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};
// #endregion

// #region widget style templates
/* Box/check atlas refs are patched per-cell at runtime (region indices come from the
 * bound atlas). Every cell sets scale=1 / opacity=1 to satisfy assert_cell_valid;
 * pressed dips scale, disabled dips opacity. */

/* ---- checkbox: checkmark pops in (fast value pop) ---- */
static const nt_ui_checkbox_style_t g_check_tmpl = {
    .box_w = 32,
    .box_h = 32,
    .overlay_w = 26,
    .overlay_h = 26,
    .gap = 14,
    .label_side = 0, /* text on the right */
    .state_speed = 16.0F,
    .value_speed = 22.0F, /* fast pop (~100 ms) */
    .text_base = {.font_id = 0, .font_size = 24, .color = {210.0F, 213.0F, 220.0F, 255.0F}},
    .unchecked =
        {
            [NT_UI_CB_IDLE] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_HOVER] = {.box_tint = 0xFFE8ECF2, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_PRESSED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 0.92F, .opacity = 1.0F},
            [NT_UI_CB_DISABLED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 0.40F},
        },
    .checked =
        {
            [NT_UI_CB_IDLE] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFF7CE08C, .text_color = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_HOVER] = {.box_tint = 0xFFE8ECF2, .check_tint = 0xFF8CF09C, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_PRESSED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFF7CE08C, .scale = 0.92F, .opacity = 1.0F},
            [NT_UI_CB_DISABLED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFF7CE08C, .scale = 1.0F, .opacity = 0.40F},
        },
};

/* ---- toggle: track recolors by value, thumb slides ---- */
static const nt_ui_checkbox_style_t g_switch_tmpl = {
    .box_w = 64,
    .box_h = 32,
    .overlay_w = 22,
    .overlay_h = 22,
    .thumb_pad = 5, /* end-margin; with overlay 22 in 32px track -> 5 px clear band all around */
    .gap = 16,
    .label_side = 1, /* text on the left */
    .state_speed = 16.0F,
    .value_speed = 12.0F, /* smooth slide */
    .text_base = {.font_id = 0, .font_size = 24, .color = {210.0F, 213.0F, 220.0F, 255.0F}},
    .unchecked =
        {
            [NT_UI_CB_IDLE] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_HOVER] = {.box_tint = 0xFFEFEFEF, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_PRESSED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 0.96F, .opacity = 1.0F},
            [NT_UI_CB_DISABLED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 0.40F},
        },
    .checked =
        {
            [NT_UI_CB_IDLE] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_HOVER] = {.box_tint = 0xFFEFEFEF, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_PRESSED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 0.96F, .opacity = 1.0F},
            [NT_UI_CB_DISABLED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 0.40F},
        },
};

/* ---- radio: ring + dot, dot pops ---- */
static const nt_ui_checkbox_style_t g_radio_tmpl = {
    .box_w = 30,
    .box_h = 30,
    .overlay_w = 16,
    .overlay_h = 16,
    .gap = 12,
    .label_side = 0,
    .state_speed = 16.0F,
    .value_speed = 22.0F, /* fast pop */
    .text_base = {.font_id = 0, .font_size = 24, .color = {210.0F, 213.0F, 220.0F, 255.0F}},
    .unchecked =
        {
            [NT_UI_CB_IDLE] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_HOVER] = {.box_tint = 0xFFE8ECF2, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_PRESSED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 0.92F, .opacity = 1.0F},
            [NT_UI_CB_DISABLED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 0.40F},
        },
    .checked =
        {
            [NT_UI_CB_IDLE] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFF6CC0F0, .text_color = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_HOVER] = {.box_tint = 0xFFE8ECF2, .check_tint = 0xFF8CD0FF, .scale = 1.0F, .opacity = 1.0F},
            [NT_UI_CB_PRESSED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFF6CC0F0, .scale = 0.92F, .opacity = 1.0F},
            [NT_UI_CB_DISABLED] = {.box_tint = 0xFFFFFFFF, .check_tint = 0xFF6CC0F0, .scale = 1.0F, .opacity = 0.40F},
        },
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

/* Runtime widget styles: const templates copied + atlas refs patched once the
 * atlas binds (region indices are not known at compile time). */
static nt_ui_checkbox_style_t s_check;
static nt_ui_checkbox_style_t s_switch;
static nt_ui_checkbox_style_t s_radio;

/* Game-owned values (Model D — state lives in the game, not the engine). */
static bool s_vsync = true;
static bool s_dark;
static int s_quality = 1;    /* 0 low / 1 med / 2 high */
static bool s_cell_selected; /* indicator-only table-cell checkbox */
static bool s_locked = true; /* a permanently-checked DISABLED checkbox */

/* Stable widget ids (loc-safe — derived from key strings, not labels). */
static uint32_t s_id_vsync;
static uint32_t s_id_dark;
static uint32_t s_id_q_low;
static uint32_t s_id_q_med;
static uint32_t s_id_q_high;
static uint32_t s_id_cell;
static uint32_t s_id_locked;
static bool s_ids_ready;

#define LAYER_IMG 1
#define LAYER_TEXT 2

#define UI_REF_W 1280.0F
#define UI_REF_H 800.0F
// #endregion

// #region binding
static void patch_row_atlas(nt_ui_cb_state_t row[4], nt_resource_t atlas, uint32_t box_region, uint32_t check_region) {
    /* Only the IDLE cell carries the atlas refs; non-idle cells inherit the whole ref
     * (resolve_ref). check_region == NT_ATLAS_INVALID_REGION = no overlay art (e.g. the
     * radio unchecked row has no dot). */
    row[NT_UI_CB_IDLE].box = (nt_atlas_region_ref_t){atlas, box_region};
    if (check_region != NT_ATLAS_INVALID_REGION) {
        row[NT_UI_CB_IDLE].check = (nt_atlas_region_ref_t){atlas, check_region};
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void try_bind_resources(void) {
    if (s_atlas_bound && s_font_bound) {
        return;
    }

    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        const uint32_t white = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS__WHITE.value);
        const uint32_t box_off = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BOX_OFF.value);
        const uint32_t checkmark = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_CHECKMARK.value);
        const uint32_t ring = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_RADIO_RING.value);
        const uint32_t dot = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_RADIO_DOT.value);
        const uint32_t track_off = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_TRACK_OFF.value);
        const uint32_t track_on = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_TRACK_ON.value);
        const uint32_t thumb = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_THUMB.value);
        NT_ASSERT(white != NT_ATLAS_INVALID_REGION && box_off != NT_ATLAS_INVALID_REGION && checkmark != NT_ATLAS_INVALID_REGION);
        NT_ASSERT(ring != NT_ATLAS_INVALID_REGION && dot != NT_ATLAS_INVALID_REGION);
        NT_ASSERT(track_off != NT_ATLAS_INVALID_REGION && track_on != NT_ATLAS_INVALID_REGION && thumb != NT_ATLAS_INVALID_REGION);

        nt_ui_set_atlas_white_region(s_ctx, s_atlas_handle, white);

        /* Checkbox: box+check on both rows; unchecked has no check overlay. */
        s_check = g_check_tmpl;
        patch_row_atlas(s_check.unchecked, s_atlas_handle, box_off, NT_ATLAS_INVALID_REGION);
        patch_row_atlas(s_check.checked, s_atlas_handle, box_off, checkmark);

        /* Radio: ring on both rows; checked adds the dot. */
        s_radio = g_radio_tmpl;
        patch_row_atlas(s_radio.unchecked, s_atlas_handle, ring, NT_ATLAS_INVALID_REGION);
        patch_row_atlas(s_radio.checked, s_atlas_handle, ring, dot);

        /* Toggle: track recolors by value (off grey / on green); thumb on both. */
        s_switch = g_switch_tmpl;
        patch_row_atlas(s_switch.unchecked, s_atlas_handle, track_off, thumb);
        patch_row_atlas(s_switch.checked, s_atlas_handle, track_on, thumb);

        s_atlas_bound = true;
        nt_log_info("ui_stateful_demo: atlas bound (box/check + ring/dot + track + thumb)");
    }

    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        nt_font_add(s_font, s_font_resource);
        nt_ui_set_font(s_ctx, 0U, s_font);
        s_font_bound = true;
        nt_log_info("ui_stateful_demo: font bound at slot 0");
    }
}
// #endregion

// #region row layout
/* One settings row: a fixed-height left-aligned strip. The widget's own row
 * sizing comes from this decl (the FIXED box only sizes the indicator). */
static const Clay_ElementDeclaration s_row_decl = {
    .layout =
        {
            .sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(44)},
            .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER},
        },
};

static void section_label(const char *text) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {.top = 14, .bottom = 4}}}) { nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), text, &g_section_style); }
}
// #endregion

// #region declare_menu (settings menu)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_menu(void) {
    if (!s_ids_ready) {
        s_id_vsync = nt_ui_id("settings/vsync");
        s_id_dark = nt_ui_id("settings/dark");
        s_id_q_low = nt_ui_id("settings/quality_low");
        s_id_q_med = nt_ui_id("settings/quality_med");
        s_id_q_high = nt_ui_id("settings/quality_high");
        s_id_cell = nt_ui_id("table/row0_select");
        s_id_locked = nt_ui_id("settings/locked");
        s_ids_ready = true;
    }

    /* Settings panel: TOP_TO_BOTTOM column of rows. */
    CLAY({.id = CLAY_ID("settings-panel"),
          .layout = {.sizing = {CLAY_SIZING_FIXED(560), CLAY_SIZING_FIT(0)},
                     .padding = CLAY_PADDING_ALL(28),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 8,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = {30.0F, 34.0F, 42.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(10)}) {

        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {.bottom = 8}}}) { nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Settings", &g_title_style); }

        // #region general (checkbox + toggle)
        section_label("General");

        if (nt_ui_checkbox(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_vsync, "Enable VSync", &s_vsync, &s_check, &s_row_decl, true)) {
            nt_log_info("ui_stateful_demo: VSync -> %s", s_vsync ? "ON" : "off");
        }

        if (nt_ui_toggle(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_dark, "Dark Mode", &s_dark, &s_switch, &s_row_decl, true)) {
            nt_log_info("ui_stateful_demo: Dark Mode -> %s", s_dark ? "ON" : "off");
        }
        // #endregion

        // #region quality (shared-int radio group)
        section_label("Quality (exclusive group)");

        if (nt_ui_radio(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_q_low, "Low", &s_quality, 0, &s_radio, &s_row_decl, true)) {
            nt_log_info("ui_stateful_demo: quality -> Low");
        }
        if (nt_ui_radio(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_q_med, "Med", &s_quality, 1, &s_radio, &s_row_decl, true)) {
            nt_log_info("ui_stateful_demo: quality -> Med");
        }
        if (nt_ui_radio(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_q_high, "High", &s_quality, 2, &s_radio, &s_row_decl, true)) {
            nt_log_info("ui_stateful_demo: quality -> High");
        }
        // #endregion

        // #region table cell (indicator-only) + disabled
        section_label("Table row (indicator-only) + disabled");

        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(44)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 40, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
            /* Indicator-only checkbox (label == NULL) — fits in a table cell. */
            nt_ui_checkbox(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_cell, NULL, &s_cell_selected, &s_check, NULL, true);
            nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "<- no-label cell", &g_help_style);
        }

        /* DISABLED checkbox: enabled=false forces the dim cell + no click. */
        nt_ui_checkbox(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_locked, "Locked (disabled)", &s_locked, &s_check, &s_row_decl, false);
        // #endregion
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

    if (nt_input_key_is_pressed(NT_KEY_D)) {
        const bool now_on = !nt_ui_inspector_is_active(s_ctx);
        nt_ui_inspector_set_active(s_ctx, now_on);
        nt_log_info("ui_stateful_demo: inspector %s", now_on ? "ON" : "OFF");
    }

    nt_resource_step();
    nt_material_step();

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

        static const char *const quality_names[3] = {"Low", "Med", "High"};
        const char *quality_name = (s_quality >= 0 && s_quality <= 2) ? quality_names[s_quality] : "?";
        char status_text[200];
        (void)snprintf(status_text, sizeof status_text, "vsync=%s  dark=%s  quality=%s  cell=%s", s_vsync ? "on" : "off", s_dark ? "on" : "off", quality_name, s_cell_selected ? "yes" : "no");
        const char *help_text = "Click box or label to toggle  |  D = inspector  |  Esc quit";

        CLAY({.id = CLAY_ID("root"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .padding = CLAY_PADDING_ALL(24),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 16,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {18.0F, 18.0F, 22.0F, 255.0F}}) {

            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), status_text, &g_status_style);
            }

            declare_menu();

            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), help_text, &g_help_style);
            }
        }

        nt_ui_end(s_ctx);

        nt_ui_target_t target = nt_ui_scale_make_target(&scale);
        nt_ui_walk(s_ctx, &target);

        nt_ui_inspector_overlay_draw(s_ctx, &target, s_font, 16.0F);

        {
            mat4 stats_model;
            glm_mat4_identity(stats_model);
            glm_translate(stats_model, (vec3){10.0F, scale.logical_h - 20.0F, 0.0F});
            const float stats_color[4] = {0.8F, 0.9F, 0.8F, 1.0F};
            nt_stats_draw(s_text_material, s_font, (const float *)stats_model, 16.0F, stats_color);
            nt_text_renderer_flush();
        }
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
    config.app_name = "ui_stateful_demo";
    config.version = 1;

    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }

    g_nt_window.width = 1280;
    g_nt_window.height = 800;
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
    NT_ASSERT(s_ctx != NULL && "ui_stateful_demo: failed to create UI context");

    g_nt_app.target_dt = 0.0F;

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    s_pack_id = nt_hash32_str("ui_stateful_demo");
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/ui_stateful_demo/ui_stateful_demo.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/ui_stateful_demo.ntpack");
#endif

    s_sprite_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_UI_STATEFUL_DEMO_ATLAS, NT_ASSET_ATLAS);
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_UI_STATEFUL_DEMO_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_UI_STATEFUL_DEMO_FONT, NT_ASSET_FONT);

    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_sprite_vs_handle,
        .fs = s_sprite_fs_handle,
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "ui_stateful_demo_sprite",
    });
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_text_vs_handle,
        .fs = s_text_fs_handle,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .params[0] = {.name = "u_alpha_cutoff", .value = {NT_TEXT_ALPHA_CUTOFF_DEFAULT}},
        .param_count = 1,
        .label = "ui_stateful_demo_text",
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

    nt_log_info("ui_stateful_demo: starting (D=inspector, Esc quit)");

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
