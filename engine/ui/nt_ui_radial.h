#ifndef NT_UI_RADIAL_H
#define NT_UI_RADIAL_H

/* Flat SDF arc/sector/ring/oval widget on the white pixel. The game owns fill/state;
 * style is static-const safe. Rides the custom-attr image path so radials sharing one
 * material batch to a single draw. Angular convention: 0 = +X, CCW+. The wedge runs CCW
 * from start to end; swapping start/end selects the COMPLEMENTARY span, not a mirror.
 * design + reveal/angle rationale: docs/spec/ui/radial-widgets.md
 * "Radial widgets & the custom-attr image path" */

#include <stdint.h>

#include "clay.h"
#include "material/nt_material.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

/* Visual-only style. color_packed is 0xAABBGGRR (0xFFFFFFFF = white). inner_radius_norm
 * [0,1) carves a ring (0 = full disc). The radial material carries the fs + extended layout. */
typedef struct {
    uint32_t color_packed;   /* 0xAABBGGRR */
    float inner_radius_norm; /* [0,1); 0 = solid sector, >0 = ring */
    nt_material_t material;  /* radial SDF material (attr_map a_radial @ loc 4); .id==0 invalid */
} nt_ui_radial_style_t;
_Static_assert(sizeof(nt_ui_radial_style_t) == 12, "nt_ui_radial_style_t stable ABI (12 B)");

/* Use instead of bare {0} — color_packed=0 renders fully transparent. material stays
 * .id==0 until the game assigns the radial material. */
static inline nt_ui_radial_style_t nt_ui_radial_style_defaults(void) { return (nt_ui_radial_style_t){.color_packed = 0xFFFFFFFFU, .inner_radius_norm = 0.0F, .material = (nt_material_t){0}}; }

/* fill→angle math, exposed for headless tests and reused by the widget. clamps
 * fill to [0,1] then maps angle_end = angle_start + fill*sweep_total. No trig —
 * the angular span is the shader's job. */
static inline float nt_ui_radial_fill_to_end(float angle_start, float fill, float sweep_total) {
    float f = fill;
    if (f < 0.0F) {
        f = 0.0F;
    } else if (f > 1.0F) {
        f = 1.0F;
    }
    return angle_start + (f * sweep_total);
}

/* Two-angle form. angle_start/angle_end in radians, mathematical convention
 * (0=+X, CCW+). The wedge runs CCW from angle_start to angle_end; swapping the two
 * selects the complementary span (API-layer, no shader branch). A negative
 * fill*sweep_total puts end before start -> the complement, not a short reverse arc.
 * Both finite-asserted. data may be NULL. decl may be NULL
 * (GROW/GROW); the widget owns image/backgroundColor/userData. Must be called
 * between nt_ui_begin and nt_ui_end on the active ctx. */
void nt_ui_radial(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, float angle_start, float angle_end, const nt_ui_radial_style_t *style, const Clay_ElementDeclaration *decl);

/* fill convenience: angle_end = angle_start + clamp(fill,0,1) * sweep_total.
 * The common cooldown / hold_progress idiom. fill is clamped [0,1]; sweep_total
 * is the full sweep in radians (e.g. 2*PI for a full disc). */
void nt_ui_radial_fill(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, float angle_start, float fill, float sweep_total, const nt_ui_radial_style_t *style,
                       const Clay_ElementDeclaration *decl);

#endif /* NT_UI_RADIAL_H */
