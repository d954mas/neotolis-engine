/* UI Buttons Demo -- WIDGET-02/03/04 visual gate + WIDGET-05 verify +
 * Phase 56 ext: transform-aware hit-test proof (D-56-07) + touch-target
 * padding (hit_padding_lrtb) + nt_ui_inspector (verbatim Clay debug view port).
 *
 * Renders a 2 x 3 GRID of LABELED button variants -- each cell has the
 * button (320x180) + a description text label above it -- so the user can
 * see at a glance what each variant demonstrates:
 *
 *   (a) STANDARD (eased)              -- baseline text button, transition_speed=12
 *   (b) SCALE (exaggerated 0.80<->1.20) -- per-state scale dramatized
 *   (c) VISUAL SWAP (blue<->green)    -- bg_region swaps per state (D-56-11)
 *   (d) ICON ONLY                      -- nt_ui_image bunny icon
 *   (e) ICON + TEXT                    -- image + label inside one button
 *   (f) DISABLED (enabled=false)      -- short-circuit gate
 *
 * Below the grid: a 7th BAKED-TRANSFORM button (offset+rotation+scale at
 * idle) that proves the inverse-affine hit-test (D-56-07) on a statically
 * transformed widget.
 *
 * Per-button click counters print to the status line.
 * Variants (a)(b)(c)(f) ship +16 px touch padding; (d)(e) and the baked
 * variant have no padding.
 *
 * Buttons run inside a runtime-controllable transform (arrow keys translate,
 * PageUp/Down scale, Q/E rotate, R reset) -- the grid visibly moves while
 * remaining clickable, proving the inverse-affine hit-test live.
 *
 * Keys:
 *   Esc      quit (native)
 *   D        toggle inspector (panel + hit-zone overlay -- ONE debug system)
 *   arrows   translate transform (+- 8 px / frame)
 *   PgUp/Dn  scale transform (+- 0.05, clamp 0.5..2.0)
 *   Q / E    rotate transform (+- 5 deg / press)
 *   R        reset transform to identity
 *
 * Build packs: build_ui_buttons_demo_packs build/examples/ui_buttons_demo */

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
#include "ui/nt_ui_debug.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_scale.h"
#include "window/nt_window.h"

#include "math/nt_math.h"
#include "memory/nt_mem_scratch.h"
#include "nt_pack_format.h"

#include "ui_buttons_demo_assets.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NT_PLATFORM_WEB
#include "platform/web/nt_platform_web.h"
#endif

#include "clay.h"
// #endregion

// #region styles
static const nt_ui_label_style_t g_status_style = {
    .font_id = 0,
    .font_size = 24,
    .color = {200.0F, 200.0F, 210.0F, 255.0F},
};

