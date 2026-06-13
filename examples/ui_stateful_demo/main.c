/* UI Stateful Demo — worked example for checkbox/toggle/radio plus slider/progress/scroll.
 * Value lives in the GAME (Model D): the engine stores no logical value, only the transient
 * eased visual + scroll physics state.
 * Keys: Esc quit (native), D toggle inspector.
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
#include "ui/nt_ui_button.h"
#include "ui/nt_ui_checkbox.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_progress.h"
#include "ui/nt_ui_scale.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_slider.h"
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
/* Box/check atlas refs are filled upfront from these templates (init_widget_styles);
 * the engine resolves the region lazily. Every cell sets scale=1 / opacity=1 to satisfy
 * assert_cell_valid; pressed dips scale, disabled dips opacity. */

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

/* ---- slider: recessed slice9 track + green slice9 fill + circular thumb ----
 * Track + fill MUST be the matched bar pair (same 6px corner radius / 8px slice9) so the
 * rounded fill caps nest inside the track groove; a radius mismatch makes the fill poke past
 * the rounded ends at low/max ratios. Hover/pressed tint the thumb. */
static const nt_ui_slider_style_t g_slider_tmpl = {
    .track_w = 320,
    .track_h = 18,
    .thumb_w = 28,
    .thumb_h = 28,
    .fill_mode = NT_UI_FILL_STRETCH,
    .fill_direction = NT_UI_FILL_LTR,
    .state_speed = 16.0F,
    .value_speed = 18.0F,               /* eased on GAME-driven preset change; 1:1 during drag */
    .hit_padding_lrtb = {0, 0, 13, 13}, /* 18px track + 13*2 pad = 44px touch target (mobile reference) */
    .states =
        {
            [NT_UI_SLIDER_IDLE] = {.track_tint = 0xFFFFFFFF, .fill_tint = 0xFFFFFFFF, .thumb_tint = 0xFFFFFFFF, .opacity = 1.0F},
            [NT_UI_SLIDER_HOVER] = {.track_tint = 0xFFFFFFFF, .fill_tint = 0xFFFFFFFF, .thumb_tint = 0xFFE8F4FF, .opacity = 1.0F},
            [NT_UI_SLIDER_PRESSED] = {.track_tint = 0xFFFFFFFF, .fill_tint = 0xFFFFFFFF, .thumb_tint = 0xFFCFE8FF, .opacity = 1.0F},
            [NT_UI_SLIDER_DISABLED] = {.track_tint = 0xFFFFFFFF, .fill_tint = 0xFFFFFFFF, .thumb_tint = 0xFFFFFFFF, .opacity = 0.40F},
        },
};

/* ---- progress bar A (horizontal STRETCH slice9): recessed track + smooth gradient fill that
 * slice9-stretches cleanly. Side-by-side with bar B this shows STRETCH (no distortion) art. ---- */
static const nt_ui_progress_style_t g_progress_stretch_tmpl = {
    .track_tint = 0xFFFFFFFF,
    .fill_tint = 0xFFFFFFFF,
    .track_w = 320,
    .track_h = 24,
    .fill_mode = NT_UI_FILL_STRETCH,
    .fill_direction = NT_UI_FILL_LTR,
    .value_speed = 6.0F, /* smooth ramp toward the target */
    .opacity = 1.0F,
};

/* ---- progress bar B (horizontal CROP clip): recessed track + the shaped diagonal-stripe fill
 * revealed (not stretched) by the clip scissor — the stripes stay crisp, proving CROP. ---- */
static const nt_ui_progress_style_t g_progress_crop_tmpl = {
    .track_tint = 0xFFFFFFFF,
    .fill_tint = 0xFFFFFFFF,
    .track_w = 320,
    .track_h = 24,
    .fill_mode = NT_UI_FILL_CROP,
    .fill_direction = NT_UI_FILL_LTR,
    .value_speed = 6.0F,
    .opacity = 1.0F,
};

