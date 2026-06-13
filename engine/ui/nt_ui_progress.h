#ifndef NT_UI_PROGRESS_H
#define NT_UI_PROGRESS_H

/* Read-only progress bar: a track + a fill whose ratio = value (clamped [0,1]),
 * no input interaction. "Slider minus interaction" — reuses nt_ui_fill_emit for the
 * track+fill composition (STRETCH slice9 | CROP UV-reveal, 4 directions). The only
 * retained state is the eased fill ratio (value_t at value_speed; 0 = instant). */

#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "ui/nt_ui.h"      /* nt_ui_element_data_t, nt_ui_widget_def_t */
#include "ui/nt_ui_fill.h" /* fill mode + direction */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_PROGRESS_DEF;

/* track/fill are atomic nt_atlas_region_ref_t; atlas.id==0 (or unresolved) = no art
 * (that part skips its IMAGE). */
typedef struct {
    nt_atlas_region_ref_t track, fill;     /* empty ref = part not drawn */
    uint32_t track_tint, fill_tint;        /* 0xFFFFFFFF = no tint */
    float track_w, track_h;                /* FIXED layout px; asserted finite > 0 */
    nt_ui_fill_mode_t fill_mode;           /* STRETCH (slice9) | CROP */
    nt_ui_fill_direction_t fill_direction; /* LTR / RTL / BOTTOM_UP / TOP_DOWN */
    float value_speed;                     /* fill ease toward value (0 = instant) */
    float opacity;                         /* whole-widget, INHERITS to children */
} nt_ui_progress_style_t;
_Static_assert(sizeof(nt_ui_progress_style_t) == 64, "nt_ui_progress_style_t stable ABI (2x16 ref + 2 tint + 2 size + 2 enum + speed + opacity)");

/* Read-only bar; value clamped to [0,1]. No interaction (no return value). Engine owns
 * .id/.userData on the decl; data must NOT set HAS_TRANSFORM/HAS_OPACITY. */
void nt_ui_progress(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t layer, uint32_t id, float value_0_to_1, nt_ui_progress_style_t *style, const Clay_ElementDeclaration *decl);

/* Valid baseline: opacity 1, no tint, sensible track size, STRETCH/LTR, value_speed ~10.
 * Caller supplies art (track/fill refs). Avoids the zero-init trap. */
nt_ui_progress_style_t nt_ui_progress_style_defaults(void);

#endif /* NT_UI_PROGRESS_H */
