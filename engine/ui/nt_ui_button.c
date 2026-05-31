#include "ui/nt_ui_button.h"

#include <math.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_button_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, nt_resource_t atlas, const nt_ui_button_style_t *style, bool enabled) {
    NT_ASSERT(ctx != NULL && "nt_ui_button_begin: ctx must be non-NULL");
    NT_ASSERT(style != NULL && "nt_ui_button_begin: style must be non-NULL");
    NT_ASSERT(atlas.id != 0 && "nt_ui_button_begin: invalid atlas handle");
    NT_ASSERT(id != 0U && "nt_ui_button_begin: id 0 is the no-widget sentinel");
    NT_ASSERT(!ctx->pending_button.active && "nt_ui_button: nested buttons unsupported");
    // #region state-pick + ease
    /* Disabled short-circuits the hit-test/capture entirely (D-56-12). The
     * padded query forwards the style's touch-target inflation (zero = old
     * behavior); disabled path still gets a zeroed interaction. */
    nt_ui_interaction_t in = enabled ? nt_ui_get_interaction_padded(ctx, id, style->hit_padding_lrtb) : (nt_ui_interaction_t){0};

    /* D-56-14 priority: disabled ? : pressed ? : hover ? : idle. */
    const nt_ui_btn_state_t *st = &style->idle;
    if (!enabled) {
        st = &style->disabled;
    } else if (in.pressed) {
        st = &style->pressed;
    } else if (in.hovered) {
        st = &style->hover;
    }

    /* Ease the picked state's visual toward the cache (Plan 02). */
    nt_ui_anim_target_t tgt = {
        .scale = st->scale,
        .off_x = st->offset_x,
        .off_y = st->offset_y,
        .opacity = st->opacity,
        .tint_t = 0.0F,
    };
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, id, &tgt, style->transition_speed);
    // #endregion
    // #region apply transform + opacity (ALWAYS -- balanced on disabled path, Pitfall 3)
    nt_ui_transform_t t = nt_ui_transform_defaults();
    t.scale_x = fmaxf(a->scale, 0.001F); /* scale must be > 0 (transform assert) */
    t.scale_y = t.scale_x;
    t.offset_x = a->off_x;
    t.offset_y = a->off_y;
    nt_ui_push_transform(ctx, &t);
    nt_ui_push_opacity(ctx, clampf(a->opacity, 0.0F, 1.0F));
    // #endregion
    // #region open Clay IMAGE element WITH .id (the ONE structural addition over panel)
    /* bg_region 0 = same as idle (D-56-11). */
    const uint32_t region = (st->bg_region != 0U) ? st->bg_region : style->idle.bg_region;

    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_button_begin: scratch alloc failed");
    *p = (nt_ui_image_payload_t){
        .atlas = atlas,
        .region_index = region,
    };
    /* slice9 from atlas default (no override); origin/flip default. */

    /* Unpack bg_tint (0xAABBGGRR) like panel unpacks color_packed. */
    Clay_Color tint = {0};
    if (st->bg_tint != 0xFFFFFFFF) {
        tint.r = (float)(st->bg_tint & 0xFFU);
        tint.g = (float)((st->bg_tint >> 8) & 0xFFU);
        tint.b = (float)((st->bg_tint >> 16) & 0xFFU);
        tint.a = (float)((st->bg_tint >> 24) & 0xFFU);
    }

    Clay__OpenElement();
    Clay__ConfigureOpenElement((Clay_ElementDeclaration){
        .id = (Clay_ElementId){.id = id},
        .backgroundColor = tint,
        .image = {.imageData = p},
        .userData = (void *)data,
    });
    // #endregion

    ctx->pending_button.active = true;
    ctx->pending_button.clicked = in.clicked;
}

bool nt_ui_button_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_button_end: ctx must be non-NULL");
    NT_ASSERT(ctx->pending_button.active && "nt_ui_button_end without begin");

    Clay__CloseElement();
    /* Reverse push order from begin (opacity then transform). */
    nt_ui_pop_opacity(ctx);
    nt_ui_pop_transform(ctx);

    const bool clicked = ctx->pending_button.clicked;
    ctx->pending_button.active = false;
    ctx->pending_button.clicked = false;
    return clicked;
}

bool nt_ui_button(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, nt_resource_t atlas, const char *label, const nt_ui_label_style_t *label_style,
                  const nt_ui_button_style_t *style, bool enabled) {
    nt_ui_button_begin(ctx, data, id, atlas, style, enabled);
    nt_ui_label(ctx, NULL, label, label_style);
    return nt_ui_button_end(ctx);
}
