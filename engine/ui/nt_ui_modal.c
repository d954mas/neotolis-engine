#include "ui/nt_ui_modal.h"

#include <math.h>

#include <stdint.h>

#include "core/nt_assert.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_popup_internal.h"

/* The modal is a thin composition over popup-core: popup-core owns the z-band push, the
 * value_t tween, the present-only catcher/occluder, the close-scan, and the panel floating decl. The
 * modal adds only the modal-specific surface — the typed anim recipe (scale-pop / slide / fade), the
 * centered placement, the dim backdrop color, and the Esc/backdrop close-reason mapping. The shared
 * depth/z-band machinery lives in exactly one place (no copy-paste of the occluder/z-band). */

const nt_ui_widget_def_t NT_UI_MODAL_DEF = {
    .name = "nt_modal",
    .pill_color = 0xFFB070D0U,
    ._reserved = 0U,
};

/* Build-time sanity net on the DEFAULT stride; the configured per-ctx value is validated at runtime
 * in nt_ui_create_context. Each level declares one stride above its enclosing floating (backdrop one
 * below the panel), so lexically nested overlays still reach stride*(depth+1). */
_Static_assert(NT_UI_MODAL_ZBAND_STRIDE *(NT_UI_MODAL_MAX_DEPTH) <= INT16_MAX, "modal z-band exceeds int16 zIndex");

#ifdef NT_TEST_ACCESS
static nt_ui_modal_close_reason_t s_last_close_reason;
static float s_last_panel_off_x;
static float s_last_panel_off_y;
#endif

nt_ui_modal_style_t nt_ui_modal_style_defaults(void) {
    const nt_ui_modal_anim_t scale_pop = {.type = NT_UI_MODAL_ANIM_SCALE_POP, .scale_start = 0.92F}; /* Material/iOS default */
    return (nt_ui_modal_style_t){
        .ease_speed = 14.0F,
        .backdrop_alpha = 0.55F,
        .open = scale_pop,
        .close = scale_pop,            /* symmetric: the exit reverses the entrance */
        .backdrop_color = 0xFF000000U, /* black; alpha is scaled by t*backdrop_alpha */
        .layer = 0U,
        .flags = (uint8_t)(NT_UI_MODAL_LISTEN_ESC | NT_UI_MODAL_CLOSE_ON_BACKDROP),
        .backdrop_close_pad = 16,
    };
}

/* Per-phase t=0 transform recipe (the popup-core start transform; popup eases it to identity at t=1).
 * +x = right, +y = down. SCALE_POP starts at scale_start; SLIDE starts displaced toward `edge`; FADE
 * starts at identity (the always-on opacity carries the fade). */
