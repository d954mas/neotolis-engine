/* UI Showcase -- Druid-style tabbed vitrine that supersedes the per-phase UI demos.
 * A static-const registry of widget tabs (name + info + code_url + render fn + nullable
 * props fn) drives a left tab-list -> right content-stage layout. The stage is wrapped in a
 * scroll container; a focused live properties panel renders only for tabs that set props_fn
 * (Slice9 insets/size, Progress value). Theme hot-swap is a single g_current pointer flip over
 * a game-owned ui_palette_t of per-widget style pointers (Model D, no engine API). Per-tab state
 * lives in a game-owned struct keyed by tab so it survives tab switches (IM re-feeds each frame).
 * Keys: Esc quit (native) | T palette dark/light | D inspector
 * Build packs: build_ui_showcase_packs build/examples/ui_showcase */

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
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_checkbox.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_panel.h"
#include "ui/nt_ui_progress.h"
#include "ui/nt_ui_scale.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_slider.h"
#include "window/nt_window.h"

#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "nt_pack_format.h"

#include "ui_showcase_assets.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

#include "clay.h"
// #endregion

// #region layers + reference resolution
/* Walker batches RECTs/IMAGEs first, then TEXT within each Clay zIndex. */
#define LAYER_BG 0
#define LAYER_IMG 1
#define LAYER_TEXT 2

#define UI_REF_W 1280.0F
#define UI_REF_H 800.0F
// #endregion

// #region forward decls
typedef struct tab_state tab_state_t;
struct nt_ui_context;
// #endregion

// #region palette label styles (vary by palette)
static const nt_ui_label_style_t g_h1_dark = {.font_id = 0, .font_size = 40, .color = {255.0F, 255.0F, 255.0F, 255.0F}};
static const nt_ui_label_style_t g_body_dark = {.font_id = 0, .font_size = 22, .color = {225.0F, 228.0F, 235.0F, 255.0F}};
static const nt_ui_label_style_t g_caption_dark = {.font_id = 0, .font_size = 16, .color = {165.0F, 170.0F, 182.0F, 255.0F}};

static const nt_ui_label_style_t g_h1_light = {.font_id = 0, .font_size = 40, .color = {18.0F, 18.0F, 24.0F, 255.0F}};
static const nt_ui_label_style_t g_body_light = {.font_id = 0, .font_size = 22, .color = {28.0F, 30.0F, 38.0F, 255.0F}};
static const nt_ui_label_style_t g_caption_light = {.font_id = 0, .font_size = 16, .color = {90.0F, 92.0F, 104.0F, 255.0F}};
// #endregion

// #region palette widget styles (filled with late-bound atlas refs at init)
/* Runtime styles: const baselines copied + late-bound refs filled upfront (init_styles).
 * Mutable so the engine memoizes the resolved region index in place. Dark + light variants. */
static nt_ui_button_style_t s_btn_primary_dark, s_btn_primary_light;
static nt_ui_button_style_t s_btn_secondary_dark, s_btn_secondary_light;
static nt_ui_checkbox_style_t s_check_dark, s_check_light;
static nt_ui_checkbox_style_t s_radio_dark, s_radio_light;
static nt_ui_checkbox_style_t s_switch_dark, s_switch_light;
static nt_ui_slider_style_t s_slider_dark, s_slider_light;
static nt_ui_progress_style_t s_progress_dark, s_progress_light;
static nt_ui_scroll_style_t s_scroll_dark, s_scroll_light;

/* Slice9 panel image style (untinted, atlas-default slice9). */
static const nt_ui_image_style_t g_panel_img_style = {.color_packed = 0xFFFFFFFF, .slice9_scale = 1.0F};
// #endregion

// #region ui_palette_t (per-widget style pointers; pointer flip is the whole hot-swap)
typedef struct {
    const nt_ui_label_style_t *h1, *body, *caption;
    const nt_ui_button_style_t *btn_primary, *btn_secondary;
    const nt_ui_checkbox_style_t *check, *radio, *toggle; /* one shared checkbox style each */
    const nt_ui_slider_style_t *slider;
    const nt_ui_progress_style_t *progress;
    const nt_ui_scroll_style_t *scroll;
    /* modal style pointer joins the palette in Plan 04. */
    Clay_Color bg, panel, list_bg, list_sel;
    const char *name;
} ui_palette_t;