static const nt_ui_label_style_t g_help_style = {
    .font_id = 0,
    .font_size = 18,
    .color = {160.0F, 170.0F, 180.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

/* Per-cell variant title (one line above each button). Bigger than help/status
 * so the user can tell variants apart at a glance ("чтобы я понимал где какая"). */
static const nt_ui_label_style_t g_cell_title_style = {
    .font_id = 0,
    .font_size = 26,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

/* Sub-line under the title for padding annotation. */
static const nt_ui_label_style_t g_cell_sub_style = {
    .font_id = 0,
    .font_size = 16,
    .color = {180.0F, 180.0F, 190.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

/* Reference button label (centered white text inside the slice9 bg). Bumped
 * to 34 px so the labels are legible at the bigger 320x120 button size on
 * HiDPI Windows. */
static const nt_ui_label_style_t g_btn_label_style = {
    .font_id = 0,
    .font_size = 34,
    .color = {255.0F, 255.0F, 255.0F, 255.0F},
    .align = CLAY_TEXT_ALIGN_CENTER,
};

/* Icon child = the icon_bunny region (Kenney CC0, 32x32). Untinted (0xFFFFFFFF
 * = no tint) so the bunny renders in its natural blue color. opacity still
 * inherits from the button's per-state push_opacity (WIDGET-05). */
static const nt_ui_image_style_t g_btn_icon_style = {
    .color_packed = 0xFFFFFFFF, /* no tint (0xAABBGGRR) -- show natural art */
    .origin_x = 0.5F,
    .origin_y = 0.5F,
};
// #endregion

// #region button style templates
/* NOTE on the Kenney slice9 "gray center" the user spotted: button_blue_depth.png
 * and button_green_depth.png are Kenney CC0 "depth" buttons -- intentional 3D
 * art with an outer bright frame + an INNER recessed light-gray-blue panel.
 * On the dark BG (#121216) the inner panel reads as gray. This is the Kenney
 * art rendering correctly, NOT a tint/state bug; the slice9 borders (16 px
 * each side) preserve the corners and stretch the inner gray patch to fill
 * the button. Swapping to "flat" Kenney buttons (no depth suffix) would
 * remove the gray look but lose the 3D affordance the demo wants to show. */

/* Variant (a) STANDARD: eased baseline. Same shape as the old g_btn_eased_style
 * from the previous round; transition_speed 12, +16 hit padding. */
static const nt_ui_button_style_t g_btn_standard_style = {
    .idle = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 1.0F},
    .hover = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.05F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 1.0F},
    .pressed = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 0.95F, .offset_x = 0.0F, .offset_y = 2.0F, .opacity = 1.0F},
    .disabled = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 0.4F},
    .transition_speed = 12.0F,
    .hit_padding_lrtb = {16, 16, 16, 16},
};

/* Variant (b) SCALE: exaggerated scale per state -- pressed 0.80, hover 1.20. */
static const nt_ui_button_style_t g_btn_scale_style = {
    .idle = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 1.0F},
    .hover = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.20F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 1.0F},
    .pressed = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 0.80F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 1.0F},
    .disabled = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 0.4F},
    .transition_speed = 12.0F,
    .hit_padding_lrtb = {16, 16, 16, 16},
};

/* Variant (c) VISUAL SWAP: bg_region swaps per state -- idle/disabled = blue,
 * hover/pressed = green; pressed tints green darker (0xFFCCCCCC). Patched at
 * runtime with real button_blue/green indices (bg_region 0 in const = sentinel). */
static const nt_ui_button_style_t g_btn_swap_style = {
    .idle = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 1.0F},
    .hover = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.05F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 1.0F},
    .pressed = {.bg_region = 0, .bg_tint = 0xFFCCCCCC, .scale = 0.95F, .offset_x = 0.0F, .offset_y = 2.0F, .opacity = 1.0F},
    .disabled = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 0.4F},
    .transition_speed = 12.0F,
    .hit_padding_lrtb = {16, 16, 16, 16},
};

/* Variant (d)(e) ICON / ICON+TEXT: same shape as STANDARD but NO touch padding,
 * so the difference visual=hit (no pad) vs visual<hit (pad) is plain. */
static const nt_ui_button_style_t g_btn_nopad_style = {
    .idle = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 1.0F},
    .hover = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.05F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 1.0F},
    .pressed = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 0.95F, .offset_x = 0.0F, .offset_y = 2.0F, .opacity = 1.0F},
    .disabled = {.bg_region = 0, .bg_tint = 0xFFFFFFFF, .scale = 1.0F, .offset_x = 0.0F, .offset_y = 0.0F, .opacity = 0.4F},
    .transition_speed = 12.0F,
    .hit_padding_lrtb = {0, 0, 0, 0},
};
// #endregion

// #region engine state
#define UI_ARENA_SIZE ((size_t)1U * 1024U * 1024U)
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
static uint32_t s_button_blue_idx;
static uint32_t s_button_green_idx;
static uint32_t s_icon_bunny_idx;

/* Reference-button runtime styles: const templates copied + bg_region patched
 * to the real atlas indices once the atlas binds (D-54-02). Button ids
 * precomputed once via nt_ui_id inside the first Clay frame. */
