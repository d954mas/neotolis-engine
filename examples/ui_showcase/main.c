/* UI Showcase -- tabbed vitrine demoing the engine's UI widgets; see README. */

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
#include "ui/nt_ui_modal.h"
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
/* Title in the header (smaller than the per-tab h1) + selected tab-row label (near-white pop). */
static const nt_ui_label_style_t g_title_dark = {.font_id = 0, .font_size = 26, .color = {255.0F, 255.0F, 255.0F, 255.0F}};
static const nt_ui_label_style_t g_row_sel_dark = {.font_id = 0, .font_size = 16, .color = {245.0F, 247.0F, 252.0F, 255.0F}};
/* Source-link line: dimmer + distinct from body/caption so it reads as a reference, not copy. */
static const nt_ui_label_style_t g_link_dark = {.font_id = 0, .font_size = 14, .color = {110.0F, 150.0F, 200.0F, 255.0F}};

static const nt_ui_label_style_t g_h1_light = {.font_id = 0, .font_size = 40, .color = {18.0F, 18.0F, 24.0F, 255.0F}};
static const nt_ui_label_style_t g_body_light = {.font_id = 0, .font_size = 22, .color = {28.0F, 30.0F, 38.0F, 255.0F}};
static const nt_ui_label_style_t g_caption_light = {.font_id = 0, .font_size = 16, .color = {90.0F, 92.0F, 104.0F, 255.0F}};
static const nt_ui_label_style_t g_title_light = {.font_id = 0, .font_size = 26, .color = {18.0F, 18.0F, 24.0F, 255.0F}};
static const nt_ui_label_style_t g_row_sel_light = {.font_id = 0, .font_size = 16, .color = {12.0F, 28.0F, 56.0F, 255.0F}};
static const nt_ui_label_style_t g_link_light = {.font_id = 0, .font_size = 14, .color = {56.0F, 100.0F, 170.0F, 255.0F}};
// #endregion

// #region palette widget styles (filled with late-bound atlas refs at init)
/* Runtime styles: const baselines copied + late-bound refs filled upfront (init_styles).
 * Mutable so the engine memoizes the resolved region index in place. Dark + light variants. */
static nt_ui_button_style_t s_btn_primary_dark, s_btn_primary_light;
static nt_ui_button_style_t s_btn_secondary_dark, s_btn_secondary_light;
/* Tab-row click target: no atlas art (idle bg_tint == no-tint => transparent so the row's
 * selected/unselected CLAY bg shows), hover/pressed paint a translucent-white lighten overlay
 * (theme-agnostic, reads on both palettes). */
static nt_ui_button_style_t s_listrow_dark, s_listrow_light;
static nt_ui_checkbox_style_t s_check_dark, s_check_light;
static nt_ui_checkbox_style_t s_radio_dark, s_radio_light;
static nt_ui_checkbox_style_t s_switch_dark, s_switch_light;
static nt_ui_slider_style_t s_slider_dark, s_slider_light;
static nt_ui_progress_style_t s_progress_dark, s_progress_light;
static nt_ui_scroll_style_t s_scroll_dark, s_scroll_light;

/* Slice9 panel image style (untinted, atlas-default slice9). */
static const nt_ui_image_style_t g_panel_img_style = {.color_packed = 0xFFFFFFFF, .slice9_scale = 1.0F};

/* Per-palette base modal styles: the palette pointer flip restyles the modal on hot-swap
 * (D-60-14). The Modals tab seeds a RUNTIME style from these each frame, then overlays the
 * props-panel transition/ease/scale-start/backdrop-alpha (D-60-13) before passing to nt_ui_modal. */
static nt_ui_modal_style_t s_modal_dark, s_modal_light;
// #endregion

// #region ui_palette_t (per-widget style pointers; pointer flip is the whole hot-swap)
typedef struct {
    const nt_ui_label_style_t *h1, *body, *caption;
    const nt_ui_label_style_t *title, *row_sel, *link; /* header title, selected tab-row label, source-link */
    /* Widget styles are non-const: the engine memoizes the resolved atlas region index in
     * place on first emit, so these point at mutable runtime storage. */
    nt_ui_button_style_t *btn_primary, *btn_secondary, *listrow;
    nt_ui_checkbox_style_t *check, *radio, *toggle; /* one shared checkbox style each */
    nt_ui_slider_style_t *slider;
    nt_ui_progress_style_t *progress;
    const nt_ui_scroll_style_t *scroll;
    /* Modal base style: restyles on the palette pointer flip (D-60-14). */
    const nt_ui_modal_style_t *modal;
    Clay_Color bg, panel, list_bg, list_sel, accent, border;
    const char *name;
} ui_palette_t;

