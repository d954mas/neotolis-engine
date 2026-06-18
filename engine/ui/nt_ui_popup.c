#include "ui/nt_ui_popup.h"

#include <math.h>

#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_popup_internal.h"
#include "ui/nt_ui_state.h"

const nt_ui_widget_def_t NT_UI_POPUP_DEF = {
    .name = "nt_popup",
    .pill_color = 0xFF60C0E0U,
    ._reserved = 0U,
};

/* The catcher id derives from the popup id (salted) so the catcher's own interaction cell never
 * aliases the panel id in the registry / capture table. */
#define NT_UI_POPUP_CATCHER_SALT 0x500CA7C0U
static inline uint32_t popup_catcher_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_POPUP_CATCHER_SALT); }

/* The tween-state cell id (salted off the popup id) lives in the NON-evicting nt_ui_state pool, unlike
 * the eviction-prone anim slot. It records whether the popup was present last frame so the entrance
 * re-seed (t=0) fires ONLY on a real closed->open edge — never when a busy frame evicts the anim slot
 * (which would re-seed every frame and flicker the eased open). */
#define NT_UI_POPUP_TWEEN_SALT 0x500B7E00U
static inline uint32_t popup_tween_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_POPUP_TWEEN_SALT); }

/* Persisted across frames in the state pool (zeroed on create -> was_present starts false). */
typedef struct {
    uint8_t was_present; /* 1 if the popup declared its panel last frame (open or still tweening closed) */
} nt_ui_popup_tween_state_t;

/* Below this eased t the popup's alpha rounds to 0 (8-bit) — treat as fully closed. */
#define NT_UI_POPUP_EPSILON (1.0F / 256.0F)

/* True when this id has no live anim slot yet (read-only probe — does NOT touch/snap the slot). */
static bool popup_anim_slot_absent(const nt_ui_context_t *ctx, uint32_t id) {
    const uint32_t base = id & (uint32_t)(NT_UI_ANIM_SLOTS - 1);
    for (uint32_t k = 0; k < NT_UI_ANIM_PROBE_MAX; ++k) {
        const nt_ui_anim_interaction_t *cand = &ctx->anim[(base + k) & (uint32_t)(NT_UI_ANIM_SLOTS - 1)];
        if (cand->valid && cand->id == id) {
            return false;
        }
    }
    return true;
}

