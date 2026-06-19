#ifndef NT_UI_DROPDOWN_H
#define NT_UI_DROPDOWN_H

/* Dropdown / combobox built ON popup-core + the scroll wrapper. A trigger button shows the current
 * selection; clicking it toggles a game-owned `bool open`. The open list is a popup-core floating
 * anchored to the trigger's bottom-left with edge-flip up near the bottom border; its rows are plain
 * rects + labels. A row click writes the row index into the game-owned `int *selected` and raises close.
 * A long list is wrapped in nt_ui_scroll_begin/end, NOT a raw Clay .clip — a raw .clip leaks a
 * scroll-container pool slot (type=7 crash).
 *
 * The GAME owns `int *selected` and `bool *open`; the widget only signals.
 *
 * Customizable game UI (button/checkbox/tabbar parity): the trigger + rows are per-state visual models
 * (idle/hover/pressed, + selected for rows) with an OPTIONAL atlas-ref bg (slice9), a tint, and a flat
 * fallback color — atlas-free still looks good. Hover/press/selected ease via nt_ui_anim through the
 * data->layer xform channel. The trigger carries an OPTIONAL chevron sprite that eases (rotation) with
 * the open state. Rows support a UNIFIED icon model: a leading icon gutter (style->icon_size) the text
 * aligns past, with an OPTIONAL per-row icon. */

#include <stdbool.h>
#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "ui/nt_ui.h"        /* nt_ui_layer_t, nt_ui_widget_def_t, nt_ui_element_data_t */
#include "ui/nt_ui_scroll.h" /* nt_ui_scroll_style_t (embedded list_scroll) */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_DROPDOWN_DEF;

/* One trigger/row visual state. bg is an atomic nt_atlas_region_ref_t:
 *   non-idle atlas.id==0 -> inherit the idle state's WHOLE ref (region 0 is a valid index, so it can't
 *   double as a per-field sentinel); idle atlas.id==0 -> NO ART, fall back to the flat `fill` color.
 * `fill` is the flat fallback rect color (0xAABBGGRR; 0 = transparent). `bg_tint` multiplies the atlas
 * art (0xFFFFFFFF = no tint). scale/opacity are the eased render-only transform via the data->layer
 * xform channel. */
typedef struct {
    nt_atlas_region_ref_t bg; /* atomic ref; atlas.id==0 = inherit idle.bg whole (idle = no art -> flat) */
    uint32_t fill;            /* flat fallback rect color 0xAABBGGRR (0 = transparent) */
    uint32_t bg_tint;         /* multiplies atlas art; 0xFFFFFFFF = no tint */
    float scale;              /* eased render-only scale (1.0 = none) */
    float opacity;            /* eased render-only opacity [0,1] */
} nt_ui_dd_state_t;
_Static_assert(sizeof(nt_ui_dd_state_t) == 32, "nt_ui_dd_state_t stable ABI (16 ref + 2 u32 + 2 float)");

/* Visual knobs. Colors are 0xAABBGGRR. The trigger + rows are per-state models; the list panel is an
 * optional slice9 (panel_bg ref) with a flat panel_fill fallback. layer comes from the call
 * (data->layer), NOT the style — mirrors checkbox/tabbar. */
typedef struct {
    nt_ui_dd_state_t trigger_idle, trigger_hover, trigger_pressed;   /* trigger per-state look */
    nt_ui_dd_state_t row_idle, row_hover, row_pressed, row_selected; /* row per-state look */
    nt_atlas_region_ref_t panel_bg;                                  /* optional list-panel slice9 art; atlas.id==0 = flat panel_fill */
    nt_atlas_region_ref_t chevron;                                   /* optional trigger affordance sprite (drawn at the right edge) */
    nt_ui_scroll_style_t list_scroll;                                /* game owns the long-list scroll feel + bar; widget keeps sizing/panel-art */
    uint32_t panel_fill;                                             /* flat panel fallback color 0xAABBGGRR (0 = transparent) */
    uint32_t panel_tint;                                             /* multiplies the panel slice9 art; 0xFFFFFFFF = no tint */
    uint32_t trigger_text;                                           /* trigger label color */
    uint32_t row_text;                                               /* enabled row text color */
    uint32_t chevron_tint;                                           /* chevron sprite tint 0xAABBGGRR */
    float font_size;                                                 /* px; asserted > 0 */
    float slice9_scale;                                              /* multiplies the atlas region's baked slice9 borders; > 0 */
    float state_speed;                                               /* eases hover/press/selected scale+opacity (0 = instant) */
    float value_speed;                                               /* eases the chevron open-rotation (0 = instant) */
    float open_ease_speed;                                           /* popup open/close tween speed (0 = snap; game opts into a tween) */
    uint16_t row_height;                                             /* px list row height */
    uint16_t min_width;                                              /* px list panel min width */
    uint16_t pad;                                                    /* px inner padding */
    uint16_t font_id;                                                /* label font */
    uint16_t max_visible_rows;                                       /* rows shown before the list scrolls (0 = no cap, never scrolls) */
    uint16_t icon_size;                                              /* px leading icon gutter (0 = no gutter / text-only rows) */
    uint16_t chevron_size;                                           /* px chevron sprite box (0 = no chevron even if a ref is set) */
    uint16_t panel_corner_radius;                                    /* px panel rounding (flat fallback only; IMAGE bg can't round) */
} nt_ui_dropdown_style_t;
_Static_assert(sizeof(nt_ui_dropdown_style_t) == 400, "nt_ui_dropdown_style_t stable ABI (7x32 state + 2x16 ref + 88 scroll + 5 u32 + 5 float + 8 u16 + tail pad)");