/* ---- mana bar (vertical STRETCH slice9, BOTTOM_UP): the smooth gradient fill stretches
 * cleanly and is tinted BLUE; it fills bottom-up (non-LTR direction). ---- */
static const nt_ui_progress_style_t g_mana_tmpl = {
    .track_tint = 0xFFFFFFFF,
    .fill_tint = 0xFF4090F0, /* blue mana */
    .track_w = 26,
    .track_h = 96,
    .fill_mode = NT_UI_FILL_STRETCH,
    .fill_direction = NT_UI_FILL_BOTTOM_UP,
    .value_speed = 5.0F,
    .opacity = 1.0F,
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

/* Headless layout probe (--probe-layout): after a settled frame, check every bottom-most
 * interactive widget fits inside the viewport, log PASS/FAIL, then quit. Result rides
 * s_probe_exit_code so a headless harness can read it via the process exit code. */
static bool s_probe_layout;
static int s_probe_settle_frames;
static int s_probe_exit_code;

/* Runtime widget styles: const templates copied + late-bound atlas refs filled upfront
 * (init_widget_styles); mutable so the engine's resolve memoizes the index in place. */
static nt_ui_checkbox_style_t s_check;
static nt_ui_checkbox_style_t s_switch;
static nt_ui_checkbox_style_t s_radio;
static nt_ui_slider_style_t s_slider;
static nt_ui_progress_style_t s_progress_stretch;
static nt_ui_progress_style_t s_progress_crop;
static nt_ui_progress_style_t s_mana;
static nt_ui_scroll_style_t s_scroll_hide; /* AUTO_HIDE bar */
static nt_ui_scroll_style_t s_scroll_always;
static nt_ui_scroll_style_t s_scroll_horiz; /* horizontal-only (scroll_x) */
static nt_ui_scroll_style_t s_scroll_xy;    /* both axes (scroll_x + scroll_y) */
static nt_ui_button_style_t s_button;

/* Game-owned values (Model D — state lives in the game, not the engine). */
static bool s_vsync = true;
static bool s_dark;
static int s_quality = 1;    /* 0 low / 1 med / 2 high */
static bool s_cell_selected; /* indicator-only table-cell checkbox */
static bool s_locked = true; /* a permanently-checked DISABLED checkbox */

/* Slider / progress game-owned values (Model D). */
static float s_volume = 0.65F; /* float slider 0..1 */
static int s_count = 4;        /* int slider 0..10 */
static float s_progress_val;   /* horizontal load bar: ramps 0->1->0 on a timer */
static bool s_progress_up = true;
static float s_mana_val = 0.5F; /* vertical mana bar: auto-ramps 0->1->0, phase-shifted from the load bar */
static bool s_mana_up = false;  /* starts heading DOWN so it runs out of sync with the load bar */

/* Physics-tuning controls exposed by the demo. */
static float s_friction = 0.92F;
static float s_wheel_ease = 18.0F;
static float s_wheel_step = 40.0F; /* px per wheel notch; engine default 40 (user tunes by feel) */

/* Stable ids for the new widgets. */
static uint32_t s_id_volume;
static uint32_t s_id_count;
static uint32_t s_id_friction;
static uint32_t s_id_wheel_ease;
static uint32_t s_id_wheel_step;
static uint32_t s_id_scroll_hide;
static uint32_t s_id_scroll_always;
static uint32_t s_id_scroll_horiz; /* horizontal-only scroll list */
static uint32_t s_id_scroll_xy;    /* both-axes scroll grid */
static uint32_t s_id_scroll_to_btn;
static uint32_t s_id_inner_btn_hide;
static uint32_t s_id_inner_btn_always;
static uint32_t s_inner_btn_hits;

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
/* Fill the idle cell's late-bound refs; non-idle cells inherit the whole ref (nt_ui_ref_or).
 * The handle is valid before the atlas data loads — the widget resolves + memoizes lazily. */
static void init_widget_styles(void) {
    /* Checkbox: box on both rows; only checked carries the check overlay. */
    s_check = g_check_tmpl;
    s_check.unchecked[NT_UI_CB_IDLE].box = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BOX_OFF.value);
    s_check.checked[NT_UI_CB_IDLE].box = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BOX_OFF.value);
    s_check.checked[NT_UI_CB_IDLE].check = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_CHECKMARK.value);

    /* Radio: ring on both rows; checked adds the dot. */
    s_radio = g_radio_tmpl;
    s_radio.unchecked[NT_UI_CB_IDLE].box = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_RADIO_RING.value);
    s_radio.checked[NT_UI_CB_IDLE].box = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_RADIO_RING.value);
    s_radio.checked[NT_UI_CB_IDLE].check = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_RADIO_DOT.value);

    /* Toggle: track recolors by value (off grey / on green); thumb on both. */
    s_switch = g_switch_tmpl;
    s_switch.unchecked[NT_UI_CB_IDLE].box = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_TRACK_OFF.value);
    s_switch.unchecked[NT_UI_CB_IDLE].check = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_THUMB.value);
    s_switch.checked[NT_UI_CB_IDLE].box = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_TRACK_ON.value);
    s_switch.checked[NT_UI_CB_IDLE].check = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_THUMB.value);

    /* Slider: bar_track groove + matched bar_fill_smooth fill (same 6px radius / 8px slice9 so
     * the rounded fill caps nest inside the track instead of poking past the ends) + thumb. */
    s_slider = g_slider_tmpl;
    s_slider.states[NT_UI_SLIDER_IDLE].track = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BAR_TRACK.value);
    s_slider.states[NT_UI_SLIDER_IDLE].fill = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BAR_FILL_SMOOTH.value);
    s_slider.states[NT_UI_SLIDER_IDLE].thumb = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_THUMB.value);

    /* Progress bar A (STRETCH slice9): recessed bar track + smooth slice9 fill. */
    s_progress_stretch = g_progress_stretch_tmpl;
    s_progress_stretch.track = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BAR_TRACK.value);
    s_progress_stretch.fill = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BAR_FILL_SMOOTH.value);

    /* Progress bar B (CROP clip): same recessed track + shaped diagonal-stripe fill (no slice9). */
    s_progress_crop = g_progress_crop_tmpl;
    s_progress_crop.track = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BAR_TRACK.value);
    s_progress_crop.fill = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BAR_FILL_SHAPED.value);

    /* Mana (vertical STRETCH slice9, BOTTOM_UP): recessed track + smooth slice9 fill, blue-tinted. */
    s_mana = g_mana_tmpl;
    s_mana.track = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BAR_TRACK.value);
    s_mana.fill = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BAR_FILL_SMOOTH.value);

    /* Scroll: two styles differing only in scrollbar visibility (AUTO_HIDE vs ALWAYS). Track is the
     * recessed scroll slot; thumb is the light capsule pill (8px slice9, reads right at any length). */
    s_scroll_hide = nt_ui_scroll_style_defaults();
    s_scroll_hide.bar_visibility = NT_UI_SCROLLBAR_AUTO_HIDE;
    s_scroll_hide.bar_thickness = 12.0F;
    s_scroll_hide.bar_thumb_min_px = 28.0F;
    s_scroll_hide.bar_fade_speed = 8.0F;
    s_scroll_hide.track_tint = 0xC0FFFFFF; /* slight alpha so the recessed slot floats over content */
    s_scroll_hide.thumb_tint = 0xFFFFFFFF; /* art is already the light capsule; no recolor needed */
    s_scroll_hide.track_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_SCROLL_TRACK.value);
    s_scroll_hide.thumb_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BAR_THUMB.value);

    s_scroll_always = s_scroll_hide;
    s_scroll_always.bar_visibility = NT_UI_SCROLLBAR_ALWAYS;
    s_scroll_always.track_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_SCROLL_TRACK.value);
    s_scroll_always.thumb_ref = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_BAR_THUMB.value);

    /* Horizontal-only: scroll_x on, scroll_y off; ALWAYS bar on the bottom edge. */
    s_scroll_horiz = s_scroll_always;
    s_scroll_horiz.scroll_x = true;
    s_scroll_horiz.scroll_y = false;

    /* Both axes: content wider AND taller than the container; ALWAYS bars on both edges. */
    s_scroll_xy = s_scroll_always;
    s_scroll_xy.scroll_x = true;
    s_scroll_xy.scroll_y = true;

    /* Inner / scroll-to button: grey slice9 pill bg, eased press dip. */
    s_button = (nt_ui_button_style_t){
        .transition_speed = 14.0F,
        .slice9_scale = 1.0F,
        .idle = {.bg_tint = 0xFF4A5266, .scale = 1.0F, .opacity = 1.0F},
        .hover = {.bg_tint = 0xFF5A6480, .scale = 1.0F, .opacity = 1.0F},
        .pressed = {.bg_tint = 0xFF3A4254, .scale = 0.96F, .opacity = 1.0F},
        .disabled = {.bg_tint = 0xFF4A5266, .scale = 1.0F, .opacity = 0.40F},
    };
    s_button.idle.bg = nt_atlas_ref(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS_TRACK_OFF.value);
}

