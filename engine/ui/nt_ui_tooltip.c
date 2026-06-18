#include "ui/nt_ui_tooltip.h"

#include <math.h>
#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_state.h"

const nt_ui_widget_def_t NT_UI_TOOLTIP_DEF = {
    .name = "nt_tooltip",
    .pill_color = 0xFF80C0FFU,
    ._reserved = 0U,
};

/* Sibling cell ids derived from the target id: the hover-delay timer + the popup panel each need a
 * stable non-aliasing id. Sparse high-bit salts (never a real low id), distinct from the target so the
 * timer cell never aliases the target's own state (T-65-19). */
#define NT_UI_TOOLTIP_TIMER_SALT 0x70900000U
#define NT_UI_TOOLTIP_POPUP_SALT 0x70910000U
#define NT_UI_TOOLTIP_PANEL_SALT 0x70920000U

static inline uint32_t tooltip_timer_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_TOOLTIP_TIMER_SALT); }
static inline uint32_t tooltip_popup_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_TOOLTIP_POPUP_SALT); }
static inline uint32_t tooltip_panel_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_TOOLTIP_PANEL_SALT); }

/* The engine-owned hover-delay cell (D-65-09: NOT a game bool). Zeroed on create -> hover starts at 0. */
typedef struct {
    float hover; /* accumulated seconds the cursor has been over the target */
} nt_ui_tooltip_cell_t;

nt_ui_tooltip_style_t nt_ui_tooltip_style_defaults(void) {
    return (nt_ui_tooltip_style_t){
        .panel_bg = 0xFF202020U,
        .text_color = 0xFFE8E8E8U,
        .delay_secs = 0.5F,
        .font_size = 13.0F,
        .open_ease_speed = 0.0F, /* snap-open by default; the game opts into a tween */
        .max_width = 240U,
        .pad = 6U,
        .font_id = 0U,
    };
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — complexity is the validation assert chain, not control flow
bool nt_ui_tooltip(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t target_id, const char *content, const nt_ui_tooltip_style_t *style) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_tooltip: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(target_id != 0U && content != NULL && style != NULL && "nt_ui_tooltip: target_id non-zero, pointers non-NULL");
    NT_ASSERT(isfinite(style->delay_secs) && style->delay_secs >= 0.0F && "nt_ui_tooltip: delay_secs finite && >= 0"); /* T-65-17 */
    NT_ASSERT(style->font_size > 0.0F && "nt_ui_tooltip: font_size > 0");

    const uint8_t fill_layer = (data != NULL) ? data->layer : 0U; /* panel fill on data->layer, label on label_layer */

    /* Engine-tracked hover-delay timer in the state pool. */
    nt_ui_tooltip_cell_t *c = nt_ui_state(ctx, tooltip_timer_id(target_id), sizeof *c, NT_UI_STATE_TAG('t', 'i', 'p', ' '));

    /* IDEMPOTENT read of the target's hover — must NOT become a second mutating step on the target id
     * (T-65-19). The game's own step_interaction stays the sole capture mutator. */
    const nt_ui_interaction_t in = nt_ui_query_interaction(ctx, target_id);
    if (in.hovered) {
        c->hover += ctx->frame_dt;
    } else {
        c->hover = 0.0F;
    }

    const bool open = (c->hover >= style->delay_secs);

    /* Anchor below the target; popup-core edge-flips ABOVE near the bottom border. */
    nt_ui_popup_anchor_t anc = {.prefer_side = NT_UI_POPUP_BELOW};
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, target_id);
    if (bb.found) {
        anc.x = bb.x;
        anc.y = bb.y;
        anc.w = bb.width;
        anc.h = bb.height;
    }

    nt_ui_popup_style_t pst = nt_ui_popup_style_defaults();
    pst.ease_speed = style->open_ease_speed; /* game-controlled open tween (0 = snap) */
    pst.flags = 0U;                          /* widget-owned (D-65-08): NO light-dismiss catcher — hover-driven, never gates base UI */
    pst.layer = fill_layer;                  /* widget-owned: the popup panel sits on the fill layer */

    bool shown = false;
    /* Low-level begin/end (no one-bool wrapper): the open state is engine-derived from the hover timer,
     * not a game bool, and there is no close_requested path to harvest. */
    const nt_ui_popup_result_t r = nt_ui_popup_begin(ctx, tooltip_popup_id(target_id), &pst, &anc, open);
    if (r.visible) {
        const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(style->text_color)};
        Clay_SizingAxis w = (style->max_width > 0U) ? CLAY_SIZING_FIT(.max = (float)style->max_width) : CLAY_SIZING_FIT(0);
        CLAY({.id = (Clay_ElementId){.id = tooltip_panel_id(target_id)},
              .layout = {.sizing = {.width = w, .height = CLAY_SIZING_FIT(0)}, .padding = CLAY_PADDING_ALL(style->pad)},
              .backgroundColor = nt_ui_unpack_abgr(style->panel_bg),
              .cornerRadius = CLAY_CORNER_RADIUS(4),
              .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)}) {
            nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), content, &lbl);
        }
        shown = true;
    }
    nt_ui_popup_end(ctx);
    return shown;
}

#ifdef NT_TEST_ACCESS
uint32_t nt_ui_tooltip_test_timer_id(uint32_t target_id) { return tooltip_timer_id(target_id); }
float nt_ui_tooltip_test_hover_secs(nt_ui_context_t *ctx, uint32_t target_id) {
    const nt_ui_tooltip_cell_t *c = (const nt_ui_tooltip_cell_t *)nt_ui_state_find(ctx, tooltip_timer_id(target_id));
    return (c != NULL) ? c->hover : 0.0F;
}
#endif
