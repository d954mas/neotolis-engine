#include "ui/nt_ui_modal.h"

#include <math.h>

#include <stdint.h>

#include "core/nt_assert.h"
#include "core/nt_builtins.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_popup_internal.h"

/* The modal is a thin composition over popup-core, which owns the stack band, the value_t tween, the open-only
 * catcher and the panel floating decl. The modal adds the typed anim recipe (scale-pop / slide / fade), the
 * centered placement, the dim backdrop and the Esc/backdrop closes its style asks for. */

const nt_ui_widget_def_t NT_UI_MODAL_DEF = {
    .name = "nt_modal",
    .pill_color = 0xFFB070D0U,
    ._reserved = 0U,
};

nt_ui_modal_style_t nt_ui_modal_style_defaults(void) {
    const nt_ui_modal_anim_t scale_pop = {.type = NT_UI_MODAL_ANIM_SCALE_POP, .scale_start = 0.92F}; /* Material/iOS default */
    return (nt_ui_modal_style_t){
        .ease_speed = 14.0F,
        .open = scale_pop,
        .close = scale_pop,            /* symmetric: the exit reverses the entrance */
        .backdrop_color = 0x8C000000U, /* black at 0.55 peak opacity; the alpha rides t */
        .layer = 0U,
        .flags = (uint8_t)(NT_UI_MODAL_LISTEN_ESC | NT_UI_MODAL_CLOSE_ON_BACKDROP),
        .backdrop_close_pad = 16,
    };
}

/* Per-phase t=0 transform recipe (the popup-core start transform; popup eases it to identity at t=1).
 * +x = right, +y = down. SCALE_POP starts at scale_start; SLIDE starts displaced toward `edge`; FADE
 * starts at identity (the always-on opacity carries the fade). */
/* Out of line: begin calls it for both phases, and two inlined copies cost ~0.3 KB of wasm. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static NT_NOINLINE nt_ui_transform_t modal_anim_start(const nt_ui_modal_anim_t *a) {
    NT_ASSERT(a->type <= NT_UI_MODAL_ANIM_SLIDE && "nt_ui_modal_begin: anim.type out of range");
    NT_ASSERT((a->type != NT_UI_MODAL_ANIM_SCALE_POP || (isfinite(a->scale_start) && a->scale_start > 0.0F)) && "nt_ui_modal_begin: SCALE_POP scale_start must be finite > 0");
    NT_ASSERT((a->type != NT_UI_MODAL_ANIM_SLIDE || (isfinite(a->offset) && a->edge <= NT_UI_MODAL_RIGHT)) && "nt_ui_modal_begin: SLIDE offset finite + edge in range");
    nt_ui_transform_t xf = nt_ui_transform_defaults();
    switch ((nt_ui_modal_anim_type_t)a->type) {
    case NT_UI_MODAL_ANIM_FADE:
        break; /* identity */
    case NT_UI_MODAL_ANIM_SLIDE:
        switch ((nt_ui_modal_edge_t)a->edge) {
        case NT_UI_MODAL_TOP:
            xf.offset_y = -a->offset;
            break;
        case NT_UI_MODAL_LEFT:
            xf.offset_x = -a->offset;
            break;
        case NT_UI_MODAL_RIGHT:
            xf.offset_x = a->offset;
            break;
        case NT_UI_MODAL_BOTTOM:
        default:
            xf.offset_y = a->offset;
            break;
        }
        break;
    case NT_UI_MODAL_ANIM_SCALE_POP:
    default:
        xf.scale_x = a->scale_start;
        xf.scale_y = a->scale_start;
        break;
    }
    return xf;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool nt_ui_modal_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, nt_ui_popup_info_t *info) {
    NT_ASSERT(style != NULL && "nt_ui_modal_begin: style must be non-NULL");
    NT_ASSERT(isfinite(style->ease_speed) && style->ease_speed >= 0.0F && "nt_ui_modal_begin: ease_speed must be finite >= 0");

    NT_ASSERT(style->backdrop_close_pad >= 0 && "nt_ui_modal_begin: backdrop_close_pad must be >= 0");

    /* Map the modal onto popup-core: centered placement + dim backdrop + Esc/backdrop close. Both recipes,
     * validated before any push: the core picks one after its own close-scan. */
    nt_ui_popup_style_t pstyle = {
        .ease_speed = style->ease_speed,
        .open_xf_start = modal_anim_start(&style->open),
        .close_xf_start = modal_anim_start(&style->close),
        .layer = style->layer,
    };
    const nt_ui_popup_anchor_t anchor = {.prefer_side = NT_UI_POPUP_CENTER};
    const nt_ui_popup_ext_t ext = {
        .def = &NT_UI_MODAL_DEF,
        .backdrop_color = style->backdrop_color,
        .close_pad = style->backdrop_close_pad,
        .esc_close = (style->flags & NT_UI_MODAL_LISTEN_ESC) != 0U,
        .outside_close = (style->flags & NT_UI_MODAL_CLOSE_ON_BACKDROP) != 0U,
    };
    return nt_ui_popup_begin_internal(ctx, id, &pstyle, &anchor, &ext, info);
}

void nt_ui_modal_open(nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && id != 0U && "nt_ui_modal_open: ctx non-NULL, id non-zero");
    /* A modal is never a light popup's child, as HTML's showModal: its parent is the innermost overlay around the call
     * that is not light, so the menu or list it is opened from closes (S2) and the modal stays. */
    uint32_t parent = 0U;
    for (uint8_t d = ctx->overlay_depth; d > 0U && parent == 0U; --d) {
        const int32_t at = nt_ui_internal_overlay_index(ctx, ctx->overlay_decl[d - 1U]);
        parent = (at >= 0 && !ctx->overlays[at].light) ? ctx->overlays[at].id : 0U;
    }
    /* A modal ALWAYS gates click-through and is not light dismiss: only the game, or its style, closes it. */
    nt_ui_internal_overlay_open(ctx, id, parent, 0U, NT_UI_OVERLAY_CATCHER);
}

void nt_ui_modal_end(nt_ui_context_t *ctx) { nt_ui_popup_end(ctx); }
