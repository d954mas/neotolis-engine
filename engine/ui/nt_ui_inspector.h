#ifndef NT_UI_INSPECTOR_H
#define NT_UI_INSPECTOR_H

/* Inspector — port of Clay's debug view injected into the user's layout pass.
 * emit_layout (called by nt_ui_end when active) emits CLAY({...}) blocks that
 * solve through the same pipeline as user UI. overlay_draw paints the post-walk
 * highlight for the currently-focused element (hover OR clicked sidebar row).
 *
 * Engine extensions: widget-type column + layer column ("L:N") per element row. */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h"
#include "ui/nt_ui.h" /* nt_ui_target_t */

typedef struct nt_ui_context nt_ui_context_t;

/* EXPERIMENTAL: API surface may change in v1.9. Game code should pin the engine version.
 *
 * Toggle the inspector. Default off; zero overhead when off. */
void nt_ui_inspector_set_active(nt_ui_context_t *ctx, bool on);
bool nt_ui_inspector_is_active(const nt_ui_context_t *ctx);

/* True when the inspector is active AND the pointer is inside the sidebar.
 * Games rolling their own click logic should query this to suppress it
 * (step_interaction already honors it internally). Stable across the frame. */
bool nt_ui_inspector_pointer_consumed(const nt_ui_context_t *ctx);

/* Runtime-tunable inspector sizing — sidebar width, row height, font, padding.
 * Consumed by the layout emit AND the post-walk overlay scissor. */
typedef struct nt_ui_inspector_metrics_t {
    float panel_width;     /* sidebar width in layout pixels (default 400) */
    float row_height;      /* element-row height (default 30) */
    uint16_t font_size;    /* text font size for all inspector labels (default 16) */
    uint8_t outer_padding; /* panel content outer padding (default 10) */
    uint8_t indent_width;  /* per-depth indent step (default 16) */
} nt_ui_inspector_metrics_t;

extern const nt_ui_inspector_metrics_t NT_UI_INSPECTOR_METRICS_DEFAULT;

/* Asserts panel_width / row_height / font_size > 0. */
void nt_ui_inspector_set_metrics(nt_ui_context_t *ctx, const nt_ui_inspector_metrics_t *metrics);

/* Engine-internal — called by nt_ui_end if active. Game code does not call this. */
void nt_ui_inspector_emit_layout(nt_ui_context_t *ctx);

/* Paints highlight + id label for the focused element. Call AFTER nt_ui_walk,
 * BEFORE nt_gfx_end_pass. `target` MUST be the one passed to nt_ui_walk.
 * label_size <= 0 skips the label; rect still drawn. Silent skip when inactive,
 * when ctx is missing bindings, or when no element is focused. */
void nt_ui_inspector_overlay_draw(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_font_t font, float label_size);

#endif /* NT_UI_INSPECTOR_H */