/* Pointers into the runtime style storage (filled in build_palettes after init_styles). */
static ui_palette_t g_dark = {
    .h1 = &g_h1_dark,
    .body = &g_body_dark,
    .caption = &g_caption_dark,
    .title = &g_title_dark,
    .row_sel = &g_row_sel_dark,
    .link = &g_link_dark,
    .btn_primary = &s_btn_primary_dark,
    .btn_secondary = &s_btn_secondary_dark,
    .listrow = &s_listrow_dark,
    .check = &s_check_dark,
    .radio = &s_radio_dark,
    .toggle = &s_switch_dark,
    .slider = &s_slider_dark,
    .progress = &s_progress_dark,
    .scroll = &s_scroll_dark,
    .modal = &s_modal_dark,
    .bg = {18.0F, 18.0F, 22.0F, 255.0F},
    .panel = {30.0F, 34.0F, 42.0F, 255.0F},
    .list_bg = {24.0F, 26.0F, 34.0F, 255.0F},
    .list_sel = {46.0F, 98.0F, 158.0F, 255.0F},
    .accent = {86.0F, 156.0F, 230.0F, 255.0F},
    .border = {58.0F, 64.0F, 78.0F, 255.0F},
    .name = "DARK",
};
static ui_palette_t g_light = {
    .h1 = &g_h1_light,
    .body = &g_body_light,
    .caption = &g_caption_light,
    .title = &g_title_light,
    .row_sel = &g_row_sel_light,
    .link = &g_link_light,
    .btn_primary = &s_btn_primary_light,
    .btn_secondary = &s_btn_secondary_light,
    .listrow = &s_listrow_light,
    .check = &s_check_light,
    .radio = &s_radio_light,
    .toggle = &s_switch_light,
    .slider = &s_slider_light,
    .progress = &s_progress_light,
    .scroll = &s_scroll_light,
    .modal = &s_modal_light,
    .bg = {238.0F, 240.0F, 246.0F, 255.0F},
    .panel = {255.0F, 255.0F, 255.0F, 255.0F},
    .list_bg = {224.0F, 227.0F, 235.0F, 255.0F},
    .list_sel = {138.0F, 182.0F, 240.0F, 255.0F},
    .accent = {52.0F, 120.0F, 214.0F, 255.0F},
    .border = {206.0F, 210.0F, 220.0F, 255.0F},
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

/* Modal props (D-60-13): transition selector + tween/anim params the panel drives live. */
typedef struct {
    int transition;       /* 0 = scale-pop, 1 = fade, 2 = slide (segmented control) */
    float ease_speed;     /* open/close tween value_speed */
    float scale_start;    /* scale-pop start (~0.85..1.0) */
    float backdrop_alpha; /* peak backdrop opacity 0..1 */
} modal_params_t;

/* Stress props (D-60-13): label count driving the render_stress loop. */
typedef struct {
    int label_count; /* segmented control: 50 / 100 / 200 / 400 */
} stress_params_t;

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
    /* Modals tab. */
    bool confirm_open;
    bool nested_open;
    modal_params_t modal;
    /* Stress tab. */
    stress_params_t stress;
};

