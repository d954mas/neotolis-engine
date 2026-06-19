/* UI Showcase -- tabbed vitrine demoing the engine's UI widgets; see README. */

// #region includes
#include "app/nt_app.h"
#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "core/nt_platform.h"
#include "debug_overlay/nt_debug_overlay.h"
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
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_checkbox.h"
#include "ui/nt_ui_dropdown.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_input.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_menu.h"
#include "ui/nt_ui_modal.h"
#include "ui/nt_ui_panel.h"
#include "ui/nt_ui_progress.h"
#include "ui/nt_ui_scale.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_slider.h"
#include "ui/nt_ui_tabbar.h"
#include "ui/nt_ui_tooltip.h"
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
/* Segment-button label: small + bright so multi-char text fits the narrow segment buttons (body 22 spills). */
static const nt_ui_label_style_t g_seg_label = {.font_id = 0, .font_size = 14, .color = {245.0F, 247.0F, 252.0F, 255.0F}};
// #endregion

// #region palette widget styles (filled with late-bound atlas refs at init)
/* Mutable so the engine can memoize the resolved atlas region index in place. */
static nt_ui_button_style_t s_btn_primary_dark, s_btn_primary_light;
static nt_ui_button_style_t s_btn_secondary_dark, s_btn_secondary_light;
static nt_ui_button_style_t s_btn_scale_dark, s_btn_scale_light;
static nt_ui_button_style_t s_btn_swap_dark, s_btn_swap_light;
static nt_ui_button_style_t s_btn_nopad_dark, s_btn_nopad_light;
static nt_ui_button_style_t s_listrow_dark, s_listrow_light;
static nt_ui_checkbox_style_t s_check_dark, s_check_light;
static nt_ui_checkbox_style_t s_radio_dark, s_radio_light;
static nt_ui_checkbox_style_t s_switch_dark, s_switch_light;
static nt_ui_slider_style_t s_slider_dark, s_slider_light;
/* Narrower slider for the focused-properties card: track+thumb fit inside the card's inner width. */
static nt_ui_slider_style_t s_slider_props_dark, s_slider_props_light;
static nt_ui_progress_style_t s_progress_dark, s_progress_light;
static nt_ui_progress_style_t s_progress_crop_dark, s_progress_crop_light;
static nt_ui_progress_style_t s_progress_vert_dark, s_progress_vert_light;
static nt_ui_scroll_style_t s_scroll_hide_dark, s_scroll_hide_light;
static nt_ui_scroll_style_t s_scroll_always_dark, s_scroll_always_light;
static nt_ui_scroll_style_t s_scroll_horiz_dark, s_scroll_horiz_light;
static nt_ui_scroll_style_t s_scroll_xy_dark, s_scroll_xy_light;
/* Input field styles (flat colors, theme-agnostic except text color) -- purely visual now; the
 * plain/numeric/password/cyrillic fields share this one look and differ only via per-field props. */
static nt_ui_input_style_t s_input_dark, s_input_light;
/* Visual-style variants: a thick non-blinking caret + a distinct selection color, to show the
 * caret/selection style fields are configurable. */
static nt_ui_input_style_t s_input_caret_dark, s_input_caret_light;
static nt_ui_input_style_t s_input_sel_dark, s_input_sel_light;
/* Sprite-backed variant: a 9-slice panel frame as the field background (idle vs focused art swap),
 * theme-agnostic. Shows bg_art/focused_bg_art -- the .image + tint convention shared with panel/button. */
static nt_ui_input_style_t s_input_art;

/* Slice9 panel image style (untinted, atlas-default slice9). */
static const nt_ui_image_style_t g_panel_img_style = {.color_packed = 0xFFFFFFFF, .slice9_scale = 1.0F};

/* Per-palette modal base; the palette pointer flip restyles the modal on hot-swap. */
static nt_ui_modal_style_t s_modal_dark, s_modal_light;

/* App-widget styles: dropdown / tooltip / menu / tab-bar, per-palette dark/light. */
static nt_ui_dropdown_style_t s_dropdown_dark, s_dropdown_light;
static nt_ui_tooltip_style_t s_tooltip_dark, s_tooltip_light;
static nt_ui_menu_style_t s_menu_dark, s_menu_light;
static nt_ui_tabbar_style_t s_tabbar_dark, s_tabbar_light;
/* Begin/end-core demo strip: horizontal, flat-color, BOTTOM accent (distinct from the vertical nav's LEFT). */
static nt_ui_tabbar_style_t s_tabs_demo_dark, s_tabs_demo_light;
// #endregion

// #region ui_palette_t (per-widget style pointers; pointer flip is the whole hot-swap)
typedef struct {
    const nt_ui_label_style_t *h1, *body, *caption;
    const nt_ui_label_style_t *title, *row_sel, *link;
    /* Non-const: the engine memoizes the resolved atlas region index in place on first emit. */
    nt_ui_button_style_t *btn_primary, *btn_secondary, *listrow;
    nt_ui_button_style_t *btn_scale, *btn_swap, *btn_nopad;
    nt_ui_checkbox_style_t *check, *radio, *toggle;
    nt_ui_slider_style_t *slider;
    nt_ui_slider_style_t *slider_props; /* card-fitted slider for the focused-properties panel */
    nt_ui_progress_style_t *progress;
    nt_ui_progress_style_t *progress_crop, *progress_vert;
    nt_ui_scroll_style_t *scroll_hide, *scroll_always, *scroll_horiz, *scroll_xy;
    nt_ui_input_style_t *input, *input_caret, *input_sel;
    const nt_ui_modal_style_t *modal;
    nt_ui_dropdown_style_t *dropdown; /* non-const: nt_ui_dropdown memoizes atlas-ref resolves into the style */
    nt_ui_tooltip_style_t *tooltip;   /* non-const: nt_ui_tooltip memoizes atlas-ref resolves into the style */
    nt_ui_menu_style_t *menu;         /* non-const: nt_ui_menu memoizes atlas-ref resolves into the style */
    nt_ui_tabbar_style_t *tabbar;     /* non-const: nt_ui_tabbar memoizes atlas-ref resolves into the style */
    nt_ui_tabbar_style_t *tabs_demo;  /* begin/end-core demo strip (horizontal, BOTTOM accent) */
    /* panel_alt: a distinct shade for the props control card so it reads apart from the stage panel. */
    Clay_Color bg, panel, panel_alt, list_bg, list_sel, accent, border;
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
    .btn_scale = &s_btn_scale_dark,
    .btn_swap = &s_btn_swap_dark,
    .btn_nopad = &s_btn_nopad_dark,
    .check = &s_check_dark,
    .radio = &s_radio_dark,
    .toggle = &s_switch_dark,
    .slider = &s_slider_dark,
    .slider_props = &s_slider_props_dark,
    .progress = &s_progress_dark,
    .progress_crop = &s_progress_crop_dark,
    .progress_vert = &s_progress_vert_dark,
    .scroll_hide = &s_scroll_hide_dark,
    .scroll_always = &s_scroll_always_dark,
    .scroll_horiz = &s_scroll_horiz_dark,
    .scroll_xy = &s_scroll_xy_dark,
    .input = &s_input_dark,
    .input_caret = &s_input_caret_dark,
    .input_sel = &s_input_sel_dark,
    .modal = &s_modal_dark,
    .dropdown = &s_dropdown_dark,
    .tooltip = &s_tooltip_dark,
    .menu = &s_menu_dark,
    .tabbar = &s_tabbar_dark,
    .tabs_demo = &s_tabs_demo_dark,
    .bg = {18.0F, 18.0F, 22.0F, 255.0F},
    .panel = {30.0F, 34.0F, 42.0F, 255.0F},
    .panel_alt = {40.0F, 45.0F, 56.0F, 255.0F},
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
    .btn_scale = &s_btn_scale_light,
    .btn_swap = &s_btn_swap_light,
    .btn_nopad = &s_btn_nopad_light,
    .check = &s_check_light,
    .radio = &s_radio_light,
    .toggle = &s_switch_light,
    .slider = &s_slider_light,
    .slider_props = &s_slider_props_light,
    .progress = &s_progress_light,
    .progress_crop = &s_progress_crop_light,
    .progress_vert = &s_progress_vert_light,
    .scroll_hide = &s_scroll_hide_light,
    .scroll_always = &s_scroll_always_light,
    .scroll_horiz = &s_scroll_horiz_light,
    .scroll_xy = &s_scroll_xy_light,
    .input = &s_input_light,
    .input_caret = &s_input_caret_light,
    .input_sel = &s_input_sel_light,
    .modal = &s_modal_light,
    .dropdown = &s_dropdown_light,
    .tooltip = &s_tooltip_light,
    .menu = &s_menu_light,
    .tabbar = &s_tabbar_light,
    .tabs_demo = &s_tabs_demo_light,
    .bg = {238.0F, 240.0F, 246.0F, 255.0F},
    .panel = {255.0F, 255.0F, 255.0F, 255.0F},
    .panel_alt = {232.0F, 235.0F, 243.0F, 255.0F},
    .list_bg = {224.0F, 227.0F, 235.0F, 255.0F},
    .list_sel = {138.0F, 182.0F, 240.0F, 255.0F},
    .accent = {52.0F, 120.0F, 214.0F, 255.0F},
    .border = {206.0F, 210.0F, 220.0F, 255.0F},
    .name = "LIGHT",
};

/* Pointer write IS the hot-swap -- engine ships no nt_ui_set_theme. */
static const ui_palette_t *g_current = &g_dark;
// #endregion

// #region per-tab state (game-owned; re-fed each frame so it survives tab switches)
typedef struct {
    int inset_l, inset_r, inset_t, inset_b; /* slice9 override insets (px) */
    int target_w, target_h;                 /* panel target size driven by the panel */
    float slice9_scale;                     /* corner/border thickness scale (>0) */
} slice9_params_t;

typedef struct {
    float value;    /* progress bar value 0..1 driven by the panel slider */
    bool auto_anim; /* auto-ramp 0->1->0 instead of manual */
    bool ramp_up;
} progress_params_t;

/* Live transform the panel drives around a button to prove inverse-affine hit-test still clicks. */
typedef struct {
    float rotation_deg; /* -180..180 */
    float scale;        /* 0.5..2.0 */
    float offset_x;     /* -120..120 px */
    float offset_y;
    uint32_t clicks; /* proof counter: still clickable while transformed */
} btn_xform_params_t;

typedef struct {
    int open_type, close_type; /* 0 = scale-pop, 1 = fade, 2 = slide (segmented) */
    int open_edge, close_edge; /* 0 = bottom, 1 = top, 2 = left, 3 = right (SLIDE) */
    float ease_speed;          /* open/close tween value_speed */
    float scale_start;         /* shared scale-pop start (~0.85..1.0) */
    float slide_offset;        /* shared SLIDE distance px */
    float backdrop_alpha;      /* peak backdrop opacity 0..1 */
} modal_params_t;

typedef struct {
    int label_count; /* segmented control: 500 / 1500 / 3000 / 6000 */
} stress_params_t;

/* Events tab: hold-to-confirm + a double-click readout (all game-owned). */
typedef struct {
    uint32_t confirms;   /* hold-to-confirm fire count (long_pressed) */
    uint32_t dbl_clicks; /* double-click readout */
    float last_progress; /* hold_progress latched for the fill display */
} events_params_t;

/* Dropdown tab: game-owned selection + open flags. */
typedef struct {
    int fruit_sel; /* short list selection */
    bool fruit_open;
    int city_sel; /* long (scrolling) list selection */
    bool city_open;
    int color_sel; /* custom swatch-trigger combo selection */
    bool color_open;
} dropdown_params_t;

/* Menu tab: two independent game-owned menus to show the optional target binding — a GLOBAL menu
 * (right-click anywhere in the tab) and a ZONE menu bound to a panel (right-click only over it). */
typedef struct {
    nt_ui_menu_state_t global_state; /* target_id == 0: arms on a right-click anywhere */
    nt_ui_menu_state_t zone_state;   /* target_id == panel: arms only over the bound panel */
    /* Game-owned per-frame menu scratch (one per logical menu, reused every frame — holds the prev-frame
     * nav record). Zero-init is valid; static storage needs no nt_ui_menu_init call. */
    nt_ui_menu_ctx_t global_menu;
    nt_ui_menu_ctx_t zone_menu;
    const char *last_chosen; /* readout label of the last chosen row; points at the row's static literal (NULL = none) */
    bool show_grid;          /* checkmark-toggle row state (global menu) */
    uint8_t opacity_pct;     /* custom-row state the inner "reset" button restores to 100 */
} menu_params_t;

/* Input tab: four game-owned field buffers (ImGui-style). The widget edits these in place;
 * the engine state pool holds only caret/selection/scroll/blink, never the string. */
typedef struct {
    char plain[64];
    char numeric[16];
    char password[32];
    char cyrillic[64];
    char caret_thick[64]; /* thick, bright, non-blinking caret variant */
    char caret_sel[64];   /* distinct selection-highlight color variant */
    char art_bg[64];      /* sprite-backed (9-slice panel frame) background variant */
    char disabled[64];    /* disabled-state demo field (enabled=false) */
} input_params_t;

struct tab_state {
    /* Toggles tab. */
    bool cb_value;
    bool cb_locked; /* disabled-checkbox demo: own fixed value (enabled=false never toggles it). */
    int radio_sel;
    bool toggle_value;
    /* Sliders tab. */
    float slider_float;
    int slider_int;
    /* Props params. */
    slice9_params_t s9;
    progress_params_t prog;
    btn_xform_params_t btn_xform;
    /* Modals tab. */
    bool confirm_open;
    bool nested_open;
    modal_params_t modal;
    /* Stress tab. */
    stress_params_t stress;
    /* Input tab. */
    input_params_t input;
    /* Interaction-events + app-widget tabs. */
    events_params_t events;
    dropdown_params_t dropdown;
    menu_params_t menu;
    /* Tabs tab: the begin/end-core demo strip's game-owned active index (Model D). */
    int tabs_demo_active;
};

