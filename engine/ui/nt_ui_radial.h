#ifndef NT_UI_RADIAL_H
#define NT_UI_RADIAL_H

/* Flat SDF arc/sector/ring/oval widget. Model-D: the game owns fill/state, the
 * style is static-const safe. One SDF fragment shader rasterizes every v1 shape
 * on a plain quad — params enable/disable features (D-66-08). The widget rides
 * the sprite renderer's custom per-vertex attribute path (plan 01) + the walker
 * per-element material binding (plan 02), so many radials sharing one material
 * batch to a single draw (D-66-07).
 *
 * Angular convention is mathematical: 0 = +X axis, CCW positive (D-66-10).
 * Two independent angles drive the sweep (D-66-09): grow one edge, rotate the
 * whole arc, or close from both sides. There is NO CW/CCW flag — direction is
 * implicit in the start/end order; flip = swap the two angles. `fill` 0..1 is a
 * thin convenience mapping to angle_end from angle_start for cooldown timers and
 * Phase-65 hold_progress. */

#include <stdint.h>

#include "clay.h"
#include "material/nt_material.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

/* Visual-only style. color_packed is 0xAABBGGRR (0xFFFFFFFF = white). inner_radius_norm
 * 0..1 carves a ring (0 = full disc). aa_softness scales the fwidth AA width (1.0 = crisp
 * default); the radial material carries the fs + extended layout. */
typedef struct {
    uint32_t color_packed;   /* 0xAABBGGRR */
    float inner_radius_norm; /* 0..1; 0 = solid sector, >0 = ring */
    float aa_softness;       /* AA width multiplier; MUST be finite > 0 */
    nt_material_t material;  /* radial SDF material (attr_map a_radial @ loc 4); .id==0 invalid */
} nt_ui_radial_style_t;
_Static_assert(sizeof(nt_ui_radial_style_t) == 16, "nt_ui_radial_style_t stable ABI (16 B)");

/* Use instead of bare {0} — color_packed=0 renders fully transparent and aa_softness
 * must be positive. material stays .id==0 until the game assigns the radial material. */
static inline nt_ui_radial_style_t nt_ui_radial_style_defaults(void) {
    return (nt_ui_radial_style_t){.color_packed = 0xFFFFFFFFU, .inner_radius_norm = 0.0F, .aa_softness = 1.0F, .material = (nt_material_t){0}};
}

/* fill→angle math, exposed for headless tests (WGT-66-04) and reused by the
 * widget. clamps fill to [0,1] then maps angle_end = angle_start + fill*sweep_total
 * (D-66-09). No trig — the angular span is the shader's job. */
static inline float nt_ui_radial_fill_to_end(float angle_start, float fill, float sweep_total) {
    float f = fill;
    if (f < 0.0F) {
        f = 0.0F;
    } else if (f > 1.0F) {
        f = 1.0F;
    }
    return angle_start + (f * sweep_total);
}

/* Two-angle form (D-66-09). angle_start/angle_end in radians, mathematical
 * convention (0=+X, CCW+). Swapping the two reverses the swept direction — flip
 * is API-layer, no shader branch. Both finite-asserted. data may be NULL.
 * decl may be NULL (GROW/GROW); the widget owns image/backgroundColor/userData.
 * Must be called between nt_ui_begin and nt_ui_end on the active ctx. */
void nt_ui_radial(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, float angle_start, float angle_end, const nt_ui_radial_style_t *style, const Clay_ElementDeclaration *decl);

/* fill convenience: angle_end = angle_start + clamp(fill,0,1) * sweep_total
 * (D-66-09). The common cooldown / hold_progress idiom. fill is clamped [0,1];
 * sweep_total is the full sweep in radians (e.g. 2*PI for a full disc). */
void nt_ui_radial_fill(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, float angle_start, float fill, float sweep_total, const nt_ui_radial_style_t *style,
                       const Clay_ElementDeclaration *decl);

#endif /* NT_UI_RADIAL_H */