static struct tab_state s_state = {
    .cb_value = true,
    .radio_sel = 1,
    .toggle_value = false,
    .slider_float = 0.65F,
    .slider_int = 4,
    .s9 = {.inset_l = 10, .inset_r = 10, .inset_t = 10, .inset_b = 10, .target_w = 480, .target_h = 200},
    .prog = {.value = 0.4F, .auto_anim = false, .ramp_up = true},
    .confirm_open = false,
    .nested_open = false,
    .modal = {.transition = 0, .ease_speed = 14.0F, .scale_start = 0.92F, .backdrop_alpha = 0.55F},
    .stress = {.label_count = 200},
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
static uint32_t s_id_modal_confirm, s_id_modal_nested;
static uint32_t s_id_modal_show_btn, s_id_modal_ok_btn, s_id_modal_cancel_btn, s_id_modal_nested_btn, s_id_modal_nested_close_btn;
static uint32_t s_id_props_ease, s_id_props_scale, s_id_props_backdrop;
static uint32_t s_id_theme_btn;
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
static void render_modals(nt_ui_context_t *ctx, tab_state_t *st);
static void render_stress(nt_ui_context_t *ctx, tab_state_t *st);
static void props_slice9(nt_ui_context_t *ctx, tab_state_t *st);
static void props_progress(nt_ui_context_t *ctx, tab_state_t *st);
static void props_modal(nt_ui_context_t *ctx, tab_state_t *st);
static void props_stress(nt_ui_context_t *ctx, tab_state_t *st);
// #endregion

// #region registry (one entry per widget category present in this plan)
/* 8 logical categories (DEMO-02). Buttons render as 3 sibling entries (D-60-13). */
static const showcase_entry_t g_tabs[] = {
    {"Labels", "h1 / body / caption label variants, themed via the palette.", "examples/ui_showcase/main.c:render_labels", render_labels, NULL},
    {"Buttons: Primary", "Primary slice9 button (idle/hover/pressed/disabled).", "examples/ui_showcase/main.c:render_button_primary", render_button_primary, NULL},
    {"Buttons: Secondary", "Secondary (alternate art) button variant.", "examples/ui_showcase/main.c:render_button_secondary", render_button_secondary, NULL},
    {"Buttons: Disabled", "Disabled button (interaction short-circuit + dim).", "examples/ui_showcase/main.c:render_button_disabled", render_button_disabled, NULL},
    {"Images & Slice9", "Slice9 panels at 3 sizes (corners stay crisp) + a live insets/size panel.", "examples/ui_showcase/main.c:render_slice9", render_slice9, props_slice9},
    {"Toggles & Radios", "Checkbox + exclusive radio group + sliding toggle.", "examples/ui_showcase/main.c:render_toggles", render_toggles, NULL},
    {"Sliders & Progress", "Float + int sliders + a progress bar driven by a live value panel.", "examples/ui_showcase/main.c:render_sliders", render_sliders, props_progress},
    {"Scroll", "Tall scroll list with a nested inner scroll (capture-steal).", "examples/ui_showcase/main.c:render_scroll", render_scroll, NULL},
    {"Modals", "Confirm + nested depth-2 modal; Esc/backdrop close; live transition panel.", "examples/ui_showcase/main.c:render_modals", render_modals, props_modal},
    {"Stress", "N labels @14pt + live frame gpu_ms / draw-calls; label-count panel.", "examples/ui_showcase/main.c:render_stress", render_stress, props_stress},
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

    /* ---- Tab-row click target: no art, idle transparent, hover/pressed translucent-white lighten. ---- */
    /* idle.bg.atlas.id stays 0 => text-only button (no IMAGE); bg_tint 0xFFFFFFFF unpacks to no-tint
     * (transparent), so the row's selected/unselected CLAY bg shows through. No scale-pop on rows. */
    nt_ui_button_style_t listrow_base = {
        .idle = {.bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F},
        .hover = {.bg_tint = 0x22FFFFFFU, .scale = 1.0F, .opacity = 1.0F},   /* ~13% white lighten */
        .pressed = {.bg_tint = 0x3AFFFFFFU, .scale = 1.0F, .opacity = 1.0F}, /* ~23% white lighten */
        .disabled = {.bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F},
        .transition_speed = 16.0F,
        .hit_padding_lrtb = {0, 0, 0, 0},
        .slice9_scale = 1.0F,
    };
    s_listrow_dark = listrow_base;
    s_listrow_light = listrow_base;

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

    /* ---- Modal: scale-pop + alpha; backdrop tuned per palette (dark dims to black, ---- */
    /* light dims to a soft slate so the panel stays legible against a bright page). */
    nt_ui_modal_style_t modal_base = nt_ui_modal_style_defaults();
    modal_base.layer = LAYER_BG;
    s_modal_dark = modal_base;
    s_modal_dark.backdrop_color = 0xFF000000U; /* black */
    s_modal_dark.backdrop_alpha = 0.60F;
    s_modal_light = modal_base;
    s_modal_light.backdrop_color = 0xFF202830U; /* slate */
    s_modal_light.backdrop_alpha = 0.40F;
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

// #region widget tab render fns (fold the superseded demos' content as tabs)
/* A labelled button slot: one button + an inline label, themed via the active palette. */
static void button_cell(nt_ui_context_t *ctx, uint32_t id, nt_ui_button_style_t *style, const char *text, bool enabled) {
    nt_ui_button_begin(
        ctx, NT_UI_DATA_LAYER(LAYER_IMG), id, style,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(72)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}},
        enabled);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), text, g_current->h1);
    (void)nt_ui_button_end(ctx);
}

static void render_labels(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Heading 1 (h1)", g_current->h1);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Body text -- the default reading style for paragraphs and descriptions.", g_current->body);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Caption -- small secondary text for hints and metadata.", g_current->caption);
    /* Wrapped + tinted variant (3rd distinct config). Wide enough to avoid ragged narrow wraps. */
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(620), CLAY_SIZING_FIT(0)}, .padding = CLAY_PADDING_ALL(16), .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = g_current->panel,
          .cornerRadius = CLAY_CORNER_RADIUS(8),
          .border = {.color = g_current->border, .width = {1, 1, 1, 1, 0}}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Boxed body label inside a themed panel; re-styles with the T-key palette flip.", g_current->body);
    }
}

static void render_button_primary(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Primary action button (idle / hover / pressed).", g_current->caption);
    button_cell(ctx, nt_ui_id("showcase/btn_primary"), g_current->btn_primary, "Primary", true);
}
static void render_button_secondary(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Secondary (alternate art) button.", g_current->caption);
    button_cell(ctx, nt_ui_id("showcase/btn_secondary"), g_current->btn_secondary, "Secondary", true);
}
static void render_button_disabled(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Disabled button: enabled=false short-circuits interaction + dims.", g_current->caption);
    button_cell(ctx, nt_ui_id("showcase/btn_disabled"), g_current->btn_primary, "Disabled", false);
}