/* White-region + font stay is_ready-gated (white is the deferred path; font needs the loaded resource). */
static void try_bind_resources(void) {
    if (s_atlas_bound && s_font_bound) {
        return;
    }

    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        const uint32_t white = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_STATEFUL_DEMO_ATLAS__WHITE.value);
        NT_ASSERT(white != NT_ATLAS_INVALID_REGION);
        nt_ui_set_atlas_white_region(s_ctx, s_atlas_handle, white);
        s_atlas_bound = true;
        nt_log_info("ui_stateful_demo: atlas white region bound");
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
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {.top = 2, .bottom = 2}}}) { nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), text, &g_section_style); }
}

/* The slider track decl: a FIXED-height row so the floating thumb has a stable parent. */
static const Clay_ElementDeclaration s_slider_decl = {
    .layout = {.sizing = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(28)}},
};
static const Clay_ElementDeclaration s_progress_decl = {
    .layout = {.sizing = {CLAY_SIZING_FIXED(320), CLAY_SIZING_FIXED(24)}},
};
// #endregion

// #region declare_widgets (slider / progress / scroll)
/* Draws a small floating value-bubble at the slider thumb: proves the
 * nt_ui_slider_thumb_pos exposure feeds a Clay floating element. */
static void slider_drag_bubble(uint32_t id, const char *value_text) {
    const nt_ui_slider_thumb_t t = nt_ui_slider_thumb_pos(s_ctx, id);
    if (!t.found) {
        return;
    }
    /* Root-anchored, offset to the thumb screen center. clipTo stays NONE (default): the bubble must
     * float over everything, including past any scroll container clip — opposite of the thumb/bar. */
    CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = t.x - 24.0F, .y = t.y - 40.0F}, .attachPoints = {.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP}},
          .layout = {.sizing = {CLAY_SIZING_FIXED(48), CLAY_SIZING_FIXED(26)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = {40.0F, 120.0F, 90.0F, 235.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(6)}) {
        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), value_text, &g_help_style);
    }
}