static struct tab_state s_state = {
    .cb_value = true,
    .cb_locked = true, /* demos a locked-ON feature; disabled so it stays fixed. */
    .radio_sel = 1,
    .toggle_value = false,
    .slider_float = 0.65F,
    .slider_int = 4,
    .s9 = {.inset_l = 10, .inset_r = 10, .inset_t = 10, .inset_b = 10, .target_w = 300, .target_h = 150, .slice9_scale = 1.0F},
    .prog = {.value = 0.4F, .auto_anim = false, .ramp_up = true},
    .btn_xform = {.rotation_deg = 20.0F, .scale = 1.0F, .offset_x = 0.0F, .offset_y = 0.0F, .clicks = 0},
    .confirm_open = false,
    .nested_open = false,
    .modal = {.open_type = 2, .close_type = 1, .open_edge = 0, .close_edge = 0, .ease_speed = 14.0F, .scale_start = 0.92F, .slide_offset = 32.0F, .backdrop_alpha = 0.55F}, /* default demo: slide-in /
                                                                                                                                                                               fade-out */
    .stress = {.label_count = 3000},
    .input = {.plain = "Edit me", .numeric = "42", .password = "secret", .cyrillic = "Привет, мир"},
    .events = {.confirms = 0, .dbl_clicks = 0, .last_progress = 0.0F},
    .dropdown = {.fruit_sel = 0, .fruit_open = false, .city_sel = -1, .city_open = false, .color_sel = -1, .color_open = false},
    .menu = {.global_state = {0}, .zone_state = {0}, .last_chosen = NULL, .show_grid = false, .opacity_pct = 60}, /* non-100 start so "reset" visibly changes it */
};
// #endregion

// #region registry types (Druid-style metadata; nullable props_fn)
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
static uint32_t s_id_progress_crop, s_id_progress_vert; /* CROP + vertical progress variants */
static uint32_t s_id_scroll_hide, s_id_scroll_always;   /* vertical AUTO_HIDE / ALWAYS lists */
static uint32_t s_id_scroll_horiz, s_id_scroll_xy;      /* horizontal-only / both-axes */
static uint32_t s_id_stress_scroll;                     /* fixed-size scroll so the label cell can't overflow */
static uint32_t s_id_stage_scroll;                      /* vertical scroll around the stage content so tall tabs fit */
static uint32_t s_id_props_scroll;                      /* vertical scroll around the props content for tall configs */
static uint32_t s_id_props_il, s_id_props_ir, s_id_props_it, s_id_props_ib;
static uint32_t s_id_props_w, s_id_props_h;
static uint32_t s_id_props_s9scale; /* slice9 corner-scale slider */
static uint32_t s_id_props_value;
static uint32_t s_id_props_rot, s_id_props_bscale, s_id_props_offx, s_id_props_offy; /* button transform panel */
static uint32_t s_id_modal_confirm, s_id_modal_nested;
static uint32_t s_id_modal_show_btn, s_id_modal_ok_btn, s_id_modal_cancel_btn, s_id_modal_nested_btn, s_id_modal_nested_close_btn;
static uint32_t s_id_props_ease, s_id_props_scale, s_id_props_backdrop, s_id_props_slide_dist;
static uint32_t s_id_theme_btn;
static uint32_t s_id_input_plain, s_id_input_numeric, s_id_input_password, s_id_input_cyrillic;
static uint32_t s_id_input_caret, s_id_input_sel, s_id_input_art, s_id_input_disabled;
static uint32_t s_id_tab_btn_base; /* per-tab list buttons salt from this + index */
/* App-widget ids. */
static uint32_t s_id_events_hold;                           /* hold-to-confirm button */
static uint32_t s_id_events_dbl;                            /* double-click target */
static uint32_t s_id_events_fill;                           /* hold_progress fill bar */
static uint32_t s_id_dd_fruit, s_id_dd_city, s_id_dd_color; /* combo triggers (color = custom swatch) */
static uint32_t s_id_tip_a, s_id_tip_b, s_id_tip_c;         /* tooltip targets */
static uint32_t s_id_menu_global;                           /* global context menu (right-click anywhere) */
static uint32_t s_id_menu_zone;                             /* zone context menu (bound to a panel) */
static uint32_t s_id_menu_panel;                            /* the zone panel the zone menu binds to (opens only over it) */
static uint32_t s_id_menu_opacity_btn;                      /* inner button on the zone menu's activatable=false custom row */
static uint32_t s_id_tabs_demo_base;                        /* begin/end-core demo strip: tabs salt from this + index */
static bool s_ids_ready;
// #endregion

// #region engine state
/* Stress tab emits up to 6000 labels + ~500 row containers; size the UI arena + Clay element cap
 * for that worst case. nt_ui_min_arena_size(7168) in the DEBUG_TOOLS build is ~8.02 MB (Clay arena +
 * pow2 widget registry dominate), so a 9 MB arena clears the create-context assert with headroom. */
#define UI_MAX_ELEMENTS ((uint32_t)7168U)
#define UI_ARENA_SIZE ((size_t)9U * 1024U * 1024U)
#define SCRATCH_ARENA_SIZE ((size_t)512U * 1024U)

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
/* Icon-button art (Kenney bunny); untinted so it shows its natural color. */
static nt_atlas_region_ref_t s_icon_bunny_ref;
/* Dropdown chevron affordance (down triangle; tintable). */
static nt_atlas_region_ref_t s_chevron_down_ref;
/* Menu submenu marker (right-pointing arrow; tintable). */
static nt_atlas_region_ref_t s_arrow_right_ref;
/* Tooltip caret/arrow (up-pointing triangle; the tooltip flips it per popup side; tintable). */
static nt_atlas_region_ref_t s_caret_ref;
/* Tabs-demo icons: bunny on idle/hover, checkmark on the selected tab (game-owned content swap). */
static nt_atlas_region_ref_t s_tabs_icon_idle_ref;
static nt_atlas_region_ref_t s_tabs_icon_sel_ref;

static int s_active_tab;
// #endregion

// #region reusable focused-panel helper (game-side; built from existing nt_ui widgets)
/* Props_fn populates the right-hand props card directly (declare_props_panel owns the card), so this
 * only emits the per-tab title -- no nested card, avoiding a card-in-card double border. */
static void showcase_panel_begin(nt_ui_context_t *ctx, const char *title) { nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), title, g_current->body); }
static void showcase_panel_end(nt_ui_context_t *ctx) { (void)ctx; }
// #endregion

// #region widget tab render fns
static void render_labels(nt_ui_context_t *ctx, tab_state_t *st);
static void render_buttons(nt_ui_context_t *ctx, tab_state_t *st);
static void render_button_transform(nt_ui_context_t *ctx, tab_state_t *st);
static void render_slice9(nt_ui_context_t *ctx, tab_state_t *st);
static void render_toggles(nt_ui_context_t *ctx, tab_state_t *st);
static void render_sliders(nt_ui_context_t *ctx, tab_state_t *st);
static void render_scroll(nt_ui_context_t *ctx, tab_state_t *st);
static void render_modals(nt_ui_context_t *ctx, tab_state_t *st);
static void render_modal_overlay(nt_ui_context_t *ctx, tab_state_t *st);
static void render_input(nt_ui_context_t *ctx, tab_state_t *st);
static void render_events(nt_ui_context_t *ctx, tab_state_t *st);
static void render_dropdown(nt_ui_context_t *ctx, tab_state_t *st);
static void render_tooltip(nt_ui_context_t *ctx, tab_state_t *st);
static void render_menu(nt_ui_context_t *ctx, tab_state_t *st);
static void render_tabs(nt_ui_context_t *ctx, tab_state_t *st);
static void render_stress(nt_ui_context_t *ctx, tab_state_t *st);
static void props_slice9(nt_ui_context_t *ctx, tab_state_t *st);
static void props_progress(nt_ui_context_t *ctx, tab_state_t *st);
static void props_button_transform(nt_ui_context_t *ctx, tab_state_t *st);
static void props_modal(nt_ui_context_t *ctx, tab_state_t *st);
static void props_stress(nt_ui_context_t *ctx, tab_state_t *st);
// #endregion

