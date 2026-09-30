#ifndef NT_UI_RADIAL_IMAGE_H
#define NT_UI_RADIAL_IMAGE_H

/* Dedicated TEXTURED radial widget — textures a real atlas region and reveals the
 * un-swept sector via four reveal modes (swept sector = full color). Separate from
 * nt_ui_image; rides the custom-attr image path (REGION geom). Works with any
 * rectangular region (full-bleed or packed, including atlas D4 orientations).
 * slice9 is rejected in v1 (patch geometry needs separate source coordinates). Before explicit
 * flips/transforms, 0 points right and +pi/2 down in local UI space; positive
 * angles sweep clockwise. The renderer supplies source-image UV regardless of
 * atlas D4; explicit flips mirror the reveal with the art.
 * design + reveal modes + v1 limits: docs/spec/ui/radial-widgets.md
 * "Radial widgets & the custom-attr image path" */

#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "material/nt_material.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

/* Reveal mode applied to the UN-SWEPT (remaining) sector; the swept sector is
 * always full color. Encoded as u_reveal_mode.x in the material. */
typedef enum {
    NT_UI_RADIAL_REVEAL_DESATURATE = 0, /* un-swept -> grayscale (luma), alpha preserved */
    NT_UI_RADIAL_REVEAL_DIM = 1,        /* un-swept -> multiplied by dim_factor */
    NT_UI_RADIAL_REVEAL_HIDE = 2,       /* un-swept -> discarded (fully hidden) */
    NT_UI_RADIAL_REVEAL_TINT = 3,       /* un-swept -> mixed toward tint_color */
} nt_ui_radial_reveal_mode_t;

/* Visual-only style. Supports origin and flip overrides; slice9 is rejected.
 * mode + dim_factor are baked on the material
 * (u_reveal_mode); tint is per-widget (a_tint), so differently-tinted radials still
 * share one material and batch. material .id==0 invalid. */
typedef struct {
    uint32_t color_packed;      /* 0xAABBGGRR; 0xFFFFFFFF = no tint */
    float inner_radius_norm;    /* [0,1); 0 = solid sector, >0 = ring */
    float origin_x;             /* 0..1; only used when ORIGIN_OVERRIDE set */
    float origin_y;             /* 0..1; only used when ORIGIN_OVERRIDE set */
    nt_material_t material;     /* radial-image material; .id==0 invalid */
    uint32_t tint_color_packed; /* 0xAABBGGRR; TINT mode target color (per-widget) */
    float tint_strength;        /* [0,1]; TINT mix strength (per-widget) */
    uint8_t flip_bits;          /* NT_SPRITE_FLAG_FLIP_X | _FLIP_Y */
    uint8_t flags;              /* NT_UI_IMAGE_ORIGIN_OVERRIDE (SLICE9_OVERRIDE rejected in v1) */
    uint8_t _reserved[2];
} nt_ui_radial_image_style_t;
_Static_assert(sizeof(nt_ui_radial_image_style_t) == 32, "nt_ui_radial_image_style_t size (32 B)");

/* Use instead of bare {0} — color_packed=0 renders fully transparent.
 * material stays .id==0 until the game assigns the radial-image
 * material for the chosen reveal mode. tint defaults to white @ 0.6 strength. */
static inline nt_ui_radial_image_style_t nt_ui_radial_image_style_defaults(void) {
    return (nt_ui_radial_image_style_t){
        .color_packed = 0xFFFFFFFFU,
        .inner_radius_norm = 0.0F,
        .origin_x = 0.5F,
        .origin_y = 0.5F,
        .material = (nt_material_t){0},
        .tint_color_packed = 0xFFFFFFFFU, /* white */
        .tint_strength = 0.6F,
    };
}

/* Radial-image material vertex_layout: sprite prefix, a_radial (loc 4, @20), a_tint (loc 5, @36),
 * a_source_uv (loc 6, @56, renderer-written) and a_aspect (loc 7, @52, walker-written). The
 * material also sets source_uv_offset = NT_UI_RADIAL_IMAGE_SOURCE_UV_OFFSET. */
extern const nt_vertex_layout_t NT_UI_RADIAL_IMAGE_VERTEX_LAYOUT;
#define NT_UI_RADIAL_IMAGE_SOURCE_UV_OFFSET 56U

/* Material param the shader reads, set once on the material by the game at creation
 * (one material per reveal mode). u_reveal_mode = {mode, dim_factor, 0, 0}. TINT is
 * per-widget (style->tint_color_packed/tint_strength -> a_tint), not a material param. */
#define NT_UI_RADIAL_IMAGE_PARAM_MODE "u_reveal_mode"

/* Two-angle form. angle_start/angle_end in local UI radians (0 right,
 * +pi/2 down, clockwise+ before flips/transforms). Swapping angles selects
 * the complementary span. region is by-pointer: resolved lazily, memoized into *region; an
 * unresolved/no-art ref skips the emit. data may be NULL. decl may be NULL
 * (GROW/GROW); the widget owns image/backgroundColor/userData. Must be called
 * between nt_ui_begin and nt_ui_end on the active ctx. Sweep caveat as
 * the angle shader: a negative fill*sweep_total puts end before start -> the
 * complementary span, not a short reverse arc. */
void nt_ui_radial_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, float angle_start, float angle_end, const nt_ui_radial_image_style_t *style,
                        const Clay_ElementDeclaration *decl);

/* fill convenience: angle_end = angle_start + clamp(fill,0,1) * sweep_total
 * The common cooldown / hold_progress idiom.
 * fill is clamped [0,1]; sweep_total is the full sweep in radians. */
void nt_ui_radial_image_fill(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, float angle_start, float fill, float sweep_total,
                             const nt_ui_radial_image_style_t *style, const Clay_ElementDeclaration *decl);

#endif /* NT_UI_RADIAL_IMAGE_H */
