#include "ui/nt_ui_modal.h"

#include <math.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_state.h"

const nt_ui_widget_def_t NT_UI_MODAL_DEF = {
    .name = "nt_modal",
    .pill_color = 0xFFB070D0U,
    ._reserved = 0U,
};

/* The backdrop id derives from the modal id (salted) so the backdrop's own interaction
 * cell never aliases the panel id in the registry / capture table. */
#define NT_UI_MODAL_BACKDROP_SALT 0x4D0BD000U
static inline uint32_t modal_backdrop_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_MODAL_BACKDROP_SALT); }

/* Below this eased t the modal's alpha rounds to 0 (8-bit) — treat as fully closed.
 * Locks Open Question 3: epsilon = 1/256 so the modal disappears when its alpha quantizes out. */
#define NT_UI_MODAL_EPSILON (1.0F / 256.0F)

#ifdef NT_TEST_ACCESS
static uint16_t s_last_panel_zband;
static uint16_t s_last_backdrop_zband;
static nt_ui_modal_close_reason_t s_last_close_reason;
#endif

nt_ui_modal_style_t nt_ui_modal_style_defaults(void) {
    return (nt_ui_modal_style_t){
        .ease_speed = 14.0F,
        .scale_start = 0.92F, /* scale-pop start; eases to 1.0 (Material/iOS default) */
        .backdrop_alpha = 0.55F,
        .slide_offset = 32.0F,
        .backdrop_color = 0xFF000000U, /* black; alpha is scaled by t*backdrop_alpha */
        .layer = 0U,
        .flags = (uint8_t)(NT_UI_MODAL_LISTEN_ESC | NT_UI_MODAL_CLOSE_ON_BACKDROP | NT_UI_MODAL_TRANSITION_SCALE_POP),
        ._pad = {0U, 0U},
    };
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_ui_modal_result_t nt_ui_modal_begin(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, bool open) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_begin: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_modal_begin: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(id != 0U && "nt_ui_modal_begin: id must be non-zero");
    NT_ASSERT(style != NULL && "nt_ui_modal_begin: style must be non-NULL");
    NT_ASSERT(isfinite(style->ease_speed) && style->ease_speed >= 0.0F && "nt_ui_modal_begin: ease_speed must be finite >= 0");
    NT_ASSERT(isfinite(style->scale_start) && style->scale_start > 0.0F && "nt_ui_modal_begin: scale_start must be finite > 0 (use defaults, never {0})");
    NT_ASSERT(isfinite(style->backdrop_alpha) && style->backdrop_alpha >= 0.0F && style->backdrop_alpha <= 1.0F && "nt_ui_modal_begin: backdrop_alpha must be in [0,1]");
    NT_ASSERT(isfinite(style->slide_offset) && "nt_ui_modal_begin: slide_offset must be finite");
    /* MODAL-05 overflow: assert BEFORE the push — fail-early, no heap growth, no silent fallback. */
    NT_ASSERT(ctx->active_modal_depth < NT_UI_MODAL_MAX_DEPTH && "nt_ui_modal_begin: nesting exceeds NT_UI_MODAL_MAX_DEPTH");

    // #region stack push + z-band
    const uint8_t depth = ctx->active_modal_depth; /* 0-based depth of THIS modal */
    ctx->active_modal_id[depth] = id;
    ++ctx->active_modal_depth;
    /* Track the deepest modal this frame -> next frame's close-scan target (1-frame IM lag). */
    if (ctx->active_modal_depth > ctx->modal_max_depth_cur) {
        ctx->modal_max_depth_cur = ctx->active_modal_depth;
        ctx->modal_top_id_cur = id;
    }
    const int16_t panel_z = (int16_t)(1000 * (depth + 1));
    const int16_t backdrop_z = (int16_t)(panel_z - 1);
    const uint32_t backdrop_id = modal_backdrop_id(id);
    // #endregion
    // #region one anim call (value_t eases t toward open?1:0)
    /* ONE anim call per modal per frame. state_speed=0 (no transform state group); value_speed eases
     * value_t. Build the target field-by-field — NEVER {0} (scale_* must be > 0, asserted in nt_ui_anim). */
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
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, id, &tgt, 0.0F, style->ease_speed);
    const float t = a->value_t; /* eased 0..1 */
    // #endregion
    // #region transition -> panel transform (scale-pop / fade / slide)
    const uint8_t transition = (uint8_t)(style->flags & NT_UI_MODAL_TRANSITION_MASK);
    nt_ui_transform_t panel_xf = nt_ui_transform_defaults();
    if (transition == NT_UI_MODAL_TRANSITION_FADE) {
        /* alpha only — scale fixed at 1 */
    } else if (transition == NT_UI_MODAL_TRANSITION_SLIDE) {
        panel_xf.offset_y = style->slide_offset * (1.0F - t); /* slides up to 0 as it opens */
    } else {
        const float scale = style->scale_start + ((1.0F - style->scale_start) * t);
        panel_xf.scale_x = scale;
        panel_xf.scale_y = scale;
    }
    // #endregion
    // #region backdrop floating decl + input-gate occluder
    /* Backdrop: full-viewport floating rect at backdrop_z; its alpha is t*backdrop_alpha so it
     * fades with the modal. The element opacity rides t too (composes onto children, of which it has none). */
    Clay_Color backdrop_col = nt_ui_unpack_abgr(style->backdrop_color);
    backdrop_col.a = backdrop_col.a * t * style->backdrop_alpha;
    const nt_ui_transform_t id_xf = nt_ui_transform_defaults();
    const nt_ui_element_data_t *backdrop_data = nt_ui_make_element_data_xform(style->layer, NULL, &id_xf, t);
    CLAY({
        .id = (Clay_ElementId){.id = backdrop_id},
        .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = backdrop_z},
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
        .backgroundColor = backdrop_col,
        .userData = (void *)backdrop_data,
    }) {}
    /* D-60-03 input-gate: the backdrop wins next-frame topmost-z over base content, so the world
     * auto-gates (wants_pointer true) and base UI is blocked. Stays active while t>0 (D-60-11). */
    nt_ui_block_pointer(ctx, backdrop_id, NULL);
    // #endregion
    // #region close-source scan (TOP modal only, D-60-08)
    nt_ui_modal_close_reason_t reason = NT_UI_MODAL_CLOSE_NONE;
    /* Top = the deepest modal resolved LAST frame (1-frame IM lag): same-frame nesting can't be known
     * at this begin's return, so the genuinely-topmost modal is the one that was deepest last frame. */
    const bool is_top = (ctx->modal_top_id_prev == id);
    if (is_top && (style->flags & NT_UI_MODAL_LISTEN_ESC) != 0U && nt_input_key_is_pressed(NT_KEY_ESCAPE)) {
        reason = NT_UI_MODAL_CLOSE_ESC;
    }
    if (is_top && (style->flags & NT_UI_MODAL_CLOSE_ON_BACKDROP) != 0U) {
        /* A click landing on the backdrop (outside the panel — the panel occludes its own area at a
         * higher z, see modal_end) raises BACKDROP. step doubles as a registry record + click source. */
        const nt_ui_interaction_t bd = nt_ui_step_interaction(ctx, backdrop_id);
        if (bd.clicked) {
            reason = NT_UI_MODAL_CLOSE_BACKDROP;
        }
    }
    // #endregion
    // #region panel floating decl (stays OPEN until nt_ui_modal_end)
    const nt_ui_element_data_t *panel_data = nt_ui_make_element_data_xform(style->layer, NULL, &panel_xf, t);
    Clay_ElementDeclaration panel_decl = {
        .id = (Clay_ElementId){.id = id},
        .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = panel_z},
        .userData = (void *)panel_data,
    };
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(panel_decl);
    nt_ui_widget_register(ctx, id, &NT_UI_MODAL_DEF, NULL);
    // #endregion

