#include "ui/nt_ui_checkbox.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_debug_hit_zones.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"

const nt_ui_widget_def_t NT_UI_CHECKBOX_DEF = {
    .name = "nt_checkbox",
    .pill_color = 0xFF70B0D0U,
    ._reserved = 0U,
};

/* Below this eased value the overlay is not worth a draw call — skip its emit. */
#define NT_UI_CB_OVERLAY_EPS 0.01F
/* Overlay pop: value_t 0->1 maps scale 0.6->1.0 (opacity rides value_t directly). */
#define NT_UI_CB_OVERLAY_POP_MIN_SCALE 0.6F

/* Fail early on out-of-range cell values — silent "almost works" would leak. */
static void assert_cell_valid(const nt_ui_cb_state_t *st) {
    NT_ASSERT(isfinite(st->scale) && st->scale > 0.0F && "nt_ui_checkbox: style cell scale must be finite > 0");
    NT_ASSERT(isfinite(st->offset_x) && isfinite(st->offset_y) && "nt_ui_checkbox: style cell offset must be finite");
    NT_ASSERT(isfinite(st->opacity) && st->opacity >= 0.0F && st->opacity <= 1.0F && "nt_ui_checkbox: style cell opacity must be finite in [0,1]");
}

/* A value row (unchecked/checked) needs SOME art at idle: box or check non-zero. */
static void assert_row_valid(const nt_ui_cb_state_t row[4]) {
    NT_ASSERT((row[NT_UI_CB_IDLE].box.atlas.id != 0U || row[NT_UI_CB_IDLE].check.atlas.id != 0U) && "nt_ui_checkbox: value row idle must have box or check art (D-58-10)");
    for (int i = 0; i < 4; ++i) {
        assert_cell_valid(&row[i]);
    }
}

/* Packed 0xAABBGGRR -> Clay_Color (0..255); 0xFFFFFFFF = no tint ({0}). */
static Clay_Color unpack_tint(uint32_t packed) {
    Clay_Color tint = {0};
    if (packed != 0xFFFFFFFFU) {
        tint.r = (float)(packed & 0xFFU);
        tint.g = (float)((packed >> 8) & 0xFFU);
        tint.b = (float)((packed >> 16) & 0xFFU);
        tint.a = (float)((packed >> 24) & 0xFFU);
    }
    return tint;
}

/* Inherit a non-idle ref from the row idle when its atlas.id is 0 (region==0 is
 * inherit too — NEVER a "no overlay" sentinel; that is value-gating / no-art). */
static nt_atlas_region_ref_t resolve_ref(nt_atlas_region_ref_t cell, nt_atlas_region_ref_t idle) {
    const nt_resource_t atlas = (cell.atlas.id != 0U) ? cell.atlas : idle.atlas;
    const uint32_t region = (cell.region != 0U) ? cell.region : idle.region;
    return (nt_atlas_region_ref_t){atlas, region};
}

/* Build a render-only element_data carrying transform + opacity. */
static nt_ui_element_data_t *make_xform_data(const nt_ui_element_data_t *src, const nt_ui_transform_t *t, float opacity) {
    nt_ui_element_data_t *d = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(d != NULL && "nt_ui_checkbox: scratch alloc failed (element_data)");
    *d = (nt_ui_element_data_t){
        .user_data = (src != NULL) ? src->user_data : NULL,
        .layer = (src != NULL) ? src->layer : 0U,
        .flags = NT_UI_ELEM_FLAG_HAS_TRANSFORM | NT_UI_ELEM_FLAG_HAS_OPACITY,
        .transform = *t,
        .opacity = opacity,
    };
    return d;
}

/* Bundle of post-pick state shared by the box + text emitters (avoids a long
 * positional arg list while keeping each emitter a small focused function). */
typedef struct {
    nt_ui_context_t *ctx;
    uint32_t id;
    const char *label;
    const nt_ui_checkbox_style_t *style;
    const nt_ui_cb_state_t *cell;
    nt_atlas_region_ref_t box_ref;
    nt_atlas_region_ref_t check_ref;
    float eased_value;
    bool is_toggle;
} cb_emit_args_t;