/* Compact dark recessed container decl for the scroll-showcase tiles (one fixed size). */
static Clay_ElementDeclaration scroll_tile_decl(float w, float h) {
    return (Clay_ElementDeclaration){
        .layout = {.sizing = {CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(h)}, .padding = CLAY_PADDING_ALL(8)},
        .backgroundColor = {24.0F, 26.0F, 34.0F, 255.0F},
        .cornerRadius = CLAY_CORNER_RADIUS(8),
    };
}

/* One scroll-showcase tile: title + a rows x cols cell grid in a scroll container. The three
 * variants (vertical labels / horizontal cells / both-axes grid) differ only in rows/cols, cell
 * size/format and an optional capture-steal button — so they share this one builder. */
typedef struct {
    uint32_t id;
    uint32_t inner_btn_id; /* 0 = no capture-steal button (only the vertical lists carry one) */
    const nt_ui_scroll_style_t *style;
    const char *title;
    int rows, cols;
    float cell_w; /* 0 = GROW width (the plain vertical label rows); else FIXED boxed cell */
    float cell_h;
    bool boxed;                                      /* tinted rounded cell vs plain left-aligned label row */
    void (*fmt)(char *buf, int n, int row, int col); /* cell text (callback keeps snprintf formats literal) */
    const nt_ui_label_style_t *cell_style;
} scroll_tile_t;