static nt_ui_button_style_t s_btn_standard;
static nt_ui_button_style_t s_btn_scale;
static nt_ui_button_style_t s_btn_swap;
static nt_ui_button_style_t s_btn_nopad;
static uint32_t s_id_std;
static uint32_t s_id_scale;
static uint32_t s_id_swap;
static uint32_t s_id_icon;
static uint32_t s_id_icontext;
static uint32_t s_id_disabled;
static uint32_t s_id_baked;
static bool s_btn_ids_ready;
/* Per-variant click counters. */
static uint32_t s_clicks_std;
static uint32_t s_clicks_scale;
static uint32_t s_clicks_swap;
static uint32_t s_clicks_icon;
static uint32_t s_clicks_icontext;
static uint32_t s_clicks_disabled; /* should always stay 0 */
static uint32_t s_clicks_baked;

/* Runtime transform around the reference button grid. */
static float s_xform_tx;
static float s_xform_ty;
static float s_xform_scale = 1.0F;
static float s_xform_deg;

/* Debug overlay state. ONE master toggle (D) drives the inspector.
 * The hit-zone overlay paints for the single focused element. */

#define LAYER_IMG 1
#define LAYER_TEXT 2

/* 1600x1200 logical: 6-cell grid (~720 px tall with status) + new BAKED
 * TRANSFORM section (~400 px with padding) + help bar. The window matches. */
#define UI_REF_W 1600.0F
#define UI_REF_H 1200.0F
// #endregion

// #region binding
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void try_bind_resources(void) {
    if (s_atlas_bound && s_font_bound) {
        return;
    }

    if (!s_atlas_bound && nt_resource_is_ready(s_atlas_handle)) {
        s_white_region_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_BUTTONS_DEMO_ATLAS__WHITE.value);
        NT_ASSERT(s_white_region_idx != NT_ATLAS_INVALID_REGION);

        s_button_blue_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_BUTTONS_DEMO_ATLAS_BUTTON_BLUE.value);
        NT_ASSERT(s_button_blue_idx != NT_ATLAS_INVALID_REGION);

        s_button_green_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_BUTTONS_DEMO_ATLAS_BUTTON_GREEN.value);
        NT_ASSERT(s_button_green_idx != NT_ATLAS_INVALID_REGION);

        s_icon_bunny_idx = nt_atlas_find_region(s_atlas_handle, ASSET_ATLAS_REGION_UI_BUTTONS_DEMO_ATLAS_ICON_BUNNY.value);
        NT_ASSERT(s_icon_bunny_idx != NT_ATLAS_INVALID_REGION);

        nt_ui_set_atlas_white_region(s_ctx, s_atlas_handle, s_white_region_idx);

        /* Patch each variant's bg_region from the templates (D-54-02). */
        s_btn_standard = g_btn_standard_style;
        s_btn_standard.idle.bg_region = s_button_blue_idx;
        s_btn_standard.hover.bg_region = s_button_blue_idx;
        s_btn_standard.pressed.bg_region = s_button_blue_idx;
        s_btn_standard.disabled.bg_region = s_button_blue_idx;

        s_btn_scale = g_btn_scale_style;
        s_btn_scale.idle.bg_region = s_button_blue_idx;
        s_btn_scale.hover.bg_region = s_button_blue_idx;
        s_btn_scale.pressed.bg_region = s_button_blue_idx;
        s_btn_scale.disabled.bg_region = s_button_blue_idx;

        /* VISUAL-SWAP: blue idle/disabled, green hover/pressed. */
        s_btn_swap = g_btn_swap_style;
        s_btn_swap.idle.bg_region = s_button_blue_idx;
        s_btn_swap.hover.bg_region = s_button_green_idx;
        s_btn_swap.pressed.bg_region = s_button_green_idx;
        s_btn_swap.disabled.bg_region = s_button_blue_idx;

        s_btn_nopad = g_btn_nopad_style;
        s_btn_nopad.idle.bg_region = s_button_blue_idx;
        s_btn_nopad.hover.bg_region = s_button_blue_idx;
        s_btn_nopad.pressed.bg_region = s_button_blue_idx;
        s_btn_nopad.disabled.bg_region = s_button_blue_idx;

        s_atlas_bound = true;
        nt_log_info("ui_buttons_demo: atlas bound (button_blue + button_green + _white + icon_bunny)");
    }

    if (!s_font_bound && nt_resource_is_ready(s_font_resource)) {
        nt_font_add(s_font, s_font_resource);
        nt_ui_set_font(s_ctx, 0U, s_font);
        s_font_bound = true;
        nt_log_info("ui_buttons_demo: font bound at slot 0");
    }
}
// #endregion