/* Pointers into the runtime style storage (filled in build_palettes after init_styles). */
static ui_palette_t g_dark = {
    .h1 = &g_h1_dark,
    .body = &g_body_dark,
    .caption = &g_caption_dark,
    .btn_primary = &s_btn_primary_dark,
    .btn_secondary = &s_btn_secondary_dark,
    .check = &s_check_dark,
    .radio = &s_radio_dark,
    .toggle = &s_switch_dark,
    .slider = &s_slider_dark,
    .progress = &s_progress_dark,
    .scroll = &s_scroll_dark,
    .bg = {18.0F, 18.0F, 22.0F, 255.0F},
    .panel = {30.0F, 34.0F, 42.0F, 255.0F},
    .list_bg = {24.0F, 26.0F, 34.0F, 255.0F},
    .list_sel = {52.0F, 92.0F, 140.0F, 255.0F},
    .name = "DARK",
};
static ui_palette_t g_light = {
    .h1 = &g_h1_light,
    .body = &g_body_light,
    .caption = &g_caption_light,
    .btn_primary = &s_btn_primary_light,
    .btn_secondary = &s_btn_secondary_light,
    .check = &s_check_light,
    .radio = &s_radio_light,
    .toggle = &s_switch_light,
    .slider = &s_slider_light,
    .progress = &s_progress_light,
    .scroll = &s_scroll_light,
    .bg = {238.0F, 240.0F, 246.0F, 255.0F},
    .panel = {255.0F, 255.0F, 255.0F, 255.0F},
    .list_bg = {224.0F, 227.0F, 235.0F, 255.0F},
    .list_sel = {150.0F, 188.0F, 240.0F, 255.0F},
    .name = "LIGHT",
};

/* Pointer write IS the hot-swap -- engine ships no nt_ui_set_theme (Model D). */
static const ui_palette_t *g_current = &g_dark;
// #endregion

// #region per-tab state (game-owned; re-fed each frame so it survives tab switches)
/* Per-tab param structs that the focused props panels read+write. */
typedef struct {
    int inset_l, inset_r, inset_t, inset_b; /* slice9 override insets (px) */
    int target_w, target_h;                 /* panel target size driven by the panel */
} slice9_params_t;

typedef struct {
    float value;    /* progress bar value 0..1 driven by the panel slider */
    bool auto_anim; /* auto-ramp 0->1->0 instead of manual */
    bool ramp_up;
} progress_params_t;

/* All per-tab logical state (slider floats, checkbox bools, radio int, scroll positions,
 * plus the props-panel param structs). Re-fed each frame -> retained across tab switches. */
struct tab_state {
    /* Toggles tab. */
    bool cb_value;
    int radio_sel;
    bool toggle_value;
    /* Sliders tab. */
    float slider_float;
    int slider_int;
    /* Props params. */
    slice9_params_t s9;
    progress_params_t prog;
};

static struct tab_state s_state = {
    .cb_value = true,
    .radio_sel = 1,
    .toggle_value = false,
    .slider_float = 0.65F,
    .slider_int = 4,
    .s9 = {.inset_l = 10, .inset_r = 10, .inset_t = 10, .inset_b = 10, .target_w = 480, .target_h = 200},
    .prog = {.value = 0.4F, .auto_anim = false, .ramp_up = true},
};
// #endregion

// #region registry types (Druid-style metadata; nullable props_fn per D-60-13)
typedef void (*showcase_render_fn)(struct nt_ui_context *ctx, tab_state_t *st);
typedef void (*showcase_props_fn)(struct nt_ui_context *ctx, tab_state_t *st); /* nullable */
typedef struct {
    const char *name;
    const char *info;
    const char *code_url;
    showcase_render_fn render;
    showcase_props_fn props_fn; /* NULL = no focused panel for this tab */
} showcase_entry_t;
// #endregion