// #region registry (one entry per widget category)
static const showcase_entry_t g_tabs[] = {
    {"Labels", "h1 / body / caption label variants, themed via the palette.", "examples/ui_showcase/main.c:render_labels", render_labels, NULL},
    {"Buttons", "Standard / scale / per-state ART SWAP / no-pad touch-target / icon / disabled.", "examples/ui_showcase/main.c:render_buttons", render_buttons, NULL},
    {"Buttons: Transform", "Rotated/scaled/offset button that STILL hit-tests (inverse-affine); driven by the panel.", "examples/ui_showcase/main.c:render_button_transform", render_button_transform,
     props_button_transform},
    {"Images & Slice9", "Slice9 panels at 3 sizes (corners stay crisp) + a live insets/size panel.", "examples/ui_showcase/main.c:render_slice9", render_slice9, props_slice9},
    {"Toggles & Radios", "Checkbox + exclusive radio group + sliding toggle.", "examples/ui_showcase/main.c:render_toggles", render_toggles, NULL},
    {"Sliders & Progress", "Float + int sliders + a progress bar driven by a live value panel.", "examples/ui_showcase/main.c:render_sliders", render_sliders, props_progress},
    {"Scroll", "Four scroll variants: vertical AUTO_HIDE / ALWAYS bar / horizontal-only / both axes.", "examples/ui_showcase/main.c:render_scroll", render_scroll, NULL},
    {"Modals", "Confirm + nested depth-2 modal; Esc/backdrop close; live transition panel.", "examples/ui_showcase/main.c:render_modals", render_modals, props_modal},
    {"Input", "Plain / numeric-filtered / password-masked / Cyrillic text fields; selection + Ctrl+C/X/V + Tab focus.", "examples/ui_showcase/main.c:render_input", render_input, NULL},
    {"Events", "Hold-to-confirm (events hold_progress fill + long_pressed) + a double-click readout.", "examples/ui_showcase/main.c:render_events", render_events, NULL},
    {"Dropdown", "Combobox on popup-core: a short list + a long scrolling list with edge-flip near the bottom.", "examples/ui_showcase/main.c:render_dropdown", render_dropdown, NULL},
    {"Tooltip", "Timed hover reveal on popup-core (no catcher, never blocks clicks).", "examples/ui_showcase/main.c:render_tooltip", render_tooltip, NULL},
    {"Menu", "Two context menus (global + zone-bound) with nested submenus: mouse-aim, edge-flip, kbd-nav.", "examples/ui_showcase/main.c:render_menu", render_menu, NULL},
    {"Tabs", "Tab-bar begin/end core: icon+text tabs with a distinct selected-tab icon; BOTTOM accent.", "examples/ui_showcase/main.c:render_tabs", render_tabs, NULL},
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

    const nt_atlas_region_ref_t btn_green = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_BUTTON_GREEN.value);
    const nt_atlas_region_ref_t btn_red = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_BUTTON_RED.value);
    const nt_atlas_region_ref_t bar_fill_shaped = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_BAR_FILL_SHAPED.value);

    s_panel_beige_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_PANEL_BEIGE.value);
    s_panel_blue_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_PANEL_BLUE.value);
    s_panel_brown_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_PANEL_BROWN.value);
    s_icon_bunny_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_ICON_BUNNY.value);
    s_chevron_down_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_CHEVRON_DOWN.value);
    s_arrow_right_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_ARROW_RIGHT.value);
    s_caret_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_CARET.value);
    s_tabs_icon_idle_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_ICON_BUNNY.value);
    s_tabs_icon_sel_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_SHOWCASE_ATLAS_CHECKMARK.value);

    /* Flat buttons: no atlas art (idle.bg.atlas.id stays 0); solid bg via per-state bg_tint. */
    nt_ui_button_style_t flat_primary = {
        .idle = {.bg_tint = 0xFFCC7A3CU, .scale = 1.0F, .opacity = 1.0F},                       /* solid blue */
        .hover = {.bg_tint = 0xFFE08F4FU, .scale = 1.05F, .opacity = 1.0F},                     /* lighter */
        .pressed = {.bg_tint = 0xFFA86230U, .scale = 0.95F, .offset_y = 2.0F, .opacity = 1.0F}, /* darker */
        .disabled = {.bg_tint = 0xFFCC7A3CU, .scale = 1.0F, .opacity = 0.4F},
        .transition_speed = 12.0F,
        .hit_padding_lrtb = {16, 16, 16, 16},
        .slice9_scale = 1.0F,
    };
    /* Flat secondary: same shape, green tints. */
    nt_ui_button_style_t flat_secondary = flat_primary;
    flat_secondary.idle.bg_tint = 0xFF5FA84FU;
    flat_secondary.hover.bg_tint = 0xFF72C25FU;
    flat_secondary.pressed.bg_tint = 0xFF4A8A3CU;
    flat_secondary.disabled.bg_tint = 0xFF5FA84FU;

    /* Flat styles are theme-agnostic -> dark == light. */
    s_btn_primary_dark = flat_primary;
    s_btn_primary_light = flat_primary;

    s_btn_secondary_dark = flat_secondary;
    s_btn_secondary_light = flat_secondary;

    /* Exaggerated-scale variant: flat blue, hover blows up to 1.20, press shrinks to 0.80 (no offset). */
    s_btn_scale_dark = flat_primary;
    s_btn_scale_dark.hover.scale = 1.20F;
    s_btn_scale_dark.pressed.scale = 0.80F;
    s_btn_scale_dark.pressed.offset_y = 0.0F;
    s_btn_scale_light = s_btn_scale_dark;

    /* Per-state art swap: bg_tint stays no-tint (0xFFFFFFFF) so the art shows its own color. */
    nt_ui_button_style_t swap_base = {
        .idle = {.bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F},
        .hover = {.bg_tint = 0xFFFFFFFFU, .scale = 1.05F, .opacity = 1.0F},
        .pressed = {.bg_tint = 0xFFFFFFFFU, .scale = 0.95F, .offset_y = 2.0F, .opacity = 1.0F},
        .disabled = {.bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 0.4F},
        .transition_speed = 12.0F,
        .hit_padding_lrtb = {16, 16, 16, 16},
        .slice9_scale = 1.0F,
    };
    s_btn_swap_dark = swap_base;
    s_btn_swap_dark.idle.bg = btn_blue;
    s_btn_swap_dark.hover.bg = btn_green;
    s_btn_swap_dark.pressed.bg = btn_red;
    s_btn_swap_light = s_btn_swap_dark;

    /* No-pad variant: zero hit padding so visual == hit. */
    s_btn_nopad_dark = flat_primary;
    s_btn_nopad_dark.hit_padding_lrtb[0] = 0;
    s_btn_nopad_dark.hit_padding_lrtb[1] = 0;
    s_btn_nopad_dark.hit_padding_lrtb[2] = 0;
    s_btn_nopad_dark.hit_padding_lrtb[3] = 0;
    s_btn_nopad_light = s_btn_nopad_dark;

    /* Tab-row target: idle bg_tint 0xFFFFFFFF is no-tint, so the row's CLAY bg shows through. */
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
    /* Disabled dim: box/check refs left {0} inherit the idle art (engine ref_or); only opacity
     * differs per cell (no fallback), so set it here to match the buttons' 0.4 disabled dim.
     * On check_base => radio/toggle inherit the same disabled look. */
    check_base.unchecked[NT_UI_CB_DISABLED].opacity = 0.4F;
    check_base.checked[NT_UI_CB_DISABLED].opacity = 0.4F;
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

    /* Props-card slider: narrower track so track + full thumb travel stay inside the props card's
     * inner width (card 340 - 2x14 pad = 312; track 290 leaves ~11px each side, thumb within track_w). */
    nt_ui_slider_style_t slider_props = slider_base;
    slider_props.track_w = 290;
    s_slider_props_dark = slider_props;
    s_slider_props_light = slider_props;

    /* ---- Progress: track + smooth STRETCH slice9 fill. ---- */
    nt_ui_progress_style_t progress_base = nt_ui_progress_style_defaults();
    progress_base.track_w = 320;
    progress_base.track_h = 24;
    progress_base.value_speed = 6.0F;
    progress_base.track = bar_track;
    progress_base.fill = bar_fill;
    s_progress_dark = progress_base;
    s_progress_light = progress_base;

    /* CROP variant: shaped fill revealed by a clip scissor (slice9 is ignored in CROP). */
    nt_ui_progress_style_t progress_crop = progress_base;
    progress_crop.fill = bar_fill_shaped;
    progress_crop.fill_mode = NT_UI_FILL_CROP;
    progress_crop.fill_direction = NT_UI_FILL_LTR;
    s_progress_crop_dark = progress_crop;
    s_progress_crop_light = progress_crop;

    /* Vertical "mana" bar: STRETCH slice9 fill, BOTTOM_UP, blue-tinted, narrow + tall. */
    nt_ui_progress_style_t progress_vert = progress_base;
    progress_vert.track_w = 28.0F;
    progress_vert.track_h = 150.0F;
    progress_vert.fill_tint = 0xFFF06030U; /* blue mana: 0xAABBGGRR, blue-dominant over low green/red */
    progress_vert.fill_mode = NT_UI_FILL_STRETCH;
    progress_vert.fill_direction = NT_UI_FILL_BOTTOM_UP;
    s_progress_vert_dark = progress_vert;
    s_progress_vert_light = progress_vert;

    /* Scroll: four variants differ only in bar visibility + which axes scroll; same art. */
    nt_ui_scroll_style_t scroll_base = nt_ui_scroll_style_defaults();
    scroll_base.bar_thickness = 12.0F;
    scroll_base.bar_thumb_min_px = 28.0F;
    scroll_base.bar_fade_speed = 8.0F;
    scroll_base.track_tint = 0xC0FFFFFF;
    scroll_base.thumb_tint = 0xFFFFFFFF;
    scroll_base.track_ref = scroll_track;
    scroll_base.thumb_ref = bar_thumb;

    nt_ui_scroll_style_t scroll_hide = scroll_base;
    scroll_hide.bar_visibility = NT_UI_SCROLLBAR_AUTO_HIDE;
    s_scroll_hide_dark = scroll_hide;
    s_scroll_hide_light = scroll_hide;

    nt_ui_scroll_style_t scroll_always = scroll_base;
    scroll_always.bar_visibility = NT_UI_SCROLLBAR_ALWAYS;
    s_scroll_always_dark = scroll_always;
    s_scroll_always_light = scroll_always;

    /* Horizontal-only: scroll_x on, scroll_y off; ALWAYS bar on the bottom edge. */
    nt_ui_scroll_style_t scroll_horiz = scroll_always;
    scroll_horiz.scroll_x = true;
    scroll_horiz.scroll_y = false;
    s_scroll_horiz_dark = scroll_horiz;
    s_scroll_horiz_light = scroll_horiz;

    /* Both axes: content wider AND taller than the container; ALWAYS bars on both edges. */
    nt_ui_scroll_style_t scroll_xy = scroll_always;
    scroll_xy.scroll_x = true;
    scroll_xy.scroll_y = true;
    s_scroll_xy_dark = scroll_xy;
    s_scroll_xy_light = scroll_xy;

    /* Input field: flat-color field (no atlas art) with a focused-vs-idle bg/border variant.
     * Light palette darkens the entered text; numeric/password vary only the filter + mask flags. */
    nt_ui_input_style_t input_base = nt_ui_input_style_defaults();
    input_base.text.font_id = 0;
    input_base.text.font_size = 22.0F;
    input_base.text.color = (Clay_Color){225.0F, 228.0F, 235.0F, 255.0F};
    input_base.placeholder.font_id = 0;
    input_base.placeholder.font_size = 22.0F;
    input_base.pad_x = 10.0F;
    input_base.pad_y = 8.0F;
    s_input_dark = input_base;
    s_input_dark.placeholder.color = (Clay_Color){120.0F, 126.0F, 138.0F, 255.0F}; /* dimmed vs the bright text */
    s_input_light = input_base;
    s_input_light.text.color = (Clay_Color){28.0F, 30.0F, 38.0F, 255.0F};
    s_input_light.placeholder.color = (Clay_Color){150.0F, 154.0F, 162.0F, 255.0F}; /* dimmed grey on the light bg */
    s_input_light.skin[NT_UI_INPUT_IDLE].bg_color = 0xFFF0F0F0U;
    s_input_light.skin[NT_UI_INPUT_FOCUSED].bg_color = 0xFFFFFFFFU;
    s_input_light.skin[NT_UI_INPUT_DISABLED] = (nt_ui_input_skin_t){.bg_color = 0xFFD8D8D8U, .border_color = 0xFFB0B0B0U}; /* light-theme greyed */
    s_input_light.caret_color = 0xFF202020U;

    /* Caret variant: a thick, bright-amber caret that never blinks (blink_rate <= 0 = always on) --
     * shows caret_color/caret_width/caret_blink_rate are configurable. */
    s_input_caret_dark = s_input_dark;
    s_input_caret_dark.caret_color = 0xFF30A0FFU; /* amber (0xAABBGGRR) */
    s_input_caret_dark.caret_width = 4.0F;
    s_input_caret_dark.caret_blink_rate = 0.0F;
    s_input_caret_light = s_input_light;
    s_input_caret_light.caret_color = 0xFF30A0FFU;
    s_input_caret_light.caret_width = 4.0F;
    s_input_caret_light.caret_blink_rate = 0.0F;

    /* Selection variant: a distinct magenta selection highlight (vs the default blue-grey) -- shows
     * selection_color is configurable. Faster blink to vary the caret too. */
    s_input_sel_dark = s_input_dark;
    s_input_sel_dark.selection_color = 0x80C040C0U; /* translucent magenta */
    s_input_sel_dark.caret_blink_rate = 0.4F;       /* faster than the 1.0s default */
    s_input_sel_light = s_input_light;
    s_input_sel_light.selection_color = 0x80C040C0U;
    s_input_sel_light.caret_blink_rate = 0.4F;

    /* Sprite background: a 9-slice panel frame per state -- idle beige, hover brown, focused blue -- so
     * the field shows three distinct frame arts (skin[]) on interaction. bg_color stays 0 (untinted) so
     * each frame draws its natural color; the frames are light enough that the dark text reads. */
    s_input_art = input_base;
    s_input_art.text.color = (Clay_Color){28.0F, 30.0F, 38.0F, 255.0F};
    s_input_art.placeholder.color = (Clay_Color){90.0F, 80.0F, 70.0F, 255.0F};
    s_input_art.caret_color = 0xFF202020U;
    /* bg_color stays 0 (untinted); each state shows a distinct frame sprite. */
    s_input_art.skin[NT_UI_INPUT_IDLE] = (nt_ui_input_skin_t){.bg_art = s_panel_beige_ref};
    s_input_art.skin[NT_UI_INPUT_HOVER] = (nt_ui_input_skin_t){.bg_art = s_panel_brown_ref};
    s_input_art.skin[NT_UI_INPUT_FOCUSED] = (nt_ui_input_skin_t){.bg_art = s_panel_blue_ref};
    s_input_art.border_width = 0.0F; /* frame lives in the art now, no vector border */

    /* Modal: only backdrop_color flips per palette; the props panel owns backdrop_alpha. */
    nt_ui_modal_style_t modal_base = nt_ui_modal_style_defaults();
    modal_base.layer = LAYER_BG;
    s_modal_dark = modal_base;
    s_modal_dark.backdrop_color = 0xFF000000U; /* black */
    s_modal_light = modal_base;
    s_modal_light.backdrop_color = 0xFF202830U; /* slate */

    /* ---- Dropdown (dogfood): COHESIVE warm-panel family (mirrors the tab-bar cohesion fix). The trigger,
     * the list panel, AND the rows all ride the SAME panel_brown slice9 so the widget reads as one family —
     * NO clashing solid-blue button. States differentiate by tint only: muted idle -> lifted hover ->
     * brightened pressed/selected, with a warm accent on the selected row. The chevron eases its open-
     * rotation; an icon gutter aligns iconed + text-only rows. The long list shows a scrollbar (scroll_track
     * / bar_thumb sprites). Flat colors stay as the atlas-free fallback. corner_radius is moot for IMAGE bg. ---- */
    s_dropdown_dark = nt_ui_dropdown_style_defaults();
    s_dropdown_dark.row_height = 30U;
    s_dropdown_dark.max_visible_rows = 6U; /* the long city list scrolls past this */
    s_dropdown_dark.icon_size = 22U;       /* leading icon gutter so iconed + text-only rows align */
    s_dropdown_dark.chevron = s_chevron_down_ref;
    s_dropdown_dark.chevron_tint = 0xFFE8F0FCU;
    s_dropdown_dark.slice9_scale = 1.0F;
    /* Trigger: the panel sprite tinted per-state (muted idle -> lifted hover -> brightened pressed). */
    s_dropdown_dark.trigger_idle.bg = s_panel_brown_ref;
    s_dropdown_dark.trigger_idle.bg_tint = 0xFF8C8C8CU; /* muted neutral at rest */
    s_dropdown_dark.trigger_idle.fill = 0xFF3A3A3AU;    /* atlas-free fallback */
    s_dropdown_dark.trigger_hover.bg = s_panel_brown_ref;
    s_dropdown_dark.trigger_hover.bg_tint = 0xFFB0AAA4U; /* lifted on hover */
    s_dropdown_dark.trigger_hover.fill = 0xFF464646U;
    s_dropdown_dark.trigger_pressed.bg = s_panel_brown_ref;
    s_dropdown_dark.trigger_pressed.bg_tint = 0xFFD6CDC6U; /* brightened while held */
    s_dropdown_dark.trigger_pressed.fill = 0xFF2E2E2EU;
    s_dropdown_dark.trigger_pressed.scale = 0.98F;
    /* List panel: the same panel_brown slice9 frame; rows ride flat fills over it (transparent idle so the
     * panel shows through), with a warm accent on the selected row — one cohesive family. */
    s_dropdown_dark.panel_bg = s_panel_brown_ref;
    s_dropdown_dark.panel_tint = 0xFFB0AAA4U;     /* lift the panel so rows read against it */
    s_dropdown_dark.row_hover.fill = 0x40FFFFFFU; /* translucent white wash on hover */
    s_dropdown_dark.row_pressed.fill = 0xFF4A3A34U;
    s_dropdown_dark.row_selected.fill = 0xFF50B0E0U; /* warm gold accent, matches the tab-bar accent */
    s_dropdown_dark.trigger_text = 0xFFFCF7F5U;
    s_dropdown_dark.row_text = 0xFFFCF7F5U;
    /* Game owns the list scroll feel + bar via the embedded list_scroll style; wire the bar sprites so the
     * long list shows a visible bar when it scrolls. */
    s_dropdown_dark.list_scroll = nt_ui_scroll_style_defaults();
    s_dropdown_dark.list_scroll.track_ref = scroll_track;
    s_dropdown_dark.list_scroll.thumb_ref = bar_thumb;
    s_dropdown_dark.list_scroll.track_tint = 0xC0FFFFFFU;
    s_dropdown_dark.list_scroll.thumb_tint = 0xFFFFFFFFU;
    s_dropdown_dark.open_ease_speed = 14.0F; /* demo the new open-tween knob: the list eases open */
    s_dropdown_light = s_dropdown_dark;
    s_dropdown_light.trigger_text = 0xFF381C0CU;
    s_dropdown_light.row_text = 0xFF381C0CU;
    s_dropdown_light.chevron_tint = 0xFF24364CU;
    s_dropdown_light.trigger_idle.bg_tint = 0xFFC4C4C4U; /* lighter neutral on the pale card */
    s_dropdown_light.trigger_hover.bg_tint = 0xFFE0DAD4U;
    s_dropdown_light.trigger_pressed.bg_tint = 0xFFFFFFFFU;
    s_dropdown_light.panel_tint = 0xFFFFFFFFU;     /* full warm panel on the pale theme */
    s_dropdown_light.row_hover.fill = 0x30000000U; /* translucent dark wash on hover */
    s_dropdown_light.row_pressed.fill = 0xFFF0B68AU;
    s_dropdown_light.row_selected.fill = 0xFF3C8CC8U; /* warm amber accent for the pale theme */

    /* ---- Tooltip: a short reveal delay; dark uses the panel_blue slice9 frame + a caret pointing at the
     * target (flips ABOVE near the bottom border) + a soft drop-shadow. Light flips to a pale flat panel +
     * dark text, keeping the caret + a thin border + a subtle shadow. ---- */
    s_tooltip_dark = nt_ui_tooltip_style_defaults();
    s_tooltip_dark.delay_secs = 0.5F;
    s_tooltip_dark.font_size = 16.0F;
    s_tooltip_dark.open_ease_speed = 14.0F; /* demo the new open-tween knob: the tooltip eases open */
    s_tooltip_dark.panel_art = s_panel_blue_ref;
    s_tooltip_dark.panel_tint = 0xFFFFFFFFU;
    s_tooltip_dark.caret = s_caret_ref;
    s_tooltip_dark.caret_tint = 0xFFCFE2FAU; /* pale blue caret matching the panel_blue frame */
    s_tooltip_dark.caret_size = 14U;
    s_tooltip_dark.shadow_color = 0x60000000U; /* translucent black drop-shadow */
    s_tooltip_dark.shadow_offset_px = 4;
    s_tooltip_light = s_tooltip_dark;
    s_tooltip_light.panel_art = (nt_atlas_region_ref_t){0}; /* flat pale panel on the light theme (no art) */
    s_tooltip_light.panel_bg = 0xFFF4F4F4U;
    s_tooltip_light.text_color = 0xFF202830U;
    s_tooltip_light.caret_tint = 0xFFF4F4F4U;   /* caret matches the pale panel */
    s_tooltip_light.border_color = 0xFFB8C2CCU; /* thin cool-grey border around the pale panel */
    s_tooltip_light.border_px = 1U;
    s_tooltip_light.shadow_color = 0x30000000U; /* lighter shadow on the pale theme */

    /* ---- Context menu (dogfood): panel_blue slice9 frame + arrow_right submenu marker + an icon gutter
     * (bunny on some rows, aligned-empty on the rest). Rows keep the eased flat hover highlight over the
     * sprite panel for legibility. Flat bg_color + ">" text marker remain the atlas-free fallback. ---- */
    s_menu_dark = nt_ui_menu_style_defaults();
    s_menu_dark.item_height = 30U;
    s_menu_dark.font_size = 16.0F;
    s_menu_dark.min_width = 200U;
    s_menu_dark.icon_size = 22U; /* leading gutter so iconed + text-only rows align */
    s_menu_dark.panel_bg = s_panel_blue_ref;
    s_menu_dark.panel_tint = 0xFFFFFFFFU;
    s_menu_dark.arrow = s_arrow_right_ref;
    s_menu_dark.arrow_tint = 0xFFE8F0FCU;
    s_menu_dark.text_disabled = 0xFF6E7682U; /* muted slate: legible on the LIGHT slice9 panel (default grey blends in) */
    s_menu_light = s_menu_dark;
    s_menu_light.bg_color = 0xFFFFFFFFU;
    s_menu_light.item_hover_color = 0xFFDCE6F4U;
    s_menu_light.text_color = 0xFF202830U;
    s_menu_light.arrow_tint = 0xFF24364CU;

    /* ---- Tab-bar (dogfood): sprite-based game UI. Each state draws a Kenney slice9 button sprite
     * tinted per-state (idle muted neutral, hover lightened, selected full blue) so the nav eases
     * between real slice9 art, not flat rects. corner_radius=0: an IMAGE bg can't round (asserts).
     * fill stays set as the atlas-free fallback. ---- */
    s_tabbar_dark = nt_ui_tabbar_style_defaults();
    s_tabbar_dark.bar_bg = 0U;        /* the surrounding card owns the bg; tabs sit on it */
    s_tabbar_dark.corner_radius = 0U; /* IMAGE bg: rounding is baked into the sprite, not Clay */
    s_tabbar_dark.tab_extent = 40U;   /* a touch taller so the 16px slice9 caps read */
    s_tabbar_dark.idle.bg = s_panel_brown_ref;
    s_tabbar_dark.idle.bg_tint = 0xFF6E6E6EU; /* muted neutral so unselected tabs recede */
    s_tabbar_dark.idle.fill = 0xFF221A18U;    /* atlas-free fallback: list_bg {24,26,34} */
    /* All states share the idle panel sprite so hover/selected stay in-family — differentiate by tint
     * (lighter on hover, brighter on selected) + the accent bar, not a different sprite. */
    s_tabbar_dark.hover.bg = s_panel_brown_ref;
    s_tabbar_dark.hover.bg_tint = 0xFF8C8C8CU; /* same panel, lifted neutral on hover */
    s_tabbar_dark.hover.fill = 0xFF332826U;    /* fallback */
    s_tabbar_dark.selected.bg = s_panel_brown_ref;
    s_tabbar_dark.selected.bg_tint = 0xFFD6CDC6U; /* same panel, brightened so the active tab reads */
    s_tabbar_dark.selected.fill = 0xFF4A3A34U;    /* fallback: list_sel */
    s_tabbar_dark.selected.scale = 1.04F;         /* gentle pop on the active tab */
    s_tabbar_dark.accent = 0xFF50B0E0U;           /* warm gold {224,176,80} — harmonizes with the brown panel */
    s_tabbar_dark.text = 0xFFB6AAA5U;             /* caption {165,170,182} */
    s_tabbar_dark.text_selected = 0xFFFCF7F5U;    /* row_sel {245,247,252} */
    s_tabbar_dark.font_size = 16.0F;
    s_tabbar_light = s_tabbar_dark;
    s_tabbar_light.idle.bg_tint = 0xFFB0B0B0U;     /* lighter neutral on the pale card */
    s_tabbar_light.idle.fill = 0xFFEBE3E0U;        /* fallback: list_bg {224,227,235} */
    s_tabbar_light.hover.bg_tint = 0xFFC4C4C4U;    /* same panel, gentle lift on the pale card */
    s_tabbar_light.hover.fill = 0xFFDDD3D0U;       /* fallback */
    s_tabbar_light.selected.bg_tint = 0xFFFFFFFFU; /* full warm panel on the active tab */
    s_tabbar_light.selected.fill = 0xFFF0B68AU;    /* fallback: list_sel */
    s_tabbar_light.accent = 0xFF3C8CC8U;           /* warm amber accent for the pale theme */
    /* Idle nav labels must read on the tan brown-panel tab fill: the prior mid-grey ({90,92,104}) was too
     * close in luminance to the medium-tan tint -> unreadable. Use a dark warm slate so idle labels have
     * strong contrast; selected stays the darker navy so the active tab still differentiates. */
    s_tabbar_light.text = 0xFF2E2620U;          /* dark warm slate {32,38,46} -- strong contrast on tan */
    s_tabbar_light.text_selected = 0xFF381C0CU; /* row_sel {12,28,56} */

    /* ---- Tabs demo strip (begin/end core): horizontal, flat-color, BOTTOM accent. The game owns each
     * tab's content (icon + text, and a brighter icon when selected) -- the style only carries the bar
     * bg / per-state fill / accent. accent_side differs from the nav's LEFT to show it's configurable. ---- */
    s_tabs_demo_dark = nt_ui_tabbar_style_defaults();
    s_tabs_demo_dark.dir = NT_UI_TABBAR_HORIZONTAL;
    s_tabs_demo_dark.accent_side = NT_UI_TABBAR_ACCENT_BOTTOM;
    s_tabs_demo_dark.bar_bg = 0xFF221A18U; /* card list_bg */
    s_tabs_demo_dark.tab_extent = 132U;    /* width (horizontal): room for icon + label */
    s_tabs_demo_dark.corner_radius = 8U;
    s_tabs_demo_dark.idle.fill = 0U; /* transparent: the bar bg shows through */
    s_tabs_demo_dark.hover.fill = 0xFF332826U;
    s_tabs_demo_dark.selected.fill = 0xFF4A3A34U; /* list_sel */
    s_tabs_demo_dark.selected.scale = 1.04F;
    s_tabs_demo_dark.accent = 0xFF50B0E0U; /* warm gold */
    s_tabs_demo_dark.text = 0xFFB6AAA5U;
    s_tabs_demo_dark.text_selected = 0xFFFCF7F5U;
    s_tabs_demo_dark.font_size = 16.0F;
    s_tabs_demo_light = s_tabs_demo_dark;
    s_tabs_demo_light.bar_bg = 0xFFEBE3E0U; /* pale card list_bg */
    s_tabs_demo_light.hover.fill = 0xFFDDD3D0U;
    s_tabs_demo_light.selected.fill = 0xFFF0B68AU; /* list_sel */
    s_tabs_demo_light.accent = 0xFF3C8CC8U;
    s_tabs_demo_light.text = 0xFF685C5AU;
    s_tabs_demo_light.text_selected = 0xFF381C0CU;
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