#ifdef NT_TEST_ACCESS
static uint16_t s_last_panel_zband;
static uint16_t s_last_catcher_zband;
static uint8_t s_last_side;
static bool s_last_catcher_present;
static uint32_t s_entrance_seed_count; /* # of entrance t=0 re-seeds; must fire once per open-edge, not per frame */
#endif

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
nt_ui_popup_result_t nt_ui_popup_begin_internal(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, bool open, const nt_ui_popup_ext_t *ext,
                                                nt_ui_popup_close_src_t *out_src) {
    NT_ASSERT(ctx != NULL && "nt_ui_popup_begin: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_popup_begin: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(id != 0U && "nt_ui_popup_begin: id must be non-zero");
    NT_ASSERT(style != NULL && "nt_ui_popup_begin: style must be non-NULL");
    NT_ASSERT(anchor != NULL && "nt_ui_popup_begin: anchor must be non-NULL");
    NT_ASSERT(isfinite(style->ease_speed) && style->ease_speed >= 0.0F && "nt_ui_popup_begin: ease_speed must be finite >= 0");
    NT_ASSERT(anchor->prefer_side <= NT_UI_POPUP_CENTER && "nt_ui_popup_begin: prefer_side out of range");
    NT_ASSERT(isfinite(anchor->x) && isfinite(anchor->y) && isfinite(anchor->w) && isfinite(anchor->h) && "nt_ui_popup_begin: anchor rect must be finite");
    /* Per-phase panel transform start: ext override (modal recipe) wins, else the style start. */
    const nt_ui_transform_t *xf_start = open ? &style->open_xf_start : &style->close_xf_start;
    if (ext != NULL) {
        const nt_ui_transform_t *ovr = open ? ext->open_xf : ext->close_xf;
        if (ovr != NULL) {
            xf_start = ovr;
        }
    }
    /* scale_* must be > 0 (nt_ui_anim asserts it via the eased transform). */
    NT_ASSERT(xf_start->scale_x > 0.0F && xf_start->scale_y > 0.0F && xf_start->scale_z > 0.0F && "nt_ui_popup_begin: transform scale must be > 0");
    /* Assert BEFORE the push — fail-early, no heap growth, no silent fallback. */
    NT_ASSERT(ctx->active_modal_depth < NT_UI_MODAL_MAX_DEPTH && "nt_ui_popup_begin: nesting exceeds NT_UI_MODAL_MAX_DEPTH");

    // #region stack push + z-band (shared depth counter with modal)
    const uint8_t depth = ctx->active_modal_depth;
    ++ctx->active_modal_depth;
    const int16_t panel_z = (int16_t)(ctx->modal_zband_stride * (depth + 1));
    const int16_t catcher_z = (int16_t)(panel_z - 1);
    const uint32_t catcher_id = popup_catcher_id(id);
    // #endregion
    // #region tween-state (non-evicting): was the popup present last frame?
    /* Eviction-proof open-edge memory: the anim slot can be evicted by a busy frame, so we must NOT use
     * "anim slot absent" to mean "first open" — that re-seeds t=0 every churned frame (flicker). */
    nt_ui_popup_tween_state_t *tw = (nt_ui_popup_tween_state_t *)nt_ui_state(ctx, popup_tween_id(id), (uint32_t)sizeof *tw, NT_UI_STATE_TAG('p', 'o', 'p', 't'));
    NT_ASSERT(tw != NULL && "nt_ui_popup_begin: tween-state pool returned NULL");
    const bool was_present_prev = tw->was_present != 0U;
    // #endregion
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
    /* Entrance must ease from 0: on a real closed->open EDGE (not present last frame), seed a fresh slot
     * at t=0 first (no-flash), then the real call eases up. Gated on the non-evicting tween-state, NOT
     * the anim slot — an evicted slot must NOT re-seed (that flickers the eased open under churn). The
     * anim-slot-absent check still guards the very first ever touch (state cell fresh, slot truly new). */
    if (open && !was_present_prev && popup_anim_slot_absent(ctx, id)) {
        nt_ui_anim_target_t seed = tgt;
        seed.value_t = 0.0F;
        (void)nt_ui_anim(ctx, id, &seed, 0.0F, 0.0F);
#ifdef NT_TEST_ACCESS
        s_entrance_seed_count++;
#endif
    }
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, id, &tgt, 0.0F, style->ease_speed);
    const float t = a->value_t;
    // #endregion
    // #region edge-flip side + catcher/backdrop (present-only) + close-scan
    const float screen_w = nt_ui_clay_priv_layout_width(ctx->clay);
    const float screen_h = nt_ui_clay_priv_layout_height(ctx->clay);
    const nt_ui_popup_side_t side = popup_resolve_side(ctx, id, anchor, screen_w, screen_h);

    /* Backdrop config: ext supplies a visible dim color (modal); otherwise the catcher is transparent. */
    const uint32_t backdrop_color = (ext != NULL) ? ext->backdrop_color : 0U;
    const float backdrop_alpha = (ext != NULL) ? ext->backdrop_alpha : 0.0F;
    const int16_t close_pad = (int16_t)((ext != NULL) ? ext->close_pad : 0);
    const bool esc_close = (ext != NULL) && ext->esc_close;
    const bool has_backdrop = backdrop_color != 0U;
    /* A plain popup dismisses on the light-dismiss flag; the modal drives dismiss/gate explicitly. */
    const bool light_dismiss = (style->flags & NT_UI_POPUP_LIGHT_DISMISS) != 0U;
    const bool dismiss_on_click = (ext != NULL) ? ext->dismiss_on_click : light_dismiss;
    const bool force_catcher = (ext != NULL) && ext->force_catcher;
    /* A fully-closed popup (!open && settled) declares NO catcher: the occluder would otherwise gate the
     * whole viewport every frame the helper runs, making base UI unclickable (present-only guard). The
     * catcher carries the dismiss source and/or the visible backdrop and/or an inert input gate. */
    const bool present = open || (t > NT_UI_POPUP_EPSILON);
    tw->was_present = present ? 1U : 0U; /* remember for next frame's open-edge seed gate */
    const bool want_catcher = present && (dismiss_on_click || has_backdrop || force_catcher);
    nt_ui_popup_close_src_t close_src = NT_UI_POPUP_CLOSE_SRC_NONE;
    if (present) {
        ctx->modal_present_cur = true;
        if (ctx->active_modal_depth >= ctx->modal_max_depth_cur) {
            ctx->modal_max_depth_cur = ctx->active_modal_depth;
            ctx->modal_top_id_cur = id;
        }
    }
    if (want_catcher) {
        /* Full-viewport catcher at catcher_z. Transparent for a plain popup; a visible dim rect when a
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
            .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = catcher_z},
            .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
            .backgroundColor = cc_col,
            .userData = (void *)catcher_data,
        }) {}
        /* Close-scan on the TOP popup only (1-frame IM lag). Esc fires first; otherwise an outside-click
         * beyond the panel grown by close_pad dismisses (a click on the panel body is the body's own).
         * A catcher that neither dismisses nor escapes stays an inert block_pointer input gate. */
        const bool is_top = (ctx->modal_top_id_prev == id);
        if (is_top && esc_close && nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
            close_src = NT_UI_POPUP_CLOSE_SRC_ESC;
        }
        if (is_top && dismiss_on_click) {
            const nt_ui_interaction_t click = nt_ui_step_interaction(ctx, catcher_id);
            if (close_src == NT_UI_POPUP_CLOSE_SRC_NONE && click.clicked) {
                const int16_t pad_lrtb[4] = {close_pad, close_pad, close_pad, close_pad};
                if (!nt_ui_internal_hit_test_padded(ctx, id, click.pos[0], click.pos[1], pad_lrtb)) {
                    close_src = NT_UI_POPUP_CLOSE_SRC_DISMISS;
                }
            }
        } else {
            nt_ui_block_pointer(ctx, catcher_id, NULL); /* inert gate (non-top, or no dismiss source) */
        }
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
    nt_ui_widget_register(ctx, id, reg_def, NULL);
    // #endregion

    if (out_src != NULL) {
        *out_src = close_src;
    }

#ifdef NT_TEST_ACCESS
    s_last_panel_zband = (uint16_t)panel_z;
    s_last_catcher_zband = (uint16_t)catcher_z;
    s_last_side = (uint8_t)side;
    s_last_catcher_present = want_catcher;
#endif

    return (nt_ui_popup_result_t){
        .t = t,
        .close_requested = (close_src != NT_UI_POPUP_CLOSE_SRC_NONE),
        .visible = present,
        .fully_closed = (!open && t <= NT_UI_POPUP_EPSILON),
        .side = (uint8_t)side,
    };
}

nt_ui_popup_result_t nt_ui_popup_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, bool open) {
    return nt_ui_popup_begin_internal(ctx, id, style, anchor, open, NULL, NULL);
}

