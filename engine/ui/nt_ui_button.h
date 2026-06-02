#ifndef NT_UI_BUTTON_H
#define NT_UI_BUTTON_H

/* Interactive container button. Near-clone of nt_ui_panel with one structural
 * addition: a Clay element .id so the engine hit-test finds it. Runs the state
 * machine automatically — step interaction, pick state (disabled→pressed→hover→
 * idle), ease scale/offset/opacity via the anim cache, apply via push_transform
 * + push_opacity. Content (label / icon / icon+text) composes as children. */

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
} nt_ui_button_style_t;

/* begin → children → bool end. enabled=false short-circuits hover/click and
 * forces the disabled visual; transform/opacity still push so end stays balanced.
 *
 * `decl` is optional (NULL = FIT default). Engine OWNS .id/.image/.backgroundColor/.userData
 * — caller must leave them zero/NULL.
 *
 * State-dependent content: query (pure read) BEFORE button_begin in the SAME
 * transform+clip scope, then use it to pick label/icon.
 *
 *   nt_ui_interaction_t in = nt_ui_query_interaction(ctx, btn_id);
 *   nt_ui_button_begin(ctx, ..., btn_id, atlas, &style, &decl, true);
 *     nt_ui_label(ctx, NULL, in.pressed ? "Saving..." : "Save", &lbl);
 *   clicked = nt_ui_button_end(ctx);
 */
void nt_ui_button_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, nt_resource_t atlas, const nt_ui_button_style_t *style, const Clay_ElementDeclaration *decl, bool enabled);
bool nt_ui_button_end(nt_ui_context_t *ctx);

#endif /* NT_UI_BUTTON_H */