// #region widget tab render fns
/* rounded=false for atlas-bg buttons: a non-zero cornerRadius on a bg IMAGE asserts in the engine. */
static void button_cell(nt_ui_context_t *ctx, uint32_t id, nt_ui_button_style_t *style, const char *text, bool enabled, bool rounded) {
    nt_ui_button_begin(
        ctx, NT_UI_DATA_LAYER(LAYER_IMG), id, style,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(72)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                                   .cornerRadius = rounded ? CLAY_CORNER_RADIUS(8) : CLAY_CORNER_RADIUS(0)},
        enabled, NULL);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), text, g_current->body);
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

/* A captioned button cell: a variant title/sub above one button, themed via the palette. */
static void labelled_button_cell(nt_ui_context_t *ctx, const char *title, const char *sub, uint32_t id, nt_ui_button_style_t *style, const char *text, bool enabled, bool rounded) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), title, g_current->body);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), sub, g_current->caption);
        button_cell(ctx, id, style, text, enabled, rounded);
    }
}

/* Buttons tab: standard / scale / art-swap / no-pad / icon / disabled, in two rows of three. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void render_buttons(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    static const Clay_ElementDeclaration grid_row = {
        .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 24, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}};

    CLAY(grid_row) {
        labelled_button_cell(ctx, "Standard", "idle/hover/pressed/disabled", nt_ui_id("showcase/btn_standard"), g_current->btn_primary, "Primary", true, true);
        labelled_button_cell(ctx, "Scale", "hover 1.20 / press 0.80", nt_ui_id("showcase/btn_scale"), g_current->btn_scale, "Boom", true, true);
        /* Art-swap bg is an atlas IMAGE: rounding is baked into the art, so the cell sets no cornerRadius. */
        labelled_button_cell(ctx, "Art swap", "blue idle / green hover / red press", nt_ui_id("showcase/btn_swap"), g_current->btn_swap, "Swap", true, false);
    }
    CLAY(grid_row) {
        labelled_button_cell(ctx, "No-pad", "hit==visual (no touch padding)", nt_ui_id("showcase/btn_nopad"), g_current->btn_nopad, "Tight", true, true);
        /* Icon button: bunny child, untinted (its own art color), inside the no-pad style. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Icon", g_current->body);
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "icon child (untinted)", g_current->caption);
            nt_ui_button_begin(
                ctx, NT_UI_DATA_LAYER(LAYER_IMG), nt_ui_id("showcase/btn_icon"), g_current->btn_nopad,
                &(Clay_ElementDeclaration){
                    .layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(72)}, .padding = CLAY_PADDING_ALL(8), .childGap = 14, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                    .cornerRadius = CLAY_CORNER_RADIUS(8)},
                true, NULL);
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(48), CLAY_SIZING_FIXED(48)}}}) { nt_ui_image(ctx, NT_UI_DATA_LAYER(LAYER_IMG), &s_icon_bunny_ref, &g_panel_img_style, NULL); }
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Play", g_current->body);
            (void)nt_ui_button_end(ctx);
        }
        labelled_button_cell(ctx, "Disabled", "enabled=false short-circuits + dims", nt_ui_id("showcase/btn_disabled"), g_current->btn_primary, "Locked", false, true);
    }
}

/* Buttons: Transform tab. Proves inverse-affine hit-test: the button still clicks while transformed. */
static void render_button_transform(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[80];
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "The button below is transformed by the properties panel. It STILL hit-tests correctly.", g_current->caption);
    (void)snprintf(buf, sizeof buf, "clicks: %u   (rot %.0f deg  scale %.2f  offset %.0f,%.0f)", st->btn_xform.clicks, (double)st->btn_xform.rotation_deg, (double)st->btn_xform.scale,
                   (double)st->btn_xform.offset_x, (double)st->btn_xform.offset_y);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->body);

    /* Stable slot so the transformed button has room to rotate/offset without overlap. */
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(520), CLAY_SIZING_FIXED(360)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        const nt_ui_transform_t xform = {
            .offset_x = st->btn_xform.offset_x,
            .offset_y = st->btn_xform.offset_y,
            .rotation_z = glm_rad(st->btn_xform.rotation_deg),
            .scale_x = st->btn_xform.scale,
            .scale_y = st->btn_xform.scale,
            .scale_z = 1.0F,
        };
        /* The wrapping CLAY's transform composes into the button's bake, so renderer + hit-test agree. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}}, .userData = NT_UI_CLAY_DATA_XFORM(0U, &xform, 1.0F)}) {
            nt_ui_button_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), nt_ui_id("showcase/btn_xform"), g_current->btn_primary,
                               &(Clay_ElementDeclaration){
                                   .layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(96)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                                   .cornerRadius = CLAY_CORNER_RADIUS(8)},
                               true, NULL);
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Click me", g_current->body);
            if (nt_ui_button_end(ctx)) {
                st->btn_xform.clicks++;
            }
        }
    }
}

/* One reference panel at a fixed size; corners stay non-stretched. */
static void slice9_ref(nt_ui_context_t *ctx, nt_atlas_region_ref_t *ref, float w, float h, const char *cap) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), cap, g_current->caption);
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}}}) { nt_ui_image(ctx, NT_UI_DATA_LAYER(LAYER_IMG), ref, &g_panel_img_style, NULL); }
    }
}

static void render_slice9(nt_ui_context_t *ctx, tab_state_t *st) {
    /* Three reference sizes prove corners stay crisp while the center stretches. Wrapped over two
     * rows so the widest set still fits the content column beside the props card (no overflow). */
    static const Clay_ElementDeclaration ref_row = {
        .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 16, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}};
    CLAY(ref_row) {
        slice9_ref(ctx, &s_panel_blue_ref, 200.0F, 70.0F, "200x70");
        slice9_ref(ctx, &s_panel_blue_ref, 300.0F, 70.0F, "300x70");
    }
    CLAY(ref_row) { slice9_ref(ctx, &s_panel_blue_ref, 260.0F, 160.0F, "260x160"); }

    /* A slice9-backed nt_ui_panel container with a child label. */
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(360), CLAY_SIZING_FIXED(90)}, .padding = CLAY_PADDING_ALL(14), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_panel_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), &s_panel_beige_ref, &g_panel_img_style, NULL);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Panel with child (corners crisp)", g_current->body);
        nt_ui_panel_end(ctx);
    }

    /* Panel-driven: the props_slice9 sliders feed insets + target size into this emit live. */
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Panel-driven (insets + size from the properties panel):", g_current->caption);
    nt_ui_image_style_t live = {
        .color_packed = 0xFFFFFFFF,
        .slice9_scale = st->s9.slice9_scale,
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
    (void)nt_ui_checkbox(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, nt_ui_id("showcase/cb_locked"), "Locked (disabled)", &st->cb_locked, g_current->check, &row, false);

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

    /* Progress: STRETCH (slice9) + CROP (clip, shaped fill) side by side, plus a vertical mana bar.
     * All three read the same panel-driven value, so they animate together. */
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Progress: STRETCH (slice9) / CROP (clip) / vertical (BOTTOM_UP)", g_current->caption);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 28, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8}}) {
            (void)snprintf(buf, sizeof buf, "STRETCH (slice9)  %d%%", (int)(st->prog.value * 100.0F));
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
            nt_ui_progress(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_IMG, s_id_progress, st->prog.value, g_current->progress, &pdecl);

            (void)snprintf(buf, sizeof buf, "CROP (clip)  %d%%", (int)(st->prog.value * 100.0F));
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
            nt_ui_progress(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_IMG, s_id_progress_crop, st->prog.value, g_current->progress_crop, &pdecl);
        }
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}}}) {
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Mana", g_current->caption);
            nt_ui_progress(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_IMG, s_id_progress_vert, st->prog.value, g_current->progress_vert,
                           &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(28), CLAY_SIZING_FIXED(150)}}});
        }
    }
}

