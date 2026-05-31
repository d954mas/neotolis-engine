#ifndef NT_UI_INSPECTOR_H
#define NT_UI_INSPECTOR_H

/* Phase 56 ext (CHUNK E): nt_ui_inspector -- engine-extended debug view.
 *
 * Starting point: Clay's built-in Clay__RenderDebugView (clay.h ~line 3392)
 * which shows an element-tree sidebar + selection details + bbox highlight.
 * Port path chosen: HYBRID -- we DO NOT copy ~500 lines of Clay internals.
 * Instead we walk Clay's layout-elements array through the same internal
 * pointers Clay's debug view reads from (Clay_Context.layoutElements +
 * layoutElementsHashMapInternal) and render our OWN sidebar via the public
 * CLAY({...}) macros. This isolates the Clay-internal dependency to a thin
 * iteration helper; sidebar layout is owned by our code so Clay upgrades
 * only touch the iteration cursor.
 *
 * Extensions on TOP of Clay's debug view:
 *   1) Widget-type column from nt_ui_widget_lookup (button/image/panel/group);
 *      labels render as "label(text)"; plain Clay elements as "(plain)".
 *   2) Hit-zone column: for elements whose id is in debug_zones (i.e. the
 *      game queried interaction on them), show padded bbox + the padding
 *      {l,r,t,b}; the existing nt_ui_debug_draw_hit_zones (with the f948916
 *      Y-flip fix) is called automatically -- inspector implies overlay.
 *   3) The Y-flip projection is REUSED from nt_ui_debug (same target arg
 *      contract -- pass the same target as nt_ui_walk).
 *
 * Public API:
 *   set_active / is_active: persistent toggle (zero overhead when off).
 *   draw: HUD-style overlay (renderer flushed before return). Must be called
 *     AFTER nt_ui_walk, BEFORE nt_gfx_end_pass. Same sprite_material binding
 *     contract as nt_ui_debug_draw_hit_zones.
 *
 * Tests: see test_nt_ui_inspector.c (widget_registry slot collision,
 * register/lookup roundtrip, reset each begin, draw no-crash). The visual
 * output is verified in ui_buttons_demo (F3 toggles). */

#include <stdbool.h>

#include "font/nt_font.h"
#include "ui/nt_ui.h" /* nt_ui_target_t */

typedef struct nt_ui_context nt_ui_context_t;

/* Toggle the inspector. Default off (no per-frame tree walk, no draws).
 * When on, nt_ui_inspector_draw renders the sidebar + invokes the
 * hit-zone overlay; the game still owns calling _draw each frame. */
void nt_ui_inspector_set_active(nt_ui_context_t *ctx, bool on);
bool nt_ui_inspector_is_active(const nt_ui_context_t *ctx);

/* Render the inspector overlay (call AFTER nt_ui_walk, BEFORE end_pass).
 * `target` MUST be the same nt_ui_target_t passed to nt_ui_walk -- the
 * hit-zone overlay applies the walker's GL Y-flip to recorded zones (the
 * Pitfall 2 fix from commit f948916). font + label_size are for the sidebar
 * text + zone labels; size <= 0 skips text (rects only).
 *
 * Silent skip when inactive, or when ctx has no atlas/sprite_material bound.
 * Same per-call binding contract as nt_ui_debug_draw_hit_zones. */
void nt_ui_inspector_draw(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_font_t font, float label_size);

#endif /* NT_UI_INSPECTOR_H */
