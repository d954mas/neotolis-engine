#ifndef NT_UI_IMAGE_H
#define NT_UI_IMAGE_H

/* Stateless image widget. Style is static-const safe; data may be NULL. */

#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "material/nt_material.h" /* nt_material_t (custom-attr path) */
#include "ui/nt_ui.h"             /* nt_ui_element_data_t, nt_ui_image_payload_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_IMAGE_DEF;

/* Style flag bits. */
#define NT_UI_IMAGE_SLICE9_OVERRIDE (1U << 0) /* use slice9_lrtb; with {0,0,0,0} a baked nine-patch draws as a plain quad */
#define NT_UI_IMAGE_ORIGIN_OVERRIDE (1U << 1) /* use origin_x/y instead of atlas default */
/* Bit 2 is NT_UI_IMAGE_ANALYTIC_SHAPE (nt_ui.h). */

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

/* decl may be NULL (GROW/GROW); engine owns image/backgroundColor/userData.
 * region is by-pointer: the engine resolves it lazily and memoizes the index into *region. */
void nt_ui_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, const nt_ui_image_style_t *style, const Clay_ElementDeclaration *decl);

/* Generic custom-attr atlas-region emit. custom_bytes == material vertex_layout.stride - 20
 * <= NT_SPRITE_CUSTOM_STRIDE_MAX; the walker writes the FLOAT bbox aspect at aspect_offset
 * (vertex bytes, 0 = none) and copies all other bytes. data/decl may be NULL.
 * spec: docs/spec/ui/radial-widgets.md */
typedef struct {
    nt_resource_t atlas;
    uint32_t region_index;
    nt_material_t material;
    const void *custom_attrs;
    uint8_t custom_bytes;
    uint8_t aspect_offset; /* vertex byte offset of the walker-written FLOAT aspect; 0 = none */
    uint8_t flip_bits;
    uint8_t flags; /* NT_UI_IMAGE_SLICE9_OVERRIDE | NT_UI_IMAGE_ORIGIN_OVERRIDE */
    uint16_t slice9_lrtb[4];
    float origin_x;
    float origin_y;
    float slice9_scale;    /* MUST be finite > 0 */
    uint32_t color_packed; /* 0xAABBGGRR; tint/opacity */
} nt_ui_image_custom_t;

void nt_ui_image_custom(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const nt_ui_image_custom_t *img, const Clay_ElementDeclaration *decl);

#endif /* NT_UI_IMAGE_H */
