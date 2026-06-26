#ifndef NT_UI_CHECKBOX_H
#define NT_UI_CHECKBOX_H

/* Stateful boolean / single-choice widgets sharing one internal core (cb_core).
 * Value lives in the GAME (bool* / int*); the engine stores no logical value --
 * only the transient eased visual via nt_ui_anim. */

#include <stdbool.h>
#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui.h"       /* nt_ui_element_data_t */
#include "ui/nt_ui_label.h" /* nt_ui_label_style_t */

typedef struct nt_ui_context nt_ui_context_t;

/* "nt_" prefix disambiguates from Clay's own config-type pills. One def per widget
 * so the inspector labels radio/toggle distinctly (not all as "nt_checkbox"). */
extern const nt_ui_widget_def_t NT_UI_CHECKBOX_DEF;
extern const nt_ui_widget_def_t NT_UI_RADIO_DEF;
extern const nt_ui_widget_def_t NT_UI_TOGGLE_DEF;
extern const nt_ui_widget_def_t NT_UI_CHECKBOX_TRI_DEF;

/* Tristate value (game-owned). MIXED is a DISPLAY-only indeterminate state set by
 * the game (aggregated from children); a click never reaches MIXED -- it resolves
 * any non-ON value to ON, then toggles ON<->OFF. */
typedef enum { NT_UI_TRI_OFF = 0, NT_UI_TRI_ON, NT_UI_TRI_MIXED } nt_ui_tristate_t;

/* 2x4 grid index: value(unchecked/checked) x interaction. */
enum { NT_UI_CB_IDLE = 0, NT_UI_CB_HOVER, NT_UI_CB_PRESSED, NT_UI_CB_DISABLED };

/* One cell of the 2x4 grid. box/check are atomic nt_atlas_region_ref_t:
 *   non-idle atlas.id==0 -> inherit the value-row idle cell's WHOLE ref (region 0 is
 *   a valid index, so it can't double as a per-field sentinel);
 *   idle terminal atlas.id==0 -> NO ART (skip that part's IMAGE). */
typedef struct {
    nt_atlas_region_ref_t box;   /* indicator / track background */
    nt_atlas_region_ref_t check; /* overlay sprite (checkmark / dot / thumb) */
    /* Per-part LOCAL packed colors. 0xFFFFFFFF = no tint; text_color 0 = inherit text_base.color. */
    uint32_t box_tint, check_tint, text_color;
    float scale, offset_x, offset_y; /* whole-widget render-only transform */
    float opacity;                   /* whole-widget, INHERITS to children */
} nt_ui_cb_state_t;
_Static_assert(sizeof(nt_ui_cb_state_t) == 64, "nt_ui_cb_state_t stable ABI (2x16 ref + 3 tint + 4 float + 8B align pad)");

/* ONE shared style for all three widgets (strict superset). Layout
 * sizing/padding live on the Clay decl; the FIXED box_w/box_h size only the
 * indicator/track, never the row. */
typedef struct {
    nt_ui_cb_state_t unchecked[4]; /* idle/hover/pressed/disabled */
    nt_ui_cb_state_t checked[4];
    nt_ui_cb_state_t mixed[4];     /* tristate MIXED dash row; inherit-from-idle like the others */
    nt_ui_label_style_t text_base; /* font/size/color/align/wrap -- parts that DON'T vary by state */
    float box_w, box_h;            /* indicator/track FIXED layout px; asserted > 0 */
    float overlay_w, overlay_h;    /* overlay px (set to art aspect) */
    float thumb_pad;               /* TOGGLE only: symmetric L/R end-margin */
    float gap;                     /* box<->text spacing px */
    uint8_t label_side;            /* 0 = text right, 1 = text left */
    bool scale_label;              /* false (default) = press/hover scale only the indicator; true = whole row incl. label. Opacity/dim is ALWAYS whole-widget. */
    float state_speed;             /* eases hover/press/disabled (nt_ui_anim state group) */
    float value_speed;             /* eases overlay pop / thumb slide (nt_ui_anim value_t); 0 = instant */
} nt_ui_checkbox_style_t;
/* scale_label occupies one of label_side's former tail padding bytes -> size unchanged.
 * Layers are NOT in the style (mirrors button/label): the indicator layer comes from
 * data->layer, the label layer from the label_layer function arg. */
_Static_assert(sizeof(nt_ui_checkbox_style_t) == 840, "nt_ui_checkbox_style_t stable ABI (+mixed[4] = +4*64)");

/* All three are LEAF widgets (no begin/end). Returns `changed` = the frame the value
 * flipped (checkbox/toggle: *value = !*value; radio: *selected = my_value).
 * Box OR label is one clickable id; label == NULL = indicator only. On the release
 * frame the widget renders the PRE-flip value (it returns `changed` AFTER drawing),
 * so the pop/slide begins the next frame -- standard 1-frame IM lag, accepted.
 *
 * Layers: the indicator draws on data->layer; the label on label_layer -- pass them
 * equal for a single-layer widget, or split to batch sprite-then-text. value_t uncheck
 * fade-out only plays if the unchecked row also carries check art; else the overlay
 * drops the frame the value flips. Radio writes *selected immediately, so a newly-
 * selected sibling can render checked one frame before the old renders unchecked.
 *
 * Engine OWNS .id/.image/.backgroundColor/.userData on the decl AND forces the row's
 * layoutDirection + childAlignment; the caller's decl supplies sizing/padding only.
 * data->flags must NOT set HAS_TRANSFORM/HAS_OPACITY (the widget owns these).
 * box_w/box_h > 0 required; each value row needs box.idle OR check.idle non-zero.
 * enabled=false short-circuits interaction + forces the disabled cell. */
bool nt_ui_checkbox(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *label, bool *value, nt_ui_checkbox_style_t *style,
                    const Clay_ElementDeclaration *decl, bool enabled);

/* Exclusive single-choice over a shared int*. changed when *selected flips to my_value. */
bool nt_ui_radio(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *label, int *selected, int my_value, nt_ui_checkbox_style_t *style,
                 const Clay_ElementDeclaration *decl, bool enabled);

/* Sliding-thumb boolean. Same return contract as checkbox; the thumb x is a
 * render-only offset DELTA eased by value_t. */
bool nt_ui_toggle(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *label, bool *value, nt_ui_checkbox_style_t *style,
                  const Clay_ElementDeclaration *decl, bool enabled);

/* Tristate checkbox: OFF / ON / MIXED. MIXED renders the `mixed` dash row but is
 * NEVER produced by a click -- a click resolves any non-ON value to ON, else
 * toggles ON->OFF. Same return contract as checkbox (true on the flip frame). */
bool nt_ui_checkbox_tri(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *label, nt_ui_tristate_t *value, nt_ui_checkbox_style_t *style,
                        const Clay_ElementDeclaration *decl, bool enabled);

/* A valid baseline style: every cell scale/opacity = 1, no tint, sensible sizes and
 * speeds. The caller still supplies art (box/check refs) and overrides any field.
 * Avoids the zero-init trap (a {0} style trips the cell scale > 0 assert). */
nt_ui_checkbox_style_t nt_ui_checkbox_style_defaults(void);

#endif /* NT_UI_CHECKBOX_H */
