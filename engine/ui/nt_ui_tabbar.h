#ifndef NT_UI_TABBAR_H
#define NT_UI_TABBAR_H

/* Reusable tab-bar. A column (or row) of full-width click targets; the active tab carries an accent bar
 * + a selected fill, hovered tabs lighten and ease. A click sets the game-owned `int *active` — the
 * widget never owns the index. No popup, no gesture cell: a tab is a plain click.
 *
 * Customizable game UI (button/checkbox parity): each tab visual is a per-state model (idle/hover/
 * selected) with an OPTIONAL atlas-ref bg (slice9), a tint, and a flat fallback color — atlas-free still
 * looks good. Hover/selected ease via nt_ui_anim through the data->layer xform channel. */

#include <stdbool.h>
#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "ui/nt_ui.h" /* nt_ui_layer_t, nt_ui_widget_def_t, nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_TABBAR_DEF;

/* Layout direction of the bar. Vertical = a left-side nav list (the showcase look); horizontal = a
 * top tab strip. The accent bar's edge is configurable via style->accent_side (default LEFT). */
typedef enum { NT_UI_TABBAR_VERTICAL = 0, NT_UI_TABBAR_HORIZONTAL } nt_ui_tabbar_dir_t;

/* Which edge the active-tab accent bar grows on. NONE disables the accent entirely. Independent of dir
 * so a horizontal strip can underline (BOTTOM) while a vertical nav marks the leading LEFT edge. */
typedef enum { NT_UI_TABBAR_ACCENT_NONE = 0, NT_UI_TABBAR_ACCENT_LEFT, NT_UI_TABBAR_ACCENT_RIGHT, NT_UI_TABBAR_ACCENT_TOP, NT_UI_TABBAR_ACCENT_BOTTOM } nt_ui_tabbar_accent_t;

/* One tab visual state (idle/hover/selected). bg is an atomic nt_atlas_region_ref_t:
 *   non-idle atlas.id==0 -> inherit the idle state's WHOLE ref (region 0 is a valid index, so it can't
 *   double as a per-field sentinel); idle atlas.id==0 -> NO ART, fall back to the flat `fill` color.
 * `fill` is the flat fallback rect color (0xAABBGGRR; 0 = transparent, bar bg shows through).
 * `bg_tint` multiplies the atlas art (0xFFFFFFFF = no tint). scale/opacity are the eased render-only
 * transform applied through the data->layer xform channel. */
typedef struct {
    nt_atlas_region_ref_t bg; /* atomic ref; atlas.id==0 = inherit idle.bg whole (idle = no art -> flat) */
    uint32_t fill;            /* flat fallback rect color 0xAABBGGRR (0 = transparent) */
    uint32_t bg_tint;         /* multiplies atlas art; 0xFFFFFFFF = no tint */
    float scale;              /* eased render-only scale (1.0 = none) */
    float opacity;            /* eased render-only opacity [0,1] */
} nt_ui_tab_state_t;
_Static_assert(sizeof(nt_ui_tab_state_t) == 32, "nt_ui_tab_state_t stable ABI (16 ref + 2 u32 + 2 float)");

/* Visual knobs. Colors are 0xAABBGGRR. Per-tab art/fill lives in the idle/hover/selected states. */
typedef struct {
    nt_ui_tab_state_t idle, hover, selected; /* per-state bg ref + flat fill + tint + eased scale/opacity */
    uint32_t bar_bg;                         /* the bar container background (0 = transparent) */
    uint32_t accent;                         /* active-tab accent bar color */
    uint32_t text;                           /* unselected tab text */
    uint32_t text_selected;                  /* selected tab text */
    float font_size;                         /* px; asserted > 0 */
    float slice9_scale;                      /* multiplies the atlas region's baked slice9 borders; > 0 */
    float state_speed;                       /* eases hover/selected scale+opacity (0 = instant) */
    float value_speed;                       /* eases the selected accent-bar grow-in (0 = instant) */
    uint16_t tab_extent;                     /* px height (vertical) or width (horizontal) of each tab */
    uint16_t accent_px;                      /* px accent-bar thickness on the leading edge */
    uint16_t corner_radius;                  /* px tab corner rounding */
    uint16_t pad;                            /* px inner padding */
    uint16_t gap;                            /* px gap between tabs */
    uint16_t font_id;                        /* label font */
    uint16_t icon_size;                      /* px leading icon gutter (0 = no gutter / text-only tabs) */
    uint8_t dir;                             /* nt_ui_tabbar_dir_t */
    uint8_t accent_side;                     /* nt_ui_tabbar_accent_t (NONE disables the accent) */
} nt_ui_tabbar_style_t;
_Static_assert(sizeof(nt_ui_tabbar_style_t) == 144, "nt_ui_tabbar_style_t stable ABI (3x32 state + 4 u32 + 4 float + 7 u16 + 1 dir + 1 accent_side)");