/* Open the box CHILD (childAlignment CENTER, FIXED box_w x box_h), emit box IMAGE
 * (skip no-art terminal), emit the overlay as a Clay child of the box (skip when
 * value <= eps or no check art), close the box. The box has NO id — the ROW owns
 * the widget id + hit-test so box OR label clicks the same widget (D-58-05). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void cb_emit_box(const cb_emit_args_t *e) {
    /* Toggle: thumb is LEFT-anchored so OFF sits at x=0 and the offset_x DELTA
     * slides it right (D-58-16). Checkbox/radio: center the dot in the box. */
    const Clay_LayoutAlignmentX align_x = e->is_toggle ? CLAY_ALIGN_X_LEFT : CLAY_ALIGN_X_CENTER;
    Clay_ElementDeclaration box_decl = {
        .layout =
            {
                .sizing = {CLAY_SIZING_FIXED(e->style->box_w), CLAY_SIZING_FIXED(e->style->box_h)},
                .childAlignment = {align_x, CLAY_ALIGN_Y_CENTER},
            },
    };
    /* No-art terminal: resolved box atlas.id == 0 => skip the IMAGE (D-58-10). */
    if (e->box_ref.atlas.id != 0U) {
        nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
        NT_ASSERT(p != NULL && "nt_ui_checkbox: scratch alloc failed (box payload)");
        *p = (nt_ui_image_payload_t){.atlas = e->box_ref.atlas, .region_index = e->box_ref.region, .slice9_scale = 1.0F};
        box_decl.image = (Clay_ImageElementConfig){.imageData = p};
    }
    box_decl.backgroundColor = unpack_tint(e->cell->box_tint);
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(box_decl);

    /* Overlay child. Checkbox/radio: value-gated pop (skip when eased value is
     * below eps). Toggle: the thumb is ALWAYS visible (the SLIDE, not appearance,
     * is the affordance) — emit whenever there is art, regardless of value_t. */
    const bool overlay_visible = e->is_toggle ? (e->check_ref.atlas.id != 0U) : (e->eased_value > NT_UI_CB_OVERLAY_EPS && e->check_ref.atlas.id != 0U);
    if (overlay_visible) {
        nt_ui_transform_t ov_t = nt_ui_transform_defaults();
        if (e->is_toggle) {
            /* Left-anchored layout; slide is a render-only offset DELTA (D-58-16). */
            const float slide = e->style->box_w - e->style->overlay_w - (2.0F * e->style->thumb_pad);
            ov_t.offset_x = e->eased_value * slide;
        } else {
            /* Centered pop: scale 0.6->1.0 mapped from value_t. */
            const float s = NT_UI_CB_OVERLAY_POP_MIN_SCALE + ((1.0F - NT_UI_CB_OVERLAY_POP_MIN_SCALE) * e->eased_value);
            ov_t.scale_x = s;
            ov_t.scale_y = s;
        }
        /* Toggle thumb is opaque at both ends; checkbox/radio dot fades in by value_t. */
        const float ov_opacity = e->is_toggle ? 1.0F : e->eased_value;
        nt_ui_element_data_t *ov_data = make_xform_data(NULL, &ov_t, ov_opacity);

        nt_ui_image_style_t ov_style = nt_ui_image_style_defaults();
        ov_style.color_packed = e->cell->check_tint;
        const Clay_ElementDeclaration ov_decl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(e->style->overlay_w), CLAY_SIZING_FIXED(e->style->overlay_h)}},
        };
        nt_ui_image(e->ctx, ov_data, e->check_ref, &ov_style, &ov_decl);
    }

    nt_ui_clay_priv_close_element();
}

/* gap spacer + the label text child. Per-cell text_color overrides text_base. */
static void cb_emit_text(const cb_emit_args_t *e) {
    nt_ui_label_style_t text_style = e->style->text_base;
    if (e->cell->text_color != 0U) {
        text_style.color = unpack_tint(e->cell->text_color);
    }
    const Clay_ElementDeclaration gap_decl = {
        .layout = {.sizing = {CLAY_SIZING_FIXED(e->style->gap), CLAY_SIZING_FIXED(0)}},
    };
    CLAY(gap_decl) {}
    nt_ui_label(e->ctx, NULL, e->label, &text_style);
}

