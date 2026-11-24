#include "ui/nt_ui_popup.h"

#include <math.h>

#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
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
        .flags = NT_UI_POPUP_LIGHT_DISMISS,
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool nt_ui_popup_begin_internal(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, bool *open_io, const nt_ui_popup_ext_t *ext,
                                nt_ui_popup_info_t *info, nt_ui_popup_close_src_t *out_src) {
    NT_ASSERT(ctx != NULL && "nt_ui_popup_begin: ctx must be non-NULL");
    NT_ASSERT(open_io != NULL && "nt_ui_popup_begin: open must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_popup_begin: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(id != 0U && "nt_ui_popup_begin: id must be non-zero");
    NT_ASSERT(style != NULL && "nt_ui_popup_begin: style must be non-NULL");
    NT_ASSERT(anchor != NULL && "nt_ui_popup_begin: anchor must be non-NULL");
    NT_ASSERT(isfinite(style->ease_speed) && style->ease_speed >= 0.0F && "nt_ui_popup_begin: ease_speed must be finite >= 0");
    NT_ASSERT(anchor->prefer_side <= NT_UI_POPUP_CENTER && "nt_ui_popup_begin: prefer_side out of range");
    NT_ASSERT(isfinite(anchor->x) && isfinite(anchor->y) && isfinite(anchor->w) && isfinite(anchor->h) && "nt_ui_popup_begin: anchor rect must be finite");
    const uint32_t catcher_id = nt_ui_internal_popup_catcher_id(id);
    /* Backdrop config: ext supplies a visible dim color (modal); otherwise the catcher is transparent. */
    const uint32_t backdrop_color = (ext != NULL) ? ext->backdrop_color : 0U;
    const float backdrop_alpha = (ext != NULL) ? ext->backdrop_alpha : 0.0F;
    const int16_t close_pad = (int16_t)((ext != NULL) ? ext->close_pad : 0);
    const bool has_backdrop = backdrop_color != 0U;
    /* A plain popup gates and dismisses on the light-dismiss flag, by an outside press or Esc as a <select>;
     * the modal and the menu drive each part explicitly. */
    const bool light_dismiss = (style->flags & NT_UI_POPUP_LIGHT_DISMISS) != 0U;
    const bool catcher = (ext != NULL) ? ext->catcher : light_dismiss;
    const bool dismiss_on_click = (ext != NULL) ? ext->dismiss_on_click : light_dismiss;
    const bool esc_close = (ext != NULL) ? ext->esc_close : light_dismiss;
    // #region close-scan: the top popup's own close, before anything of it is declared
    /* Decided from last frame's records and this frame's input (spec: "When a rule runs"): Esc when no field
     * held the keys at nt_ui_begin, or a press of the primary pointer beyond the panel grown by close_pad on
     * its catcher or on no record at all (a letterbox bar, a ray past the UI plane), as Dear ImGui closes a
     * popup on the press. The frame it closes is inert from the start and registers no catcher, so the next
     * press reaches what lies under. */
    nt_ui_popup_close_src_t close_src = NT_UI_POPUP_CLOSE_SRC_NONE;
    if (*open_io && ctx->inert_from == 0U && ctx->overlay_top_id_prev == id) {
        const nt_pointer_t *pp = &ctx->frame_pointers[ctx->frame_primary];
        const bool pressed = pp->buttons[NT_BUTTON_LEFT].is_pressed || pp->buttons[NT_BUTTON_RIGHT].is_pressed;
        const int16_t pad_lrtb[4] = {close_pad, close_pad, close_pad, close_pad};
        const uint32_t hot = ctx->pointer_hot[ctx->frame_primary].id;
        if (esc_close && !ctx->field_owns_keys && nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
            close_src = NT_UI_POPUP_CLOSE_SRC_ESC;
        } else if (dismiss_on_click && pressed && (hot == catcher_id || hot == 0U) && !nt_ui_inspector_pointer_consumed(ctx) && !nt_ui_internal_hit_test_padded(ctx, id, pp->x, pp->y, pad_lrtb)) {
            close_src = NT_UI_POPUP_CLOSE_SRC_DISMISS;
        }
        if (close_src != NT_UI_POPUP_CLOSE_SRC_NONE) {
            *open_io = false; /* a signal for the game's bool; t decays over the next frames */
        }
    }
    if (out_src != NULL) {
        *out_src = close_src;
    }
    const bool open = *open_io;
    // #endregion
    const nt_ui_transform_t *xf_start = open ? &style->open_xf_start : &style->close_xf_start;
    /* scale_* must be > 0 (nt_ui_anim asserts it via the eased transform). */
    NT_ASSERT(xf_start->scale_x > 0.0F && xf_start->scale_y > 0.0F && xf_start->scale_z > 0.0F && "nt_ui_popup_begin: transform scale must be > 0");
    const bool root_overlay = ctx->overlay_depth == 0U;
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
    /* The entrance eases from 0 on a real closed->open edge: the panel was not declared last frame (a hidden
     * popup declares nothing). An anim slot a busy frame evicted restarts at its target, so it never flickers. */
    if (open && style->ease_speed > 0.0F && nt_ui_internal_live_slot(ctx, id) < 0) {
        nt_ui_anim_target_t seed = tgt;
        seed.value_t = 0.0F;
        nt_ui_anim_seed(ctx, id, &seed); /* WRITE the pool slot at t=0; the eased call below ramps from it */
    }
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, id, &tgt, 0.0F, style->ease_speed);
    const float t = a->value_t;
    // #endregion
    const float screen_w = nt_ui_clay_priv_layout_width(ctx->clay);
    const float screen_h = nt_ui_clay_priv_layout_height(ctx->clay);
    const nt_ui_popup_side_t side = popup_resolve_side(ctx, id, anchor, screen_w, screen_h);
    const bool present = open || (t > NT_UI_POPUP_EPSILON);
    if (info != NULL) {
        *info = (nt_ui_popup_info_t){.t = t, .side = (uint8_t)side};
    }
    if (!present) {
        return false; /* a hidden popup declares nothing: no panel, no band, no end */
    }
    // #region z-band + stack push (shared depth counter with modal)
    /* Assert BEFORE the push — fail-early, no heap growth, no silent fallback. */
    NT_ASSERT(ctx->overlay_depth < NT_UI_MODAL_MAX_DEPTH && "nt_ui_popup_begin: nesting exceeds NT_UI_MODAL_MAX_DEPTH");
    /* Root overlays are a top layer: each takes the frame's next absolute root band, whatever game floating
     * encloses it, so a later one covers an earlier one with all its nested levels; a nested one sits a band
     * above its parent, over the parent's widget parts (delta 0). The catcher, declared before its panel on
     * the same band, lies between them. */
    int16_t panel_z = 1;
    if (root_overlay) {
        const int32_t band = ctx->modal_zband_stride + ((int32_t)ctx->overlay_root_count * NT_UI_MODAL_MAX_DEPTH);
        const int32_t enclosing = nt_ui_clay_priv_open_floating_z(ctx->clay);
        NT_ASSERT(band + NT_UI_MODAL_MAX_DEPTH - 1 <= INT16_MAX && "nt_ui_popup: a root overlay's nested levels would pass the int16 z-band");
        NT_ASSERT(enclosing < ctx->modal_zband_stride && band - enclosing <= INT16_MAX &&
                  "nt_ui_popup: a root overlay's enclosing floating must lie under modal_zband_stride, within int16 of the band");
        panel_z = (int16_t)(band - enclosing);
        ++ctx->overlay_root_count;
    }
    ++ctx->overlay_depth;
    // #endregion
    // #region catcher/backdrop
    /* Only an open popup outside an inert subtree gates the UI under it with its catcher and claims the top;
     * otherwise it is inert and only paints, its backdrop included (spec: "Input ownership"). Only a
     * catcher-bearing popup claims the top (and nt_ui_overlay_active): a catcher-less one (a tooltip) must
     * not steal it from a coexisting dropdown. */
    const bool inert_parent = ctx->inert_from != 0U;
    const bool want_catcher = open && !inert_parent && catcher;
    if (want_catcher || has_backdrop) {
        /* Full-viewport catcher on the panel's band. Transparent for a plain popup; a visible dim rect when a
         * backdrop color is given (alpha rides t*backdrop_alpha exactly once — folding t into element
         * opacity too would dim t²). It wins next-frame topmost-z, gating click-through to base UI. */
        const nt_ui_transform_t id_xf = nt_ui_transform_defaults();
        const nt_ui_element_data_t *catcher_data = nt_ui_make_element_data_xform(style->layer, NULL, &id_xf, 1.0F);
        Clay_Color cc_col = {0};
        if (has_backdrop) {
            cc_col = nt_ui_unpack_abgr(backdrop_color);
            cc_col.a = cc_col.a * t * backdrop_alpha;
        }
        CLAY({
            .id = (Clay_ElementId){.id = catcher_id},
            .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = panel_z},
            .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
            .backgroundColor = cc_col,
            .userData = (void *)catcher_data,
        }) {}
    }
    if (want_catcher) {
        nt_ui_block_pointer(ctx, catcher_id, NULL); /* the gate; the close-scan above reads the press on it */
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
    const nt_ui_widget_def_t *reg_def = (ext != NULL && ext->def != NULL) ? ext->def : &NT_UI_POPUP_DEF;
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(panel_decl);
    nt_ui_widget_register(ctx, id, reg_def, NULL, true);
    /* The panel is the innermost open floating here, so this IS the band Clay stamped. The claim below
     * ranks by band, ties to the last declared: the panels' tree-root order, which paints them and is the
     * 2D pointer arbiter's key, so the winner's own catcher takes the outside press. A 3D ctx arbitrates by
     * ray distance and does not rank overlays by band at all. */
    const int32_t panel_band = nt_ui_clay_priv_open_floating_z(ctx->clay);
    /* A catcher on the base band loses the hit arbitration and silently stops gating while
     * nt_ui_overlay_active() still reports the overlay up. */
    NT_ASSERT((!want_catcher || panel_band > 0) && "nt_ui_popup: the catcher must sit above the base band (0)");
    if (want_catcher && panel_band >= ctx->overlay_top_z_cur) {
        ctx->overlay_top_z_cur = panel_band;
        ctx->overlay_top_id_cur = id;
    }
    if (!open && !inert_parent) {
        ctx->inert_from = ctx->overlay_depth; /* its body, nested popups included, is inert */
    }
    // #endregion
    return true;
}

bool nt_ui_popup_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, bool *open, nt_ui_popup_info_t *info) {
    return nt_ui_popup_begin_internal(ctx, id, style, anchor, open, NULL, info, NULL);
}

void nt_ui_popup_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_popup_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_popup_end: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(ctx->overlay_depth > 0U && "nt_ui_popup_end: unbalanced begin/end (stack underflow)");
    nt_ui_clay_priv_close_element();
    if (ctx->inert_from == ctx->overlay_depth) {
        ctx->inert_from = 0U; /* the inert popup's subtree ends */
    }
    --ctx->overlay_depth;
}
