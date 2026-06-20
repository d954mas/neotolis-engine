#ifndef NT_UI_RADIAL_IMAGE_H
#define NT_UI_RADIAL_IMAGE_H

/* Dedicated TEXTURED radial widget. Unlike nt_ui_radial (a flat SDF shape on the
 * white pixel), this textures a real atlas region and reveals the un-swept sector
 * via four reveal modes — the swept sector renders at full color. nt_ui_image is
 * left UNTOUCHED; this is a separate widget.
 *
 * It rides the sprite renderer's textured emit_region path through the custom
 * per-vertex attribute capability: the a_radial FLOAT4 (angle_start, angle_end,
 * inner_radius_norm, aspect) is baked into every vertex of the region quad, and
 * the walker binds the radial-image material (the per-element material) which
 * carries the SDF fs + reveal-mode params. aspect is bbox-derived at emit (the
 * walker overwrites the widget's placeholder with the real bbox w/h).
 *
 * Works with ANY rectangular atlas region — a full-bleed [0,1] texture OR a packed
 * sub-region. The walker bakes the region's atlas UV rect into a_uvrect; the reveal
 * fs normalizes v_texcoord into region-local [-1,1] so the wedge always centers on
 * the region, wherever it sits in the page.
 *
 * HARD constraint — slice9 unsupported: the UV is non-linear across slice9 patches,
 * so the ring/reveal would deform. The slice9 struct fields remain (ABI) but the
 * widget asserts they are unset. A real geometry-local coord is the future path.
 *
 * Angular convention is mathematical: 0 = +X axis, CCW positive. Two independent
 * angles drive the sweep; `fill` 0..1 is a thin convenience mapping to angle_end
 * for cooldown / hold_progress idioms. */

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
 * block baked per-vert. nt_ui_image.h is untouched, so this lives here. */
#define NT_UI_IMAGE_FLAG_RADIAL_IMAGE (1U << 3)

/* Reveal mode applied to the UN-SWEPT (remaining) sector; the swept sector is
 * always full color. Encoded as u_reveal_mode.x in the material. */
typedef enum {
    NT_UI_RADIAL_REVEAL_DESATURATE = 0, /* un-swept -> grayscale (luma), alpha preserved */
    NT_UI_RADIAL_REVEAL_DIM = 1,        /* un-swept -> multiplied by dim_factor */
    NT_UI_RADIAL_REVEAL_HIDE = 2,       /* un-swept -> discarded (fully hidden) */
    NT_UI_RADIAL_REVEAL_TINT = 3,       /* un-swept -> mixed toward tint_color */
} nt_ui_radial_reveal_mode_t;

/* Visual-only style. Mirrors nt_ui_image_style_t's slice9/origin/flip fields for ABI
 * parity, but slice9 is REJECTED in v1 (region-only — see header note). mode + dim_factor are baked on
 * the MATERIAL at creation (u_reveal_mode), so N widgets sharing a material reveal in
 * the same MODE. The TINT, however, is PER-WIDGET (tint_color_packed + tint_strength
 * -> baked into a_tint), so many differently-tinted radials share ONE tint-mode
 * material and batch to one draw. The radial-image material (attr_map a_radial @ loc 4
 * + a_tint @ loc 5 + the u_reveal_mode param) is supplied by the game per reveal mode.
 * .id==0 invalid. */
typedef struct {
    uint32_t color_packed;      /* 0xAABBGGRR; 0xFFFFFFFF = no tint */
    float inner_radius_norm;    /* [0,1); 0 = solid sector, >0 = ring */
    uint16_t slice9_lrtb[4];    /* ABI only — v1 asserts {0,0,0,0} + SLICE9 flag unset (region-only) */
    float origin_x;             /* 0..1; only used when ORIGIN_OVERRIDE set */
    float origin_y;             /* 0..1; only used when ORIGIN_OVERRIDE set */
    float slice9_scale;         /* MUST be finite > 0 (helper asserts) */
    nt_material_t material;     /* radial-image material; .id==0 invalid */
    uint32_t tint_color_packed; /* 0xAABBGGRR; TINT mode target color (per-widget) */
    float tint_strength;        /* [0,1]; TINT mix strength (per-widget) */
    uint8_t flip_bits;          /* NT_SPRITE_FLAG_FLIP_X | _FLIP_Y */
    uint8_t flags;              /* NT_UI_IMAGE_ORIGIN_OVERRIDE (SLICE9_OVERRIDE rejected in v1) */
    uint8_t _reserved[2];
} nt_ui_radial_image_style_t;
_Static_assert(sizeof(nt_ui_radial_image_style_t) == 44, "nt_ui_radial_image_style_t stable ABI (44 B)");

/* Use instead of bare {0} — color_packed=0 renders fully transparent, slice9_scale
 * must be positive. material stays .id==0 until the game assigns the radial-image
 * material for the chosen reveal mode. tint defaults to white @ 0.6 strength. */
static inline nt_ui_radial_image_style_t nt_ui_radial_image_style_defaults(void) {
    return (nt_ui_radial_image_style_t){
        .color_packed = 0xFFFFFFFFU,
        .inner_radius_norm = 0.0F,
        .origin_x = 0.5F,
        .origin_y = 0.5F,
        .slice9_scale = 1.0F,
        .material = (nt_material_t){0},
        .tint_color_packed = 0xFFFFFFFFU, /* white */
        .tint_strength = 0.6F,
    };
}

/* Material param the shader reads, set once on the material by the game at creation
 * (one material per reveal mode). u_reveal_mode = {mode, dim_factor, 0, 0}. TINT is
 * per-widget (style->tint_color_packed/tint_strength -> a_tint), not a material param. */
#define NT_UI_RADIAL_IMAGE_PARAM_MODE "u_reveal_mode"

/* Two-angle form. angle_start/angle_end in radians, mathematical convention
 * (0=+X, CCW+). region is by-pointer: resolved lazily, memoized into *region; an
 * unresolved/no-art ref skips the emit. data may be NULL. decl may be NULL
 * (GROW/GROW); the widget owns image/backgroundColor/userData. Must be called
 * between nt_ui_begin and nt_ui_end on the active ctx. */
void nt_ui_radial_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, float angle_start, float angle_end, const nt_ui_radial_image_style_t *style,
                        const Clay_ElementDeclaration *decl);

/* fill convenience: angle_end = angle_start + clamp(fill,0,1) * sweep_total
 * (shares nt_ui_radial_fill_to_end). The common cooldown / hold_progress idiom.
 * fill is clamped [0,1]; sweep_total is the full sweep in radians. */
void nt_ui_radial_image_fill(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, float angle_start, float fill, float sweep_total,
                             const nt_ui_radial_image_style_t *style, const Clay_ElementDeclaration *decl);

#endif /* NT_UI_RADIAL_IMAGE_H */
