#ifndef NT_UI_TOOLTIP_H
#define NT_UI_TOOLTIP_H

/* Tooltip (WGT-03) built ON popup-core (Plan 03) WITHOUT a light-dismiss catcher (D-65-08 exception:
 * hover-driven, no close_requested path) so it never gates base UI. The engine tracks the hover-delay
 * in a state-pool cell keyed by a SALTED tooltip id (D-65-09: NOT a game bool) — the game just declares
 * the target_id + content + delay. The cell accumulates hover seconds (ctx->frame_dt) while the cursor
 * is over the target and resets to 0 on leave; the floating panel is declared at the target's anchor
 * only once the accumulated hover reaches style->delay_secs.
 *
 * The target's hover is read via nt_ui_query_interaction (IDEMPOTENT) on a salted id so the tooltip
 * never becomes a second mutating interaction step on the underlying widget (T-65-19). */

#include <stdbool.h>
#include <stdint.h>

#include "ui/nt_ui.h" /* nt_ui_layer_t, nt_ui_widget_def_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_TOOLTIP_DEF;

/* Visual + timing knobs. Colors are 0xAABBGGRR. */
typedef struct {
    uint32_t panel_bg;   /* tooltip panel fill */
    uint32_t text_color; /* tooltip label color */
    float delay_secs;    /* accumulated hover before reveal; asserted finite && >= 0 (T-65-17) */
    float font_size;     /* px; asserted > 0 */
    uint16_t max_width;  /* px panel max width (0 = no cap, label drives width) */
    uint16_t pad;        /* px inner padding */
    uint16_t font_id;    /* label font */
    nt_ui_layer_t layer; /* draw layer */
    uint8_t _pad[1];
} nt_ui_tooltip_style_t;
_Static_assert(sizeof(nt_ui_tooltip_style_t) == 24, "nt_ui_tooltip_style_t stable ABI (2 u32 + 2 float + 3 u16 + 1 u8 layer + 1 pad)");

/* Valid baseline style (dark), 0.5s reveal delay. */
nt_ui_tooltip_style_t nt_ui_tooltip_style_defaults(void);

/* Declare a tooltip for `target_id` (a widget declared THIS frame). Call every frame AFTER the target
 * so the target carries a queryable bbox. The engine owns the hover timer in the state pool; the panel
 * appears below the target (edge-flip ABOVE near the bottom border) once the cursor has hovered the
 * target for style->delay_secs, and hides the instant the cursor leaves. NO catcher is declared, so the
 * tooltip cannot gate base UI. Returns true on the frames the tooltip panel is declared (visible).
 * ctx/content/style non-NULL; target_id non-zero. */
bool nt_ui_tooltip(nt_ui_context_t *ctx, uint32_t target_id, const char *content, const nt_ui_tooltip_style_t *style);

#ifdef NT_TEST_ACCESS
/* The salted state-pool id the tooltip uses for its hover-delay timer cell (test probe). */
uint32_t nt_ui_tooltip_test_timer_id(uint32_t target_id);
/* The accumulated hover seconds in the timer cell for target_id (0 if no cell). */
float nt_ui_tooltip_test_hover_secs(nt_ui_context_t *ctx, uint32_t target_id);
#endif

#endif /* NT_UI_TOOLTIP_H */
