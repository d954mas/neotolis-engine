#ifndef NT_UI_DROPDOWN_H
#define NT_UI_DROPDOWN_H

/* Dropdown / combobox (WGT-01) built ON popup-core (Plan 03) + the Phase-59 scroll wrapper. A trigger
 * button shows the current selection; clicking it toggles a game-owned `bool open`. The open list is a
 * popup-core floating anchored to the trigger's bottom-left with edge-flip up near the bottom border;
 * its rows are plain rects + labels. A row click writes the row index into the game-owned `int *selected`
 * and raises close. A long list is wrapped in nt_ui_scroll_begin/end (NOT a raw Clay .clip, which leaks
 * a scroll-container pool slot -> type=7 crash, Pitfall 7).
 *
 * Model D: the GAME owns `int *selected` and `bool *open`; the widget only signals.
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
#include "ui/nt_ui.h" /* nt_ui_layer_t, nt_ui_widget_def_t, nt_ui_element_data_t */

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
    uint32_t panel_fill;                                             /* flat panel fallback color 0xAABBGGRR (0 = transparent) */
    uint32_t panel_tint;                                             /* multiplies the panel slice9 art; 0xFFFFFFFF = no tint */
    uint32_t trigger_text;                                           /* trigger label color */
    uint32_t row_text;                                               /* enabled row text color */
    uint32_t chevron_tint;                                           /* chevron sprite tint 0xAABBGGRR */
    float font_size;                                                 /* px; asserted > 0 */
    float slice9_scale;                                              /* multiplies the atlas region's baked slice9 borders; > 0 */
    float state_speed;                                               /* eases hover/press/selected scale+opacity (0 = instant) */
    float value_speed;                                               /* eases the chevron open-rotation (0 = instant) */
    uint16_t row_height;                                             /* px list row height */
    uint16_t min_width;                                              /* px list panel min width */
    uint16_t pad;                                                    /* px inner padding */
    uint16_t font_id;                                                /* label font */
    uint16_t max_visible_rows;                                       /* rows shown before the list scrolls (0 = no cap, never scrolls) */
    uint16_t icon_size;                                              /* px icon gutter width (0 = no gutter / text-only rows) */
    uint16_t chevron_size;                                           /* px chevron sprite box (0 = no chevron even if a ref is set) */
    uint16_t panel_corner_radius;                                    /* px panel rounding (flat fallback only; IMAGE bg can't round) */
} nt_ui_dropdown_style_t;
_Static_assert(sizeof(nt_ui_dropdown_style_t) == 312, "nt_ui_dropdown_style_t stable ABI (7x32 state + 2x16 ref + 5 u32 + 4 float + 8 u16 + 4 tail pad)");

/* Valid baseline style (dark) that looks polished with flat colors and NO atlas art (wire refs to opt in). */
nt_ui_dropdown_style_t nt_ui_dropdown_style_defaults(void);

/* Trigger button: shows the label of the current *selected (or `placeholder` when out of range / -1)
 * and toggles *open on click. Declared like any other widget inside the layout. id/labels/selected/
 * open/style non-NULL; count >= 0; *selected in [-1,count). Returns true on the frame it toggled.
 * Layers: the trigger fill + chevron draw on data->layer, its label on label_layer (split to batch).
 * data may be NULL (fill falls to layer 0). style is mutated in place to memoize resolved refs. */
bool nt_ui_dropdown_trigger(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *const *labels, int count, int selected, const char *placeholder,
                            nt_ui_dropdown_style_t *style, const Clay_ElementDeclaration *decl, bool *open);

/* The open list: a popup-core floating anchored to the trigger's bbox (queried by `id`) with edge-flip
 * up near the bottom border. Rows are per-state rects; a row click sets *selected and clears *open. A
 * list taller than style->max_visible_rows is wrapped in nt_ui_scroll (GC'd; no clip leak). Call every
 * frame AFTER the trigger; it self-balances when fully closed (no end needed by the caller). id/labels/
 * selected/open/style non-NULL; *selected in [-1,count). Returns true if a selection was made this frame.
 *
 * `icons` is an OPTIONAL parallel array (length `count`, or NULL = text-only). When style->icon_size > 0
 * each row reserves a leading gutter of icon_size px so text stays aligned; the icon is drawn if its ref
 * is set, else the gutter is left empty (OS-menu icon-column behavior). NULL `icons` with icon_size > 0
 * still reserves an aligned-empty gutter on every row.
 *
 * Layers: the panel + row fills + icons draw on data->layer (also the popup panel layer), row text on
 * label_layer (split to batch). data may be NULL (fills fall to layer 0). style is mutated in place. */
bool nt_ui_dropdown_list(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *const *labels, const nt_atlas_region_ref_t *icons, int count,
                         int *selected, nt_ui_dropdown_style_t *style, bool *open);

#ifdef NT_TEST_ACCESS
/* The popup side chosen for the list on the last nt_ui_dropdown_list call (edge-flip probe). */
uint8_t nt_ui_dropdown_test_last_side(void);
/* The scroll id the list used for its long-list wrapper (0 if the list did not scroll). */
uint32_t nt_ui_dropdown_test_scroll_id(uint32_t dropdown_id);
/* The prev-frame bbox of a row's text label cell (icon-gutter alignment probe). */
nt_ui_bbox_t nt_ui_dropdown_test_row_label_bbox(const nt_ui_context_t *ctx, uint32_t dropdown_id, int idx);
#endif

#endif /* NT_UI_DROPDOWN_H */
