#ifndef NT_UI_IMAGE_H
#define NT_UI_IMAGE_H

/* Stateless image widget. Atlas + region are runtime arguments; style
 * contains visual properties only (tint, flip, slice9 override) and is
 * static-const safe. data may be NULL (= no layer, no user_data). */

#include <stdint.h>

#include "clay.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t, nt_ui_image_payload_t */

typedef struct nt_ui_context nt_ui_context_t;

typedef struct {
    uint32_t color_packed;   /* tint (0xAABBGGRR), 0xFFFFFFFF = no tint */
    uint16_t slice9_lrtb[4]; /* override atlas default, {0,0,0,0} = use atlas */
    uint8_t flip_bits;       /* NT_SPRITE_FLAG_FLIP_X | _FLIP_Y */
} nt_ui_image_style_t;
_Static_assert(sizeof(nt_ui_image_style_t) <= 16, "nt_ui_image_style_t fits in 16 B");

/* Leaf image widget. Atlas+region are arguments (runtime handles).
 * Style contains visual properties only (static const safe).
 * data may be NULL (= no layer, no user_data). */
void nt_ui_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_resource_t atlas, uint32_t region_index, const nt_ui_image_style_t *style);

#endif /* NT_UI_IMAGE_H */