static nt_ui_transform_t modal_anim_start(const nt_ui_modal_anim_t *a) {
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

static nt_ui_modal_close_reason_t modal_reason_from_src(nt_ui_popup_close_src_t src) {
    switch (src) {
    case NT_UI_POPUP_CLOSE_SRC_ESC:
        return NT_UI_MODAL_CLOSE_ESC;
    case NT_UI_POPUP_CLOSE_SRC_DISMISS:
        return NT_UI_MODAL_CLOSE_BACKDROP;
    case NT_UI_POPUP_CLOSE_SRC_NONE:
    default:
        return NT_UI_MODAL_CLOSE_NONE;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_ui_modal_result_t nt_ui_modal_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, bool open) {
    NT_ASSERT(style != NULL && "nt_ui_modal_begin: style must be non-NULL");
    NT_ASSERT(isfinite(style->ease_speed) && style->ease_speed >= 0.0F && "nt_ui_modal_begin: ease_speed must be finite >= 0");
    NT_ASSERT(isfinite(style->backdrop_alpha) && style->backdrop_alpha >= 0.0F && style->backdrop_alpha <= 1.0F && "nt_ui_modal_begin: backdrop_alpha must be in [0,1]");
    NT_ASSERT(style->backdrop_close_pad >= 0 && "nt_ui_modal_begin: backdrop_close_pad must be >= 0");
    /* Validate the active recipe before any push so a bad recipe fails early. */
    const nt_ui_modal_anim_t *anim = open ? &style->open : &style->close;
    NT_ASSERT(anim->type <= NT_UI_MODAL_ANIM_SLIDE && "nt_ui_modal_begin: anim.type out of range");
    NT_ASSERT((anim->type != NT_UI_MODAL_ANIM_SCALE_POP || (isfinite(anim->scale_start) && anim->scale_start > 0.0F)) && "nt_ui_modal_begin: SCALE_POP scale_start must be finite > 0");
    NT_ASSERT((anim->type != NT_UI_MODAL_ANIM_SLIDE || (isfinite(anim->offset) && anim->edge <= NT_UI_MODAL_RIGHT)) && "nt_ui_modal_begin: SLIDE offset finite + edge in range");

    /* Map the modal recipe onto popup-core: centered placement + dim backdrop + Esc/backdrop close. */
    const nt_ui_transform_t start = modal_anim_start(anim);
    nt_ui_popup_style_t pstyle = {
        .ease_speed = style->ease_speed,
        .open_xf_start = start, /* unused (ext overrides) but kept valid (scale > 0) */
        .close_xf_start = start,
        .layer = style->layer,
        .flags = 0U, /* modal drives dismiss/gate via ext, not the light-dismiss flag */
    };
    const nt_ui_popup_anchor_t anchor = {.prefer_side = NT_UI_POPUP_CENTER};
    const nt_ui_popup_ext_t ext = {
        .open_xf = &start,
        .close_xf = &start,
        .def = &NT_UI_MODAL_DEF,
        .backdrop_color = style->backdrop_color,
        .backdrop_alpha = style->backdrop_alpha,
        .close_pad = style->backdrop_close_pad,
        .esc_close = (style->flags & NT_UI_MODAL_LISTEN_ESC) != 0U,
        .dismiss_on_click = (style->flags & NT_UI_MODAL_CLOSE_ON_BACKDROP) != 0U,
        .force_catcher = true, /* a modal ALWAYS gates click-through (inert occluder when no backdrop-close) */
    };
    nt_ui_popup_close_src_t src = NT_UI_POPUP_CLOSE_SRC_NONE;
    const nt_ui_popup_result_t pr = nt_ui_popup_begin_internal(ctx, id, &pstyle, &anchor, open, &ext, &src);
    const nt_ui_modal_close_reason_t reason = modal_reason_from_src(src);

#ifdef NT_TEST_ACCESS
    s_last_close_reason = reason;
    /* Panel offset = start offset eased by (1-t); matches popup-core's panel transform exactly. */
    s_last_panel_off_x = start.offset_x * (1.0F - pr.t);
    s_last_panel_off_y = start.offset_y * (1.0F - pr.t);
#endif

    return (nt_ui_modal_result_t){
        .t = pr.t,
        .reason = reason,
        .close_requested = (reason != NT_UI_MODAL_CLOSE_NONE),
        .visible = pr.visible,
        .fully_closed = pr.fully_closed,
    };
}

void nt_ui_modal_end(nt_ui_context_t *ctx) { nt_ui_popup_end(ctx); }

bool nt_ui_modal_visible(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, bool *p_open) {
    NT_ASSERT(p_open != NULL && "nt_ui_modal_visible: p_open must be non-NULL");
    const nt_ui_modal_result_t r = nt_ui_modal_begin(ctx, id, style, *p_open);
    if (r.close_requested) {
        *p_open = false; /* signal -> game's bool clears; t decays over the next frames */
    }
    if (!r.visible) {
        nt_ui_modal_end(ctx); /* fully closed: balance the stack, caller skips the body */
        return false;
    }
    return true; /* caller declares the body then MUST call nt_ui_modal_end */
}

bool nt_ui_modal_active(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_active: ctx must be non-NULL");
    /* Prev-frame presence: the live depth is always 0 once nt_ui_end balanced the stack, so the
     * game gates its NEXT-frame hotkeys on whether a modal was up last frame (inherent IM lag). */
    return ctx->modal_present_prev;
}

void nt_ui_modal_clear_state(nt_ui_context_t *ctx, uint32_t id) { nt_ui_popup_clear_state(ctx, id); }

#ifdef NT_TEST_ACCESS
uint8_t nt_ui_modal_test_stack_depth(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_test_stack_depth: ctx must be non-NULL");
    return ctx->active_modal_depth;
}
nt_ui_modal_close_reason_t nt_ui_modal_test_last_close_reason(void) { return s_last_close_reason; }
float nt_ui_modal_test_tween(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_test_tween: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_modal_test_tween: id must be non-zero");
    /* Delegate to the popup-core tween probe (the modal eases through popup-core's anim slot). */
    return nt_ui_popup_test_tween(ctx, id);
}
void nt_ui_modal_test_last_panel_offset(float *ox, float *oy) {
    if (ox != NULL) {
        *ox = s_last_panel_off_x;
    }
    if (oy != NULL) {
        *oy = s_last_panel_off_y;
    }
}
#endif