// #region grid cell sizing
/* Each cell = vertical stack: title text + sub text + button.
 * Cell is 360 wide so the 320 button has 20 px breathing room. Button
 * bumped 120 -> 180 per user feedback ("кнопки сделаем больше по высоте")
 * so the Kenney slice9 art reads clearly + icons aren't dwarfed. Cell H
 * bumped proportionally (240 -> 320) so title+sub still fit above. */
#define CELL_W 360
#define CELL_H 320
#define BTN_W 320
#define BTN_H 180

/* Macro to declare the title + sub strip of a cell (callable inside a CLAY block).
 * The cell itself + the button slot are declared inline in the caller. */
#define CELL_LABELS(title_str, sub_str)                                                                                                                                                                \
    do {                                                                                                                                                                                               \
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {                                                        \
            nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), (title_str), &g_cell_title_style);                                                                                                        \
        }                                                                                                                                                                                              \
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {                                                        \
            nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), (sub_str), &g_cell_sub_style);                                                                                                            \
        }                                                                                                                                                                                              \
    } while (0)

/* Common layout for the per-cell column. */
#define CELL_LAYOUT                                                                                                                                                                                    \
    {                                                                                                                                                                                                  \
        .layout = {                                                                                                                                                                                    \
            .sizing = {CLAY_SIZING_FIXED(CELL_W), CLAY_SIZING_FIXED(CELL_H)},                                                                                                                          \
            .padding = CLAY_PADDING_ALL(8),                                                                                                                                                            \
            .layoutDirection = CLAY_TOP_TO_BOTTOM,                                                                                                                                                     \
            .childGap = 6,                                                                                                                                                                             \
            .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_TOP}                                                                                                                                  \
        }                                                                                                                                                                                              \
    }

/* Common layout for the button slot inside a cell (centers the button). */
#define BTN_SLOT_LAYOUT                                                                                                                                                                                \
    {                                                                                                                                                                                                  \
        .layout = {.sizing = {CLAY_SIZING_FIXED(BTN_W), CLAY_SIZING_FIXED(BTN_H)}, .padding = CLAY_PADDING_ALL(8), .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER} }                      \
    }

/* nt_ui_button_begin opens a Clay IMAGE element WITHOUT layout sizing -- it
 * defaults to FIT (Clay clay.h:301), so the slice9 background shrinks to the
 * label/icon natural size (~80x40 for a label, ~72x72 for an icon) instead of
 * filling the BTN_SLOT_LAYOUT 320x180 outer wrapper. Result: tiny slice9 button
 * floating in a big empty slot -- the slice9's gray inner panel + light corners
 * look like a "small white-ish square" against the dark BG. Wrapping the
 * button's children in a FIXED inner Clay block forces the FIT button
 * container to grow to that bbox so the slice9 renders at the full intended
 * size. Subtract 16 px for the BTN_SLOT padding (8 px each side). */
#define BTN_CONTENT_W (BTN_W - 16)
#define BTN_CONTENT_H (BTN_H - 16)
// #endregion