void nt_ui_popup_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_popup_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_popup_end: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(ctx->active_modal_depth > 0U && "nt_ui_popup_end: unbalanced begin/end (stack underflow)");
    nt_ui_clay_priv_close_element();
    --ctx->active_modal_depth;
}

bool nt_ui_popup_visible(nt_ui_context_t *ctx, uint32_t id, const nt_ui_popup_style_t *style, const nt_ui_popup_anchor_t *anchor, bool *p_open) {
    NT_ASSERT(p_open != NULL && "nt_ui_popup_visible: p_open must be non-NULL");
    const nt_ui_popup_result_t r = nt_ui_popup_begin(ctx, id, style, anchor, *p_open);
    if (r.close_requested) {
        *p_open = false; /* signal -> game's bool clears; t decays over the next frames */
    }
    if (!r.visible) {
        nt_ui_popup_end(ctx); /* fully closed: balance the stack, caller skips the body */
        return false;
    }
    return true;
}

#ifdef NT_TEST_ACCESS
uint16_t nt_ui_popup_test_last_zband(void) { return s_last_panel_zband; }
uint16_t nt_ui_popup_test_last_catcher_zband(void) { return s_last_catcher_zband; }
uint8_t nt_ui_popup_test_stack_depth(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_popup_test_stack_depth: ctx must be non-NULL");
    return ctx->active_modal_depth;
}
uint8_t nt_ui_popup_test_last_side(void) { return s_last_side; }
bool nt_ui_popup_test_last_catcher_present(void) { return s_last_catcher_present; }
uint32_t nt_ui_popup_test_entrance_seed_count(void) { return s_entrance_seed_count; }
void nt_ui_popup_test_entrance_seed_reset(void) { s_entrance_seed_count = 0U; }
float nt_ui_popup_test_tween(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_popup_test_tween: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_popup_test_tween: id must be non-zero");
    const uint32_t base = id & (uint32_t)(NT_UI_ANIM_SLOTS - 1);
    for (uint32_t k = 0; k < NT_UI_ANIM_PROBE_MAX; ++k) {
        const nt_ui_anim_interaction_t *cand = &ctx->anim[(base + k) & (uint32_t)(NT_UI_ANIM_SLOTS - 1)];
        if (cand->valid && cand->id == id) {
            return cand->value_t;
        }
    }
    return 0.0F;
}
#endif
