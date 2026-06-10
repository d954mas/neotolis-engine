#ifndef NT_UI_BUTTON_H
#define NT_UI_BUTTON_H

/* Interactive container button. Auto state machine + eased visuals; content composes as children. */

#include <stdbool.h>
#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

/* "nt_" prefix disambiguates from Clay's own config-type pills. */
extern const nt_ui_widget_def_t NT_UI_BUTTON_DEF;

/* Layout/sizing/padding live on the Clay begin element, not on the style. */
typedef struct {
    nt_atlas_region_ref_t bg; /* atomic ref; atlas.id==0 = inherit idle.bg whole (region 0 is a valid index) */
    uint32_t bg_tint;         /* 0xAABBGGRR */
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
_Static_assert(sizeof(nt_ui_btn_state_t) == 40, "nt_ui_btn_state_t stable ABI (16 ref + tint + 4 float + 8B align pad)");
_Static_assert(sizeof(nt_ui_button_style_t) == 176, "nt_ui_button_style_t stable ABI");

/* enabled=false short-circuits interaction and forces the disabled visual.
 * Engine OWNS .id/.image/.backgroundColor/.userData on the Clay decl.
 * style->idle.bg.atlas.id == 0 = text-only button (no background IMAGE emitted);
 * other states inherit idle when their bg.atlas.id == 0. */
void nt_ui_button_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, nt_ui_button_style_t *style, const Clay_ElementDeclaration *decl, bool enabled);
bool nt_ui_button_end(nt_ui_context_t *ctx);

/* Content-less button (slice9 bg only). Returns clicked. Same args as begin. */
bool nt_ui_button(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, nt_ui_button_style_t *style, const Clay_ElementDeclaration *decl, bool enabled);

#endif /* NT_UI_BUTTON_H */