/* One vertical-list item row (fills container width). */
static void scroll_row(nt_ui_context_t *ctx, const char *text) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(28)}, .padding = {.left = 8}, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), text, g_current->body);
    }
}

/* One fixed-size boxed cell (horizontal + XY grids need intrinsic content larger than the box). */
static void scroll_cell(nt_ui_context_t *ctx, const char *text, float w, float h) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = g_current->list_bg,
          .cornerRadius = CLAY_CORNER_RADIUS(4)}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), text, g_current->caption);
    }
}

/* A captioned scroll tile: title above its own scroll container (NOT nested in another scroll). */
static void scroll_tile_begin(nt_ui_context_t *ctx, const char *title, uint32_t id, nt_ui_scroll_style_t *style, float w, float h) {
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), title, g_current->caption);
    nt_ui_scroll_begin(ctx, NULL, id, style,
                       &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}, .padding = CLAY_PADDING_ALL(6)},
                                                  .backgroundColor = g_current->bg,
                                                  .cornerRadius = CLAY_CORNER_RADIUS(8)});
}

/* Four independent (non-nested) scroll containers in a 2x2 grid; each owns its drag-capture + bars. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void render_scroll(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    char buf[48];
    static const Clay_ElementDeclaration tile = {.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}};
    static const Clay_ElementDeclaration grow_col = {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}};
    static const Clay_ElementDeclaration grid_row = {
        .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 20, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}};

    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 16}}) {
        /* Top row: two vertical lists (AUTO_HIDE bar lingers then fades / ALWAYS bar stays). */
        CLAY(grid_row) {
            CLAY(tile) {
                scroll_tile_begin(ctx, "Vertical AUTO_HIDE", s_id_scroll_hide, g_current->scroll_hide, 280.0F, 150.0F);
                CLAY(grow_col) {
                    for (int i = 0; i < 24; ++i) {
                        (void)snprintf(buf, sizeof buf, "AUTO_HIDE row %02d", i);
                        scroll_row(ctx, buf);
                    }
                }
                nt_ui_scroll_end(ctx);
            }
            CLAY(tile) {
                scroll_tile_begin(ctx, "Vertical ALWAYS", s_id_scroll_always, g_current->scroll_always, 280.0F, 150.0F);
                CLAY(grow_col) {
                    for (int i = 0; i < 24; ++i) {
                        (void)snprintf(buf, sizeof buf, "ALWAYS row %02d", i);
                        scroll_row(ctx, buf);
                    }
                }
                nt_ui_scroll_end(ctx);
            }
        }
        /* Bottom row: horizontal-only (wide single row) + both-axes (grid bigger than the box). */
        CLAY(grid_row) {
            CLAY(tile) {
                scroll_tile_begin(ctx, "Horizontal-only", s_id_scroll_horiz, g_current->scroll_horiz, 280.0F, 88.0F);
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 6}}) {
                    for (int c = 0; c < 14; ++c) {
                        (void)snprintf(buf, sizeof buf, "col %02d", c);
                        scroll_cell(ctx, buf, 78.0F, 44.0F);
                    }
                }
                nt_ui_scroll_end(ctx);
            }
            CLAY(tile) {
                scroll_tile_begin(ctx, "Both axes (X + Y)", s_id_scroll_xy, g_current->scroll_xy, 280.0F, 150.0F);
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6}}) {
                    for (int r = 0; r < 8; ++r) {
                        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 6}}) {
                            for (int c = 0; c < 8; ++c) {
                                (void)snprintf(buf, sizeof buf, "%d,%d", r, c);
                                scroll_cell(ctx, buf, 56.0F, 30.0F);
                            }
                        }
                    }
                }
                nt_ui_scroll_end(ctx);
            }
        }
    }
}

/* Slice9 panel: sliders for insets L/R/T/B + target size, fed into render_slice9. */
static void props_slice9(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const Clay_ElementDeclaration sdecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_FIXED(26)}}};
    showcase_panel_begin(ctx, "Slice9 properties");

    (void)snprintf(buf, sizeof buf, "Inset L  %d", st->s9.inset_l);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_il, NULL, &st->s9.inset_l, 0, 48, 1, g_current->slider_props, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Inset R  %d", st->s9.inset_r);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_ir, NULL, &st->s9.inset_r, 0, 48, 1, g_current->slider_props, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Inset T  %d", st->s9.inset_t);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_it, NULL, &st->s9.inset_t, 0, 48, 1, g_current->slider_props, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Inset B  %d", st->s9.inset_b);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_ib, NULL, &st->s9.inset_b, 0, 48, 1, g_current->slider_props, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Width  %d", st->s9.target_w);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_w, NULL, &st->s9.target_w, 100, 320, 1, g_current->slider_props, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Height  %d", st->s9.target_h);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_int(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_h, NULL, &st->s9.target_h, 60, 300, 1, g_current->slider_props, &sdecl, true);

    /* Corner scale grows/shrinks the slice9 border thickness while the center keeps stretching. */
    (void)snprintf(buf, sizeof buf, "Corner scale  %.2f", (double)st->s9.slice9_scale);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_s9scale, NULL, &st->s9.slice9_scale, 0.5F, 3.0F, 0.0F, g_current->slider_props, &sdecl, true);

    showcase_panel_end(ctx);
}

/* Progress panel: a slider drives the bar value 0..1 + an auto-animate toggle. */
static void props_progress(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const Clay_ElementDeclaration sdecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_FIXED(26)}}};
    static const Clay_ElementDeclaration row = {.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(40)}, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}};
    showcase_panel_begin(ctx, "Progress properties");

    (void)snprintf(buf, sizeof buf, "Value  %.2f", (double)st->prog.value);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    /* Manual value slider is disabled while auto-animate drives the value. */
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_value, NULL, &st->prog.value, 0.0F, 1.0F, 0.0F, g_current->slider_props, &sdecl, !st->prog.auto_anim);

    (void)nt_ui_toggle(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, nt_ui_id("showcase/props_auto"), "Auto-animate", &st->prog.auto_anim, g_current->toggle, &row, true);

    showcase_panel_end(ctx);
}

/* Button-transform panel: rotation / scale / offset sliders drive the live transform each frame. */
static void props_button_transform(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const Clay_ElementDeclaration sdecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_FIXED(26)}}};
    showcase_panel_begin(ctx, "Transform properties");

    (void)snprintf(buf, sizeof buf, "Rotation  %.0f deg", (double)st->btn_xform.rotation_deg);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_rot, NULL, &st->btn_xform.rotation_deg, -180.0F, 180.0F, 0.0F, g_current->slider_props, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Scale  %.2f", (double)st->btn_xform.scale);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_bscale, NULL, &st->btn_xform.scale, 0.5F, 2.0F, 0.0F, g_current->slider_props, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Offset X  %.0f", (double)st->btn_xform.offset_x);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_offx, NULL, &st->btn_xform.offset_x, -120.0F, 120.0F, 0.0F, g_current->slider_props, &sdecl, true);

    (void)snprintf(buf, sizeof buf, "Offset Y  %.0f", (double)st->btn_xform.offset_y);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_offy, NULL, &st->btn_xform.offset_y, -120.0F, 120.0F, 0.0F, g_current->slider_props, &sdecl, true);

    nt_ui_button_begin(
        ctx, NT_UI_DATA_LAYER(LAYER_IMG), nt_ui_id("showcase/btn_xform_reset"), g_current->btn_secondary,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(44)}, .padding = CLAY_PADDING_ALL(6), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}},
        true, NULL);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Reset", g_current->body);
    if (nt_ui_button_end(ctx)) {
        st->btn_xform.rotation_deg = 20.0F;
        st->btn_xform.scale = 1.0F;
        st->btn_xform.offset_x = 0.0F;
        st->btn_xform.offset_y = 0.0F;
    }

    showcase_panel_end(ctx);
}

/* Runtime modal style: re-seeded from the palette each frame, then overlaid with the panel values. */
static nt_ui_modal_style_t s_modal_style_runtime;

/* Build one animation recipe from the segmented props (type index 0/1/2, edge 0..3, shared scalars). */
static nt_ui_modal_anim_t modal_param_anim(int type, int edge, float scale_start, float offset) {
    nt_ui_modal_anim_t a = {.type = NT_UI_MODAL_ANIM_SCALE_POP, .scale_start = scale_start};
    if (type == 1) {
        a.type = NT_UI_MODAL_ANIM_FADE;
    } else if (type == 2) {
        a.type = NT_UI_MODAL_ANIM_SLIDE;
        a.edge = (uint8_t)edge;
        a.offset = offset;
    }
    return a;
}

/* A labelled action button inside a modal body. */
static bool modal_action_btn(nt_ui_context_t *ctx, uint32_t id, const char *text) {
    /* FIT width (min 140) so the longest label ("Show nested") fits; 16px side padding gives breathing room. */
    nt_ui_button_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), id, g_current->btn_primary,
                       &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIT(.min = 140), CLAY_SIZING_FIXED(56)},
                                                             .padding = {.left = 16, .right = 16, .top = 8, .bottom = 8},
                                                             .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                                                  .cornerRadius = CLAY_CORNER_RADIUS(8)},
                       true, NULL);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), text, g_current->body);
    return nt_ui_button_end(ctx);
}

/* Tab body: description + the trigger only. The modal is declared at ROOT in render_modal_overlay
 * so its floating panel is never clipped by the stage scissor; this fn must NOT call nt_ui_modal_visible. */
static void render_modals(nt_ui_context_t *ctx, tab_state_t *st) {
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Open a confirm dialog; the properties panel drives the transition + tween live.", g_current->caption);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Esc closes the TOP modal; clicking the backdrop closes; the backdrop blocks click-through.", g_current->caption);

    nt_ui_button_begin(
        ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_modal_show_btn, g_current->btn_primary,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(64)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                                   .cornerRadius = CLAY_CORNER_RADIUS(8)},
        true, NULL);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Show confirm", g_current->body);
    if (nt_ui_button_end(ctx)) {
        st->confirm_open = true;
    }
}

/* Root overlay: confirm modal + nested depth-2 modal; Esc + backdrop close. Declared at ROOT (no
 * scissor ancestor) so the panel renders unclipped; the body stays declared through the close tween. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void render_modal_overlay(nt_ui_context_t *ctx, tab_state_t *st) {
    /* Seed the runtime style from the active palette, then overlay the props-panel values. */
    s_modal_style_runtime = *g_current->modal;
    s_modal_style_runtime.ease_speed = st->modal.ease_speed;
    s_modal_style_runtime.backdrop_alpha = st->modal.backdrop_alpha;
    s_modal_style_runtime.flags |= (uint8_t)(NT_UI_MODAL_LISTEN_ESC | NT_UI_MODAL_CLOSE_ON_BACKDROP);
    s_modal_style_runtime.open = modal_param_anim(st->modal.open_type, st->modal.open_edge, st->modal.scale_start, st->modal.slide_offset);
    s_modal_style_runtime.close = modal_param_anim(st->modal.close_type, st->modal.close_edge, st->modal.scale_start, st->modal.slide_offset);

    if (nt_ui_modal_visible(ctx, s_id_modal_confirm, &s_modal_style_runtime, &st->confirm_open)) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(520), CLAY_SIZING_FIT(0)},
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
            if (nt_ui_modal_visible(ctx, s_id_modal_nested, &s_modal_style_runtime, &st->nested_open)) {
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

/* One captioned text field: a caption above a fixed-width field that edits the game-owned buffer.
 * props carries the per-field behaviour + empty-hint string; style is the shared per-look visual. */
static void input_field(nt_ui_context_t *ctx, const char *caption, uint32_t id, char *buffer, size_t buffer_size, const nt_ui_input_props_t *props, const nt_ui_input_style_t *style, bool enabled) {
    static const Clay_ElementDeclaration field_decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(40)}}};
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), caption, g_current->caption);
        (void)nt_ui_input_text(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, id, buffer, buffer_size, props, style, &field_decl, enabled, NULL);
    }
}

/* Input tab: plain / numeric-filtered / password-masked / Cyrillic fields. All four edit
 * game-owned buffers in place; click to focus, type, select, Ctrl+C/X/V, Tab to advance, Esc to unfocus. */
static void render_input(nt_ui_context_t *ctx, tab_state_t *st) {
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Click a field to focus; type Latin or Cyrillic; select with Shift+arrows / drag / double-click / Ctrl+A; Ctrl+C/X/V clipboard.",
                g_current->caption);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Tab advances to the next field; Esc unfocuses. Empty fields show a dimmed 'edit me' hint. [T]/[D] hotkeys yield while typing.", g_current->caption);

    /* One shared visual style (g_current->input) per look; per-field behaviour + hint via props. */
    static const nt_ui_input_props_t props_plain = {.placeholder = "edit me", .allow = NULL, .max_length = 0U, .keyboard = NT_UI_KB_TEXT, .password = false};
    static const nt_ui_input_props_t props_numeric = {.placeholder = "edit me", .allow = nt_ui_filter_numeric, .max_length = 0U, .keyboard = NT_UI_KB_NUMERIC, .password = false};
    static const nt_ui_input_props_t props_password = {.placeholder = "edit me", .allow = NULL, .max_length = 0U, .keyboard = NT_UI_KB_PASSWORD, .password = true};
    static const nt_ui_input_props_t props_caret = {.placeholder = "thick amber caret", .allow = NULL, .max_length = 0U, .keyboard = NT_UI_KB_TEXT, .password = false};
    static const nt_ui_input_props_t props_sel = {.placeholder = "select me (magenta)", .allow = NULL, .max_length = 0U, .keyboard = NT_UI_KB_TEXT, .password = false};
    static const nt_ui_input_props_t props_art = {.placeholder = "framed by a sprite", .allow = NULL, .max_length = 0U, .keyboard = NT_UI_KB_TEXT, .password = false};
    static const nt_ui_input_props_t props_disabled = {.placeholder = "can't focus or type", .allow = NULL, .max_length = 0U, .keyboard = NT_UI_KB_TEXT, .password = false};

    input_field(ctx, "Plain text", s_id_input_plain, st->input.plain, sizeof st->input.plain, &props_plain, g_current->input, true);
    input_field(ctx, "Numeric only ([0-9.+-])", s_id_input_numeric, st->input.numeric, sizeof st->input.numeric, &props_numeric, g_current->input, true);
    input_field(ctx, "Password (masked)", s_id_input_password, st->input.password, sizeof st->input.password, &props_password, g_current->input, true);
    input_field(ctx, "Cyrillic (multi-byte UTF-8)", s_id_input_cyrillic, st->input.cyrillic, sizeof st->input.cyrillic, &props_plain, g_current->input, true);
    /* Visual-style variants: prove caret_color/width/blink + selection_color are configurable. */
    input_field(ctx, "Thick non-blinking amber caret", s_id_input_caret, st->input.caret_thick, sizeof st->input.caret_thick, &props_caret, g_current->input_caret, true);
    input_field(ctx, "Magenta selection + fast blink", s_id_input_sel, st->input.caret_sel, sizeof st->input.caret_sel, &props_sel, g_current->input_sel, true);
    /* Sprite-backed field: a 9-slice panel frame PER STATE -- beige idle, brown hover, blue focused. */
    input_field(ctx, "Sprite frame per state (idle/hover/focus)", s_id_input_art, st->input.art_bg, sizeof st->input.art_bg, &props_art, &s_input_art, true);
    /* Disabled field: enabled=false forces the disabled skin (dimmed) and rejects focus/typing. */
    input_field(ctx, "Disabled (greyed, no focus)", s_id_input_disabled, st->input.disabled, sizeof st->input.disabled, &props_disabled, g_current->input, false);
}