/* Valid baseline style (dark) that looks polished with flat colors and NO atlas art (wire refs to opt in). */
nt_ui_dropdown_style_t nt_ui_dropdown_style_defaults(void);

/* ---- Immediate combo API (begin/selectable/end) — mirrors the menu core. ----
 * The GAME owns `int *selected` (writes it on a selectable's clicked return) + `bool *open`; the combo
 * only signals + clears *open on a row click (Model-D). Combos do NOT nest (asserted).
 * Layers: every combo fill (trigger, rows, panel, chevron, scrollbar) draws on data->layer; trigger +
 * row text on label_layer (split to batch fills-then-text). data may be NULL (fills fall to layer 0).
 * The popup-nested scrollbar rides the SAME fill layer as the panel and floats above it (floating draws
 * over same-layer siblings), so the band ordering holds at any base the game picks. Mirrors menu/checkbox/
 * tabbar: layer comes from the CALL, NOT the style. Row + label ids are fmix-derived over
 * (combo id, key, row_idx) — never additive. */

/* Trigger + list root: emits the trigger button (showing `preview`, the game-fed current-selection
 * label) and, while *open (or still tweening closed), opens the popup list for the frame. A click on
 * the trigger toggles *open. Returns "declare the selectables this frame?" — true while the list body
 * should be built. Call combo_selectable rows between this and nt_ui_combo_end. id non-zero; preview may
 * be NULL (empty trigger text); style/open non-NULL. data carries the fill layer; text on label_layer. */
bool nt_ui_combo_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *preview, nt_ui_dropdown_style_t *style, bool *open);

/* Custom-trigger escape hatch: leaves the TRIGGER element OPEN so the game declares arbitrary content
 * (swatch+text, icon+label) inside, between preview_begin and preview_end. preview_end closes the trigger,
 * does its one step_interaction (toggling *open on a click), THEN opens the list and returns "declare the
 * selectables this frame?" (true while the list body should be built). data/label_layer set the combo's
 * fill/text layers (threaded through to the list rows). Usage:
 *   nt_ui_combo_preview_begin(ctx, NT_UI_DATA_LAYER(LAYER_IMG), LAYER_TEXT, id, &style, &open);
 *   ... game trigger content ...
 *   if (nt_ui_combo_preview_end(ctx)) { ...selectables...; nt_ui_combo_end(ctx); } */
void nt_ui_combo_preview_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, nt_ui_dropdown_style_t *style, bool *open);
bool nt_ui_combo_preview_end(nt_ui_context_t *ctx);

/* One list row: a per-state rect + label (row_selected look when `selected`). Returns true on the frame
 * it was clicked — the GAME writes its own *selected; the combo clears *open. `key` need only be unique
 * among sibling rows (the combo derives the pool id via mix(id,key,row_idx)). */
bool nt_ui_combo_selectable(nt_ui_context_t *ctx, uint32_t key, const char *label, bool selected);

/* Iconed row: same single engine column as the plain selectable — the icon is drawn in the leading
 * gutter (style->icon_size) and the label is left-aligned past it, so iconed and text-only rows align
 * their labels at the SAME x. Requires style->icon_size > 0 to show a gutter. `icon` non-NULL; pass NULL
 * to nt_ui_combo_selectable instead for an explicitly empty-gutter row. */
bool nt_ui_combo_selectable_icon(nt_ui_context_t *ctx, uint32_t key, const nt_atlas_region_ref_t *icon, const char *label, bool selected);

/* Custom-content row: opens the row element OPEN for the game's children (icon+label, badges). _end
 * closes it + does the row's one step_interaction. _begin returns the row's prev-frame clicked (the game
 * writes *selected); the combo clears *open on the click in _end. `selected` drives the per-state look. */
bool nt_ui_combo_selectable_begin(nt_ui_context_t *ctx, uint32_t key, bool selected);
void nt_ui_combo_selectable_end(nt_ui_context_t *ctx);

/* Close the list body (the scroll wrapper or the plain panel) + balance the popup. Pairs combo_begin. */
void nt_ui_combo_end(nt_ui_context_t *ctx);

#ifdef NT_TEST_ACCESS
/* The popup side chosen for the combo list on its last open (edge-flip probe). */
uint8_t nt_ui_dropdown_test_last_side(void);
/* The scroll id the list used for its long-list wrapper (0 if the list did not scroll). */
uint32_t nt_ui_dropdown_test_scroll_id(uint32_t dropdown_id);
/* The prev-frame bbox of a row's text label cell (icon-gutter alignment probe). */
nt_ui_bbox_t nt_ui_dropdown_test_row_label_bbox(const nt_ui_context_t *ctx, uint32_t dropdown_id, int idx);
/* The prev-frame bbox of the custom-trigger (preview form) chevron element (found => the chevron was drawn). */
nt_ui_bbox_t nt_ui_dropdown_test_chevron_bbox(const nt_ui_context_t *ctx, uint32_t dropdown_id);
#endif

#endif /* NT_UI_DROPDOWN_H */