// #region stable ids
static uint32_t s_id_cb;
static uint32_t s_id_radio_a, s_id_radio_b, s_id_radio_c;
static uint32_t s_id_toggle;
static uint32_t s_id_slider_f, s_id_slider_i;
static uint32_t s_id_progress;
static uint32_t s_id_stage_scroll;
static uint32_t s_id_inner_scroll;
static uint32_t s_id_props_il, s_id_props_ir, s_id_props_it, s_id_props_ib;
static uint32_t s_id_props_w, s_id_props_h;
static uint32_t s_id_props_value;
static uint32_t s_id_tab_btn_base; /* per-tab list buttons salt from this + index */
static bool s_ids_ready;
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

/* Slice9 panel art refs (late-bound, memoized on first emit). */
static nt_atlas_region_ref_t s_panel_beige_ref;
static nt_atlas_region_ref_t s_panel_blue_ref;
static nt_atlas_region_ref_t s_panel_brown_ref;
static nt_atlas_region_ref_t s_button_green_ref;

static int s_active_tab;
// #endregion

// #region reusable focused-panel helper (game-side; built from existing nt_ui widgets)
/* A titled control strip the props_fn populates. Same spirit as the palette swap -- NO engine
 * symbol. begin opens a padded panel container with a title; end closes it. */
static void showcase_panel_begin(nt_ui_context_t *ctx, const char *title) {
    Clay_ElementDeclaration decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIT(0)},
                   .padding = CLAY_PADDING_ALL(14),
                   .layoutDirection = CLAY_TOP_TO_BOTTOM,
                   .childGap = 8,
                   .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
        .backgroundColor = g_current->panel,
        .cornerRadius = CLAY_CORNER_RADIUS(10),
    };
    nt_ui_group_begin(ctx, NT_UI_DATA_LAYER(LAYER_BG), &decl);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), title, g_current->body);
}
static void showcase_panel_end(nt_ui_context_t *ctx) { nt_ui_group_end(ctx); }
// #endregion

// #region widget tab render fns (filled in Task 2; stubs in Task 1)
static void render_labels(nt_ui_context_t *ctx, tab_state_t *st);
static void render_button_primary(nt_ui_context_t *ctx, tab_state_t *st);
static void render_button_secondary(nt_ui_context_t *ctx, tab_state_t *st);
static void render_button_disabled(nt_ui_context_t *ctx, tab_state_t *st);
static void render_slice9(nt_ui_context_t *ctx, tab_state_t *st);
static void render_toggles(nt_ui_context_t *ctx, tab_state_t *st);
static void render_sliders(nt_ui_context_t *ctx, tab_state_t *st);
static void render_scroll(nt_ui_context_t *ctx, tab_state_t *st);
static void props_slice9(nt_ui_context_t *ctx, tab_state_t *st);
static void props_progress(nt_ui_context_t *ctx, tab_state_t *st);
// #endregion

// #region registry (one entry per widget category present in this plan)
/* Modals + Stress tabs land in Plan 04 (need the modal helper + stress harness). */
static const showcase_entry_t g_tabs[] = {
    {"Labels", "h1 / body / caption label variants, themed via the palette.", "examples/ui_showcase/main.c:render_labels", render_labels, NULL},
    {"Buttons: Primary", "Primary slice9 button (idle/hover/pressed/disabled).", "examples/ui_showcase/main.c:render_button_primary", render_button_primary, NULL},
    {"Buttons: Secondary", "Secondary (alternate art) button variant.", "examples/ui_showcase/main.c:render_button_secondary", render_button_secondary, NULL},
    {"Buttons: Disabled", "Disabled button (interaction short-circuit + dim).", "examples/ui_showcase/main.c:render_button_disabled", render_button_disabled, NULL},
    {"Images & Slice9", "Slice9 panels at 3 sizes (corners stay crisp) + a live insets/size panel.", "examples/ui_showcase/main.c:render_slice9", render_slice9, props_slice9},
    {"Toggles & Radios", "Checkbox + exclusive radio group + sliding toggle.", "examples/ui_showcase/main.c:render_toggles", render_toggles, NULL},
    {"Sliders & Progress", "Float + int sliders + a progress bar driven by a live value panel.", "examples/ui_showcase/main.c:render_sliders", render_sliders, props_progress},
    {"Scroll", "Tall scroll list with a nested inner scroll (capture-steal).", "examples/ui_showcase/main.c:render_scroll", render_scroll, NULL},
};
#define TAB_COUNT ((int)(sizeof g_tabs / sizeof g_tabs[0]))
// #endregion

