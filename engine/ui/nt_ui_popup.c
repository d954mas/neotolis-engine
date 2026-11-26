#include "ui/nt_ui_popup.h"

#include <math.h>

#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_popup_internal.h"

const nt_ui_widget_def_t NT_UI_POPUP_DEF = {
    .name = "nt_popup",
    .pill_color = 0xFF60C0E0U,
    ._reserved = 0U,
};

/* Below this eased t the popup's alpha rounds to 0 (8-bit) — treat as fully closed. */
#define NT_UI_POPUP_EPSILON (1.0F / 256.0F)

nt_ui_popup_style_t nt_ui_popup_style_defaults(void) {
    return (nt_ui_popup_style_t){
        .ease_speed = 14.0F,
        .open_xf_start = nt_ui_transform_defaults(),  /* identity: no spatial animation by default */
        .close_xf_start = nt_ui_transform_defaults(), /* identity */
        .layer = 0U,
    };
}

/* Edge-flip: pick the side actually used this frame. CENTER never flips. Reads the popup's prev-frame
 * size; found==false the first frame -> keep prefer_side (A3). +x = right, +y = down. */
static nt_ui_popup_side_t popup_resolve_side(const nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_anchor_t *anc, float screen_w, float screen_h) {
    const nt_ui_popup_side_t prefer = (nt_ui_popup_side_t)anc->prefer_side;
    if (prefer == NT_UI_POPUP_CENTER) {
        return NT_UI_POPUP_CENTER;
    }
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, id);
    if (!bb.found) {
        return prefer; /* first frame: no measured size, assume preferred side */
    }
    switch (prefer) {
    case NT_UI_POPUP_BELOW:
        return (anc->y + anc->h + bb.height <= screen_h) ? NT_UI_POPUP_BELOW : NT_UI_POPUP_ABOVE;
    case NT_UI_POPUP_ABOVE:
        return (anc->y - bb.height >= 0.0F) ? NT_UI_POPUP_ABOVE : NT_UI_POPUP_BELOW;
    case NT_UI_POPUP_RIGHT:
        return (anc->x + anc->w + bb.width <= screen_w) ? NT_UI_POPUP_RIGHT : NT_UI_POPUP_LEFT;
    case NT_UI_POPUP_LEFT:
        return (anc->x - bb.width >= 0.0F) ? NT_UI_POPUP_LEFT : NT_UI_POPUP_RIGHT;
    case NT_UI_POPUP_CENTER:
    default:
        return NT_UI_POPUP_CENTER;
    }
}

/* Resolved side -> the popup attach corner + the root-space offset of that corner. The popup attaches
 * its own corner to the root (top-left origin) at the returned offset, so it is placed relative to the
 * anchor rect without needing the trigger element to carry a Clay id (avoids parent-not-found). CENTER
 * uses CENTER_CENTER -> root center. +x = right, +y = down. */
static Clay_FloatingAttachPoints popup_attach_points(nt_ui_popup_side_t side) {
    switch (side) {
    case NT_UI_POPUP_BELOW: /* popup top-left placed at the anchor bottom-left */
    case NT_UI_POPUP_RIGHT: /* popup top-left placed at the anchor top-right */
        return (Clay_FloatingAttachPoints){.element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP};
    case NT_UI_POPUP_ABOVE: /* popup bottom-left placed at the anchor top-left */
        return (Clay_FloatingAttachPoints){.element = CLAY_ATTACH_POINT_LEFT_BOTTOM, .parent = CLAY_ATTACH_POINT_LEFT_TOP};
    case NT_UI_POPUP_LEFT: /* popup top-right placed at the anchor top-left */
        return (Clay_FloatingAttachPoints){.element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP};
    case NT_UI_POPUP_CENTER:
    default:
        return (Clay_FloatingAttachPoints){.element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER};
    }
}

/* Root-space offset where the popup's chosen attach corner lands (top-left origin). The vertical
 * sides (BELOW/ABOVE) sit at the anchor's left edge; the horizontal sides (RIGHT/LEFT) at the anchor's
 * top edge. The chosen attach corner (popup_attach_points) makes the popup grow the correct way. */
