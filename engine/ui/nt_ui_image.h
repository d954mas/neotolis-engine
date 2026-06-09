#ifndef NT_UI_IMAGE_H
#define NT_UI_IMAGE_H

/* Stateless image widget. Style is static-const safe; data may be NULL. */

#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t, nt_ui_image_payload_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_IMAGE_DEF;

/* Style flag bits. */
#define NT_UI_IMAGE_SLICE9_OVERRIDE (1U << 0) /* use slice9_lrtb even if {0,0,0,0} */
#define NT_UI_IMAGE_ORIGIN_OVERRIDE (1U << 1) /* use origin_x/y instead of atlas default */

typedef struct {
    uint32_t color_packed;   /* 0xAABBGGRR; 0xFFFFFFFF = no tint */
    uint16_t slice9_lrtb[4]; /* {0,0,0,0} + no flag = atlas default */
    float origin_x;          /* 0..1; only used when ORIGIN_OVERRIDE set */
    float origin_y;
    float slice9_scale; /* MUST be finite > 0 (helper asserts) */
    uint8_t flip_bits;  /* NT_SPRITE_FLAG_FLIP_X | _FLIP_Y */
    uint8_t flags;      /* NT_UI_IMAGE_SLICE9_OVERRIDE | NT_UI_IMAGE_ORIGIN_OVERRIDE */
} nt_ui_image_style_t;
_Static_assert(sizeof(nt_ui_image_style_t) <= 28, "nt_ui_image_style_t fits in 28 B");

/* Use instead of bare {0} — color_packed=0 would render fully transparent. */
static inline nt_ui_image_style_t nt_ui_image_style_defaults(void) { return (nt_ui_image_style_t){.color_packed = 0xFFFFFFFF, .origin_x = 0.5F, .origin_y = 0.5F, .slice9_scale = 1.0F}; }

/* decl may be NULL (GROW/GROW); engine owns image/backgroundColor/userData. */
void nt_ui_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t region, const nt_ui_image_style_t *style, const Clay_ElementDeclaration *decl);

#endif /* NT_UI_IMAGE_H */