// #region init styles (fill late-bound refs into dark/light variants)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void init_styles(void) {
    const nt_atlas_region_ref_t box = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_BOX_OFF.value);
    const nt_atlas_region_ref_t check = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_CHECKMARK.value);
    const nt_atlas_region_ref_t ring = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_RADIO_RING.value);
    const nt_atlas_region_ref_t dot = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_RADIO_DOT.value);
    const nt_atlas_region_ref_t track_off = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_TRACK_OFF.value);
    const nt_atlas_region_ref_t track_on = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_TRACK_ON.value);
    const nt_atlas_region_ref_t thumb = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_THUMB.value);
    const nt_atlas_region_ref_t bar_track = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_BAR_TRACK.value);
    const nt_atlas_region_ref_t bar_fill = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_BAR_FILL_SMOOTH.value);
    const nt_atlas_region_ref_t scroll_track = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_SCROLL_TRACK.value);
    const nt_atlas_region_ref_t bar_thumb = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_BAR_THUMB.value);
    const nt_atlas_region_ref_t btn_blue = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_BUTTON_BLUE.value);

    s_panel_beige_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_PANEL_BEIGE.value);
    s_panel_blue_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_PANEL_BLUE.value);
    s_panel_brown_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_PANEL_BROWN.value);
    s_button_green_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_BUTTON_GREEN.value);

    /* ---- Buttons: primary (blue slice9) + secondary (green slice9). ---- */
    nt_ui_button_style_t btn_base = {
        .idle = {.bg_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 1.0F},
        .hover = {.bg_tint = 0xFFFFFFFF, .scale = 1.05F, .opacity = 1.0F},
        .pressed = {.bg_tint = 0xFFFFFFFF, .scale = 0.95F, .offset_y = 2.0F, .opacity = 1.0F},
        .disabled = {.bg_tint = 0xFFFFFFFF, .scale = 1.0F, .opacity = 0.4F},
        .transition_speed = 12.0F,
        .hit_padding_lrtb = {16, 16, 16, 16},
        .slice9_scale = 1.0F,
    };
    s_btn_primary_dark = btn_base;
    s_btn_primary_dark.idle.bg = btn_blue;
    s_btn_primary_light = s_btn_primary_dark;

    s_btn_secondary_dark = btn_base;
    s_btn_secondary_dark.idle.bg = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_BUTTON_GREEN.value);
    s_btn_secondary_light = s_btn_secondary_dark;

    /* ---- Checkbox: box + checkmark pop. Light variant darkens text. ---- */
    nt_ui_checkbox_style_t check_base = nt_ui_checkbox_style_defaults();
    check_base.box_w = 32;
    check_base.box_h = 32;
    check_base.overlay_w = 26;
    check_base.overlay_h = 26;
    check_base.gap = 14;
    check_base.value_speed = 22.0F;
    check_base.text_base = (nt_ui_label_style_t){.font_id = 0, .font_size = 22, .color = {220.0F, 223.0F, 230.0F, 255.0F}};
    check_base.unchecked[NT_UI_CB_IDLE].box = box;
    check_base.checked[NT_UI_CB_IDLE].box = box;
    check_base.checked[NT_UI_CB_IDLE].check = check;
    check_base.checked[NT_UI_CB_IDLE].check_tint = 0xFF7CE08C;
    s_check_dark = check_base;
    s_check_light = check_base;
    s_check_light.text_base.color = (Clay_Color){30.0F, 32.0F, 40.0F, 255.0F};

    /* ---- Radio: ring + dot. ---- */
    nt_ui_checkbox_style_t radio_base = check_base;
    radio_base.box_w = 30;
    radio_base.box_h = 30;
    radio_base.overlay_w = 16;
    radio_base.overlay_h = 16;
    radio_base.gap = 12;
    radio_base.unchecked[NT_UI_CB_IDLE].box = ring;
    radio_base.unchecked[NT_UI_CB_IDLE].check = (nt_atlas_region_ref_t){0};
    radio_base.checked[NT_UI_CB_IDLE].box = ring;
    radio_base.checked[NT_UI_CB_IDLE].check = dot;
    radio_base.checked[NT_UI_CB_IDLE].check_tint = 0xFF6CC0F0;
    s_radio_dark = radio_base;
    s_radio_light = radio_base;
    s_radio_light.text_base.color = (Clay_Color){30.0F, 32.0F, 40.0F, 255.0F};

    /* ---- Toggle: track recolors off/on; thumb slides. ---- */
    nt_ui_checkbox_style_t switch_base = check_base;
    switch_base.box_w = 64;
    switch_base.box_h = 32;
    switch_base.overlay_w = 22;
    switch_base.overlay_h = 22;
    switch_base.thumb_pad = 5;
    switch_base.gap = 16;
    switch_base.label_side = 1;
    switch_base.value_speed = 12.0F;
    switch_base.unchecked[NT_UI_CB_IDLE].box = track_off;
    switch_base.unchecked[NT_UI_CB_IDLE].check = thumb;
    switch_base.checked[NT_UI_CB_IDLE].box = track_on;
    switch_base.checked[NT_UI_CB_IDLE].check = thumb;
    switch_base.checked[NT_UI_CB_IDLE].check_tint = 0xFFFFFFFF;
    s_switch_dark = switch_base;
    s_switch_light = switch_base;
    s_switch_light.text_base.color = (Clay_Color){30.0F, 32.0F, 40.0F, 255.0F};

    /* ---- Slider: track + smooth fill + thumb. ---- */
    nt_ui_slider_style_t slider_base = nt_ui_slider_style_defaults();
    slider_base.track_w = 320;
    slider_base.track_h = 18;
    slider_base.thumb_w = 28;
    slider_base.thumb_h = 28;
    slider_base.value_speed = 18.0F;
    slider_base.hit_padding_lrtb[2] = 13;
    slider_base.hit_padding_lrtb[3] = 13;
    slider_base.states[NT_UI_SLIDER_IDLE].track = bar_track;
    slider_base.states[NT_UI_SLIDER_IDLE].fill = bar_fill;
    slider_base.states[NT_UI_SLIDER_IDLE].thumb = thumb;
    s_slider_dark = slider_base;
    s_slider_light = slider_base;

    /* ---- Progress: track + smooth STRETCH slice9 fill. ---- */
    nt_ui_progress_style_t progress_base = nt_ui_progress_style_defaults();
    progress_base.track_w = 320;
    progress_base.track_h = 24;
    progress_base.value_speed = 6.0F;
    progress_base.track = bar_track;
    progress_base.fill = bar_fill;
    s_progress_dark = progress_base;
    s_progress_light = progress_base;

    /* ---- Scroll: recessed slot track + capsule thumb, ALWAYS bar. ---- */
    nt_ui_scroll_style_t scroll_base = nt_ui_scroll_style_defaults();
    scroll_base.bar_visibility = NT_UI_SCROLLBAR_ALWAYS;
    scroll_base.bar_thickness = 12.0F;
    scroll_base.bar_thumb_min_px = 28.0F;
    scroll_base.track_tint = 0xC0FFFFFF;
    scroll_base.thumb_tint = 0xFFFFFFFF;
    scroll_base.track_ref = scroll_track;
    scroll_base.thumb_ref = bar_thumb;
    s_scroll_dark = scroll_base;
    s_scroll_light = scroll_base;
}
// #endregion