/* One reference panel at a fixed size (DEMO-07: corners stay non-stretched). */
static void slice9_ref(nt_ui_context_t *ctx, nt_atlas_region_ref_t *ref, float w, float h, const char *cap) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), cap, g_current->caption);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}}}) { nt_ui_image(ctx, NT_UI_DATA_LAYER(LAYER_IMG), ref, &g_panel_img_style, NULL); }
    }
}

static void render_slice9(nt_ui_context_t *ctx, tab_state_t *st) {
    /* Three reference sizes prove corners stay crisp while the center stretches (DEMO-07). */
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 16, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
        slice9_ref(ctx, &s_panel_blue_ref, 300.0F, 100.0F, "300x100");
        slice9_ref(ctx, &s_panel_blue_ref, 600.0F, 100.0F, "600x100");
    }
    slice9_ref(ctx, &s_panel_blue_ref, 600.0F, 400.0F, "600x400");

    /* A slice9-backed nt_ui_panel container with a child label (folds the slice9_demo panel use). */
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(360), CLAY_SIZING_FIXED(90)}, .padding = CLAY_PADDING_ALL(14), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_panel_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), &s_panel_beige_ref, &g_panel_img_style, NULL);
        {
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Panel with child (corners crisp)", g_current->body);
        }
        nt_ui_panel_end(ctx);
    }

    /* Panel-driven: the props_slice9 sliders feed insets + target size into this emit live. */
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Panel-driven (insets + size from the properties panel):", g_current->caption);
    nt_ui_image_style_t live = {
        .color_packed = 0xFFFFFFFF,
        .slice9_scale = 1.0F,
        .slice9_lrtb = {(uint16_t)st->s9.inset_l, (uint16_t)st->s9.inset_r, (uint16_t)st->s9.inset_t, (uint16_t)st->s9.inset_b},
        .flags = NT_UI_IMAGE_SLICE9_OVERRIDE,
    };
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED((float)st->s9.target_w), CLAY_SIZING_FIXED((float)st->s9.target_h)}}}) {
        nt_ui_image(ctx, NT_UI_DATA_LAYER(LAYER_IMG), &s_panel_brown_ref, &live, NULL);
    }
}

static void render_toggles(nt_ui_context_t *ctx, tab_state_t *st) {
    static const Clay_ElementDeclaration row = {.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(44)}, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}};
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Checkbox", g_current->caption);
    (void)nt_ui_checkbox(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_cb, "Enable feature", &st->cb_value, g_current->check, &row, true);
    /* A second, disabled checkbox for the disabled-state config. */
    (void)nt_ui_checkbox(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, nt_ui_id("showcase/cb_locked"), "Locked (disabled)", &st->cb_value, g_current->check, &row, false);

    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Radio group (exclusive)", g_current->caption);
    (void)nt_ui_radio(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_radio_a, "Low", &st->radio_sel, 0, g_current->radio, &row, true);
    (void)nt_ui_radio(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_radio_b, "Medium", &st->radio_sel, 1, g_current->radio, &row, true);
    (void)nt_ui_radio(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_radio_c, "High", &st->radio_sel, 2, g_current->radio, &row, true);

    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Toggle", g_current->caption);
    (void)nt_ui_toggle(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_toggle, "Dark mode", &st->toggle_value, g_current->toggle, &row, true);
}

static void render_sliders(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const Clay_ElementDeclaration sdecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(28)}}};
    static const Clay_ElementDeclaration pdecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(24)}}};

    (void)snprintf(buf, sizeof buf, "Volume  %.2f", (double)st->slider_float);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_slider_f, NULL, &st->slider_float, 0.0F, 1.0F, 0.0F, g_current->slider, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Count   %d", st->slider_int);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_slider_i, NULL, &st->slider_int, 0, 10, 1, g_current->slider, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Progress  %d%%", (int)(st->prog.value * 100.0F));
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    nt_ui_progress(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_progress, st->prog.value, g_current->progress, &pdecl);
}

/* One scrollable item row. */
static void scroll_row(nt_ui_context_t *ctx, const char *text) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30)}, .padding = {.left = 8}, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), text, g_current->body);
    }
}

