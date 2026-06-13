#ifndef NT_UI_FILL_H
#define NT_UI_FILL_H

/* Shared fill-emit helper consumed by slider fill AND progress fill.
 * STRETCH: a slice9-aware region stretched to ratio*track. CROP: a full-size region
 * revealed by ratio via a Clay .clip scissor (no distortion); slice9 borders are
 * IGNORED in CROP — geometrically incompatible with a partial reveal. */

#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "clay.h"
#include "ui/nt_ui.h" /* nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

typedef enum { NT_UI_FILL_STRETCH = 0, NT_UI_FILL_CROP } nt_ui_fill_mode_t;
typedef enum { NT_UI_FILL_LTR = 0, NT_UI_FILL_RTL, NT_UI_FILL_BOTTOM_UP, NT_UI_FILL_TOP_DOWN } nt_ui_fill_direction_t;

/* Emits the fill child of a track. ratio is clamped to [0,1]. direction picks the
 * growing edge (LTR left-anchored, RTL right, BOTTOM_UP bottom, TOP_DOWN top).
 * fill_ref resolves in place; an unresolved / no-art ref skips the emit entirely
 * (no-art rule). track_w/h are the fill container size (finite > 0).
 * Must be called between nt_ui_begin and nt_ui_end on the active ctx. */
void nt_ui_fill_emit(nt_ui_context_t *ctx, uint8_t layer, nt_atlas_region_ref_t *fill_ref, uint32_t tint, float ratio, float track_w, float track_h, nt_ui_fill_mode_t mode,
                     nt_ui_fill_direction_t direction, float opacity);

#endif /* NT_UI_FILL_H */