// #region binding
static void try_bind_resources(void) {
    if (s_atlas_bound && s_font_bound) {
        return;
    }
    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        const uint32_t white = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS__WHITE.value);
        NT_ASSERT(white != NT_ATLAS_INVALID_REGION);
        nt_ui_set_atlas_white_region(s_ctx, s_atlas_handle, white);
        s_atlas_bound = true;
        nt_log_info("ui_showcase: atlas white region bound");
    }
    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        nt_font_add(s_font, s_font_resource);
        nt_ui_set_font(s_ctx, 0U, s_font);
        s_font_bound = true;
        nt_log_info("ui_showcase: font bound at slot 0");
    }
}
// #endregion

// #region widget tab render fns (Task 1 stubs -- Task 2 fills these)
static void render_labels(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Labels (stub)", g_current->body);
}
static void render_button_primary(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Button primary (stub)", g_current->body);
}
static void render_button_secondary(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Button secondary (stub)", g_current->body);
}
static void render_button_disabled(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Button disabled (stub)", g_current->body);
}
static void render_slice9(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(60)}}}) { nt_ui_image(ctx, NT_UI_DATA_LAYER(LAYER_IMG), &s_panel_beige_ref, &g_panel_img_style, NULL); }
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Slice9 (stub)", g_current->body);
}
static void render_toggles(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Toggles (stub)", g_current->body);
}
static void render_sliders(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Sliders (stub)", g_current->body);
}
static void render_scroll(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Scroll (stub)", g_current->body);
}
static void props_slice9(nt_ui_context_t *ctx, tab_state_t *st) {
    showcase_panel_begin(ctx, "Slice9 (stub)");
    (void)st;
    showcase_panel_end(ctx);
}
static void props_progress(nt_ui_context_t *ctx, tab_state_t *st) {
    showcase_panel_begin(ctx, "Progress (stub)");
    (void)st;
    showcase_panel_end(ctx);
}
// #endregion