#ifdef NT_TEST_ACCESS
    s_last_panel_zband = (uint16_t)panel_z;
    s_last_backdrop_zband = (uint16_t)backdrop_z;
    s_last_close_reason = reason;
#endif

    return (nt_ui_modal_result_t){
        .t = t,
        .reason = reason,
        .close_requested = (reason != NT_UI_MODAL_CLOSE_NONE),
        .visible = (t > NT_UI_MODAL_EPSILON),
        .fully_closed = (!open && t <= NT_UI_MODAL_EPSILON),
    };
}

void nt_ui_modal_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_modal_end: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(ctx->active_modal_depth > 0U && "nt_ui_modal_end: unbalanced begin/end (stack underflow)");
    /* The panel occludes its own area so a click on the panel background does NOT leak to the
     * backdrop (which would mis-fire CLOSE_ON_BACKDROP). Done at end so it sits inside the open panel. */
    nt_ui_block_pointer(ctx, ctx->active_modal_id[ctx->active_modal_depth - 1U], NULL);
    nt_ui_clay_priv_close_element(); /* close the panel floating element */
    --ctx->active_modal_depth;
}

bool nt_ui_modal(nt_ui_context_t *ctx, uint32_t id, const nt_ui_modal_style_t *style, bool *p_open) {
    NT_ASSERT(p_open != NULL && "nt_ui_modal: p_open must be non-NULL");
    const nt_ui_modal_result_t r = nt_ui_modal_begin(ctx, id, style, *p_open);
    if (r.close_requested) {
        *p_open = false; /* signal -> game's bool clears; t decays over the next frames (D-60-11) */
    }
    if (!r.visible) {
        nt_ui_modal_end(ctx); /* fully closed: balance the stack, caller skips the body */
        return false;
    }
    return true; /* caller declares the body then MUST call nt_ui_modal_end */
}

bool nt_ui_modal_active(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_active: ctx must be non-NULL");
    return ctx->active_modal_depth > 0U;
}

#ifdef NT_TEST_ACCESS
uint16_t nt_ui_modal_test_last_zband(void) { return s_last_panel_zband; }
uint16_t nt_ui_modal_test_last_backdrop_zband(void) { return s_last_backdrop_zband; }
uint8_t nt_ui_modal_test_stack_depth(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_test_stack_depth: ctx must be non-NULL");
    return ctx->active_modal_depth;
}
nt_ui_modal_close_reason_t nt_ui_modal_test_last_close_reason(void) { return s_last_close_reason; }
float nt_ui_modal_test_tween(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_modal_test_tween: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_modal_test_tween: id must be non-zero");
    /* Read the eased value_t directly from the anim slot (read-only; mirrors nt_ui_anim's slot-map).
     * Calling nt_ui_anim here would re-snap the slot, so probe by hand instead. */
    const uint32_t base = id & (uint32_t)(NT_UI_ANIM_SLOTS - 1);
    for (uint32_t k = 0; k < NT_UI_ANIM_PROBE_MAX; ++k) {
        const nt_ui_anim_interaction_t *cand = &ctx->anim[(base + k) & (uint32_t)(NT_UI_ANIM_SLOTS - 1)];
        if (cand->valid && cand->id == id) {
            return cand->value_t;
        }
    }
    return 0.0F; /* no slot = never animated */
}
#endif
