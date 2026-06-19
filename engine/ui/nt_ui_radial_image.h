#ifndef NT_UI_RADIAL_IMAGE_H
#define NT_UI_RADIAL_IMAGE_H

/* Dedicated TEXTURED radial widget (D-66-12). Unlike nt_ui_radial (a flat SDF
 * shape on the white pixel), this textures a real atlas region and reveals the
 * un-swept sector via four reveal modes — the swept sector renders at full
 * color. nt_ui_image is left UNTOUCHED (D-66-12); this is a separate widget.
 *
 * It rides the sprite renderer's textured emit paths (emit_region / emit_slice9)
 * through the plan-01 custom per-vertex attribute capability: the a_radial FLOAT4
 * (angle_start, angle_end, inner_radius_norm, aspect) is baked into every vertex
 * of the region/slice9 quad, and the walker binds the radial-image material
 * (plan-02 per-element material) which carries the SDF fs + reveal-mode params.
 *
 * Angular convention is mathematical: 0 = +X axis, CCW positive (D-66-10). Two
 * independent angles drive the sweep (D-66-09); `fill` 0..1 is a thin convenience
 * mapping to angle_end for cooldown / hold_progress idioms (Phase-65). */

#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "material/nt_material.h"
#include "ui/nt_ui.h"        /* nt_ui_element_data_t */
#include "ui/nt_ui_radial.h" /* nt_ui_radial_fill_to_end (shared fill->angle math) */

typedef struct nt_ui_context nt_ui_context_t;

/* This widget's payload flag bit on nt_ui_image_payload_t.flags. Distinct from
 * NT_UI_IMAGE_FLAG_RADIAL (the white-pixel flat radial): RADIAL_IMAGE routes the
 * walker through the TEXTURED emit_region/emit_slice9 path with the a_radial
 * block baked per-vert. nt_ui_image.h is untouched (D-66-12), so this lives here. */
#define NT_UI_IMAGE_FLAG_RADIAL_IMAGE (1U << 3)

/* Reveal mode applied to the UN-SWEPT (remaining) sector; the swept sector is
 * always full color (D-66-12). Encoded as u_reveal_mode.x in the material. */
typedef enum {
    NT_UI_RADIAL_REVEAL_DESATURATE = 0, /* un-swept -> grayscale (luma), alpha preserved */
    NT_UI_RADIAL_REVEAL_DIM = 1,        /* un-swept -> multiplied by dim_factor */
    NT_UI_RADIAL_REVEAL_HIDE = 2,       /* un-swept -> discarded (fully hidden) */
    NT_UI_RADIAL_REVEAL_TINT = 3,       /* un-swept -> mixed toward tint_color */
} nt_ui_radial_reveal_mode_t;

/* Visual-only style. Mirrors nt_ui_image_style_t's slice9/origin/flip fields so a
 * textured radial supports polygon + slice9 regions, plus the radial-specific
 * reveal params. The radial-image material (attr_map a_radial @ loc 4 + the
 * u_reveal_mode / u_reveal_tint params) is supplied by the game per reveal mode
 * (one material per mode -> one batch per mode, RESEARCH A4). .id==0 invalid. */
typedef struct {
    uint32_t color_packed;      /* 0xAABBGGRR; 0xFFFFFFFF = no tint */
    float inner_radius_norm;    /* 0..1; 0 = solid sector, >0 = ring */
    float dim_factor;           /* DIM: un-swept brightness multiplier (0..1); finite >= 0 */
    uint32_t tint_color_packed; /* TINT: 0xAABBGGRR target color for the un-swept sector */
    float tint_strength;        /* TINT: mix amount toward tint_color (0..1) */
    uint16_t slice9_lrtb[4];    /* {0,0,0,0} + no flag = atlas default */
    float origin_x;             /* 0..1; only used when ORIGIN_OVERRIDE set */
    float origin_y;             /* 0..1; only used when ORIGIN_OVERRIDE set */
    float slice9_scale;         /* MUST be finite > 0 (helper asserts) */
    nt_material_t material;     /* radial-image material; .id==0 invalid */
    uint8_t mode;               /* nt_ui_radial_reveal_mode_t */
    uint8_t flip_bits;          /* NT_SPRITE_FLAG_FLIP_X | _FLIP_Y */
    uint8_t flags;              /* NT_UI_IMAGE_SLICE9_OVERRIDE | NT_UI_IMAGE_ORIGIN_OVERRIDE */
    uint8_t _reserved;
} nt_ui_radial_image_style_t;
_Static_assert(sizeof(nt_ui_radial_image_style_t) == 48, "nt_ui_radial_image_style_t stable ABI (48 B)");

/* Use instead of bare {0} — color_packed=0 renders fully transparent, slice9_scale
 * must be positive, dim_factor defaults to a visible-but-dimmed 0.4. material stays
 * .id==0 until the game assigns the radial-image material for the chosen mode. */
static inline nt_ui_radial_image_style_t nt_ui_radial_image_style_defaults(void) {
    return (nt_ui_radial_image_style_t){
        .color_packed = 0xFFFFFFFFU,
        .inner_radius_norm = 0.0F,
        .dim_factor = 0.4F,
        .tint_color_packed = 0xFF808080U,
        .tint_strength = 0.6F,
        .origin_x = 0.5F,
        .origin_y = 0.5F,
        .slice9_scale = 1.0F,
        .material = (nt_material_t){0},
        .mode = (uint8_t)NT_UI_RADIAL_REVEAL_DESATURATE,
    };
}

/* Material param names the shader reads (set per-call from the style). u_reveal_mode
 * = {mode, dim_factor, 0, 0}; u_reveal_tint = {r, g, b, tint_strength} in 0..1. */
#define NT_UI_RADIAL_IMAGE_PARAM_MODE "u_reveal_mode"
#define NT_UI_RADIAL_IMAGE_PARAM_TINT "u_reveal_tint"

/* Two-angle form (D-66-09). angle_start/angle_end in radians, mathematical
 * convention (0=+X, CCW+). region is by-pointer: resolved lazily, memoized into
 * *region; an unresolved/no-art ref skips the emit. data may be NULL. decl may be
 * NULL (GROW/GROW); the widget owns image/backgroundColor/userData. Must be called
 * between nt_ui_begin and nt_ui_end on the active ctx. */
void nt_ui_radial_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, float angle_start, float angle_end, const nt_ui_radial_image_style_t *style,
                        const Clay_ElementDeclaration *decl);

/* fill convenience: angle_end = angle_start + clamp(fill,0,1) * sweep_total
 * (D-66-09; shares nt_ui_radial_fill_to_end). The common cooldown / hold_progress
 * idiom. fill is clamped [0,1]; sweep_total is the full sweep in radians. */
void nt_ui_radial_image_fill(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, float angle_start, float fill, float sweep_total,
                             const nt_ui_radial_image_style_t *style, const Clay_ElementDeclaration *decl);

#endif /* NT_UI_RADIAL_IMAGE_H */