static void render_scroll(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    char buf[48];
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Tall list (the stage scroll handles the outer scroll):", g_current->caption);
    for (int i = 0; i < 12; ++i) {
        (void)snprintf(buf, sizeof buf, "Outer row %02d", i);
        scroll_row(ctx, buf);
    }
    /* Nested inner scroll: a drag inside it scrolls the inner container (capture-steal). */
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Nested inner scroll:", g_current->caption);
    nt_ui_scroll_begin(ctx, NULL, s_id_inner_scroll, g_current->scroll,
                       &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(280), CLAY_SIZING_FIXED(120)}, .padding = CLAY_PADDING_ALL(6)},
                                                  .backgroundColor = g_current->bg,
                                                  .cornerRadius = CLAY_CORNER_RADIUS(8)});
    {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}}) {
            for (int i = 0; i < 20; ++i) {
                (void)snprintf(buf, sizeof buf, "  Inner item %02d", i);
                scroll_row(ctx, buf);
            }
        }
    }
    nt_ui_scroll_end(ctx);
}

/* D-60-13 Slice9 panel: sliders for insets L/R/T/B + target size, fed into render_slice9. */
static void props_slice9(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const Clay_ElementDeclaration sdecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(280), CLAY_SIZING_FIXED(26)}}};
    showcase_panel_begin(ctx, "Slice9 properties");

    (void)snprintf(buf, sizeof buf, "Inset L  %d", st->s9.inset_l);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_il, NULL, &st->s9.inset_l, 0, 48, 1, g_current->slider, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Inset R  %d", st->s9.inset_r);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_ir, NULL, &st->s9.inset_r, 0, 48, 1, g_current->slider, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Inset T  %d", st->s9.inset_t);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_it, NULL, &st->s9.inset_t, 0, 48, 1, g_current->slider, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Inset B  %d", st->s9.inset_b);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_ib, NULL, &st->s9.inset_b, 0, 48, 1, g_current->slider, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Width  %d", st->s9.target_w);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_w, NULL, &st->s9.target_w, 100, 700, 1, g_current->slider, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Height  %d", st->s9.target_h);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_h, NULL, &st->s9.target_h, 60, 460, 1, g_current->slider, &sdecl, true);

    showcase_panel_end(ctx);
}

/* D-60-13 Progress panel: a slider drives the bar value 0..1 + an auto-animate toggle. */
static void props_progress(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const Clay_ElementDeclaration sdecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(280), CLAY_SIZING_FIXED(26)}}};
    static const Clay_ElementDeclaration row = {.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(40)}, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}};
    showcase_panel_begin(ctx, "Progress properties");

    (void)snprintf(buf, sizeof buf, "Value  %.2f", (double)st->prog.value);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    /* Manual value slider is disabled while auto-animate drives the value. */
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_value, NULL, &st->prog.value, 0.0F, 1.0F, 0.0F, g_current->slider, &sdecl, !st->prog.auto_anim);

    (void)nt_ui_toggle(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, nt_ui_id("showcase/props_auto"), "Auto-animate", &st->prog.auto_anim, g_current->toggle, &row, true);

    showcase_panel_end(ctx);
}

/* Runtime modal style the props panel mutates -- seeded from the palette modal style each frame,
 * then overlaid with the panel's transition/ease/scale-start/backdrop-alpha so the open/close
 * animation visibly tracks the panel (D-60-13). NOT a static-const. */
static nt_ui_modal_style_t s_modal_style_runtime;

/* Map the segmented transition index to the modal flag bits, preserving close-source flags. */
static void modal_set_transition(nt_ui_modal_style_t *s, int transition) {
    s->flags &= (uint8_t)~NT_UI_MODAL_TRANSITION_MASK;
    if (transition == 1) {
        s->flags |= NT_UI_MODAL_TRANSITION_FADE;
    } else if (transition == 2) {
        s->flags |= NT_UI_MODAL_TRANSITION_SLIDE;
    } else {
        s->flags |= NT_UI_MODAL_TRANSITION_SCALE_POP;
    }
}

/* A labelled action button inside a modal body. */
static bool modal_action_btn(nt_ui_context_t *ctx, uint32_t id, const char *text) {
    nt_ui_button_begin(
        ctx, NT_UI_DATA_LAYER(LAYER_IMG), id, g_current->btn_primary,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(150), CLAY_SIZING_FIXED(56)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}},
        true);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), text, g_current->body);
    return nt_ui_button_end(ctx);
}