static Clay_Vector2 popup_offset(nt_ui_popup_side_t side, const nt_ui_popup_anchor_t *anc) {
    switch (side) {
    case NT_UI_POPUP_BELOW:
        return (Clay_Vector2){.x = anc->x, .y = anc->y + anc->h};
    case NT_UI_POPUP_ABOVE:
        return (Clay_Vector2){.x = anc->x, .y = anc->y};
    case NT_UI_POPUP_RIGHT:
        return (Clay_Vector2){.x = anc->x + anc->w, .y = anc->y};
    case NT_UI_POPUP_LEFT:
        return (Clay_Vector2){.x = anc->x, .y = anc->y};
    case NT_UI_POPUP_CENTER:
    default:
        return (Clay_Vector2){.x = 0.0F, .y = 0.0F};
    }
}

/* Eased transform between the per-phase start transform and identity at t=1 (offset + scale lerp).
 * Rotation is carried through linearly too (3D card-flip popups). */
static nt_ui_transform_t popup_eased_transform(const nt_ui_transform_t *start, float t) {
    const float k = 1.0F - t;
    nt_ui_transform_t xf = nt_ui_transform_defaults();
    xf.offset_x = start->offset_x * k;
    xf.offset_y = start->offset_y * k;
    xf.offset_z = start->offset_z * k;
    xf.rotation_x = start->rotation_x * k;
    xf.rotation_y = start->rotation_y * k;
    xf.rotation_z = start->rotation_z * k;
    xf.scale_x = start->scale_x + ((1.0F - start->scale_x) * t);
    xf.scale_y = start->scale_y + ((1.0F - start->scale_y) * t);
    xf.scale_z = start->scale_z + ((1.0F - start->scale_z) * t);
    return xf;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): an assert chain, no control flow