/* The single shared composition path (D-58-02). LEAF: opens the box, emits the
 * box IMAGE + overlay child + text child, closes the box — all in one call.
 *   value_is_checked   selects the value row (game owns the bool/int).
 *   is_toggle          drives the thumb slide instead of centered pop.
 *   out_clicked        receives the release-over-widget edge for the wrapper. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void cb_core(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, const char *label, bool value_is_checked, bool is_toggle, const nt_ui_checkbox_style_t *style,
                    const Clay_ElementDeclaration *decl, bool enabled, bool *out_clicked) {
    // #region entry asserts
    NT_ASSERT(ctx != NULL && "nt_ui_checkbox: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_checkbox: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_checkbox: style must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_checkbox: id 0 is the no-widget sentinel");
    NT_ASSERT(isfinite(style->state_speed) && style->state_speed >= 0.0F && "nt_ui_checkbox: style.state_speed must be finite >= 0");
    NT_ASSERT(isfinite(style->value_speed) && style->value_speed >= 0.0F && "nt_ui_checkbox: style.value_speed must be finite >= 0");
    NT_ASSERT(style->box_w > 0.0F && style->box_h > 0.0F && "nt_ui_checkbox: style.box_w/box_h must be > 0 (D-58-14)");
    NT_ASSERT(style->overlay_w >= 0.0F && style->overlay_h >= 0.0F && "nt_ui_checkbox: style.overlay_w/h must be >= 0");
    assert_row_valid(style->unchecked);
    assert_row_valid(style->checked);
    /* Engine owns id/image/backgroundColor/userData; caller's decl must leave these zero. */
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_checkbox: decl->id must be 0 (id is the explicit param)");
        NT_ASSERT(decl->image.imageData == NULL && "nt_ui_checkbox: decl->image.imageData must be NULL (style controls box art)");
        NT_ASSERT(decl->backgroundColor.a == 0.0F && "nt_ui_checkbox: decl->backgroundColor must be zero (style controls tint)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_checkbox: decl->userData must be NULL (data param controls)");
    }
    /* Widget owns transform/opacity; caller may pass layer/user_data in data. */
    if (data != NULL) {
        NT_ASSERT((data->flags & (NT_UI_ELEM_FLAG_HAS_TRANSFORM | NT_UI_ELEM_FLAG_HAS_OPACITY)) == 0U &&
                  "nt_ui_checkbox: data->flags must not set HAS_TRANSFORM/HAS_OPACITY (widget owns these); wrap with a CLAY xform parent instead (D-58-04)");
    }
    // #endregion
    // #region interaction
    /* Disabled skips hit-test but still records a zone so the overlay can show why. */
    nt_ui_interaction_t in;
    if (enabled) {
        in = nt_ui_step_interaction_padded(ctx, id, NULL);
    } else {
        in = (nt_ui_interaction_t){0};
        nt_ui_debug_record_disabled_zone(ctx, id, NULL);
    }
    *out_clicked = in.clicked;
    // #endregion
    // #region value-row + state pick
    const nt_ui_cb_state_t *row = value_is_checked ? style->checked : style->unchecked;
    int state = NT_UI_CB_IDLE;
    if (!enabled) {
        state = NT_UI_CB_DISABLED;
    } else if (in.pressed) {
        state = NT_UI_CB_PRESSED;
    } else if (in.hovered) {
        state = NT_UI_CB_HOVER;
    }
    const nt_ui_cb_state_t *cell = &row[state];
    const nt_atlas_region_ref_t box_ref = resolve_ref(cell->box, row[NT_UI_CB_IDLE].box);
    const nt_atlas_region_ref_t check_ref = resolve_ref(cell->check, row[NT_UI_CB_IDLE].check);
    // #endregion
    // #region one anim call
    /* ONE combined target: state group at state_speed + value_t at value_speed (D-58-18). */
    nt_ui_anim_target_t tgt = {
        .scale_x = cell->scale,
        .scale_y = cell->scale,
        .scale_z = 1.0F,
        .off_x = cell->offset_x,
        .off_y = cell->offset_y,
        .off_z = 0.0F,
        .rot_x = 0.0F,
        .rot_y = 0.0F,
        .rot_z = 0.0F,
        .opacity = cell->opacity,
        .tint_t = 0.0F,
        .value_t = value_is_checked ? 1.0F : 0.0F,
    };
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, id, &tgt, style->state_speed, style->value_speed);
    const float eased_value = a->value_t;
    // #endregion
    // #region open ROW (carries id + hit-test + whole-widget transform/opacity)
    /* The ROW is the registered widget: box OR label clicks the same id (D-58-05).
     * Whole-widget state-group transform + opacity ride the row; opacity inherits
     * down to box/overlay/text (D-58-11). Box/overlay/text are plain children. */
    nt_ui_transform_t row_t = nt_ui_transform_defaults();
    row_t.scale_x = a->scale_x;
    row_t.scale_y = a->scale_y;
    row_t.scale_z = a->scale_z;
    row_t.offset_x = a->off_x;
    row_t.offset_y = a->off_y;
    row_t.offset_z = a->off_z;
    row_t.rotation_x = a->rot_x;
    row_t.rotation_y = a->rot_y;
    row_t.rotation_z = a->rot_z;
    nt_ui_element_data_t *row_data = make_xform_data(data, &row_t, a->opacity);

    Clay_ElementDeclaration row_decl = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    row_decl.id = (Clay_ElementId){.id = id};
    row_decl.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    row_decl.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    row_decl.userData = (void *)row_data;
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(row_decl);
    nt_ui_widget_register(ctx, id, &NT_UI_CHECKBOX_DEF, NULL);
    // #endregion
    // #region ordered children (text-side first, then box; or box, then text)
    cb_emit_args_t args = {
        .ctx = ctx,
        .id = id,
        .label = label,
        .style = style,
        .cell = cell,
        .box_ref = box_ref,
        .check_ref = check_ref,
        .eased_value = eased_value,
        .is_toggle = is_toggle,
    };
    if (label != NULL && style->label_side != 0U) {
        cb_emit_text(&args); /* text on the LEFT */
        cb_emit_box(&args);
    } else {
        cb_emit_box(&args);
        if (label != NULL) {
            cb_emit_text(&args); /* text on the RIGHT (or no label) */
        }
    }
    // #endregion
    nt_ui_clay_priv_close_element(); /* close ROW */
}