// #region declare_reference_buttons (2 x 3 GRID)
/* Builds the 6-variant grid (2 rows x 3 cols). Each cell shows its title
 * above the button so the user can tell variants apart at a glance.
 * ids precomputed once on the first Clay frame. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void declare_reference_buttons(void) {
    if (!s_btn_ids_ready) {
        s_id_std = nt_ui_id("btn_standard");
        s_id_scale = nt_ui_id("btn_scale");
        s_id_swap = nt_ui_id("btn_swap");
        s_id_icon = nt_ui_id("btn_icon");
        s_id_icontext = nt_ui_id("btn_icontext");
        s_id_disabled = nt_ui_id("btn_disabled");
        s_id_baked = nt_ui_id("btn_baked");
        s_btn_ids_ready = true;
    }

    /* Outer container = grid: TOP_TO_BOTTOM, two rows each LEFT_TO_RIGHT. */
    CLAY({.id = CLAY_ID("ref-btn-grid"),
          .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 30, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {

        /* ===== ROW 1: (a) STANDARD | (b) SCALE | (c) VISUAL SWAP ===== */
        CLAY({.id = CLAY_ID("ref-btn-row1"),
              .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 24, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {

            // #region (a) STANDARD
            CLAY(CELL_LAYOUT) {
                CELL_LABELS("STANDARD (eased)", "+16 px touch padding");
                CLAY(BTN_SLOT_LAYOUT) {
                    nt_ui_button_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_std, s_atlas_handle, &s_btn_standard, true);
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(BTN_CONTENT_W), CLAY_SIZING_FIXED(BTN_CONTENT_H)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Save", &g_btn_label_style);
                    }
                    if (nt_ui_button_end(s_ctx)) {
                        s_clicks_std++;
                    }
                }
            }
            // #endregion

            // #region (b) SCALE
            CLAY(CELL_LAYOUT) {
                CELL_LABELS("SCALE 0.80<->1.20", "+16 px touch padding");
                CLAY(BTN_SLOT_LAYOUT) {
                    nt_ui_button_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_scale, s_atlas_handle, &s_btn_scale, true);
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(BTN_CONTENT_W), CLAY_SIZING_FIXED(BTN_CONTENT_H)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Boom", &g_btn_label_style);
                    }
                    if (nt_ui_button_end(s_ctx)) {
                        s_clicks_scale++;
                    }
                }
            }
            // #endregion

            // #region (c) VISUAL SWAP
            CLAY(CELL_LAYOUT) {
                CELL_LABELS("VISUAL SWAP blue<->green", "+16 px touch padding");
                CLAY(BTN_SLOT_LAYOUT) {
                    nt_ui_button_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_swap, s_atlas_handle, &s_btn_swap, true);
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(BTN_CONTENT_W), CLAY_SIZING_FIXED(BTN_CONTENT_H)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Swap", &g_btn_label_style);
                    }
                    if (nt_ui_button_end(s_ctx)) {
                        s_clicks_swap++;
                    }
                }
            }
            // #endregion
        }

        /* ===== ROW 2: (d) ICON ONLY | (e) ICON + TEXT | (f) DISABLED ===== */
        CLAY({.id = CLAY_ID("ref-btn-row2"),
              .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 24, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {

            // #region (d) ICON ONLY
            CLAY(CELL_LAYOUT) {
                CELL_LABELS("ICON ONLY", "no padding");
                CLAY(BTN_SLOT_LAYOUT) {
                    nt_ui_button_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_icon, s_atlas_handle, &s_btn_nopad, true);
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(BTN_CONTENT_W), CLAY_SIZING_FIXED(BTN_CONTENT_H)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(96)}}}) {
                            nt_ui_image(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_atlas_handle, s_icon_bunny_idx, &g_btn_icon_style);
                        }
                    }
                    if (nt_ui_button_end(s_ctx)) {
                        s_clicks_icon++;
                    }
                }
            }
            // #endregion

            // #region (e) ICON + TEXT
            CLAY(CELL_LAYOUT) {
                CELL_LABELS("ICON + TEXT", "no padding");
                CLAY(BTN_SLOT_LAYOUT) {
                    nt_ui_button_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_icontext, s_atlas_handle, &s_btn_nopad, true);
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(BTN_CONTENT_W), CLAY_SIZING_FIXED(BTN_CONTENT_H)},
                                     .layoutDirection = CLAY_LEFT_TO_RIGHT,
                                     .childGap = 16,
                                     .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                        CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(80), CLAY_SIZING_FIXED(80)}}}) {
                            nt_ui_image(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_atlas_handle, s_icon_bunny_idx, &g_btn_icon_style);
                        }
                        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Play", &g_btn_label_style);
                    }
                    if (nt_ui_button_end(s_ctx)) {
                        s_clicks_icontext++;
                    }
                }
            }
            // #endregion

            // #region (f) DISABLED
            CLAY(CELL_LAYOUT) {
                CELL_LABELS("DISABLED (enabled=false)", "+16 px (no hover)");
                CLAY(BTN_SLOT_LAYOUT) {
                    nt_ui_button_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_disabled, s_atlas_handle, &s_btn_standard, false);
                    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(BTN_CONTENT_W), CLAY_SIZING_FIXED(BTN_CONTENT_H)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                        nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Locked", &g_btn_label_style);
                    }
                    if (nt_ui_button_end(s_ctx)) {
                        s_clicks_disabled++; /* unreachable while disabled -- proves the gate */
                    }
                }
            }
            // #endregion
        }

        // #region (g) BAKED TRANSFORM (7th button -- D-56-07 static proof)
        /* User scope COMMIT 3: a 7th button wrapped in nt_ui_push_transform
         * with non-trivial BAKED values so the user sees it offset+rotated+
         * scaled at IDLE (before any hover), and clicking it at the
         * transformed on-screen position proves the inverse-affine hit-test
         * (D-56-07) works for statically-transformed widgets too. Baked values:
         *   offset_x: +60   (clearly offset from layout slot)
         *   offset_y: -20
         *   rotation: 25 deg (CCW visually -- Clay Y-down -> screen flip)
         *   scale_x:  1.15  (anisotropic)
         *   scale_y:  0.85
         * Outer slot has ~80 px extra padding so the rotated button does not
         * overlap row 2 or the help bar. The button itself uses the eased
         * STANDARD style; click counter s_clicks_baked. */
        CLAY({.id = CLAY_ID("ref-btn-baked-section"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                         .padding = {.left = 0, .right = 0, .top = 80, .bottom = 80},
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 12,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "BAKED TRANSFORM (idle has offset+rotation+scale; click should still work)", &g_cell_title_style);
            }
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "offset=(+60,-20) rotation=25 deg scale=(1.15,0.85)", &g_cell_sub_style);
            }
            CLAY(BTN_SLOT_LAYOUT) {
                const nt_ui_transform_t baked = {
                    .offset_x = 60.0F,
                    .offset_y = -20.0F,
                    .rotation = 25.0F * 0.017453292F, /* 25 deg -> rad */
                    .scale_x = 1.15F,
                    .scale_y = 0.85F,
                };
                nt_ui_push_transform(s_ctx, &baked);
                nt_ui_button_begin(s_ctx, NT_UI_DATA_LAYER(LAYER_IMG), s_id_baked, s_atlas_handle, &s_btn_standard, true);
                CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(BTN_CONTENT_W), CLAY_SIZING_FIXED(BTN_CONTENT_H)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                    nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), "Baked", &g_btn_label_style);
                }
                if (nt_ui_button_end(s_ctx)) {
                    s_clicks_baked++;
                }
                nt_ui_pop_transform(s_ctx);
            }
        }
        // #endregion
    }
}
// #endregion