/* Events tab: a hold-to-confirm button (nt_ui_events gesture cfg -> hold_progress fill + long_pressed
 * fire) and a double-click target. All counters are game-owned (Model D); the engine owns only the
 * gesture cell behind the button id. */
static void render_events(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[96];
    /* Hold past this many seconds (without dragging off) confirms; the fill ramps over the same window. */
    static const nt_ui_events_cfg_t hold_cfg = {.long_press_secs = 1.5F, .double_click = false};
    static const nt_ui_events_cfg_t dbl_cfg = {.long_press_secs = 0.0F, .double_click = true};
    static const Clay_ElementDeclaration bar_decl = {.layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(14)}}};

    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Press and HOLD the button: the fill ramps to full over ~1.5s and confirms at the top.", g_current->caption);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Drag the cursor off the button mid-hold to RESET the fill (no confirm).", g_current->caption);

    /* The button itself drives the gesture cell via its cfg; we read the latched result back
     * idempotently after end() for the fill + the one-shot confirm. */
    nt_ui_button_begin(
        ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_events_hold, g_current->btn_primary,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(64)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                                   .cornerRadius = CLAY_CORNER_RADIUS(8)},
        true, &hold_cfg);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Hold to confirm", g_current->body);
    (void)nt_ui_button_end(ctx);

    const nt_ui_events_t hold = nt_ui_query_events(ctx, s_id_events_hold);
    st->events.last_progress = hold.hold_progress;
    if (hold.long_pressed) {
        st->events.confirms++;
    }

    /* Render the hold progress as a fill bar so the ramp is visible. */
    nt_ui_progress(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_IMG, s_id_events_fill, st->events.last_progress, g_current->progress, &bar_decl);

    /* A separate double-click target (a plain button) with a dbl-click readout. */
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Double-click the button below:", g_current->caption);
    nt_ui_button_begin(
        ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_events_dbl, g_current->btn_secondary,
        &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(56)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                                   .cornerRadius = CLAY_CORNER_RADIUS(8)},
        true, &dbl_cfg);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Double-click me", g_current->body);
    (void)nt_ui_button_end(ctx);
    if (nt_ui_query_events(ctx, s_id_events_dbl).double_clicked) {
        st->events.dbl_clicks++;
    }

    (void)snprintf(buf, sizeof buf, "confirms: %u   double-clicks: %u   hold_progress: %.2f", st->events.confirms, st->events.dbl_clicks, (double)st->events.last_progress);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->body);
}

/* Dropdown tab: the IMMEDIATE combo (begin/selectable/end). A short list, a long (scrolling) list to
 * exercise the scroll path + edge-flip-up near the bottom border, and a custom-trigger / custom-row combo
 * (preview_begin/end + selectable_begin/end). The game feeds rows per-call and owns selection +
 * open (Model D): a selectable's clicked return is where the game writes *selected; the combo clears *open. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — three combos' row loops, not deep nesting
static void render_dropdown(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const char *const fruits[] = {"Apple", "Banana", "Cherry", "Date", "Elderberry"};
    static const char *const cities[] = {"Amsterdam", "Berlin",  "Cairo", "Delhi",  "Edinburgh", "Florence", "Geneva", "Helsinki", "Istanbul", "Jakarta", "Kyoto",  "Lisbon",
                                         "Madrid",    "Nairobi", "Oslo",  "Prague", "Quito",     "Rome",     "Seoul",  "Tokyo",    "Utrecht",  "Vienna",  "Warsaw", "Zurich"};
    static const int fruit_count = (int)(sizeof fruits / sizeof fruits[0]);
    static const int city_count = (int)(sizeof cities / sizeof cities[0]);

    /* Some fruit rows carry a leading icon, the rest leave the gutter aligned-empty (OS-menu icon column). */
    static const bool fruit_iconed[] = {true, false, true, false, false};

    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Click a trigger to open its list; pick a row; open the long list near the window bottom to see the edge-flip up.", g_current->caption);

    /* Short list: fits without scrolling. A custom trigger so the CLOSED preview shows the selected fruit's
     * icon + name in the SAME icon-gutter idiom as the open rows (mirrors the engine row gutter); a couple of
     * rows draw an icon in the engine gutter (combo_selectable_icon) — all labels stay in one aligned column. */
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Fruit (short list, icon gutter)", g_current->caption);
    nt_ui_combo_preview_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_dd_fruit, g_current->dropdown, &st->dropdown.fruit_open);
    {
        const int sel = st->dropdown.fruit_sel;
        const float gut = (float)g_current->dropdown->icon_size; /* same gutter as the open rows so the label x matches */
        /* Reserve the icon gutter every state so the label x stays put; draw the icon only for iconed rows. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(gut), CLAY_SIZING_FIXED(gut)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            if (sel >= 0 && fruit_iconed[sel]) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) { nt_ui_image(ctx, NT_UI_DATA_LAYER(LAYER_IMG), &s_icon_bunny_ref, &g_panel_img_style, NULL); }
            }
        }
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), (sel >= 0) ? fruits[sel] : "Pick a fruit", g_current->body);
    }
    if (nt_ui_combo_preview_end(ctx)) {
        for (int i = 0; i < fruit_count; ++i) {
            /* Iconed + text-only rows go through the SAME engine column (one icon gutter + left label), so
             * every label aligns at the same x. Iconed rows draw the icon in the gutter; the rest leave it
             * empty (OS-menu icon-column behavior). */
            const bool clicked = fruit_iconed[i] ? nt_ui_combo_selectable_icon(ctx, (uint32_t)i, &s_icon_bunny_ref, fruits[i], i == st->dropdown.fruit_sel)
                                                 : nt_ui_combo_selectable(ctx, (uint32_t)i, fruits[i], i == st->dropdown.fruit_sel);
            if (clicked) {
                st->dropdown.fruit_sel = i; /* game writes *selected */
            }
        }
        nt_ui_combo_end(ctx);
    }

    /* Long list: more than max_visible_rows -> the body wraps in a scroll container; opening near the window
     * bottom flips the list ABOVE the trigger. Plain selectables (no icon gutter). */
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "City (long scrolling list)", g_current->caption);
    const char *city_preview = (st->dropdown.city_sel >= 0) ? cities[st->dropdown.city_sel] : "Pick a city";
    if (nt_ui_combo_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_dd_city, city_preview, g_current->dropdown, &st->dropdown.city_open)) {
        for (int i = 0; i < city_count; ++i) {
            if (nt_ui_combo_selectable(ctx, (uint32_t)i, cities[i], i == st->dropdown.city_sel)) {
                st->dropdown.city_sel = i;
            }
        }
        nt_ui_combo_end(ctx);
    }

    /* Custom-trigger combo: a swatch + label preview instead of the plain string trigger. The
     * trigger content lives between preview_begin/end; the list body is plain selectables. */
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Color (custom swatch trigger)", g_current->caption);
    static const char *const colors[] = {"Crimson", "Emerald", "Azure", "Amber"};
    static const Clay_Color swatches[] = {{208, 48, 48, 255}, {48, 176, 80, 255}, {48, 128, 208, 255}, {224, 192, 48, 255}}; /* RGBA */
    static const int color_count = (int)(sizeof colors / sizeof colors[0]);
    nt_ui_combo_preview_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_dd_color, g_current->dropdown, &st->dropdown.color_open);
    {
        const int sel = st->dropdown.color_sel;
        const Clay_Color sw = (sel >= 0 && sel < color_count) ? swatches[sel] : (Clay_Color){128, 128, 128, 255};
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16)}}, .backgroundColor = sw, .cornerRadius = CLAY_CORNER_RADIUS(3)}) {}
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), (sel >= 0 && sel < color_count) ? colors[sel] : "Pick a color", g_current->body);
    }
    if (nt_ui_combo_preview_end(ctx)) {
        for (int i = 0; i < color_count; ++i) {
            if (nt_ui_combo_selectable(ctx, (uint32_t)i, colors[i], i == st->dropdown.color_sel)) {
                st->dropdown.color_sel = i;
            }
        }
        nt_ui_combo_end(ctx);
    }

    (void)snprintf(buf, sizeof buf, "fruit: %s", (st->dropdown.fruit_sel >= 0) ? fruits[st->dropdown.fruit_sel] : "(none)");
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->body);
    (void)snprintf(buf, sizeof buf, "city: %s", (st->dropdown.city_sel >= 0) ? cities[st->dropdown.city_sel] : "(none)");
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->body);
    (void)snprintf(buf, sizeof buf, "color: %s", (st->dropdown.color_sel >= 0) ? colors[st->dropdown.color_sel] : "(none)");
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->body);
}

/* Tooltip tab: three hover targets, each with a timed reveal. The engine owns the hover-delay timer
 * behind a salted id; the tooltip declares NO catcher so it never blocks the base UI. */
static void render_tooltip(nt_ui_context_t *ctx, tab_state_t *st) {
    (void)st;
    static const Clay_ElementDeclaration target_decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(220), CLAY_SIZING_FIXED(56)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
        .cornerRadius = CLAY_CORNER_RADIUS(8)};

    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Hover a target and wait ~0.5s for its tooltip; move away to hide. The tooltip never blocks clicks underneath.", g_current->caption);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT),
                "The panel is a slice9 frame with a caret pointing at the target + a drop-shadow; near the bottom border the caret flips to the panel's bottom edge.", g_current->caption);

    /* The targets are plain buttons so they are still clickable (proving the tooltip has no catcher).
     * The tooltip is declared AFTER each target so the target carries a queryable bbox this frame. */
    nt_ui_button_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_tip_a, g_current->btn_primary, &target_decl, true, NULL);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Hover me", g_current->body);
    (void)nt_ui_button_end(ctx);
    (void)nt_ui_tooltip(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_tip_a, "A simple tooltip revealed after the hover delay.", g_current->tooltip);

    nt_ui_button_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_tip_b, g_current->btn_secondary, &target_decl, true, NULL);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "And me", g_current->body);
    (void)nt_ui_button_end(ctx);
    (void)nt_ui_tooltip(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_tip_b, "Tooltips wrap to the style max_width so long content stays readable on one panel.", g_current->tooltip);

    /* Bottom-edge target: a tall block with a GROW spacer pushes this target to the bottom of the visible
     * content area, so its tooltip has no room below and visibly flips ABOVE the target (edge-flip demo). */
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(360)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_BOTTOM}}}) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}}) {} /* GROW spacer: pin the target to the bottom */
        nt_ui_button_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_tip_c, g_current->btn_primary, &target_decl, true, NULL);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Bottom edge", g_current->body);
        (void)nt_ui_button_end(ctx);
    }
    (void)nt_ui_tooltip(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_tip_c, "Near the bottom border the tooltip flips ABOVE the target instead of below.", g_current->tooltip);
}

/* Stable sibling keys for the immediate menu rows (unique among siblings; the scope stack disambiguates
 * depth so a submenu may reuse a value). */
enum {
    MK_NEW = 1,
    MK_RECENT,
    MK_PREFS,
    MK_TOGGLE_GRID,
    MK_QUIT,
    MK_RECENT_A,
    MK_RECENT_B,
    MK_RECENT_CLEAR,
    MK_EDIT,
    MK_DUP,
    MK_MOVE,
    MK_DELETE,
    MK_CUSTOM_OPACITY,
    MK_MOVE_FRONT,
    MK_MOVE_BACK,
    MK_MOVE_GROUP,
};

/* GLOBAL menu (app actions): a rich New row (icon + Ctrl+N shortcut), an "Open recent" submenu, a
 * separator, a checkmark toggle, a disabled-via-custom row, and Quit. Built in CODE every frame. */
static void render_menu_global(nt_ui_context_t *ctx, tab_state_t *st) {
    nt_ui_menu_ctx_t *menu = &st->menu.global_menu;
    nt_ui_menu_begin(menu, ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_menu_global, &st->menu.global_state, g_current->menu);
    nt_ui_menu_item_opts_t newo = {0};
    newo.icon = s_icon_bunny_ref;
    newo.shortcut = "Ctrl+N";
    if (nt_ui_menu_item_ex(menu, MK_NEW, "New", newo)) {
        st->menu.last_chosen = "New";
    }
    if (nt_ui_menu_submenu_begin(menu, MK_RECENT, "Open recent")) {
        if (nt_ui_menu_item(menu, MK_RECENT_A, "level_01.ntpack")) {
            st->menu.last_chosen = "Open recent > level_01.ntpack";
        }
        if (nt_ui_menu_item(menu, MK_RECENT_B, "ui_showcase.ntpack")) {
            st->menu.last_chosen = "Open recent > ui_showcase.ntpack";
        }
        nt_ui_menu_item_opts_t clr = {.disabled = true}; /* greyed, not selectable */
        (void)nt_ui_menu_item_ex(menu, MK_RECENT_CLEAR, "(clear list)", clr);
        nt_ui_menu_submenu_end(menu);
    }
    nt_ui_menu_separator(menu);
    /* Checkmark toggle row: `selected` draws the checkmark; clicking flips the game-owned bool. */
    nt_ui_menu_item_opts_t grid = {.selected = st->menu.show_grid};
    if (nt_ui_menu_item_ex(menu, MK_TOGGLE_GRID, "Show grid", grid)) {
        st->menu.show_grid = !st->menu.show_grid;
        st->menu.last_chosen = "Show grid";
    }
    if (nt_ui_menu_item(menu, MK_PREFS, "Preferences")) {
        st->menu.last_chosen = "Preferences";
    }
    if (nt_ui_menu_item(menu, MK_QUIT, "Quit")) {
        st->menu.last_chosen = "Quit";
    }
    nt_ui_menu_end(menu);
}

