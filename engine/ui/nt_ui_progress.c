#include "ui/nt_ui_progress.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_fill.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"

const nt_ui_widget_def_t NT_UI_PROGRESS_DEF = {
    .name = "nt_progress",
    .pill_color = 0xFFB0D070U,
    ._reserved = 0U,
};

/* Scratch element_data carrying layer + whole-widget opacity (no transform). */
static nt_ui_element_data_t *progress_make_data(void *user_data, uint8_t layer, float opacity) {
    nt_ui_element_data_t *d = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(d != NULL && "nt_ui_progress: scratch alloc failed (element_data)");
    *d = (nt_ui_element_data_t){
        .user_data = user_data,
        .layer = layer,
        .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_OPACITY,
        .transform = nt_ui_transform_defaults(),
        .opacity = opacity,
    };
    return d;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_progress(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t layer, uint32_t id, float value_0_to_1, nt_ui_progress_style_t *style, const Clay_ElementDeclaration *decl) {
    // #region entry asserts
    NT_ASSERT(ctx != NULL && "nt_ui_progress: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_progress: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_progress: style must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_progress: id 0 is the no-widget sentinel");
    NT_ASSERT(isfinite(value_0_to_1) && "nt_ui_progress: value must be finite");
    NT_ASSERT(isfinite(style->track_w) && style->track_w > 0.0F && isfinite(style->track_h) && style->track_h > 0.0F && "nt_ui_progress: style.track_w/track_h must be finite > 0");
    NT_ASSERT(isfinite(style->value_speed) && style->value_speed >= 0.0F && "nt_ui_progress: style.value_speed must be finite >= 0");
    NT_ASSERT(isfinite(style->opacity) && style->opacity >= 0.0F && style->opacity <= 1.0F && "nt_ui_progress: style.opacity must be finite in [0,1]");
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_progress: decl->id must be 0 (id is the explicit param)");
        NT_ASSERT(decl->image.imageData == NULL && "nt_ui_progress: decl->image.imageData must be NULL (style controls track art)");
        NT_ASSERT(decl->backgroundColor.a == 0.0F && "nt_ui_progress: decl->backgroundColor must be zero (style controls tint)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_progress: decl->userData must be NULL (data param controls)");
    }
    if (data != NULL) {
        NT_ASSERT((data->flags & (NT_UI_ELEM_FLAG_HAS_TRANSFORM | NT_UI_ELEM_FLAG_HAS_OPACITY)) == 0U && "nt_ui_progress: data->flags must not set HAS_TRANSFORM/HAS_OPACITY (widget owns these)");
    }
    // #endregion
    const uint8_t draw_layer = (data != NULL) ? data->layer : layer;
    void *user = (data != NULL) ? data->user_data : NULL;

    /* Resolve track + fill refs in place (memoizes the region index). */
    nt_atlas_resolve_ref(&style->track);
    nt_atlas_resolve_ref(&style->fill);

    // #region one anim call (value_t ease toward the clamped target)
    const float target = nt_ui_clampf(value_0_to_1, 0.0F, 1.0F);
    nt_ui_anim_target_t tgt = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = style->opacity, .value_t = target};
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, id, &tgt, 0.0F, style->value_speed);
    const float eased_ratio = nt_ui_clampf(a->value_t, 0.0F, 1.0F);
    const float widget_opacity = (a->opacity >= 0.0F) ? a->opacity : style->opacity;
    // #endregion

    /* OUTER track container is the registered widget id; track art rides it (no-art skip). */
    Clay_ElementDeclaration track_decl = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    track_decl.id = (Clay_ElementId){.id = id};
    track_decl.layout.sizing = (Clay_Sizing){CLAY_SIZING_FIXED(style->track_w), CLAY_SIZING_FIXED(style->track_h)};
    track_decl.userData = (void *)progress_make_data(user, draw_layer, widget_opacity);
    if (style->track.atlas.id != 0U && style->track.region != NT_ATLAS_INVALID_REGION) {
        nt_ui_image_payload_t *tp = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
        NT_ASSERT(tp != NULL && "nt_ui_progress: scratch alloc failed (track payload)");
        *tp = (nt_ui_image_payload_t){.atlas = style->track.atlas, .region_index = style->track.region, .slice9_scale = 1.0F};
        track_decl.image = (Clay_ImageElementConfig){.imageData = tp};
        track_decl.backgroundColor = nt_ui_unpack_tint(style->track_tint);
    }
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(track_decl);
    nt_ui_widget_register(ctx, id, &NT_UI_PROGRESS_DEF, NULL, true);

    /* Fill child via the shared helper; the eased ratio drives the reveal. No-art skip. */
    if (style->fill.atlas.id != 0U && style->fill.region != NT_ATLAS_INVALID_REGION) {
        nt_atlas_region_ref_t fill_ref = style->fill;
        nt_ui_fill_emit(ctx, draw_layer, &fill_ref, style->fill_tint, eased_ratio, style->track_w, style->track_h, style->fill_mode, style->fill_direction, 1.0F);
    }

    nt_ui_clay_priv_close_element();
}

nt_ui_progress_style_t nt_ui_progress_style_defaults(void) {
    nt_ui_progress_style_t s;
    memset(&s, 0, sizeof s); /* memset, not = {0}: emscripten -Werror rejects {0} on aggregate-first */
    s.track_tint = 0xFFFFFFFFU;
    s.fill_tint = 0xFFFFFFFFU;
    s.track_w = 200.0F;
    s.track_h = 16.0F;
    s.fill_mode = NT_UI_FILL_STRETCH;
    s.fill_direction = NT_UI_FILL_LTR;
    s.value_speed = 10.0F;
    s.opacity = 1.0F;
    return s;
}