static void popup_check_args(const nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor) {
    NT_ASSERT(ctx != NULL && "nt_ui_popup_begin: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_popup_begin: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(id != 0U && "nt_ui_popup_begin: id must be non-zero");
    NT_ASSERT(style != NULL && "nt_ui_popup_begin: style must be non-NULL");
    NT_ASSERT(anchor != NULL && "nt_ui_popup_begin: anchor must be non-NULL");
    NT_ASSERT(isfinite(style->ease_speed) && style->ease_speed >= 0.0F && "nt_ui_popup_begin: ease_speed must be finite >= 0");
    NT_ASSERT(anchor->prefer_side <= NT_UI_POPUP_CENTER && "nt_ui_popup_begin: prefer_side out of range");
    NT_ASSERT(isfinite(anchor->x) && isfinite(anchor->y) && isfinite(anchor->w) && isfinite(anchor->h) && "nt_ui_popup_begin: anchor rect must be finite");
}

/* Declares a shown overlay on its stack slot's band (spec: "Input ownership", overlay lifecycle); a tooltip passes no
 * `slot` and takes the band above every slot with no catcher or record. False once it faded out: the slot stays
 * for nt_ui_begin to drop, so no band moves within the frame. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool popup_declare(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, const nt_ui_popup_ext_t *ext, nt_ui_overlay_t *slot, bool open,
                          nt_ui_popup_info_t *info) {
    /* Live: open and not declared inside a CLOSING body (an OPENING parent does not delay it). */
    const bool live = open && ctx->closing_from == 0U;
    const bool hears = live && slot != NULL && slot->live; /* OPEN: it was declared open last frame */
    const nt_ui_transform_t *xf_start = open ? &style->open_xf_start : &style->close_xf_start;
    /* scale_* must be > 0 (nt_ui_anim asserts it via the eased transform). */
    NT_ASSERT(xf_start->scale_x > 0.0F && xf_start->scale_y > 0.0F && xf_start->scale_z > 0.0F && "nt_ui_popup_begin: transform scale must be > 0");
    // #region one anim call (value_t eases t toward open?1:0)
    nt_ui_anim_target_t tgt = {
        .scale_x = 1.0F,
        .scale_y = 1.0F,
        .scale_z = 1.0F,
        .off_x = 0.0F,
        .off_y = 0.0F,
        .off_z = 0.0F,
        .rot_x = 0.0F,
        .rot_y = 0.0F,
        .rot_z = 0.0F,
        .opacity = 1.0F,
        .tint_t = 0.0F,
        .value_t = open ? 1.0F : 0.0F,
    };
    if (open && style->ease_speed > 0.0F && nt_ui_internal_live_slot(ctx, id) < 0) {
        nt_ui_anim_target_t seed = tgt;
        seed.value_t = 0.0F;
        nt_ui_anim_seed(ctx, id, &seed); /* the entrance: the panel was not declared last frame */
    }
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, id, &tgt, 0.0F, style->ease_speed);
    const float t = a->value_t;
    // #endregion
    const float screen_w = nt_ui_clay_priv_layout_width(ctx->clay);
    const float screen_h = nt_ui_clay_priv_layout_height(ctx->clay);
    const nt_ui_popup_side_t side = popup_resolve_side(ctx, id, anchor, screen_w, screen_h);
    if (info != NULL) {
        *info = (nt_ui_popup_info_t){.side = (uint8_t)side, .reason = (slot != NULL) ? slot->reason : NT_UI_POPUP_CLOSE_NONE};
    }
    if (slot != NULL) {
        slot->reason = NT_UI_POPUP_CLOSE_NONE;
    }
    if (!open && t <= NT_UI_POPUP_EPSILON) {
        if (slot != NULL) {
            slot->seen = false; /* faded out: the next nt_ui_begin drops it */
        }
        return false;
    }
    if (slot != NULL) {
        slot->seen = true;
        slot->live = live;
        slot->flags = (uint8_t)((slot->flags & (NT_UI_OVERLAY_LIGHT | NT_UI_OVERLAY_CATCHER)) | ext->closes);
        slot->close_pad = ext->close_pad;
    }
    const bool catcher = live && slot != NULL && (slot->flags & NT_UI_OVERLAY_CATCHER) != 0U;
    const int32_t at = (slot != NULL) ? (int32_t)(slot - ctx->overlays) : NT_UI_OVERLAY_MAX; /* a tooltip: above every slot */
    const int32_t band = ctx->overlay_zband_stride + (at * NT_UI_OVERLAY_MAX);
    // #region absolute band, whatever floating encloses it; the catcher shares it, declared before the panel
    const int32_t enclosing = nt_ui_clay_priv_open_floating_z(ctx->clay);
    NT_ASSERT((ctx->overlay_depth > 0U || enclosing < ctx->overlay_zband_stride) && band - enclosing >= INT16_MIN && band - enclosing <= INT16_MAX &&
              "nt_ui_popup: a root overlay's enclosing floating must lie under overlay_zband_stride, within int16 of the band");
    const int16_t panel_z = (int16_t)(band - enclosing);
    if (slot != NULL) {
        NT_ASSERT(ctx->overlay_depth < NT_UI_OVERLAY_MAX && "nt_ui_popup: overlays nested deeper than NT_UI_OVERLAY_MAX");
        ctx->overlay_decl[ctx->overlay_depth++] = id; /* what nested opens see as the one they are inside */
    }
    // #endregion
    // #region catcher/backdrop
    const bool has_backdrop = ext->backdrop_color != 0U;
    const uint32_t catcher_id = nt_ui_internal_popup_catcher_id(id);
    if (catcher || has_backdrop) {
        /* Full-viewport catcher on the panel's band. Transparent for a popup; a visible dim rect when a
         * backdrop color is given (its alpha rides t exactly once — folding t into element
         * opacity too would dim t²). */
        const nt_ui_transform_t id_xf = nt_ui_transform_defaults();
        const nt_ui_element_data_t *catcher_data = nt_ui_make_element_data_xform(style->layer, NULL, &id_xf, 1.0F);
        Clay_Color cc_col = {0};
        if (has_backdrop) {
            cc_col = nt_ui_unpack_abgr(ext->backdrop_color);
            cc_col.a = cc_col.a * t;
        }
        CLAY({
            .id = (Clay_ElementId){.id = catcher_id},
            .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = panel_z},
            .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
            .backgroundColor = cc_col,
            .userData = (void *)catcher_data,
        }) {}
    }
    // #endregion
    // #region panel floating decl (stays OPEN until nt_ui_popup_end)
    const nt_ui_transform_t panel_xf = popup_eased_transform(xf_start, t);
    const nt_ui_element_data_t *panel_data = nt_ui_make_element_data_xform(style->layer, NULL, &panel_xf, t);
    /* Always attach to the root; the anchor rect drives the offset (no trigger-id dependency, so a
     * missing trigger element can't trip Clay's parent-not-found). CENTER folds to root center. */
    Clay_ElementDeclaration panel_decl = {
        .id = (Clay_ElementId){.id = id},
        .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = panel_z, .attachPoints = popup_attach_points(side), .offset = popup_offset(side, anchor)},
    };
    panel_decl.userData = (void *)panel_data;
    const nt_ui_widget_def_t *reg_def = (ext->def != NULL) ? ext->def : &NT_UI_POPUP_DEF;
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(panel_decl);
    nt_ui_widget_register(ctx, id, reg_def, NULL, true);
    if (slot != NULL && open) {
        nt_ui_block_pointer(ctx, id, NULL); /* the drawn panel takes a press, its rows (declared later) win theirs */
    }
    if (slot != NULL && !hears && ctx->inert_from == 0U) {
        ctx->inert_from = ctx->overlay_depth; /* an OPENING or CLOSING body, nested overlays included, is inert */
    }
    if (slot != NULL && !open && ctx->closing_from == 0U) {
        ctx->closing_from = ctx->overlay_depth; /* nothing declared in a CLOSING body is live, nor can it be the top */
    }
    // #endregion
    return true;
}