/* ZONE menu (panel actions): distinct rows + a "Move to" submenu so the bound menu is unmistakable, plus a
 * custom non_activatable row whose inner control owns the click while the row only highlights. */
static void render_menu_zone(nt_ui_context_t *ctx, tab_state_t *st) {
    nt_ui_menu_ctx_t *menu = &st->menu.zone_menu;
    nt_ui_menu_begin(menu, ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_menu_zone, &st->menu.zone_state, g_current->menu);
    nt_ui_menu_item_opts_t edit = {0};
    edit.icon = s_icon_bunny_ref;
    if (nt_ui_menu_item_ex(menu, MK_EDIT, "Edit panel", edit)) {
        st->menu.last_chosen = "Edit panel";
    }
    if (nt_ui_menu_item(menu, MK_DUP, "Duplicate")) {
        st->menu.last_chosen = "Duplicate";
    }
    nt_ui_menu_separator(menu);
    nt_ui_menu_item_opts_t move = {0};
    move.icon = s_icon_bunny_ref;
    if (nt_ui_menu_submenu_begin_ex(menu, MK_MOVE, "Move to", move)) {
        if (nt_ui_menu_item(menu, MK_MOVE_FRONT, "Front")) {
            st->menu.last_chosen = "Move to > Front";
        }
        if (nt_ui_menu_item(menu, MK_MOVE_BACK, "Back")) {
            st->menu.last_chosen = "Move to > Back";
        }
        nt_ui_menu_item_opts_t grp = {.disabled = true};
        (void)nt_ui_menu_item_ex(menu, MK_MOVE_GROUP, "Group", grp);
        nt_ui_menu_submenu_end(menu);
    }
    /* Custom-content row (non_activatable): the inner button owns the click; the row only
     * highlights on hover. The game reads the inner button's own interaction. */
    nt_ui_menu_item_opts_t op = {.non_activatable = true};
    /* Guard the custom body with the return: a CLOSED (present-only) menu returns false and skips the body,
     * so the inner control never leaks onto the scene. item_end is still called unconditionally to balance. */
    if (nt_ui_menu_item_begin(menu, MK_CUSTOM_OPACITY, op)) {
        char opacity_buf[24];
        (void)snprintf(opacity_buf, sizeof opacity_buf, "Opacity %u%%", st->menu.opacity_pct);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), opacity_buf, g_current->body);
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
        nt_ui_button_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_menu_opacity_btn, g_current->btn_secondary, NULL, true, NULL);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "reset", g_current->body);
        if (nt_ui_button_end(ctx)) {
            st->menu.opacity_pct = 100; /* the inner control owns the click; a real state change, not a magic log */
            st->menu.last_chosen = "Opacity reset";
        }
    }
    nt_ui_menu_item_end(menu);
    if (nt_ui_menu_item(menu, MK_DELETE, "Delete")) {
        st->menu.last_chosen = "Delete";
    }
    nt_ui_menu_end(menu);
}

/* Menu tab: a right-click / long-press context menu with a nested submenu, exercising the mouse-aim
 * hover-intent, per-level edge-flip, nested dismiss, and keyboard nav. The immediate API builds the tree in
 * CODE every frame (no static items[]); the game owns the open flag; activation is reported inline. The
 * nt_ui_menu_open_trigger arms the menu at the cursor on a right-click / long-press. */
static void render_menu(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];

    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT),
                "Right-click the panel -> ZONE menu (Edit panel / Duplicate / Move to...); right-click anywhere else -> GLOBAL menu (New / Open recent... / Quit). Click outside / Esc closes.",
                g_current->caption);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Opening one closes the other. Hover the submenu row to fly it out; arrows/Enter navigate.", g_current->caption);

    /* ZONE menu: a visible panel the menu binds to, so a right-click / long-press arms it ONLY over this
     * panel. The game owns the panel's single canonical events step (long-press gesture for touch); the
     * trigger does only idempotent reads and binds the right-click to the panel via a bbox geometry hit-test
     * (so a right-click over the panel RE-opens the menu at the cursor through its own occluder). */
    CLAY({.id = (Clay_ElementId){.id = s_id_menu_panel},
          .layout = {.sizing = {CLAY_SIZING_FIXED(520), CLAY_SIZING_FIXED(220)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = g_current->panel_alt,
          .cornerRadius = CLAY_CORNER_RADIUS(10),
          .border = {.color = g_current->border, .width = {1, 1, 1, 1, 0}}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Zone menu: right-click the panel = (re)open here", g_current->body);
    }
    static const nt_ui_events_cfg_t menu_cfg = {.long_press_secs = 0.5F, .double_click = false};
    const nt_ui_events_t zone_ev = nt_ui_events(ctx, s_id_menu_panel, &menu_cfg);

    /* Mutually exclusive arming: a right-click over the zone panel must arm ONLY the zone menu, anywhere
     * else ONLY the global. Same cursor + bbox snapshot the trigger reads (prev-frame bbox vs this-frame
     * pointer), so over_zone matches the zone trigger's own geometry hit-test. */
    const nt_ui_bbox_t zone_bb = nt_ui_get_bbox(ctx, s_id_menu_panel);
    const bool over_zone = zone_bb.found && zone_ev.pos[0] >= zone_bb.x && zone_ev.pos[0] <= zone_bb.x + zone_bb.width && zone_ev.pos[1] >= zone_bb.y && zone_ev.pos[1] <= zone_bb.y + zone_bb.height;

    const bool zone_armed = nt_ui_menu_open_trigger(ctx, s_id_menu_zone, s_id_menu_panel, zone_ev.long_pressed, &st->menu.zone_state);
    /* Global only when NOT over the zone, so a right-click on the panel never also arms the global. */
    const bool global_armed = !over_zone && nt_ui_menu_open_trigger(ctx, s_id_menu_global, /*target_id=*/0U, /*long_pressed=*/false, &st->menu.global_state);

    /* Opening one forces the other closed (open_trigger returns true only on the arming frame). */
    if (zone_armed) {
        st->menu.global_state.open = false;
    } else if (global_armed) {
        st->menu.zone_state.open = false;
    }

    /* Render both menus (independent state + ids); arming is gated so at most one is open. Each tree is
     * built in CODE; every row reports activation via its inline bool (if (menu_item(...)) ...), writing
     * last_chosen — the SINGLE idiom for both mouse and keyboard (no chosen_id sink). */
    render_menu_global(ctx, st);
    render_menu_zone(ctx, st);

    (void)snprintf(buf, sizeof buf, "last chosen: %s", st->menu.last_chosen ? st->menu.last_chosen : "(none)");
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->body);
}

/* Tabs tab: the nt_ui_tabbar begin/end CORE dogfooded. The engine owns each tab's styled per-state bg
 * (eased) + the BOTTOM accent + the click; the GAME owns the content -- an icon + a label, with a
 * DIFFERENT icon on the selected tab (branch on `on`). Contrast the LEFT nav, which uses the one-call
 * labels[] convenience wrapper. The strip's accent_side (BOTTOM) differs from the nav's (LEFT). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — the tab loop + per-tab content, not real nesting
static void render_tabs(nt_ui_context_t *ctx, tab_state_t *st) {
    static const char *const names[] = {"Home", "Search", "Profile", "Settings"};
    static const int count = (int)(sizeof names / sizeof names[0]);
    nt_ui_tabbar_style_t *style = g_current->tabs_demo;

    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT),
                "Begin/end core: the GAME composes each tab (icon + text); the selected tab swaps in a different icon. The engine owns the bg, the BOTTOM accent, and the click.", g_current->caption);

    /* Wrap the strip so it sizes to content height instead of growing to fill the scroll. */
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(72)}}}) {
        nt_ui_tabbar_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_tabs_demo_base, style);
        for (int i = 0; i < count; ++i) {
            const bool on = (i == st->tabs_demo_active);
            if (nt_ui_tab_begin(ctx, i, on)) {
                st->tabs_demo_active = i;
            }
            /* Game-owned content: the tab keeps its OWN icon always (swapping it to a checkmark loses the
             * tab's identity); the selected tab ADDS a trailing checkmark badge instead. */
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24)}}}) { nt_ui_image(ctx, NT_UI_DATA_LAYER(LAYER_IMG), &s_tabs_icon_idle_ref, &g_panel_img_style, NULL); }
            const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = on ? g_current->row_sel->color : g_current->caption->color};
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), names[i], &lbl);
            if (on) {
                /* Selected badge: a small checkmark trailing the label (added, not replacing the icon). */
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(16), CLAY_SIZING_FIXED(16)}}}) { nt_ui_image(ctx, NT_UI_DATA_LAYER(LAYER_IMG), &s_tabs_icon_sel_ref, &g_panel_img_style, NULL); }
            }
            nt_ui_tab_end(ctx);
        }
        nt_ui_tabbar_end(ctx);
    }

    char buf[64];
    (void)snprintf(buf, sizeof buf, "selected: %s", names[st->tabs_demo_active]);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->body);
}

/* N labels @14pt + a frame gpu_ms readout. No nested ui_text GPU segment: the host frame loop owns
 * the "frame" segment and GL_TIME_ELAPSED queries can't nest. */
static void render_stress(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const nt_ui_label_style_t stress_label = {.font_id = 0, .font_size = 14, .color = {200.0F, 210.0F, 220.0F, 255.0F}};

    const float gpu_ms = nt_debug_overlay_get_gpu_ms();
    if (gpu_ms < 0.0F) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "frame gpu: n/a (timer extension absent)", g_current->caption);
    } else {
        (void)snprintf(buf, sizeof buf, "frame gpu: %.2f ms", (double)gpu_ms);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    }
    (void)snprintf(buf, sizeof buf, "draw calls: %u   labels: %d", nt_ui_get_last_walk_draw_calls(ctx), st->stress.label_count);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);

    /* GROW scroll container: fills the content panel's allocation (min 0) so the 6000-label case
     * can't force the GROW content panel wider and push the FIXED props panel off-screen. */
    nt_ui_scroll_begin(ctx, NULL, s_id_stress_scroll, g_current->scroll_xy,
                       &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(420)}, .padding = CLAY_PADDING_ALL(6)},
                                                  .backgroundColor = g_current->bg,
                                                  .cornerRadius = CLAY_CORNER_RADIUS(8)});
    {
        /* GROW column of LEFT_TO_RIGHT rows: fills the viewport width and grows DOWN, so vertical
         * scroll reaches every label. Both-axes scroll covers any horizontal extent on narrow windows. */
        enum { STRESS_COLS = 10 };
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}}) {
            for (int i = 0; i < st->stress.label_count; i += STRESS_COLS) {
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 6, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
                    for (int c = 0; c < STRESS_COLS && (i + c) < st->stress.label_count; ++c) {
                        (void)snprintf(buf, sizeof buf, "lbl%03d", i + c);
                        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, &stress_label);
                    }
                }
            }
        }
    }
    nt_ui_scroll_end(ctx);
}

/* Segmented selector row: `count` buttons; clicking sets *value to the index. */
static void modal_seg_select(nt_ui_context_t *ctx, const char *title, uint32_t base_id, const char *const *names, int count, int *value, int btn_w) {
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), title, g_current->caption);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8}}) {
        for (int i = 0; i < count; ++i) {
            const bool sel = (*value == i);
            nt_ui_button_begin(
                ctx, NT_UI_DATA_LAYER(LAYER_IMG), base_id + (uint32_t)i, sel ? g_current->btn_primary : g_current->btn_secondary,
                &(Clay_ElementDeclaration){
                    .layout = {.sizing = {CLAY_SIZING_FIXED((float)btn_w), CLAY_SIZING_FIXED(36)}, .padding = CLAY_PADDING_ALL(4), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                    .cornerRadius = CLAY_CORNER_RADIUS(8)},
                true, NULL);
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), names[i], &g_seg_label);
            if (nt_ui_button_end(ctx)) {
                *value = i;
            }
        }
    }
}

/* Modal panel: independent Open/Close animation selectors + shared sliders. Slide-only (edge + distance)
 * and scale-only (scale start) controls show conditionally to stay compact; the props panel scrolls. */
static void props_modal(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const Clay_ElementDeclaration sdecl = {.layout = {.sizing = {CLAY_SIZING_FIXED(290), CLAY_SIZING_FIXED(26)}}};
    static const char *const types[3] = {"Scale-pop", "Fade", "Slide"};
    static const char *const edges[4] = {"Bottom", "Top", "Left", "Right"};
    showcase_panel_begin(ctx, "Modal properties");

    modal_seg_select(ctx, "Open", nt_ui_id("showcase/modal_open_type"), types, 3, &st->modal.open_type, 80);
    if (st->modal.open_type == 2) {
        modal_seg_select(ctx, "Open from", nt_ui_id("showcase/modal_open_edge"), edges, 4, &st->modal.open_edge, 60);
    }
    modal_seg_select(ctx, "Close", nt_ui_id("showcase/modal_close_type"), types, 3, &st->modal.close_type, 80);
    if (st->modal.close_type == 2) {
        modal_seg_select(ctx, "Close to", nt_ui_id("showcase/modal_close_edge"), edges, 4, &st->modal.close_edge, 60);
    }

    if (st->modal.open_type == 2 || st->modal.close_type == 2) {
        (void)snprintf(buf, sizeof buf, "Slide distance  %.0f", (double)st->modal.slide_offset);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
        (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_slide_dist, NULL, &st->modal.slide_offset, 0.0F, 240.0F, 0.0F, g_current->slider_props, &sdecl, true);
    }

    (void)snprintf(buf, sizeof buf, "Ease speed  %.1f", (double)st->modal.ease_speed);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_ease, NULL, &st->modal.ease_speed, 4.0F, 30.0F, 0.0F, g_current->slider_props, &sdecl, true);

    if (st->modal.open_type == 0 || st->modal.close_type == 0) {
        (void)snprintf(buf, sizeof buf, "Scale start  %.2f", (double)st->modal.scale_start);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
        (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_scale, NULL, &st->modal.scale_start, 0.85F, 1.0F, 0.0F, g_current->slider_props, &sdecl, true);
    }

    (void)snprintf(buf, sizeof buf, "Backdrop alpha  %.2f", (double)st->modal.backdrop_alpha);
    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->caption);
    (void)nt_ui_slider_float(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_props_backdrop, NULL, &st->modal.backdrop_alpha, 0.0F, 1.0F, 0.0F, g_current->slider_props, &sdecl, true);

    showcase_panel_end(ctx);
}