// #region input_handling
/* Per-frame: arrows translate, PageUp/Dn scale, Q/E rotate, R reset,
 * D toggles the inspector (single master debug key).
 * Press-edge for one-shots; held for continuous arrows. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void handle_transform_and_debug_input(void) {
#ifndef NT_PLATFORM_WEB
    /* Translation (continuous). 8 px/frame is brisk but controllable. */
    const float tstep = 8.0F;
    if (nt_input_key_is_down(NT_KEY_ARROW_LEFT)) {
        s_xform_tx -= tstep;
    }
    if (nt_input_key_is_down(NT_KEY_ARROW_RIGHT)) {
        s_xform_tx += tstep;
    }
    if (nt_input_key_is_down(NT_KEY_ARROW_UP)) {
        s_xform_ty -= tstep;
    }
    if (nt_input_key_is_down(NT_KEY_ARROW_DOWN)) {
        s_xform_ty += tstep;
    }
    /* Scale (one-shot press edge: PageUp / PageDown). 0.05 step, clamp 0.5..2.0. */
    if (nt_input_key_is_pressed(NT_KEY_PAGE_UP)) {
        s_xform_scale += 0.05F;
    }
    if (nt_input_key_is_pressed(NT_KEY_PAGE_DOWN)) {
        s_xform_scale -= 0.05F;
    }
    if (s_xform_scale < 0.5F) {
        s_xform_scale = 0.5F;
    }
    if (s_xform_scale > 2.0F) {
        s_xform_scale = 2.0F;
    }
    /* Rotation (one-shot, 5 deg). */
    if (nt_input_key_is_pressed(NT_KEY_Q)) {
        s_xform_deg -= 5.0F;
    }
    if (nt_input_key_is_pressed(NT_KEY_E)) {
        s_xform_deg += 5.0F;
    }
    /* Reset. */
    if (nt_input_key_is_pressed(NT_KEY_R)) {
        s_xform_tx = 0.0F;
        s_xform_ty = 0.0F;
        s_xform_scale = 1.0F;
        s_xform_deg = 0.0F;
    }
    /* D: toggle nt_ui_inspector (single master debug key, Phase 56 ext rework).
     * The inspector is the verbatim Clay debug view port + engine extensions
     * (widget-type pill + layer column). Clay's built-in debug is gone --
     * there is ONE debug system now. */
    if (nt_input_key_is_pressed(NT_KEY_D)) {
        const bool now_on = !nt_ui_inspector_is_active(s_ctx);
        nt_ui_inspector_set_active(s_ctx, now_on);
        nt_log_info("ui_buttons_demo: inspector %s", now_on ? "ON" : "OFF");
    }