/* Per-variant cell-text formatters (callbacks keep snprintf format strings literal). */
static void scroll_fmt_row(char *buf, int n, int row, int col) {
    (void)col;
    (void)snprintf(buf, (size_t)n, "  Row %02d - scrollable item", row);
}
static void scroll_fmt_col(char *buf, int n, int row, int col) {
    (void)row;
    (void)snprintf(buf, (size_t)n, "Col %02d", col);
}
static void scroll_fmt_rc(char *buf, int n, int row, int col) { (void)snprintf(buf, (size_t)n, "R%dC%d", row, col); }

/* The capture-steal button inside the vertical lists: a tap clicks, a drag scrolls. */
static void scroll_inner_button(uint32_t inner_btn_id) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34)}, .padding = {.left = 10, .right = 10}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_button_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), inner_btn_id, &s_button,
                           &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}, true);
        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Tap me (drag scrolls)", &g_help_style);
        if (nt_ui_button_end(s_ctx)) {
            s_inner_btn_hits++;
            nt_log_info("ui_stateful_demo: inner button clicked (%u)", s_inner_btn_hits);
        }
    }
}

/* One grid cell: either a plain left-aligned label row (GROW width) or a tinted rounded box. */
static void scroll_cell(const scroll_tile_t *t, const char *text) {
    if (t->boxed) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(t->cell_w), CLAY_SIZING_FIXED(t->cell_h)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {40.0F, 46.0F, 60.0F, 255.0F},
              .cornerRadius = CLAY_CORNER_RADIUS(6)}) {
            nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), text, t->cell_style);
        }
    } else {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(t->cell_h)}, .padding = {.left = 6}, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
            nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), text, t->cell_style);
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void scroll_tile(const scroll_tile_t *t) {
    /* Cell text is constant — format every (r,c) string once into a frame-stable scratch, not per frame. */
    enum { TILE_MAX_CELLS = 64, TILE_CELL_CHARS = 48 };
    static char s_cells[TILE_MAX_CELLS][TILE_CELL_CHARS];
    NT_ASSERT((t->rows * t->cols) <= TILE_MAX_CELLS && "scroll_tile: too many cells for the scratch");
    for (int r = 0; r < t->rows; ++r) {
        for (int c = 0; c < t->cols; ++c) {
            t->fmt(s_cells[(r * t->cols) + c], TILE_CELL_CHARS, r, c);
        }
    }

    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4}}) {
        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), t->title, &g_help_style);

        const Clay_ElementDeclaration scroll_decl = scroll_tile_decl(220.0F, 78.0F);
        nt_ui_scroll_begin(s_ctx, NULL, t->id, t->style, &scroll_decl);
        {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6}}) {
                if (t->inner_btn_id != 0U) {
                    scroll_inner_button(t->inner_btn_id);
                }
                for (int r = 0; r < t->rows; ++r) {
                    /* Each row is a LEFT_TO_RIGHT strip of cols cells (a single cell when cols==1). */
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 6}}) {
                        for (int c = 0; c < t->cols; ++c) {
                            scroll_cell(t, s_cells[(r * t->cols) + c]);
                        }
                    }
                }
            }
        }
        nt_ui_scroll_end(s_ctx);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_widgets_panel(void) {
    char buf[64];

    CLAY({.id = CLAY_ID("widgets-panel"),
          .layout = {.sizing = {CLAY_SIZING_FIXED(620), CLAY_SIZING_FIT(0)},
                     .padding = CLAY_PADDING_ALL(14),
                     .layoutDirection = CLAY_TOP_TO_BOTTOM,
                     .childGap = 2,
                     .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}},
          .backgroundColor = {30.0F, 34.0F, 42.0F, 255.0F},
          .cornerRadius = CLAY_CORNER_RADIUS(10)}) {

        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = {.bottom = 2}}}) {
            nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Sliders / Progress / Scroll", &g_title_style);
        }

        // #region sliders (float + int) with drag-bubble
        section_label("Sliders (drag the thumb)");

        (void)snprintf(buf, sizeof buf, "Volume  %.2f", (double)s_volume);
        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, &g_status_style);
        (void)nt_ui_slider_float(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_volume, NULL, &s_volume, 0.0F, 1.0F, 0.0F, &s_slider, &s_slider_decl, true);
        (void)snprintf(buf, sizeof buf, "%.2f", (double)s_volume);
        slider_drag_bubble(s_id_volume, buf);

        (void)snprintf(buf, sizeof buf, "Count   %d", s_count);
        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, &g_status_style);
        (void)nt_ui_slider_int(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_count, NULL, &s_count, 0, 10, 1, &s_slider, &s_slider_decl, true);
        (void)snprintf(buf, sizeof buf, "%d", s_count);
        slider_drag_bubble(s_id_count, buf);
        // #endregion

        // #region progress (STRETCH vs CROP, side by side) + vertical mana
        section_label("Progress: STRETCH (slice9) vs CROP (clip)");

        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 28, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6}}) {
                /* Bar A: STRETCH slice9 — the smooth gradient pill stretches with no distortion. */
                (void)snprintf(buf, sizeof buf, "STRETCH (slice9)  %d%%", (int)(s_progress_val * 100.0F));
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, &g_status_style);
                nt_ui_progress(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, nt_ui_id("progress/stretch"), s_progress_val, &s_progress_stretch, &s_progress_decl);

                /* Bar B: CROP clip — the diagonal-stripe art is revealed crisp, not stretched. */
                (void)snprintf(buf, sizeof buf, "CROP (clip)  %d%%", (int)(s_progress_val * 100.0F));
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, &g_status_style);
                nt_ui_progress(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, nt_ui_id("progress/crop"), s_progress_val, &s_progress_crop, &s_progress_decl);
            }
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 6, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Mana", &g_help_style);
                nt_ui_progress(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, nt_ui_id("progress/mana"), s_mana_val, &s_mana,
                               &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(96)}}});
            }
        }
        // #endregion

        // #region scroll containers (2x2: AUTO_HIDE vert / ALWAYS vert / horizontal / XY) + scroll-to
        section_label("Scroll (vertical / horizontal / both axes)");

        /* 2x2 tile grid: two vertical lists on top, horizontal + both-axes below. */
        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 8}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 24, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
                scroll_tile(&(scroll_tile_t){.id = s_id_scroll_hide,
                                             .inner_btn_id = s_id_inner_btn_hide,
                                             .style = &s_scroll_hide,
                                             .title = "AUTO_HIDE bar (vertical)",
                                             .rows = 24,
                                             .cols = 1,
                                             .cell_w = 0.0F,
                                             .cell_h = 28.0F,
                                             .boxed = false,
                                             .fmt = scroll_fmt_row,
                                             .cell_style = &g_status_style});
                scroll_tile(&(scroll_tile_t){.id = s_id_scroll_always,
                                             .inner_btn_id = s_id_inner_btn_always,
                                             .style = &s_scroll_always,
                                             .title = "ALWAYS bar (vertical)",
                                             .rows = 24,
                                             .cols = 1,
                                             .cell_w = 0.0F,
                                             .cell_h = 28.0F,
                                             .boxed = false,
                                             .fmt = scroll_fmt_row,
                                             .cell_style = &g_status_style});
            }
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 24, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_TOP}}}) {
                scroll_tile(&(scroll_tile_t){.id = s_id_scroll_horiz,
                                             .inner_btn_id = 0U,
                                             .style = &s_scroll_horiz,
                                             .title = "Horizontal scroll",
                                             .rows = 1,
                                             .cols = 14,
                                             .cell_w = 78.0F,
                                             .cell_h = 44.0F,
                                             .boxed = true,
                                             .fmt = scroll_fmt_col,
                                             .cell_style = &g_status_style});
                scroll_tile(&(scroll_tile_t){.id = s_id_scroll_xy,
                                             .inner_btn_id = 0U,
                                             .style = &s_scroll_xy,
                                             .title = "Both axes (X + Y)",
                                             .rows = 8,
                                             .cols = 8,
                                             .cell_w = 56.0F,
                                             .cell_h = 30.0F,
                                             .boxed = true,
                                             .fmt = scroll_fmt_rc,
                                             .cell_style = &g_help_style});
            }
        }

        CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(40)}, .padding = {.top = 6}, .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}}}) {
            nt_ui_button_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_scroll_to_btn, &s_button,
                               &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(34)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}, true);
            nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Scroll all to start", &g_status_style);
            if (nt_ui_button_end(s_ctx)) {
                nt_ui_scroll_to(s_ctx, s_id_scroll_hide, 0.0F, 0.0F);
                nt_ui_scroll_to(s_ctx, s_id_scroll_always, 0.0F, 0.0F);
                nt_ui_scroll_to(s_ctx, s_id_scroll_horiz, 0.0F, 0.0F); /* reset X */
                nt_ui_scroll_to(s_ctx, s_id_scroll_xy, 0.0F, 0.0F);    /* reset X + Y */
            }
        }
        // #endregion

        // #region physics tuning
        section_label("Physics tuning (feel it out)");

        (void)snprintf(buf, sizeof buf, "Friction   %.3f", (double)s_friction);
        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, &g_status_style);
        if (nt_ui_slider_float(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_friction, NULL, &s_friction, 0.80F, 0.99F, 0.0F, &s_slider, &s_slider_decl, true)) {
            s_scroll_hide.friction = s_friction;
            s_scroll_always.friction = s_friction;
        }

        (void)snprintf(buf, sizeof buf, "Wheel ease %.1f", (double)s_wheel_ease);
        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, &g_status_style);
        if (nt_ui_slider_float(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_wheel_ease, NULL, &s_wheel_ease, 4.0F, 40.0F, 0.0F, &s_slider, &s_slider_decl, true)) {
            s_scroll_hide.wheel_ease_speed = s_wheel_ease;
            s_scroll_always.wheel_ease_speed = s_wheel_ease;
        }

        (void)snprintf(buf, sizeof buf, "Wheel step %.0f px/notch", (double)s_wheel_step);
        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), buf, &g_status_style);
        if (nt_ui_slider_float(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, s_id_wheel_step, NULL, &s_wheel_step, 10.0F, 80.0F, 0.0F, &s_slider, &s_slider_decl, true)) {
            s_scroll_hide.wheel_step_px = s_wheel_step;
            s_scroll_always.wheel_step_px = s_wheel_step;
        }
        // #endregion
    }
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
        s_id_volume = nt_ui_id("settings/volume");
        s_id_count = nt_ui_id("settings/count");
        s_id_friction = nt_ui_id("tune/friction");
        s_id_wheel_ease = nt_ui_id("tune/wheel_ease");
        s_id_wheel_step = nt_ui_id("tune/wheel_step");
        s_id_scroll_hide = nt_ui_id("scroll/list_autohide");
        s_id_scroll_always = nt_ui_id("scroll/list_always");
        s_id_scroll_horiz = nt_ui_id("scroll/list_horiz");
        s_id_scroll_xy = nt_ui_id("scroll/list_xy");
        s_id_scroll_to_btn = nt_ui_id("scroll/to_top");
        s_id_inner_btn_hide = nt_ui_id("scroll/inner_btn_hide");
        s_id_inner_btn_always = nt_ui_id("scroll/inner_btn_always");
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