bool nt_ui_popup_begin_internal(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, const nt_ui_popup_ext_t *ext, nt_ui_popup_info_t *info) {
    popup_check_args(ctx, id, style, anchor);
    NT_ASSERT(ext != NULL && "nt_ui_popup_begin_internal: ext must be non-NULL");
    const int32_t at = nt_ui_internal_overlay_index(ctx, id);
    if (at < 0) {
        if (info != NULL) {
            *info = (nt_ui_popup_info_t){.side = anchor->prefer_side};
        }
        return false; /* not open and not fading: declares nothing */
    }
    return popup_declare(ctx, id, style, anchor, ext, &ctx->overlays[at], ctx->overlays[at].open, info);
}

bool nt_ui_internal_tooltip_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, bool open, nt_ui_popup_info_t *info) {
    popup_check_args(ctx, id, style, anchor);
    static const nt_ui_popup_ext_t none = {0};
    return popup_declare(ctx, id, style, anchor, &none, NULL, open, info);
}

void nt_ui_popup_open(nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && id != 0U && "nt_ui_popup_open: ctx non-NULL, id non-zero");
    nt_ui_internal_overlay_open(ctx, id, nt_ui_internal_overlay_here(ctx, 0U), 0U, NT_UI_OVERLAY_LIGHT | NT_UI_OVERLAY_CATCHER);
}

bool nt_ui_popup_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, nt_ui_popup_info_t *info) {
    /* A popup closes on an outside press or Esc, as a <select>. */
    static const nt_ui_popup_ext_t popup = {.closes = NT_UI_OVERLAY_ESC | NT_UI_OVERLAY_OUTSIDE};
    return nt_ui_popup_begin_internal(ctx, id, style, anchor, &popup, info);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the assert chain inflates the count; control flow is flat
void nt_ui_popup_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_popup_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_popup_end: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(ctx->overlay_depth > 0U && "nt_ui_popup_end: unbalanced begin/end (stack underflow)");
    nt_ui_clay_priv_close_element();
    if (ctx->inert_from == ctx->overlay_depth) {
        ctx->inert_from = 0U; /* the inert popup's subtree ends */
    }
    if (ctx->closing_from == ctx->overlay_depth) {
        ctx->closing_from = 0U;
    }
    --ctx->overlay_depth;
}
