#include "ui/nt_ui_button.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_debug_hit_zones.h"
#include "ui/nt_ui_internal.h"

const nt_ui_widget_def_t NT_UI_BUTTON_DEF = {
    .name = "nt_button",
    .pill_color = 0xFF60D070U,
    ._reserved = 0U,
};

/* Fail early on out-of-range style values — silent "almost works" would otherwise leak. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void assert_state_valid(const nt_ui_btn_state_t *st, const char *which) {
    (void)which;
    NT_ASSERT(isfinite(st->scale) && st->scale > 0.0F && "nt_ui_button_begin: style.scale must be finite and > 0");
    NT_ASSERT(isfinite(st->offset_x) && isfinite(st->offset_y) && "nt_ui_button_begin: style.offset must be finite");
    NT_ASSERT(isfinite(st->opacity) && st->opacity >= 0.0F && st->opacity <= 1.0F && "nt_ui_button_begin: style.opacity must be finite in [0,1]");
    /* bg_tint with alpha=0 silently renders an invisible button (white RGB clipped by tint mul).
     * Game wanting fade-to-invisible should use opacity, not tint alpha. 0xFFFFFFFFU = no tint. */
    NT_ASSERT((st->bg_tint == 0xFFFFFFFFU || (st->bg_tint & 0xFF000000U) != 0U) && "nt_ui_button_begin: style.bg_tint alpha=0 hides the button; use 0xFFFFFFFFU for no tint or set opacity instead");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_button_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, const nt_ui_button_style_t *style, const Clay_ElementDeclaration *decl, bool enabled) {
    NT_ASSERT(ctx != NULL && "nt_ui_button_begin: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_button_begin: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_button_begin: style must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_button_begin: id 0 is the no-widget sentinel");
    NT_ASSERT(!ctx->pending_button.active && "nt_ui_button: nested buttons unsupported");
    NT_ASSERT(isfinite(style->transition_speed) && style->transition_speed >= 0.0F && "nt_ui_button_begin: style.transition_speed must be finite >= 0");
    NT_ASSERT(isfinite(style->slice9_scale) && style->slice9_scale > 0.0F && "nt_ui_button_begin: style.slice9_scale must be finite > 0");
    assert_state_valid(&style->idle, "idle");
    assert_state_valid(&style->hover, "hover");
    assert_state_valid(&style->pressed, "pressed");
    assert_state_valid(&style->disabled, "disabled");
    /* Engine owns id/image/backgroundColor/userData; caller's decl must leave these zero. */
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_button_begin: decl->id must be 0 (id is the explicit param)");
        NT_ASSERT(decl->image.imageData == NULL && "nt_ui_button_begin: decl->image.imageData must be NULL (atlas+region controls image)");
        NT_ASSERT(decl->backgroundColor.a == 0.0F && "nt_ui_button_begin: decl->backgroundColor must be zero (style->bg_tint controls)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_button_begin: decl->userData must be NULL (data param controls)");
    }
    /* Button owns transform/opacity; caller may pass layer/user_data in data. */
    if (data != NULL) {
        NT_ASSERT((data->flags & (NT_UI_ELEM_FLAG_HAS_TRANSFORM | NT_UI_ELEM_FLAG_HAS_OPACITY)) == 0U &&
                  "nt_ui_button_begin: data->flags must not set HAS_TRANSFORM/HAS_OPACITY (button owns these via style); wrap with a CLAY xform parent instead");
    }
    // #region state-pick + ease
    /* Disabled skips hit-test but still records a zone so the overlay can show why. */
    nt_ui_interaction_t in;
    if (enabled) {
        in = nt_ui_step_interaction_padded(ctx, id, style->hit_padding_lrtb);
    } else {
        in = (nt_ui_interaction_t){0};
        nt_ui_debug_record_disabled_zone(ctx, id, style->hit_padding_lrtb);
    }

    /* Priority: disabled → pressed → hover → idle. */
    const nt_ui_btn_state_t *st = &style->idle;
    if (!enabled) {
        st = &style->disabled;
    } else if (in.pressed) {
        st = &style->pressed;
    } else if (in.hovered) {
        st = &style->hover;
    }

    /* Button style is 2D-only (scale uniform, no Z-axis fields, no rotation) — feed 1/0 defaults to
     * the 3D-extended target so the eased anim slot still tracks the full 9-field state for ids
     * shared with 3D widget consumers. */
    nt_ui_anim_target_t tgt = {
        .scale_x = st->scale,
        .scale_y = st->scale,
        .scale_z = 1.0F,
        .off_x = st->offset_x,
        .off_y = st->offset_y,
        .off_z = 0.0F,
        .rot_x = 0.0F,
        .rot_y = 0.0F,
        .rot_z = 0.0F,
        .opacity = st->opacity,
        .tint_t = 0.0F,
    };
    /* Button is value_t-agnostic: pass value_t=0 (in tgt) + value_speed=0. */
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, id, &tgt, style->transition_speed, 0.0F);
    // #endregion
    // #region build_element_data
    /* Parent xform requires wrapping with a CLAY block — build_tree composes downward. */
    nt_ui_element_data_t *btn_data = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(btn_data != NULL && "nt_ui_button_begin: scratch alloc failed (element_data)");
    *btn_data = (nt_ui_element_data_t){
        .user_data = (data != NULL) ? data->user_data : NULL,
        .layer = (data != NULL) ? data->layer : 0U,
        .flags = NT_UI_ELEM_FLAG_HAS_TRANSFORM | NT_UI_ELEM_FLAG_HAS_OPACITY,
        .transform = {.scale_x = a->scale_x,
                      .scale_y = a->scale_y,
                      .scale_z = a->scale_z,
                      .offset_x = a->off_x,
                      .offset_y = a->off_y,
                      .offset_z = a->off_z,
                      .rotation_x = a->rot_x,
                      .rotation_y = a->rot_y,
                      .rotation_z = a->rot_z},
        .opacity = a->opacity,
    };
    // #endregion
    // #region open_clay_image
    /* atlas/region 0 falls back to idle. */
    const nt_resource_t st_atlas = (st->bg.atlas.id != 0U) ? st->bg.atlas : style->idle.bg.atlas;
    const uint32_t region = (st->bg.region != 0U) ? st->bg.region : style->idle.bg.region;

    const Clay_Color tint = (st->bg_tint == 0xFFFFFFFFU) ? (Clay_Color){0} : nt_ui_unpack_abgr(st->bg_tint);

    Clay_ElementDeclaration final = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    final.id = (Clay_ElementId){.id = id};
    /* Resolved atlas.id == 0 = text-only button: no background art, no IMAGE payload. */
    if (st_atlas.id != 0U) {
        nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
        NT_ASSERT(p != NULL && "nt_ui_button_begin: scratch alloc failed (image_payload)");
        *p = (nt_ui_image_payload_t){
            .atlas = st_atlas,
            .region_index = region,
            .slice9_scale = style->slice9_scale,
        };
        final.image = (Clay_ImageElementConfig){.imageData = p};
    }
    final.backgroundColor = tint;
    final.userData = (void *)btn_data;
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(final);
    // #endregion

    nt_ui_widget_register(ctx, id, &NT_UI_BUTTON_DEF, style->hit_padding_lrtb);

    ctx->pending_button.active = true;
    ctx->pending_button.clicked = in.clicked;
}

bool nt_ui_button_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_button_end: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_button_end: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(ctx->pending_button.active && "nt_ui_button_end without begin");

    nt_ui_clay_priv_close_element();

    const bool clicked = ctx->pending_button.clicked;
    ctx->pending_button.active = false;
    ctx->pending_button.clicked = false;
    return clicked;
}

bool nt_ui_button(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, const nt_ui_button_style_t *style, const Clay_ElementDeclaration *decl, bool enabled) {
    nt_ui_button_begin(ctx, data, id, style, decl, enabled);
    return nt_ui_button_end(ctx);
}
