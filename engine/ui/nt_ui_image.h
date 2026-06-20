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

/* decl may be NULL (GROW/GROW); engine owns image/backgroundColor/userData.
 * region is by-pointer: the engine resolves it lazily and memoizes the index into *region. */
void nt_ui_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, const nt_ui_image_style_t *style, const Clay_ElementDeclaration *decl);

/* ---- Generic custom-attr atlas-region emit ----
 *
 * Draws one atlas region with a per-vertex custom-attr material — the single public
 * path every custom-attr widget (radial, radial_image, future SDF/effect widgets)
 * rides. The walker has ONE generic custom-emit branch keyed on payload.custom_bytes;
 * there are NO per-widget flags or branches.
 *
 * Injection is NAME-BOUND: the material's attr_map is the single source of truth. The
 * widget supplies its data block with ZERO placeholders where walker-derived attrs sit;
 * the walker scans the attr_map and fills any attr it recognizes by name:
 *   - a_layout vec4 = {aspect = bbox w/h, bbox_width_px, bbox_height_px, 0}
 *   - a_uvrect vec4 = {u0, v0, u1, v1} = the region's min/max atlas UV
 * Block float-offset of attr i = i*4 (attr_map declaration order; each attr is a FLOAT4).
 * To add a new injected value: pick a new attr name, fill it in the walker, name it in a
 * material's attr_map. No payload/API change.
 *
 * WALLS (what this path does NOT do):
 *   - set_custom_attrs is UNIFORM across a widget's verts — no per-vertex data. A
 *     composite widget (segmented bar, sparkline, minimap blips) is N separate calls.
 *   - 16-float cap: 4 × vec4 at NT_SPRITE_CUSTOM_STRIDE_MAX (64 B).
 *   - time / animation is NOT a walker injection — the wrapper writes it into
 *     custom_attrs itself each frame.
 *   - a 2nd texture is supplied via the material (textures[]), not this block.
 *
 * Contract:
 *   - material MUST resolve ready with attr_map_count > 0, custom_bytes ==
 *     attr_map_count*16 (asserted) and <= NT_SPRITE_CUSTOM_STRIDE_MAX.
 *   - custom_attrs points to custom_bytes worth of floats, copied into the payload.
 *   - geom_mode: NT_UI_IMAGE_GEOM_REGION (textured region emit, origin/flip/slice9
 *     honored) or NT_UI_IMAGE_GEOM_GEOMETRY (clean white-region bbox quad for SDF
 *     shaders that need gl_VertexID&3 corner derivation).
 *   - color_packed: 0xAABBGGRR tint (transparency rides element opacity, not alpha).
 *
 * atlas/region_index are by-value (already resolved by the caller). data may be NULL.
 * decl may be NULL (GROW/GROW); the engine owns image/backgroundColor/userData. Must
 * be called between nt_ui_begin and nt_ui_end on the active ctx. */
typedef struct {
    nt_resource_t atlas;
    uint32_t region_index;
    nt_material_t material;
    const float *custom_attrs;
    uint8_t custom_bytes;
    uint8_t geom_mode; /* NT_UI_IMAGE_GEOM_REGION | NT_UI_IMAGE_GEOM_GEOMETRY */
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