/* DEMO-05: confirm modal (high-level one-bool wrapper) + a nested depth-2 modal; Esc + backdrop
 * close. The body is declared while the wrapper returns true (keep declaring through the close
 * animation -- Pitfall 2). The runtime style is driven by props_modal so the anim tracks live. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void render_modals(nt_ui_context_t *ctx, tab_state_t *st) {
    /* Seed the runtime style from the active palette, then overlay the props-panel values. */
    s_modal_style_runtime = *g_current->modal;
    s_modal_style_runtime.ease_speed = st->modal.ease_speed;
    s_modal_style_runtime.scale_start = st->modal.scale_start;
    s_modal_style_runtime.backdrop_alpha = st->modal.backdrop_alpha;
    s_modal_style_runtime.flags |= (uint8_t)(NT_UI_MODAL_LISTEN_ESC | NT_UI_MODAL_CLOSE_ON_BACKDROP);
    modal_set_transition(&s_modal_style_runtime, st->modal.transition);

    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Open a confirm dialog; the properties panel drives the transition + tween live.", g_current->caption);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Esc closes the TOP modal; clicking the backdrop closes; the backdrop blocks click-through.", g_current->caption);

    nt_ui_button_begin(
        ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_modal_show_btn, g_current->btn_primary,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(64)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}},
        true);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Show confirm", g_current->h1);
    if (nt_ui_button_end(ctx)) {
        st->confirm_open = true;
    }

    if (nt_ui_modal(ctx, s_id_modal_confirm, &s_modal_style_runtime, &st->confirm_open)) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(420), CLAY_SIZING_FIT(0)},
                         .padding = CLAY_PADDING_ALL(22),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 16,
                         .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
              .backgroundColor = g_current->panel,
              .cornerRadius = CLAY_CORNER_RADIUS(12)}) {
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Confirm action", g_current->h1);
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "This is a modal dialog. The backdrop blocks the base UI and world.", g_current->body);

            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 12}}) {
                if (modal_action_btn(ctx, s_id_modal_ok_btn, "OK")) {
                    st->confirm_open = false;
                }
                if (modal_action_btn(ctx, s_id_modal_cancel_btn, "Cancel")) {
                    st->confirm_open = false;
                }
                if (modal_action_btn(ctx, s_id_modal_nested_btn, "Show nested")) {
                    st->nested_open = true;
                }
            }

            /* Nested modal (depth 2): proves z-banding + top-only Esc targeting. */
            if (nt_ui_modal(ctx, s_id_modal_nested, &s_modal_style_runtime, &st->nested_open)) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(340), CLAY_SIZING_FIT(0)}, .padding = CLAY_PADDING_ALL(20), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 14},
                      .backgroundColor = g_current->panel,
                      .cornerRadius = CLAY_CORNER_RADIUS(12)}) {
                    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Nested modal (depth 2)", g_current->h1);
                    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Esc closes only this top modal.", g_current->body);
                    if (modal_action_btn(ctx, s_id_modal_nested_close_btn, "Close")) {
                        st->nested_open = false;
                    }
                }
                nt_ui_modal_end(ctx);
            }
        }
        nt_ui_modal_end(ctx);
    }
    /* Outer closed -> the nested child can't be declared; clear its bool so the stack stays balanced. */
    if (!st->confirm_open) {
        st->nested_open = false;
    }
}

/* DEMO-06: a cell of N labels @14pt + a frame gpu_ms readout (n/a guard on WebGL2 absent ext,
 * Pitfall 3). NO nested ui_text GPU segment (the host frame loop owns the "frame" segment;
 * nesting trips nt_gfx_gl.c:483). */
static void render_stress(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const nt_ui_label_style_t stress_label = {.font_id = 0, .font_size = 14, .color = {200.0F, 210.0F, 220.0F, 255.0F}};

    const float gpu_ms = nt_stats_get_gpu_ms();
    if (gpu_ms < 0.0F) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "frame gpu: n/a (timer extension absent)", g_current->caption);
    } else {
        (void)snprintf(buf, sizeof buf, "frame gpu: %.2f ms", (double)gpu_ms);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    }
    (void)snprintf(buf, sizeof buf, "draw calls: %u   labels: %d", nt_ui_get_last_walk_draw_calls(ctx), st->stress.label_count);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);

    /* N small labels at 14pt -- the Slug GPU-cost proxy cell. */
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(760), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 6, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
        for (int i = 0; i < st->stress.label_count; ++i) {
            (void)snprintf(buf, sizeof buf, "lbl%03d", i);
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, &stress_label);
        }
    }
}

/* D-60-13 Modal panel (the showcase piece): segmented transition + sliders for ease / scale-start
 * / backdrop-alpha, all written into the Modals param struct that render_modals reads each frame. */
static void props_modal(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const Clay_ElementDeclaration sdecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(280), CLAY_SIZING_FIXED(26)}}};
    static const char *const names[3] = {"Scale-pop", "Fade", "Slide"};
    showcase_panel_begin(ctx, "Modal properties");

    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Transition", g_current->caption);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8}}) {
        for (int i = 0; i < 3; ++i) {
            const bool sel = (st->modal.transition == i);
            if (nt_ui_button(ctx, NT_UI_DATA_LAYER(LAYER_IMG), nt_ui_id("showcase/modal_trans") + (uint32_t)i, sel ? g_current->btn_primary : g_current->btn_secondary,
                             &(Clay_ElementDeclaration){
                                 .layout = {.sizing = {CLAY_SIZING_FIXED(92), CLAY_SIZING_FIXED(40)}, .padding = CLAY_PADDING_ALL(4), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}},
                             true)) {
                st->modal.transition = i;
            }
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), names[i], g_current->caption);
        }
    }

    (void)snprintf(buf, sizeof buf, "Ease speed  %.1f", (double)st->modal.ease_speed);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_ease, NULL, &st->modal.ease_speed, 4.0F, 30.0F, 0.0F, g_current->slider, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Scale start  %.2f", (double)st->modal.scale_start);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_scale, NULL, &st->modal.scale_start, 0.85F, 1.0F, 0.0F, g_current->slider, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Backdrop alpha  %.2f", (double)st->modal.backdrop_alpha);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_backdrop, NULL, &st->modal.backdrop_alpha, 0.0F, 1.0F, 0.0F, g_current->slider, &sdecl, true);

    showcase_panel_end(ctx);
}

