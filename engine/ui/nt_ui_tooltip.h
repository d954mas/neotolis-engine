#ifndef NT_UI_TOOLTIP_H
#define NT_UI_TOOLTIP_H

/* Tooltip built ON popup-core WITHOUT a light-dismiss catcher: hover-driven with no close_requested
 * path, so it never gates base UI. The engine tracks the hover-delay in a state-pool cell keyed by a
 * SALTED tooltip id (engine-owned, not a game bool) — the game just declares the target_id + content +
 * delay. The cell accumulates hover seconds (ctx->frame_dt) while the cursor is over the target and
 * resets to 0 on leave; the floating panel is declared at the target's anchor only once the accumulated
 * hover reaches style->delay_secs.
 *
 * The target's hover is read via nt_ui_query_interaction (IDEMPOTENT) on a salted id so the tooltip
 * never becomes a second mutating interaction step on the underlying widget. */

#include <stdbool.h>
#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "ui/nt_ui.h"       /* nt_ui_layer_t, nt_ui_widget_def_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_TOOLTIP_DEF;

/* Visual + timing knobs. Colors are 0xAABBGGRR. The panel is an OPTIONAL slice9 (panel_art ref) with a
 * flat panel_bg fallback; an OPTIONAL caret sprite points at the target (flips with the popup side); an
 * OPTIONAL thin border + drop-shadow (both 0 = off). layer comes from the call (data->layer), NOT the
 * style — mirrors checkbox. */
typedef struct {
    nt_atlas_region_ref_t panel_art; /* optional panel slice9 art; atlas.id==0 = flat panel_bg */
    nt_atlas_region_ref_t caret;     /* optional caret/arrow sprite pointing at the target; atlas.id==0 = no caret */
    uint32_t panel_bg;               /* flat panel fallback color 0xAABBGGRR (also the panel art tint base) */
    uint32_t text_color;             /* tooltip label color */
    uint32_t panel_tint;             /* multiplies the panel slice9 art; 0xFFFFFFFF = no tint */
    uint32_t caret_tint;             /* caret sprite tint 0xAABBGGRR */
    uint32_t border_color;           /* thin panel border 0xAABBGGRR (0 = no border) */
    uint32_t shadow_color;           /* drop-shadow rect 0xAABBGGRR, usually translucent (0 = no shadow) */
    float delay_secs;                /* accumulated hover before reveal; asserted finite && >= 0 */
    float font_size;                 /* px; asserted > 0 */
    float open_ease_speed;           /* popup open/close tween speed (0 = snap; game opts into a tween) */
    float slice9_scale;              /* multiplies the panel region's baked slice9 borders; > 0 */
    int16_t shadow_offset_px;        /* drop-shadow x==y offset (0 = no shadow even if shadow_color set) */
    uint16_t max_width;              /* px panel max width (0 = no cap, label drives width) */
    uint16_t pad;                    /* px inner padding */
    uint16_t font_id;                /* label font */
    uint16_t caret_size;             /* px caret sprite box (0 = no caret even if a ref is set) */
    uint16_t border_px;              /* px border width (0 = no border even if border_color set) */
    uint16_t corner_radius;          /* px panel rounding (flat fallback only; IMAGE bg can't round) */
} nt_ui_tooltip_style_t;
_Static_assert(sizeof(nt_ui_tooltip_style_t) == 88, "nt_ui_tooltip_style_t stable ABI (2x16 ref + 6 u32 + 4 float + 1 i16 + 6 u16 + tail pad to align 8)");

/* Valid baseline style (dark), 0.5s reveal delay. */
nt_ui_tooltip_style_t nt_ui_tooltip_style_defaults(void);

/* Declare a tooltip for `target_id` (a widget declared THIS frame). Call every frame AFTER the target
 * so the target carries a queryable bbox. The engine owns the hover timer in the state pool; the panel
 * appears below the target (edge-flip ABOVE near the bottom border) once the cursor has hovered the
 * target for style->delay_secs, and hides the instant the cursor leaves. NO catcher is declared, so the
 * tooltip cannot gate base UI. Returns true on the frames the tooltip panel is declared (visible).
 * ctx/content/style non-NULL; target_id non-zero.
 * Layers: the panel fill + caret + shadow draw on data->layer (also the popup panel layer), the label on
 * label_layer (split to batch). data may be NULL (fill falls to layer 0). style is mutated in place to
 * memoize resolved atlas refs (panel_art / caret). */
bool nt_ui_tooltip(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t target_id, const char *content, nt_ui_tooltip_style_t *style);

#ifdef NT_TEST_ACCESS
/* The salted state-pool id the tooltip uses for its hover-delay timer cell (test probe). */
uint32_t nt_ui_tooltip_test_timer_id(uint32_t target_id);
/* The accumulated hover seconds in the timer cell for target_id (0 if no cell). */
float nt_ui_tooltip_test_hover_secs(nt_ui_context_t *ctx, uint32_t target_id);
#endif

#endif /* NT_UI_TOOLTIP_H */
