#ifndef NT_UI_INSPECTOR_H
#define NT_UI_INSPECTOR_H

/* Phase 56 ext rework (verbatim Clay debug view port).
 *
 * Architecture: at nt_ui_end -- BEFORE Clay_EndLayout -- the engine injects
 * nt_ui_inspector_emit_layout(ctx) which emits CLAY({...}) blocks that
 * REPRODUCE Clay's `Clay__RenderDebugView` (deps/clay/clay.h:3392) 1:1 into
 * the SAME layout pass. The user's UI and the inspector solve, render, and
 * cull through the exact same pipeline. There is now ONE debug system --
 * Clay's built-in `Clay_SetDebugModeEnabled` wiring has been removed and
 * `nt_ui_set_debug_overlay` / `nt_ui_get_debug_overlay` are deleted from
 * the public API.
 *
 * The post-walk overlay (nt_ui_inspector_overlay_draw) paints the hit-zone
 * + id label for EXACTLY ONE element: the one the user is currently focused
 * on (hovered widget OR clicked sidebar row). Filter state lives in
 * ctx->inspector_highlight_id, written during emit_layout by the verbatim
 * port and consumed by overlay_draw. No mode cycling -- the focus is the
 * filter.
 *
 * Public API (3 functions):
 *   set_active / is_active   -- persistent toggle (zero overhead when off)
 *   emit_layout              -- called by nt_ui_end (engine-internal use)
 *   overlay_draw             -- call AFTER nt_ui_walk, BEFORE nt_gfx_end_pass
 *
 * Engine extensions on top of the verbatim port:
 *   1) Widget-type column     -- nt_ui_widget_lookup pill per element row
 *      (button / image / label / panel / group, or "-" for plain Clay).
 *   2) Layer column           -- "L:N" or "-" read from
 *      nt_ui_element_data_t.layer via Clay's userData slot. */

#include <stdbool.h>
#include <stdint.h>

#include "font/nt_font.h"
#include "ui/nt_ui.h" /* nt_ui_target_t */

typedef struct nt_ui_context nt_ui_context_t;

/* EXPERIMENTAL (Phase 56 ext): API surface may change in v1.9. Used by
 * ui_buttons_demo and inspector internals. Game code adopting this should
 * pin the engine version. Applies to the entire public surface in this
 * header (set_active / is_active / pointer_consumed / emit_layout /
 * overlay_draw).
 *
 * Toggle the inspector. Default off (no debug elements injected, no per-frame
 * tree walk, no extra draw calls). When on, nt_ui_end injects the Clay debug
 * view BEFORE Clay_EndLayout so the inspector participates in the same layout
 * solve as the user's UI. */
void nt_ui_inspector_set_active(nt_ui_context_t *ctx, bool on);
bool nt_ui_inspector_is_active(const nt_ui_context_t *ctx);

/* True for the current frame iff the inspector is active AND the pointer is
 * inside the sidebar footprint (right-attached, ctx->inspector_metrics.panel_width
 * wide). The engine already uses this internally to gate
 * nt_ui_get_interaction[_padded] so widgets behind the sidebar do NOT register
 * hover/press/click; games that roll their OWN interactive zones (not via
 * nt_ui_get_interaction) can query this to suppress their own click logic
 * when the inspector is taking input. Returns false when the inspector is
 * inactive. Computed in nt_ui_begin from the primary pointer position; the
 * value is stable across the frame. */
bool nt_ui_inspector_pointer_consumed(const nt_ui_context_t *ctx);

/* Phase 56 ext (REVIEW-2 P3-1): runtime-tunable inspector sizing. Pre-fix the
 * sidebar width, row height, font size, padding, and indent were hardcoded as
 * file-scope #defines (NT_UI_INSPECTOR_PANEL_WIDTH 400, CDV_ROW_HEIGHT 30,
 * CDV_OUTER_PADDING 10, CDV_INDENT_WIDTH 16, .fontSize = 16 literal). Games
 * targeting different screen sizes (mobile portrait, ultrawide, etc.) or with
 * different DPI scaling could not adjust. The struct is consumed by both the
 * verbatim Clay debug-view layout emit AND the post-walk overlay's GPU scissor
 * (panel_width). NT_UI_INSPECTOR_METRICS_DEFAULT preserves the previous
 * hardcoded values exactly. nt_ui_create_context initializes ctx with the
 * default; nt_ui_inspector_set_metrics overrides at any time (outside or
 * inside a frame -- the new values take effect from the next emit_layout). */
typedef struct nt_ui_inspector_metrics_t {
    float panel_width;     /* sidebar width in layout pixels (default 400) */
    float row_height;      /* element-row height (default 30) */
    uint16_t font_size;    /* text font size for all inspector labels (default 16) */
    uint8_t outer_padding; /* panel content outer padding (default 10) */
    uint8_t indent_width;  /* per-depth indent step (default 16) */
} nt_ui_inspector_metrics_t;

extern const nt_ui_inspector_metrics_t NT_UI_INSPECTOR_METRICS_DEFAULT;

/* Replaces ctx->inspector_metrics with *metrics. Asserts ctx and metrics
 * non-NULL, asserts panel_width / row_height / font_size strictly > 0. */
void nt_ui_inspector_set_metrics(nt_ui_context_t *ctx, const nt_ui_inspector_metrics_t *metrics);

/* Engine-internal: called by nt_ui_end if the inspector is active. Emits the
 * verbatim Clay debug view as CLAY({...}) blocks INSIDE the in-progress
 * layout pass (between user UI and Clay_EndLayout). Asserts in-frame.
 * Game code does NOT call this directly. */
void nt_ui_inspector_emit_layout(nt_ui_context_t *ctx);

/* Render the hit-zone + id label for the currently-focused element (sidebar
 * hover/click OR viewport hover, as set inside emit_layout). Single-element
 * overlay -- not all zones. Call AFTER nt_ui_walk, BEFORE nt_gfx_end_pass.
 *   target: MUST be the same nt_ui_target_t passed to nt_ui_walk.
 *   font / label_size: id label text (size <= 0 skips label, rect still drawn).
 *
 * Silent skip when inactive, when ctx has no atlas/sprite_material bound, or
 * when no element is focused (inspector_highlight_id == 0). */
void nt_ui_inspector_overlay_draw(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_font_t font, float label_size);

#endif /* NT_UI_INSPECTOR_H */