/* D-60-13 Stress panel: segmented label count (50/100/200/400) driving render_stress + the live
 * frame gpu_ms / draw-calls readout. */
static void props_stress(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const int counts[4] = {50, 100, 200, 400};
    showcase_panel_begin(ctx, "Stress properties");

    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Label count", g_current->caption);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8}}) {
        for (int i = 0; i < 4; ++i) {
            const bool sel = (st->stress.label_count == counts[i]);
            (void)snprintf(buf, sizeof buf, "%d", counts[i]);
            if (nt_ui_button(ctx, NT_UI_DATA_LAYER(LAYER_IMG), nt_ui_id("showcase/stress_n") + (uint32_t)i, sel ? g_current->btn_primary : g_current->btn_secondary,
                             &(Clay_ElementDeclaration){
                                 .layout = {.sizing = {CLAY_SIZING_FIXED(66), CLAY_SIZING_FIXED(40)}, .padding = CLAY_PADDING_ALL(4), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}},
                             true)) {
                st->stress.label_count = counts[i];
            }
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
        }
    }

    const float gpu_ms = nt_stats_get_gpu_ms();
    if (gpu_ms < 0.0F) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "frame gpu: n/a", g_current->caption);
    } else {
        (void)snprintf(buf, sizeof buf, "frame gpu: %.2f ms", (double)gpu_ms);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    }
    (void)snprintf(buf, sizeof buf, "draw calls: %u", nt_ui_get_last_walk_draw_calls(ctx));
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);

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
    s_id_modal_confirm = nt_ui_id("showcase/modal_confirm");
    s_id_modal_nested = nt_ui_id("showcase/modal_nested");
    s_id_modal_show_btn = nt_ui_id("showcase/modal_show_btn");
    s_id_modal_ok_btn = nt_ui_id("showcase/modal_ok_btn");
    s_id_modal_cancel_btn = nt_ui_id("showcase/modal_cancel_btn");
    s_id_modal_nested_btn = nt_ui_id("showcase/modal_nested_btn");
    s_id_modal_nested_close_btn = nt_ui_id("showcase/modal_nested_close_btn");
    s_id_props_ease = nt_ui_id("showcase/props_ease");
    s_id_props_scale = nt_ui_id("showcase/props_scale");
    s_id_props_backdrop = nt_ui_id("showcase/props_backdrop");
    s_id_theme_btn = nt_ui_id("showcase/theme_btn");
    s_id_tab_btn_base = nt_ui_id("showcase/tab_btn");
    s_ids_ready = true;
}

/* Header (DEMO-04): a Dark/Light toggle button + a live ui_draw_calls + frame gpu_ms readout
 * (the draw-call count IS the batching evidence -- no sort-by-material toggle, DEMO-09 REMOVED). */
static void declare_header(nt_ui_context_t *ctx) {
    char buf[96];
    /* Title | theme button | live readout | (GROW spacer) | dim keyboard hints, with a 1px
     * bottom separator under the whole bar. */
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {.bottom = 10}, .childGap = 16, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
          .border = {.color = g_current->border, .width = {.bottom = 1}}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Neotolis UI Showcase", g_current->title);

        nt_ui_button_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_theme_btn, g_current->btn_primary,
                           &(Clay_ElementDeclaration){
                               .layout = {.sizing = {CLAY_SIZING_FIXED(150), CLAY_SIZING_FIXED(40)}, .padding = CLAY_PADDING_ALL(4), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}},
                           true);
        (void)snprintf(buf, sizeof buf, "Theme: %s", g_current->name);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->body);
        if (nt_ui_button_end(ctx)) {
            g_current = (g_current == &g_dark) ? &g_light : &g_dark;
        }

        const float gpu_ms = nt_stats_get_gpu_ms();
        if (gpu_ms < 0.0F) {
            (void)snprintf(buf, sizeof buf, "draw calls: %u   gpu: n/a", nt_ui_get_last_walk_draw_calls(ctx));
        } else {
            (void)snprintf(buf, sizeof buf, "draw calls: %u   gpu: %.2f ms", nt_ui_get_last_walk_draw_calls(ctx), (double)gpu_ms);
        }
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->body);

        /* Spacer pushes the keyboard hints to the far right, dimmer than the live readout. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "[T] palette  [D] inspector  [Esc] quit", g_current->caption);
    }
}

/* One tab-list row: full-row click target (transparent list-row button so the row's CLAY bg shows
 * the selected/unselected state), a 3px left accent bar on the active row, hover/pressed lighten. */