/* Valid baseline style (dark, vertical) that looks polished with flat colors and NO atlas art. */
nt_ui_tabbar_style_t nt_ui_tabbar_style_defaults(void);

/* ---- begin/end core (button parity) ----
 * The ENGINE owns the styled per-state bg sprite (eased scale/opacity), the accent bar, and the click
 * interaction; the GAME owns each tab's content (icon + text, a different icon when selected, badges).
 *
 * Usage:
 *   nt_ui_tabbar_begin(ctx, data, label_layer, base_id, style);
 *   for (i ...) {
 *       bool clicked = nt_ui_tab_begin(ctx, i, i == active);   // tab element is now OPEN
 *       // declare content children here (nt_ui_image on the fill layer, nt_ui_label on label_layer)
 *       nt_ui_tab_end(ctx);
 *   }
 *   nt_ui_tabbar_end(ctx);
 *
 * Layers: bar bg + tab fills/icons draw on data->layer; labels on label_layer (split batches fills-then-
 * text). data may be NULL (fills fall to layer 0). style is mutated in place to memoize resolved refs. */
void nt_ui_tabbar_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t base_id, nt_ui_tabbar_style_t *style);

/* Opens ONE tab element (id = mixed hash of base_id + index, collision-proof vs Clay anon child ids):
 * picks idle/hover/selected, runs one nt_ui_anim
 * (scale/opacity + accent value_t), emits the per-state bg (slice9 when an atlas ref is set, flat fill
 * otherwise) + the accent on style->accent_side, applies the eased xform, and leaves the element OPEN
 * for the game's content children. Returns true the frame this tab was clicked. */
bool nt_ui_tab_begin(nt_ui_context_t *ctx, int index, bool active);

/* Runs the open tab's step_interaction, closes the element. Pair with each nt_ui_tab_begin. */
void nt_ui_tab_end(nt_ui_context_t *ctx);

/* Closes the bar container opened by nt_ui_tabbar_begin. */
void nt_ui_tabbar_end(nt_ui_context_t *ctx);

/* Declare the tab-bar: a container holding `count` full-extent tabs. A click on tab i sets *active = i.
 * base_id salts each tab's id via a mixed hash. The bar grows to fill its parent on the cross
 * axis. ctx/labels/active/style non-NULL; count >= 0; *active in [0,count) when count > 0 (or -1 = none).
 * Returns the index clicked this frame, or -1 if no tab was clicked.
 *
 * `icons` is an OPTIONAL parallel array (length `count`, or NULL = text-only). When style->icon_size > 0
 * each tab reserves a leading gutter of icon_size px so labels stay aligned; the icon is drawn if its ref
 * is set, else the gutter is left empty (OS-menu icon-column behavior). NULL `icons` with icon_size > 0
 * still reserves an aligned-empty gutter on every tab. icon_size==0 -> no gutter at all (pure text). This
 * mirrors nt_ui_dropdown_list's icons[] one-function model; the begin/end core stays the fully-custom path.
 *
 * Layers: the bar bg + tab fills + icons draw on data->layer; the tab labels on label_layer -- pass them
 * split (fills on the img layer, text on the text layer) to batch fills-then-text in one segment. data may
 * be NULL (fills fall to layer 0). */
int nt_ui_tabbar(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t base_id, const char *const *labels, const nt_atlas_region_ref_t *icons, int count, int *active,
                 nt_ui_tabbar_style_t *style);

#ifdef NT_TEST_ACCESS
/* The prev-frame bbox of a wrapper tab's text label cell (icon-gutter alignment probe; mirrors the
 * dropdown row-label probe). Only the convenience wrapper declares the label cell. */
nt_ui_bbox_t nt_ui_tabbar_test_label_bbox(const nt_ui_context_t *ctx, uint32_t base_id, int idx);
#endif

#endif /* NT_UI_TABBAR_H */