// #region tab list + stage layout
static void ensure_ids(void) {
    if (s_ids_ready) {
        return;
    }
    s_id_cb = nt_ui_id("showcase/cb");
    s_id_radio_a = nt_ui_id("showcase/radio_a");
    s_id_radio_b = nt_ui_id("showcase/radio_b");
    s_id_radio_c = nt_ui_id("showcase/radio_c");
    s_id_toggle = nt_ui_id("showcase/toggle");
    s_id_slider_f = nt_ui_id("showcase/slider_f");
    s_id_slider_i = nt_ui_id("showcase/slider_i");
    s_id_progress = nt_ui_id("showcase/progress");
    s_id_stage_scroll = nt_ui_id("showcase/stage_scroll");
    s_id_inner_scroll = nt_ui_id("showcase/inner_scroll");
    s_id_props_il = nt_ui_id("showcase/props_il");
    s_id_props_ir = nt_ui_id("showcase/props_ir");
    s_id_props_it = nt_ui_id("showcase/props_it");
    s_id_props_ib = nt_ui_id("showcase/props_ib");
    s_id_props_w = nt_ui_id("showcase/props_w");
    s_id_props_h = nt_ui_id("showcase/props_h");
    s_id_props_value = nt_ui_id("showcase/props_value");
    s_id_tab_btn_base = nt_ui_id("showcase/tab_btn");
    s_ids_ready = true;
}

/* Left tab list: one button per registry entry; clicking selects the active tab. */
static void declare_tab_list(nt_ui_context_t *ctx) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(10),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 6,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = g_current->list_bg}) {
        for (int i = 0; i < TAB_COUNT; ++i) {
            const bool selected = (i == s_active_tab);
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(38)}, .padding = {.left = 10, .right = 10}, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
                  .backgroundColor = selected ? g_current->list_sel : g_current->list_bg,
                  .cornerRadius = CLAY_CORNER_RADIUS(6)}) {
                /* Use the primary button as a transparent-ish clickable row via begin/end. */
                if (nt_ui_button(ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_tab_btn_base + (uint32_t)i, &s_btn_primary_dark,
                                 &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(8), CLAY_SIZING_FIXED(8)}}}, true)) {
                    s_active_tab = i;
                }
                nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), g_tabs[i].name, g_current->caption);
            }
        }
    }
}

