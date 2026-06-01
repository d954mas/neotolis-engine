#ifndef NT_UI_INSPECTOR_H
#define NT_UI_INSPECTOR_H

/* Phase 56 ext rework: nt_ui_inspector -- a Clay-styled debug view + engine
 * extensions.
 *
 * Design ported from Clay's built-in `Clay__RenderDebugView` (deps/clay/clay.h:
 * lines ~3392-3800 -- the floating right-pinned debug panel with element tree,
 * collapse indicators, config-type pills, and per-element info pane). Clay is
 * zlib-licensed; the visual structure (colors `CLAY__DEBUGVIEW_COLOR_1..4`,
 * row heights, layout proportions, info-pane attribute rows) is reproduced
 * here as faithfully as Clay's source supports for a post-walk overlay
 * (Clay's own debug view runs INSIDE the declaration phase via CLAY({...})
 * macros; ours runs AFTER nt_ui_walk and emits sprite + text commands
 * directly through our renderers, which keeps the inspector callable in the
 * same place as nt_stats_draw).
 *
 * Extensions ON TOP of the Clay design:
 *   1) Widget-type pill per row -- pulled from ctx->widget_registry: button /
 *      image / label / panel / group (or "(plain Clay)" for raw CLAY).
 *   2) Hit-zone overlay -- the inspector calls nt_ui_debug_draw_hit_zones at
 *      the end of its draw when debug_recording is on. This RE-COUPLES the
 *      inspector and the overlay: F3 toggles both, F1 is redundant.
 *
 * Public API:
 *   set_active / is_active  -- persistent toggle (zero overhead when off).
 *   draw                    -- call AFTER nt_ui_walk, BEFORE nt_gfx_end_pass.
 *
 * The inspector reads Clay state (layoutElements + hashmap + idStrings) via
 * nt_ui_internal_collect_tree_rows / nt_ui_internal_get_element_info -- those
 * accessors live in nt_ui.c next to CLAY_IMPLEMENTATION, so the inspector TU
 * never sees Clay private types directly.
 *
 * Tests: tests/unit/test_nt_ui_inspector.c -- widget_registry (untouched) +
 * inspector toggle + no-crash on zero/many widgets. The visual look is
 * verified in ui_buttons_demo (F3 toggle). */

#include <stdbool.h>

#include "font/nt_font.h"
#include "ui/nt_ui.h" /* nt_ui_target_t */

typedef struct nt_ui_context nt_ui_context_t;

/* Toggle the inspector. Default off (no per-frame draws). When on,
 * nt_ui_inspector_draw renders the Clay-styled element tree + info pane AND,
 * if ctx->debug_recording is true, the hit-zone overlay on top of the
 * declared widgets. */
void nt_ui_inspector_set_active(nt_ui_context_t *ctx, bool on);
bool nt_ui_inspector_is_active(const nt_ui_context_t *ctx);

/* Render the inspector (call AFTER nt_ui_walk, BEFORE end_pass).
 * `target` MUST be the same nt_ui_target_t passed to nt_ui_walk -- the
 * inspector emits in world space using the same Y-flip and viewport.
 * font + label_size drive the panel text; size <= 0 skips text (panel
 * background still renders).
 *
 * Silent skip when inactive, or when ctx has no atlas/sprite_material bound. */
void nt_ui_inspector_draw(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_font_t font, float label_size);

#endif /* NT_UI_INSPECTOR_H */