static void tab_row(nt_ui_context_t *ctx, int i, bool selected) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(38)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = selected ? g_current->list_sel : g_current->list_bg,
          .cornerRadius = CLAY_CORNER_RADIUS(6)}) {
        /* 3px left accent bar on the active row (transparent spacer otherwise => stable layout). */
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(3), CLAY_SIZING_GROW(0)}}, .backgroundColor = selected ? g_current->accent : (Clay_Color){0}}) {}
        nt_ui_button_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_tab_btn_base + (uint32_t)i, g_current->listrow,
                           &(Clay_ElementDeclaration){
                               .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = {.left = 12, .right = 10}, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
                               .cornerRadius = {0, 6, 0, 6}},
                           true);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), g_tabs[i].name, selected ? g_current->row_sel : g_current->caption);
        if (nt_ui_button_end(ctx)) {
            s_active_tab = i;
        }
    }
}

/* Left tab list: one full-row click target per registry entry; clicking selects the active tab. */
static void declare_tab_list(nt_ui_context_t *ctx) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(10),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 6,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = g_current->list_bg,
          .border = {.color = g_current->border, .width = {1, 1, 1, 1, 0}}}) {
        for (int i = 0; i < TAB_COUNT; ++i) {
            tab_row(ctx, i, i == s_active_tab);
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
        /* Source reference: a dim "link" style distinct from body/caption copy. */
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), e->code_url, g_current->link);

        /* Content stage + (optional) props panel side by side. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 16, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
            /* Scroll-wrapped content (DEMO-12): exercises the scissor stack. */
            nt_ui_scroll_begin(ctx, NULL, s_id_stage_scroll, g_current->scroll,
                               &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(8)}});
            {
                /* Cap the readable content column (~720px) so text/cards don't stretch edge-to-edge. */
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0, 720), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 10}}) { e->render(ctx, &s_state); }
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
    /* Prev-frame modal state (cached after nt_ui_end below): gameplay/global hotkeys yield to an open
     * modal so Esc closes the top modal first and the palette/inspector keys don't fire underneath it. */
    static bool s_modal_was_active;
    nt_stats_frame_begin();
    nt_window_poll();
    nt_input_poll();
    nt_mem_scratch_reset();

#ifndef NT_PLATFORM_WEB
    if (!s_modal_was_active && nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif

    if (!s_modal_was_active) {
        if (nt_input_key_is_pressed(NT_KEY_T)) {
            g_current = (g_current == &g_dark) ? &g_light : &g_dark;
            nt_log_info("ui_showcase: palette -> %s", g_current->name);
        }
        if (nt_input_key_is_pressed(NT_KEY_D)) {
            const bool now_on = !nt_ui_inspector_is_active(s_ctx);
            nt_ui_inspector_set_active(s_ctx, now_on);
            nt_log_info("ui_showcase: inspector %s", now_on ? "ON" : "OFF");
        }
    }

    nt_resource_step();
    nt_material_step();

    /* Auto-animate progress when its panel toggle is on. On the off->on edge, derive the ramp direction
     * from the current value so it continues toward the nearer end instead of snapping. */
    static bool s_prog_auto_prev;
    if (s_state.prog.auto_anim && !s_prog_auto_prev) {
        s_state.prog.ramp_up = (s_state.prog.value < 1.0F);
    }
    s_prog_auto_prev = s_state.prog.auto_anim;
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
        /* Force a style re-init next frame so memoized atlas region indices refresh after GL restore. */
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
            declare_header(s_ctx);
            /* Left tab list -> right content stage. */
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 12, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
                declare_tab_list(s_ctx);
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
                      .backgroundColor = g_current->panel,
                      .cornerRadius = CLAY_CORNER_RADIUS(10),
                      .border = {.color = g_current->border, .width = {1, 1, 1, 1, 0}}}) {
                    declare_stage(s_ctx);
                }
            }
        }

        nt_ui_end(s_ctx);
        s_modal_was_active = nt_ui_modal_active(s_ctx);

        nt_ui_target_t target = nt_ui_scale_make_target(&scale);
        nt_ui_walk(s_ctx, &target);

        nt_ui_inspector_overlay_draw(s_ctx, &target, s_font, 16.0F);

        {
            /* Bottom-left overlay: nt_stats_draw emits 4 lines (FPS/CPU/GPU/Draws) descending in
             * y-up text space, so anchor high enough (~5 line-heights) that the last line stays on
             * screen and clear of the header's theme button at the top. */
            mat4 stats_model;
            glm_mat4_identity(stats_model);
            glm_translate(stats_model, (vec3){10.0F, 92.0F, 0.0F});
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
