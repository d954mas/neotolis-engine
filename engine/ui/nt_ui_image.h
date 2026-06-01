#ifndef NT_UI_IMAGE_H
#define NT_UI_IMAGE_H

/* Stateless image widget. Atlas + region are runtime arguments; style
 * contains visual properties only (tint, flip, slice9 override) and is
 * static-const safe. data may be NULL (= no layer, no user_data). */

#include <stdint.h>

#include "clay.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t, nt_ui_image_payload_t */

typedef struct nt_ui_context nt_ui_context_t;

/* Inspector descriptor: pill name + color shown in the element tree. Game
 * code may compare pointer identity against this symbol if it needs to
 * filter by "is this an image" without parsing names. */
extern const nt_ui_widget_def_t NT_UI_IMAGE_DEF;

/* Style flag bits. */
#define NT_UI_IMAGE_SLICE9_OVERRIDE (1U << 0) /* use slice9_lrtb even if {0,0,0,0} */
#define NT_UI_IMAGE_ORIGIN_OVERRIDE (1U << 1) /* use origin_x/y instead of atlas default */

typedef struct {
    uint32_t color_packed;   /* tint (0xAABBGGRR), 0xFFFFFFFF = no tint */
    uint16_t slice9_lrtb[4]; /* override; {0,0,0,0} + no flag = atlas default */
    float origin_x;          /* pivot 0..1; only used if ORIGIN_OVERRIDE flag set */
    float origin_y;
    uint8_t flip_bits; /* NT_SPRITE_FLAG_FLIP_X | _FLIP_Y */
    uint8_t flags;     /* NT_UI_IMAGE_SLICE9_OVERRIDE | NT_UI_IMAGE_ORIGIN_OVERRIDE */
} nt_ui_image_style_t;
_Static_assert(sizeof(nt_ui_image_style_t) <= 24, "nt_ui_image_style_t fits in 24 B");

/* Zero-init safe default: untinted white. Use instead of bare {0}. */
static inline nt_ui_image_style_t nt_ui_image_style_defaults(void) { return (nt_ui_image_style_t){.color_packed = 0xFFFFFFFF, .origin_x = 0.5F, .origin_y = 0.5F}; }

/* Leaf image widget. Atlas+region are arguments (runtime handles).
 * Style contains visual properties only (static const safe).
 * data may be NULL (= no layer, no user_data). */
void nt_ui_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_resource_t atlas, uint32_t region_index, const nt_ui_image_style_t *style);

#endif /* NT_UI_IMAGE_H */