bool nt_ui_checkbox(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, const char *label, bool *value, const nt_ui_checkbox_style_t *style, const Clay_ElementDeclaration *decl,
                    bool enabled) {
    NT_ASSERT(value != NULL && "nt_ui_checkbox: value must be non-NULL");
    bool clicked = false;
    cb_core(ctx, data, id, label, *value, false, style, decl, enabled, &clicked);
    if (clicked) {
        *value = !*value;
        return true;
    }
    return false;
}

bool nt_ui_radio(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, const char *label, int *selected, int my_value, const nt_ui_checkbox_style_t *style,
                 const Clay_ElementDeclaration *decl, bool enabled) {
    NT_ASSERT(selected != NULL && "nt_ui_radio: selected must be non-NULL");
    /* Exclusivity is FREE: every radio in the group reads the same *selected, so
     * no engine-side group state is needed (D-58-01/05). value row = am I it? */
    const bool value_is_checked = (*selected == my_value);
    bool clicked = false;
    cb_core(ctx, data, id, label, value_is_checked, false, style, decl, enabled, &clicked);
    /* Re-selecting the already-selected option is a no-op (no flicker, D-58-05). */
    if (clicked && *selected != my_value) {
        *selected = my_value;
        return true;
    }
    return false;
}

bool nt_ui_toggle(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, const char *label, bool *value, const nt_ui_checkbox_style_t *style, const Clay_ElementDeclaration *decl,
                  bool enabled) {
    NT_ASSERT(value != NULL && "nt_ui_toggle: value must be non-NULL");
    /* Same value logic as checkbox; is_toggle=true drives the always-visible
     * thumb + render-only offset_x slide DELTA in cb_emit_box (D-58-16). */
    bool clicked = false;
    cb_core(ctx, data, id, label, *value, true, style, decl, enabled, &clicked);
    if (clicked) {
        *value = !*value;
        return true;
    }
    return false;
}