/* Right stage: title/info, the scroll-wrapped content, and (if set) the focused props panel. */
static void declare_stage(nt_ui_context_t *ctx) {
    NT_ASSERT(s_active_tab >= 0 && s_active_tab < TAB_COUNT && "active tab out of range");
    const showcase_entry_t *e = &g_tabs[s_active_tab];

    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 10,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), e->name, g_current->h1);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), e->info, g_current->caption);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), e->code_url, g_current->caption);

        /* Content stage + (optional) props panel side by side. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 16, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
            /* Scroll-wrapped content (DEMO-12): exercises the scissor stack. */
            nt_ui_scroll_begin(ctx, NULL, s_id_stage_scroll, g_current->scroll,
                               &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(8)}});
            {
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 10}}) { e->render(ctx, &s_state); }
            }
            nt_ui_scroll_end(ctx);

            if (e->props_fn != NULL) {
                e->props_fn(ctx, &s_state);
            }
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

    if (nt_input_key_is_pressed(NT_KEY_T)) {
        g_current = (g_current == &g_dark) ? &g_light : &g_dark;
        nt_log_info("ui_showcase: palette -> %s", g_current->name);
    }
    if (nt_input_key_is_pressed(NT_KEY_D)) {
        const bool now_on = !nt_ui_inspector_is_active(s_ctx);
        nt_ui_inspector_set_active(s_ctx, now_on);
        nt_log_info("ui_showcase: inspector %s", now_on ? "ON" : "OFF");
    }

    nt_resource_step();
    nt_material_step();

    /* Auto-animate progress when its panel toggle is on. */
    if (s_state.prog.auto_anim) {
        s_state.prog.value += (s_state.prog.ramp_up ? 1.0F : -1.0F) * 0.3F * g_nt_app.dt;
        if (s_state.prog.value >= 1.0F) {
            s_state.prog.value = 1.0F;
            s_state.prog.ramp_up = false;
        } else if (s_state.prog.value <= 0.0F) {
            s_state.prog.value = 0.0F;
            s_state.prog.ramp_up = true;
        }
    }

    if (!s_atlas_bound) {
        try_bind_resources();
        if (s_atlas_bound) {
            init_styles();
        }
    } else {
        try_bind_resources();
    }

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
        .clear_color = {g_current->bg.r / 255.0F, g_current->bg.g / 255.0F, g_current->bg.b / 255.0F, 1.0F},
        .clear_depth = 1.0F,
    });

    nt_font_step();

    const nt_material_info_t *sprite_info = nt_material_get_info(s_sprite_material);
    const nt_material_info_t *text_info = nt_material_get_info(s_text_material);
    const bool can_render = s_atlas_bound && s_font_bound && sprite_info && sprite_info->ready && text_info && text_info->ready;

    if (can_render) {
        nt_gfx_update_buffer(s_frame_ubo, &uniforms, sizeof(uniforms));
        nt_gfx_bind_uniform_buffer(s_frame_ubo, 0);

        ensure_ids();

        const nt_pointer_t mouse_logical = nt_ui_scale_apply_pointer(&scale, g_nt_input.pointers[0]);
        nt_ui_begin(s_ctx, scale.logical_w, scale.logical_h, g_nt_app.dt, &mouse_logical, 1);

        CLAY({.id = CLAY_ID("root"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .padding = CLAY_PADDING_ALL(12),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 8,
                         .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
              .backgroundColor = g_current->bg}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "UI Showcase  |  [T] palette  [D] inspector  [Esc] quit", g_current->caption);
            }
            /* Left tab list -> right content stage. */
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 12, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
                declare_tab_list(s_ctx);
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .backgroundColor = g_current->panel, .cornerRadius = CLAY_CORNER_RADIUS(10)}) { declare_stage(s_ctx); }
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
    config.app_name = "ui_showcase";
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
    NT_ASSERT(s_ctx != NULL && "ui_showcase: failed to create UI context");

    g_nt_app.target_dt = 0.0F;

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    s_pack_id = nt_hash32_str("ui_showcase");
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/ui_showcase/ui_showcase.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/ui_showcase.ntpack");
#endif

    s_sprite_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_UI_SHOWCASE_ATLAS, NT_ASSET_ATLAS);
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_UI_SHOWCASE_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_UI_SHOWCASE_FONT, NT_ASSET_FONT);

    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_sprite_vs_handle,
        .fs = s_sprite_fs_handle,
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "ui_showcase_sprite",
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
        .label = "ui_showcase_text",
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

    nt_log_info("ui_showcase: starting (T=palette, D=inspector, Esc quit)");

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
