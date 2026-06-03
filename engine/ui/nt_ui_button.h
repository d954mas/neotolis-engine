#ifndef NT_UI_BUTTON_H
#define NT_UI_BUTTON_H

/* Interactive container button. Near-clone of nt_ui_panel with a Clay element
 * .id so the engine hit-test finds it. Auto state machine + eased visuals
 * applied via userData; content (label / icon / icon+text) composes as children. */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

/* Inspector descriptor; "nt_" prefix disambiguates from Clay's own config-type pills. */
extern const nt_ui_widget_def_t NT_UI_BUTTON_DEF;

/* Per-state container visual. Layout/sizing/padding live on the Clay begin
 * element, not on the style — matches panel. */
typedef struct {
    uint32_t bg_region; /* slice9 region; 0 = same as idle */
    uint32_t bg_tint;   /* 0xAABBGGRR */
    float scale;        /* render scale, e.g. pressed 0.95 */
    float offset_x, offset_y;
    float opacity; /* inherits to content */
} nt_ui_btn_state_t;

typedef struct {
    nt_ui_btn_state_t idle, hover, pressed, disabled;
    float transition_speed; /* 0 = instant; >0 = eased via anim cache */
    /* Touch-target inflation {l,r,t,b} in layout pixels. Inflated BEFORE the
     * inverse-affine so the padded zone rotates with the widget. */
    int16_t hit_padding_lrtb[4];
    /* Multiplies the atlas region's baked slice9 borders; applies to every state.
     * 1.0F preserves atlas-verbatim render. <=0 is asserted in button_begin. */
    float slice9_scale;
} nt_ui_button_style_t;

/* begin → children → bool end. enabled=false short-circuits hover/click and
 * forces the disabled visual. `decl` is optional (NULL = FIT default). Engine
 * OWNS .id/.image/.backgroundColor/.userData — caller leaves them zero/NULL.
 * For state-dependent content, query BEFORE button_begin to pick label/icon. */
void nt_ui_button_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, nt_resource_t atlas, const nt_ui_button_style_t *style, const Clay_ElementDeclaration *decl, bool enabled);
bool nt_ui_button_end(nt_ui_context_t *ctx);

#endif /* NT_UI_BUTTON_H */
