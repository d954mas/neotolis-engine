#ifndef NT_UI_BUTTON_H
#define NT_UI_BUTTON_H

/* Interactive container button. Auto state machine + eased visuals; content composes as children. */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

/* "nt_" prefix disambiguates from Clay's own config-type pills. */
extern const nt_ui_widget_def_t NT_UI_BUTTON_DEF;

/* Layout/sizing/padding live on the Clay begin element, not on the style. */
typedef struct {
    uint32_t bg_region; /* slice9 region; 0 = same as idle */
    uint32_t bg_tint;   /* 0xAABBGGRR */
    float scale;
    float offset_x, offset_y;
    float opacity; /* inherits to content */
} nt_ui_btn_state_t;

typedef struct {
    nt_ui_btn_state_t idle, hover, pressed, disabled;
    float transition_speed; /* 0 = instant; >0 = eased via anim cache */
    /* Inflated BEFORE the inverse-affine so the padded zone rotates with the widget. */
    int16_t hit_padding_lrtb[4];
    /* Multiplies the atlas region's baked slice9 borders. */
    float slice9_scale;
} nt_ui_button_style_t;

/* enabled=false short-circuits interaction and forces the disabled visual.
 * Engine OWNS .id/.image/.backgroundColor/.userData on the Clay decl. */
void nt_ui_button_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, nt_resource_t atlas, const nt_ui_button_style_t *style, const Clay_ElementDeclaration *decl, bool enabled);
bool nt_ui_button_end(nt_ui_context_t *ctx);

#endif /* NT_UI_BUTTON_H */