// #region layout probe (headless viewport-fit assertion)
/* One interactive widget's bottom edge vs the viewport; logs and returns whether it fits. */
static bool probe_fits(uint32_t id, const char *name, float viewport_h) {
    const Clay_ElementData d = Clay_GetElementData((Clay_ElementId){.id = id});
    if (!d.found) {
        nt_log_error("probe: '%s' not found in layout (id=%u)", name, id);
        return false;
    }
    const float bottom = d.boundingBox.y + d.boundingBox.height;
    const bool fits = bottom <= viewport_h + 0.5F;
    nt_log_info("probe: %-22s bottom=%.1f / viewport=%.0f -> %s", name, (double)bottom, (double)viewport_h, fits ? "FIT" : "CLIP");
    return fits;
}

/* Assert the bottom-most interactive widgets sit within the viewport; logs PASS/FAIL + quits. */
static void probe_layout_and_quit(float viewport_h) {
    bool ok = true;
    ok &= probe_fits(s_id_scroll_horiz, "scroll horizontal", viewport_h);
    ok &= probe_fits(s_id_scroll_xy, "scroll both-axes", viewport_h);
    ok &= probe_fits(s_id_scroll_to_btn, "scroll-to button", viewport_h);
    ok &= probe_fits(s_id_friction, "friction slider", viewport_h);
    ok &= probe_fits(s_id_wheel_ease, "wheel-ease slider", viewport_h);
    ok &= probe_fits(s_id_wheel_step, "wheel-step slider", viewport_h);
    nt_log_info("probe: LAYOUT %s", ok ? "PASS (all interactive widgets fit)" : "FAIL (a widget is clipped)");
    s_probe_exit_code = ok ? 0 : 7; /* exit code is the headless signal: 0 fit, 7 clip */
    nt_app_quit();
}
// #endregion