#endif
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

    handle_transform_and_debug_input();

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

        // #region status + help text
        char status_text[360];
        const uint32_t total_clicks = s_clicks_std + s_clicks_scale + s_clicks_swap + s_clicks_icon + s_clicks_icontext + s_clicks_baked;
        (void)snprintf(status_text, sizeof status_text, "clicks: Std=%u Scale=%u Swap=%u Icon=%u IconTxt=%u Disabled=%u Baked=%u (total=%u)   tx=%.0f ty=%.0f s=%.2f deg=%.0f   inspector=%s",
                       s_clicks_std, s_clicks_scale, s_clicks_swap, s_clicks_icon, s_clicks_icontext, s_clicks_disabled, s_clicks_baked, total_clicks, (double)s_xform_tx, (double)s_xform_ty,
                       (double)s_xform_scale, (double)s_xform_deg, nt_ui_inspector_is_active(s_ctx) ? "ON" : "off");
        const char *help_text = "D = debug  |  arrows/PgUp-Dn/Q-E/R transform  |  Esc quit";
        // #endregion

        // #region clay declaration
        const nt_ui_transform_t row_xform = {
            .offset_x = s_xform_tx,
            .offset_y = s_xform_ty,
            .rotation = s_xform_deg * 0.017453292F, /* deg -> rad */
            .scale_x = s_xform_scale,
            .scale_y = s_xform_scale,
        };

        /* Y_CENTER alignment so a portrait-resized window keeps content
         * vertically centered instead of clamping to the top edge. */
        CLAY({.id = CLAY_ID("root"),
              .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                         .padding = CLAY_PADDING_ALL(20),
                         .layoutDirection = CLAY_TOP_TO_BOTTOM,
                         .childGap = 18,
                         .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
              .backgroundColor = {18.0F, 18.0F, 22.0F, 255.0F}}) {
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), status_text, &g_status_style);
            }

            /* Wrap the grid in the runtime transform. The accumulated transform
             * feeds the engine's inverse-affine hit-test (D-56-07) so every
             * variant stays clickable even rotated 30 deg. */
            nt_ui_push_transform(s_ctx, &row_xform);
            declare_reference_buttons();
            nt_ui_pop_transform(s_ctx);

            /* Help bar pinned at the bottom. */
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                nt_ui_label(s_ctx, NT_UI_DATA_LAYER(LAYER_TEXT), help_text, &g_help_style);
            }
        }
        // #endregion

        nt_ui_end(s_ctx);

        nt_ui_target_t target = nt_ui_scale_make_target(&scale);
        nt_ui_walk(s_ctx, &target);

        // #region nt_ui_inspector (single-element hit-zone overlay)
        /* The inspector panel itself was already emitted into the layout pass
         * inside nt_ui_end. The post-walk call here paints a hit-zone +
         * id label for the ONE element the user is focused on (sidebar
         * hover/click OR viewport hover via Clay's pointerOver). One key
         * (D) toggles the whole system. */
        nt_ui_inspector_overlay_draw(s_ctx, &target, s_font, 16.0F);
        // #endregion

        // #region stats overlay
        {
            mat4 stats_model;
            glm_mat4_identity(stats_model);
            glm_translate(stats_model, (vec3){10.0F, scale.logical_h - 20.0F, 0.0F});
            const float stats_color[4] = {0.8F, 0.9F, 0.8F, 1.0F};
            nt_stats_draw(s_text_material, s_font, (const float *)stats_model, 16.0F, stats_color);
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
    config.app_name = "ui_buttons_demo";
    config.version = 1;

    if (nt_engine_init(&config) != NT_OK) {
        return 1;
    }

    /* Bigger logical window: 1600x1200 so the 2 x 3 grid + BAKED TRANSFORM
     * section + status/help bars all fit without scaling. */
    g_nt_window.width = 1600;
    g_nt_window.height = 1200;
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
    NT_ASSERT(s_ctx != NULL && "ui_buttons_demo: failed to create UI context");

    g_nt_app.target_dt = 0.0F;

    s_frame_ubo = nt_gfx_make_buffer(&(nt_buffer_desc_t){
        .type = NT_BUFFER_UNIFORM,
        .usage = NT_USAGE_DYNAMIC,
        .size = sizeof(nt_frame_uniforms_t),
        .label = "frame_uniforms",
    });

    s_pack_id = nt_hash32_str("ui_buttons_demo");
    nt_resource_mount(s_pack_id, 100);
#ifdef NT_CDN_URL
    nt_resource_load_auto(s_pack_id, NT_CDN_URL "/ui_buttons_demo/ui_buttons_demo.ntpack");
#else
    nt_resource_load_auto(s_pack_id, "assets/ui_buttons_demo.ntpack");
#endif

    s_sprite_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_VERT, NT_ASSET_SHADER_CODE);
    s_sprite_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SPRITE_FRAG, NT_ASSET_SHADER_CODE);
    s_text_vs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_VERT, NT_ASSET_SHADER_CODE);
    s_text_fs_handle = nt_resource_request(ASSET_SHADER_ASSETS_SHADERS_SLUG_TEXT_FRAG, NT_ASSET_SHADER_CODE);
    s_atlas_handle = nt_resource_request(ASSET_ATLAS_UI_BUTTONS_DEMO_ATLAS, NT_ASSET_ATLAS);
    s_atlas_tex_handle = nt_resource_request(ASSET_TEXTURE_UI_BUTTONS_DEMO_ATLAS_TEX0, NT_ASSET_TEXTURE);
    s_font_resource = nt_resource_request(ASSET_FONT_UI_BUTTONS_DEMO_FONT, NT_ASSET_FONT);

    s_sprite_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_sprite_vs_handle,
        .fs = s_sprite_fs_handle,
        .textures = {{.name = "u_texture", .resource = s_atlas_tex_handle}},
        .texture_count = 1,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "ui_buttons_demo_sprite",
    });
    s_text_material = nt_material_create(&(nt_material_create_desc_t){
        .vs = s_text_vs_handle,
        .fs = s_text_fs_handle,
        .blend_mode = NT_BLEND_MODE_ALPHA,
        .depth_test = false,
        .depth_write = false,
        .cull_mode = NT_CULL_NONE,
        .label = "ui_buttons_demo_text",
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

    nt_log_info("ui_buttons_demo: starting (D=inspector, arrows/PgUp-Dn/Q-E/R transform, Esc quit)");

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