/* Stress panel: segmented label count (500/1500/3000/6000) + the live frame gpu_ms / draw-calls readout. */
static void props_stress(nt_ui_context_t *ctx, tab_state_t *st) {
    char buf[64];
    static const int counts[4] = {500, 1500, 3000, 6000};
    showcase_panel_begin(ctx, "Stress properties");

    nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Label count", g_current->caption);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 8}}) {
        for (int i = 0; i < 4; ++i) {
            const bool sel = (st->stress.label_count == counts[i]);
            (void)snprintf(buf, sizeof buf, "%d", counts[i]);
            nt_ui_button_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), nt_ui_id("showcase/stress_n") + (uint32_t)i, sel ? g_current->btn_primary : g_current->btn_secondary,
                               &(Clay_ElementDeclaration){
                                   .layout = {.sizing = {CLAY_SIZING_FIXED(60), CLAY_SIZING_FIXED(40)}, .padding = CLAY_PADDING_ALL(4), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                                   .cornerRadius = CLAY_CORNER_RADIUS(8)},
                               true, NULL);
            nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, &g_seg_label);
            if (nt_ui_button_end(ctx)) {
                st->stress.label_count = counts[i];
            }
        }
    }

    const float gpu_ms = nt_debug_overlay_get_gpu_ms();
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
    s_id_progress_crop = nt_ui_id("showcase/progress_crop");
    s_id_progress_vert = nt_ui_id("showcase/progress_vert");
    s_id_scroll_hide = nt_ui_id("showcase/scroll_hide");
    s_id_scroll_always = nt_ui_id("showcase/scroll_always");
    s_id_scroll_horiz = nt_ui_id("showcase/scroll_horiz");
    s_id_scroll_xy = nt_ui_id("showcase/scroll_xy");
    s_id_stress_scroll = nt_ui_id("showcase/stress_scroll");
    s_id_stage_scroll = nt_ui_id("showcase/stage_scroll");
    s_id_props_scroll = nt_ui_id("showcase/props_scroll");
    s_id_props_il = nt_ui_id("showcase/props_il");
    s_id_props_ir = nt_ui_id("showcase/props_ir");
    s_id_props_it = nt_ui_id("showcase/props_it");
    s_id_props_ib = nt_ui_id("showcase/props_ib");
    s_id_props_w = nt_ui_id("showcase/props_w");
    s_id_props_h = nt_ui_id("showcase/props_h");
    s_id_props_s9scale = nt_ui_id("showcase/props_s9scale");
    s_id_props_value = nt_ui_id("showcase/props_value");
    s_id_props_rot = nt_ui_id("showcase/props_rot");
    s_id_props_bscale = nt_ui_id("showcase/props_bscale");
    s_id_props_offx = nt_ui_id("showcase/props_offx");
    s_id_props_offy = nt_ui_id("showcase/props_offy");
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
    s_id_props_slide_dist = nt_ui_id("showcase/props_slide_dist");
    s_id_theme_btn = nt_ui_id("showcase/theme_btn");
    s_id_input_plain = nt_ui_id("showcase/input_plain");
    s_id_input_numeric = nt_ui_id("showcase/input_numeric");
    s_id_input_password = nt_ui_id("showcase/input_password");
    s_id_input_cyrillic = nt_ui_id("showcase/input_cyrillic");
    s_id_input_caret = nt_ui_id("showcase/input_caret");
    s_id_input_sel = nt_ui_id("showcase/input_sel");
    s_id_input_art = nt_ui_id("showcase/input_art");
    s_id_input_disabled = nt_ui_id("showcase/input_disabled");
    s_id_tab_btn_base = nt_ui_id("showcase/tab_btn");
    s_id_tabs_demo_base = nt_ui_id("showcase/tabs_demo");
    s_id_events_hold = nt_ui_id("showcase/events_hold");
    s_id_events_dbl = nt_ui_id("showcase/events_dbl");
    s_id_events_fill = nt_ui_id("showcase/events_fill");
    s_id_dd_fruit = nt_ui_id("showcase/dd_fruit");
    s_id_dd_city = nt_ui_id("showcase/dd_city");
    s_id_dd_color = nt_ui_id("showcase/dd_color");
    s_id_tip_a = nt_ui_id("showcase/tip_a");
    s_id_tip_b = nt_ui_id("showcase/tip_b");
    s_id_tip_c = nt_ui_id("showcase/tip_c");
    s_id_menu_global = nt_ui_id("showcase/menu_global");
    s_id_menu_zone = nt_ui_id("showcase/menu_zone");
    s_id_menu_panel = nt_ui_id("showcase/menu_panel");
    s_id_menu_opacity_btn = nt_ui_id("showcase/menu_opacity_btn");
    s_ids_ready = true;
}

/* Header: theme toggle + a live ui_draw_calls / frame gpu_ms readout (the draw-call count is the
 * batching evidence; the engine has no sort-by-material toggle). */
static void declare_header(nt_ui_context_t *ctx) {
    char buf[96];
    /* Title | theme button | live readout | (GROW spacer) | dim keyboard hints, with a 1px
     * bottom separator under the whole bar. */
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {.bottom = 10}, .childGap = 16, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
          .border = {.color = g_current->border, .width = {.bottom = 1}}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Neotolis UI Showcase", g_current->title);

        nt_ui_button_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_theme_btn, g_current->btn_primary,
                           &(Clay_ElementDeclaration){
                               .layout = {.sizing = {CLAY_SIZING_FIXED(150), CLAY_SIZING_FIXED(40)}, .padding = CLAY_PADDING_ALL(4), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                               .cornerRadius = CLAY_CORNER_RADIUS(8)},
                           true, NULL);
        (void)snprintf(buf, sizeof buf, "Theme: %s", g_current->name);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->body);
        if (nt_ui_button_end(ctx)) {
            g_current = (g_current == &g_dark) ? &g_light : &g_dark;
        }

        const float gpu_ms = nt_debug_overlay_get_gpu_ms();
        if (gpu_ms < 0.0F) {
            (void)snprintf(buf, sizeof buf, "draw calls: %u   gpu: n/a", nt_ui_get_last_walk_draw_calls(ctx));
        } else {
            (void)snprintf(buf, sizeof buf, "draw calls: %u   gpu: %.2f ms", nt_ui_get_last_walk_draw_calls(ctx), (double)gpu_ms);
        }
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, g_current->body);

        /* Spacer pushes the keyboard hints to the far right, dimmer than the live readout. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "[T] palette  [D] inspector  [Esc] unfocus/quit", g_current->caption);
    }
}

/* The tab labels passed to nt_ui_tabbar: the registry names, gathered once (the registry is static). */
static const char *s_tab_labels[TAB_COUNT];
static bool s_tab_labels_ready;
static void ensure_tab_labels(void) {
    if (s_tab_labels_ready) {
        return;
    }
    for (int i = 0; i < TAB_COUNT; ++i) {
        s_tab_labels[i] = g_tabs[i].name;
    }
    s_tab_labels_ready = true;
}

/* Demo-selection panel (left, FIXED 240): the reusable nt_ui_tabbar dogfooded. The game owns
 * s_active_tab (Model D); the widget draws the accent bar + selected fill + hover lighten and writes the
 * active index on click. Styled as a card (bg + border + radius) consistent with the other panels. */
static void declare_tab_list(nt_ui_context_t *ctx) {
    ensure_tab_labels();
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(240), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(10),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 6,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = g_current->list_bg,
          .cornerRadius = CLAY_CORNER_RADIUS(10),
          .border = {.color = g_current->border, .width = {1, 1, 1, 1, 0}}}) {
        /* Text-only nav (icons NULL): the left rail stays clean/cohesive. icon_size stays 0 so no gutter. */
        (void)nt_ui_tabbar(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_tab_btn_base, s_tab_labels, NULL, TAB_COUNT, &s_active_tab, g_current->tabbar);
    }
}

/* Content panel (middle, GROW): tab title/info/source-link, then the tab content in a vertical scroll
 * so tall tabs (Slice9/Stress) fit. Props live in a separate sibling card; the modal is declared at
 * ROOT in frame(), not here, so its panel escapes this card's scissor. */
static void declare_content(nt_ui_context_t *ctx) {
    NT_ASSERT(s_active_tab >= 0 && s_active_tab < TAB_COUNT && "active tab out of range");
    const showcase_entry_t *e = &g_tabs[s_active_tab];

    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 10,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = g_current->panel,
          .cornerRadius = CLAY_CORNER_RADIUS(10),
          .border = {.color = g_current->border, .width = {1, 1, 1, 1, 0}}}) {
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), e->name, g_current->h1);
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), e->info, g_current->caption);
        /* Source reference: a dim "link" style distinct from body/caption copy. */
        nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), e->code_url, g_current->link);

        /* Vertical scroll bounds the content to the card height; the inner column FITs (grows taller
         * than the viewport on tall tabs -> scrolls). */
        nt_ui_scroll_begin(ctx, NULL, s_id_stage_scroll, g_current->scroll_hide, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}});
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 10}}) { e->render(ctx, &s_state); }
        nt_ui_scroll_end(ctx);
    }
}

/* Properties panel (right, FIXED 340): a distinct panel_alt card always present so the layout doesn't
 * jump between tabs. Tabs with a props_fn populate it directly (the card IS this panel -- no nested
 * card); tabs without show a muted placeholder. */
static void declare_props_panel(nt_ui_context_t *ctx) {
    const showcase_entry_t *e = &g_tabs[s_active_tab];

    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(340), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(14),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 8,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = g_current->panel_alt,
          .cornerRadius = CLAY_CORNER_RADIUS(10),
          .border = {.color = g_current->border, .width = {1, 1, 1, 1, 0}}}) {
        /* Vertical scroll so a tall props set (e.g. modal Open+Close = Slide) never clips on short windows. */
        nt_ui_scroll_begin(ctx, NULL, s_id_props_scroll, g_current->scroll_hide, &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}});
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8}}) {
            if (e->props_fn != NULL) {
                e->props_fn(ctx, &s_state);
            } else {
                nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Properties", g_current->body);
                nt_ui_label(ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "No properties for this tab.", g_current->caption);
            }
        }
        nt_ui_scroll_end(ctx);
    }
}
// #endregion

// #region frame
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void frame(void) {
    nt_debug_overlay_frame_begin();
    nt_window_poll();
    nt_input_poll();
    nt_mem_scratch_reset();

    /* Gameplay/global hotkeys yield to an open modal so Esc closes the top modal first and the
     * palette/inspector keys don't fire underneath it. nt_ui_modal_active reports last frame's
     * presence (polled here before this frame's nt_ui_begin) -- the inherent IM 1-frame gate. */
    const bool modal_was_active = nt_ui_modal_active(s_ctx);

#ifndef NT_PLATFORM_WEB
    /* Esc unfocuses a focused field first (the field consumes it in its own pass); only quit when
     * no modal is up AND no field holds focus. focused_input_id is last frame's (read pre-begin). */
    if (!modal_was_active && !nt_ui_input_any_focused(s_ctx) && nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        nt_app_quit();
    }
#endif

    /* Single-key [T]/[D] hotkeys yield while a field holds focus so typing 'd'/'t' edits the field
     * instead of toggling the inspector/palette. any_focused reads last frame's focus, same as the
     * modal gate above (both polled here before this frame's nt_ui_begin). */
    if (!modal_was_active && !nt_ui_input_any_focused(s_ctx)) {
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
            /* Three sibling panels: demo selection (left) | content (middle, GROW) | properties (right). */
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 12, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
                declare_tab_list(s_ctx);
                declare_content(s_ctx);
                declare_props_panel(s_ctx);
            }
        }

        /* Modal declared at ROOT (after the scene, before nt_ui_end): no scissor ancestor, so the
         * floating panel renders unclipped. Self-gates on confirm_open; only the Modals tab drives it. */
        if (g_tabs[s_active_tab].render == render_modals) {
            render_modal_overlay(s_ctx, &s_state);
        }

        nt_ui_end(s_ctx);

        nt_ui_target_t target = nt_ui_scale_make_target(&scale);
        nt_ui_walk(s_ctx, &target);

        nt_ui_inspector_overlay_draw(s_ctx, &target, s_font, 16.0F);

        {
            /* Bottom-RIGHT overlay: nt_debug_overlay_draw emits 4 lines (FPS/CPU/GPU/Draws) descending in
             * y-up text space. Anchor near the right edge (reserve ~170px for the widest line) and
             * ~92px up so the last line stays on screen — keeps the HUD clear of the left nav tabs. */
            mat4 stats_model;
            glm_mat4_identity(stats_model);
            glm_translate(stats_model, (vec3){scale.logical_w - 170.0F, 92.0F, 0.0F});
            const float stats_color[4] = {0.8F, 0.9F, 0.8F, 1.0F};
            nt_debug_overlay_draw(s_text_material, s_font, (const float *)stats_model, 16.0F, stats_color);
            nt_text_renderer_flush();
        }
    }

    nt_gfx_end_pass();
    nt_gfx_end_segment();
    nt_gfx_end_frame();
    nt_debug_overlay_frame_end();

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

    /* Startup build provenance: confirms which build (type + preset) is actually running -- handy for
     * telling a freshly-built artifact apart from a stale/cached one. */
    nt_log_info("ui_showcase: %s build (%s)", nt_engine_build_string(), nt_engine_preset_string());

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
    nt_ui_create_desc_t ui_desc = nt_ui_create_desc_defaults();
    ui_desc.max_elements = UI_MAX_ELEMENTS; /* Stress tab worst case (6000 labels + rows). */
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

    nt_debug_overlay_desc_t stats_desc = nt_debug_overlay_desc_defaults();
    nt_debug_overlay_init(&stats_desc);

#ifdef NT_PLATFORM_WEB
    nt_platform_web_loading_complete();
#endif

    nt_log_info("ui_showcase: starting (T=palette, D=inspector, Esc unfocus/quit)");

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
    nt_debug_overlay_shutdown();
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