// #region frame
/* Bounce a 0..1 value up then down by rate_dt per call, flipping *up at each end. */
static void ramp01(float *v, bool *up, float rate_dt) {
    *v += *up ? rate_dt : -rate_dt;
    if (*v >= 1.0F) {
        *v = 1.0F;
        *up = false;
    } else if (*v <= 0.0F) {
        *v = 0.0F;
        *up = true;
    }
}

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

    /* Auto-ramp the load bar 0->1->0 (value_t ease visible) and the mana bar independently at a
     * different rate (phase-shifted so the two bars never move in sync). */
    ramp01(&s_progress_val, &s_progress_up, 0.25F * g_nt_app.dt);
    ramp01(&s_mana_val, &s_mana_up, 0.18F * g_nt_app.dt);

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
        const char *help_text = "Drag sliders  |  scroll/fling lists (tap inner button, drag to scroll)  |  D = inspector  |  Esc quit";

        /* Y_TOP (not Y_CENTER): the panels are taller than the viewport, so centering would clip
         * the bottom rows (scroll-to button + tuning sliders) off-screen and unhittable. */
        CLAY({.id = CLAY_ID("root"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .padding = CLAY_PADDING_ALL(10),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 6,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}},
              .backgroundColor = {18.0F, 18.0F, 22.0F, 255.0F}}) {

            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), status_text, &g_status_style);
            }

            /* Two panels side by side: the settings widgets + the slider/progress/scroll widgets. */
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 20, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}}}) {
                declare_menu();
                declare_widgets_panel();
            }

            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), help_text, &g_help_style);
            }
        }

        /* Headless probe: query the PREVIOUS frame's solved bboxes (Clay_GetElementData reads the
         * persistent hashmap, valid only INSIDE begin/end). After a few settle frames assert every
         * bottom-most interactive widget fits the logical viewport, then quit. */
        if (s_probe_layout && ++s_probe_settle_frames >= 4) {
            probe_layout_and_quit(scale.logical_h);
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
    /* --probe-layout: headless viewport-fit gate (settle, assert all interactive widgets fit, quit). */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--probe-layout") == 0) {
            s_probe_layout = true;
        }
    }

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

    /* Handle is valid immediately; fill late-bound widget refs upfront. */
    init_widget_styles();

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
    return s_probe_exit_code; /* 0 normally; --probe-layout sets 7 if a widget is clipped */
}
// #endregion
