#include "ui/nt_ui_slider.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_debug_hit_zones.h"
#include "ui/nt_ui_fill.h"
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_state.h"

const nt_ui_widget_def_t NT_UI_SLIDER_DEF = {
    .name = "nt_slider",
    .pill_color = 0xFFD0A070U,
    ._reserved = 0U,
};

/* Drag grab-offset cell (state pool, keyed off the slider id). press ON the thumb stores the
 * relative grab so it is preserved across the drag; press on the track stores thumb_w/2 (thumb
 * jumps under the pointer). Lives press->release; captures[] has no public payload slot for it. */
#define NT_UI_SLIDER_DRAG_SALT 0x510D3201U
typedef struct {
    float grab_offset; /* press_pos.x - thumb_left at grab; thumb_w/2 = track-jump */
    uint8_t active;    /* 1 while a drag is live */
    uint8_t _pad[3];
} nt_ui_slider_drag_t;

/* Persistent thumb-view cell (separate salt; written EVERY frame, never cleared). Lets the
 * const-ctx nt_ui_slider_thumb_pos recover the eased fraction + axis-aware thumb geometry without an
 * anim-pool accessor — the pool's no-eviction keeps last frame's value live. */
#define NT_UI_SLIDER_VIEW_SALT 0x510D7E00U
typedef struct {
    float fraction; /* last eased value fraction [0,1] */
    float track_w, track_h;
    float thumb_w, thumb_h;
    uint8_t orientation; /* nt_ui_slider_orientation_t: thumb_pos branches on the AXIS */
    uint8_t invert;      /* vertical BOTTOM_UP: thumb pos measured from the far (bottom) edge */
    uint8_t _pad[2];
} nt_ui_slider_view_t;

/* Linear position->value map, clamped to [min,max]. usable = track_extent - thumb_extent.
 * Axis-neutral: callers pass either X (horizontal) or Y (vertical) coordinates. invert
 * flips the fraction for BOTTOM_UP (screen-Y grows down, value grows up). */
static float slider_pos_to_value(float pointer_pos, float track_origin, float usable, float thumb_extent, float min, float max, bool invert) {
    const float thumb_lead = pointer_pos - track_origin - (thumb_extent * 0.5F);
    float frac = (usable > 0.0F) ? nt_ui_clampf(thumb_lead / usable, 0.0F, 1.0F) : 0.0F;
    if (invert) {
        frac = 1.0F - frac;
    }
    return min + ((max - min) * frac);
}

/* step != 0 quantizes value onto the min + k*step grid; 0 = continuous. */
static float slider_quantize(float value, float min, float max, float step) {
    if (step <= 0.0F) {
        return nt_ui_clampf(value, min, max);
    }
    const float k = roundf((value - min) / step);
    return nt_ui_clampf(min + (k * step), min, max);
}

/* Value -> [0,1] fraction over [min,max]; degenerate min==max collapses to 0. */
static inline float slider_value_to_frac(float value, float min, float max) { return (max != min) ? nt_ui_clampf((value - min) / (max - min), 0.0F, 1.0F) : 0.0F; }

/* Scratch element_data with optional opacity (no transform). */
static nt_ui_element_data_t *slider_make_data(void *user_data, uint8_t layer, float opacity) {
    nt_ui_element_data_t *d = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
    NT_ASSERT(d != NULL && "nt_ui_slider: scratch alloc failed (element_data)");
    uint32_t flags = 0U;
    if (opacity >= 0.0F) {
        flags |= NT_UI_ELEM_FLAG_HAS_OPACITY;
    }
    *d = (nt_ui_element_data_t){
        .user_data = user_data,
        .layer = layer,
        .flags = (uint8_t)flags,
        .transform = nt_ui_transform_defaults(),
        .opacity = (opacity >= 0.0F) ? opacity : 1.0F,
    };
    return d;
}

static void assert_cell_valid(const nt_ui_slider_cell_t *st) {
    NT_ASSERT(isfinite(st->opacity) && st->opacity >= 0.0F && st->opacity <= 1.0F && "nt_ui_slider: style cell opacity must be finite in [0,1]");
}

/* Salted derivations so the drag + view cells can't alias the slider's own id in the state pool. */
static inline uint32_t slider_drag_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_SLIDER_DRAG_SALT); }
static inline uint32_t slider_view_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_SLIDER_VIEW_SALT); }

/* Press-scoped drag resolve. Reads the prev-frame track bbox + grab cell, returns the new value
 * fraction. press_now: thumb-grab keeps value (offset stored) | track press jumps; held: thumb
 * follows pointer minus the grab offset. */
static float slider_resolve_drag(nt_ui_context_t *ctx, uint32_t id, const nt_ui_interaction_t *in, const nt_ui_slider_style_t *style, nt_ui_fill_direction_t fill_dir, float frac, float min,
                                 float max) {
    /* Axis-branch: vertical reads the Y component + track height/thumb height; BOTTOM_UP inverts. */
    const bool vertical = (style->orientation == NT_UI_SLIDER_VERTICAL);
    const int axis = vertical ? 1 : 0;
    const float track_extent = vertical ? style->track_h : style->track_w;
    const float thumb_extent = vertical ? style->thumb_h : style->thumb_w;
    const bool invert = vertical && (fill_dir == NT_UI_FILL_BOTTOM_UP);
    const float usable = (track_extent - thumb_extent > 0.0F) ? (track_extent - thumb_extent) : 0.0F;
    nt_ui_slider_drag_t *drag = (nt_ui_slider_drag_t *)nt_ui_state(ctx, slider_drag_id(id), (uint32_t)sizeof(nt_ui_slider_drag_t), NT_UI_STATE_TAG('s', 'l', 'd', 'g'));
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, id); /* prev-frame track bbox */
    const float bb_origin = vertical ? bb.y : bb.x;
    const float track_origin = bb.found ? bb_origin : in->press_pos[axis];

    if (in->pressed_now) {
        const float pos_frac = invert ? (1.0F - frac) : frac; /* thumb screen position fraction from the origin edge */
        const float thumb_lead = track_origin + (pos_frac * usable);
        const bool on_thumb = bb.found && in->press_pos[axis] >= thumb_lead && in->press_pos[axis] <= (thumb_lead + thumb_extent);
        drag->active = 1U;
        if (on_thumb) {
            drag->grab_offset = in->press_pos[axis] - thumb_lead; /* relative grab, value unchanged this frame */
            return frac;
        }
        drag->grab_offset = thumb_extent * 0.5F; /* track jump: thumb centers under the click */
        const float v = slider_pos_to_value(in->press_pos[axis], track_origin, usable, thumb_extent, min, max, invert);
        return slider_value_to_frac(v, min, max);
    }
    if (drag->active != 0U) { /* held: thumb follows pointer keeping the grab offset */
        const float v = slider_pos_to_value(in->pos[axis] - drag->grab_offset + (thumb_extent * 0.5F), track_origin, usable, thumb_extent, min, max, invert);
        return slider_value_to_frac(v, min, max);
    }
    return frac;
}

/* Emits track + fill + thumb at fraction in [0,1]. Returns nothing; the registered id is
 * the OUTER track container (one clickable target). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void slider_compose(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *label, const nt_ui_slider_cell_t *cell,
                           const nt_ui_slider_cell_t *idle, const nt_ui_slider_style_t *style, nt_ui_fill_direction_t fill_dir, float fraction, float eased_opacity,
                           const Clay_ElementDeclaration *decl, bool enabled) {
    const uint8_t layer = (data != NULL) ? data->layer : 0U;
    void *user = (data != NULL) ? data->user_data : NULL;

    /* Resolve style-owned cell + idle refs in place BEFORE the inherit pick. */
    nt_ui_slider_cell_t *mcell = (nt_ui_slider_cell_t *)cell;
    nt_ui_slider_cell_t *micell = (nt_ui_slider_cell_t *)idle;
    nt_atlas_resolve_ref(&mcell->track);
    nt_atlas_resolve_ref(&mcell->fill);
    nt_atlas_resolve_ref(&mcell->thumb);
    nt_atlas_resolve_ref(&micell->track);
    nt_atlas_resolve_ref(&micell->fill);
    nt_atlas_resolve_ref(&micell->thumb);
    nt_atlas_region_ref_t track_ref = nt_ui_ref_or(mcell->track, micell->track);
    nt_atlas_region_ref_t fill_ref = nt_ui_ref_or(mcell->fill, micell->fill);
    nt_atlas_region_ref_t thumb_ref = nt_ui_ref_or(mcell->thumb, micell->thumb);

    /* OUTER track container is the registered widget id. */
    Clay_ElementDeclaration track_decl = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    track_decl.id = (Clay_ElementId){.id = id};
    track_decl.layout.sizing = (Clay_Sizing){CLAY_SIZING_FIXED(style->track_w), CLAY_SIZING_FIXED(style->track_h)};
    track_decl.userData = (void *)slider_make_data(user, layer, eased_opacity);
    /* Track background art on the container itself (no-art skip). */
    if (track_ref.atlas.id != 0U && track_ref.region != NT_ATLAS_INVALID_REGION) {
        nt_ui_image_payload_t *tp = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
        NT_ASSERT(tp != NULL && "nt_ui_slider: scratch alloc failed (track payload)");
        *tp = (nt_ui_image_payload_t){.atlas = track_ref.atlas, .region_index = track_ref.region, .slice9_scale = 1.0F};
        track_decl.image = (Clay_ImageElementConfig){.imageData = tp};
        track_decl.backgroundColor = nt_ui_unpack_tint(cell->track_tint);
    }
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(track_decl);
    nt_ui_widget_register(ctx, id, &NT_UI_SLIDER_DEF, NULL, enabled);

    const bool vertical = (style->orientation == NT_UI_SLIDER_VERTICAL);

    /* Fill child (shared helper). The fill edge meets the THUMB CENTER, not fraction*track —
     * the thumb travels [thumb/2 .. track - thumb/2], a raw fraction under/overshoots it.
     * fill_direction picks the axis (vertical anchors BOTTOM_UP/TOP_DOWN inside nt_ui_fill_emit). */
    if (fill_ref.atlas.id != 0U && fill_ref.region != NT_ATLAS_INVALID_REGION) {
        float fill_frac = fraction;
        const float track_extent = vertical ? style->track_h : style->track_w;
        const float thumb_extent = vertical ? style->thumb_h : style->thumb_w;
        if (thumb_extent > 0.0F && track_extent > thumb_extent) {
            fill_frac = ((thumb_extent * 0.5F) + (fraction * (track_extent - thumb_extent))) / track_extent;
        }
        nt_ui_fill_emit(ctx, layer, &fill_ref, cell->fill_tint, fill_frac, style->track_w, style->track_h, style->fill_mode, fill_dir, 1.0F);
    }

    /* Thumb child: offset along the axis by fraction*(track - thumb), centered on the cross axis.
     * Vertical BOTTOM_UP places value 0 at the bottom (offset measured down from the top). No-art skip. */
    if (thumb_ref.atlas.id != 0U && thumb_ref.region != NT_ATLAS_INVALID_REGION) {
        nt_ui_transform_t tt = nt_ui_transform_defaults();
        Clay_FloatingAttachPointType attach = CLAY_ATTACH_POINT_LEFT_CENTER;
        if (vertical) {
            const float usable_h = style->track_h - style->thumb_h;
            const float travel = (usable_h > 0.0F) ? usable_h : 0.0F;
            const bool invert = (fill_dir == NT_UI_FILL_BOTTOM_UP);
            const float pos_frac = invert ? (1.0F - fraction) : fraction;
            tt.offset_y = pos_frac * travel;
            attach = CLAY_ATTACH_POINT_CENTER_TOP;
        } else {
            const float usable_w = style->track_w - style->thumb_w;
            tt.offset_x = fraction * ((usable_w > 0.0F) ? usable_w : 0.0F);
        }
        nt_ui_element_data_t *thumb_data = NT_MEM_SCRATCH_ALLOC(nt_ui_element_data_t);
        NT_ASSERT(thumb_data != NULL && "nt_ui_slider: scratch alloc failed (thumb data)");
        *thumb_data = (nt_ui_element_data_t){.user_data = NULL, .layer = layer, .flags = (uint8_t)NT_UI_ELEM_FLAG_HAS_TRANSFORM, .transform = tt, .opacity = 1.0F};
        nt_ui_image_style_t thumb_style = nt_ui_image_style_defaults();
        thumb_style.color_packed = cell->thumb_tint;
        /* Floating so the thumb offset overlays the track without consuming layout. clipTo the
         * attached parent so a slider inside a scroll container can't leak its thumb past the clip.
         * No zIndex: delta 0 keeps the thumb in the track's own band, painted after it (NT patch 4). */
        const Clay_ElementDeclaration thumb_decl = {
            .layout = {.sizing = {CLAY_SIZING_FIXED(style->thumb_w), CLAY_SIZING_FIXED(style->thumb_h)}},
            .floating = {.attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT, .attachPoints = {.element = attach, .parent = attach}},
        };
        nt_atlas_region_ref_t tref = thumb_ref;
        nt_ui_image(ctx, thumb_data, &tref, &thumb_style, &thumb_decl);
    }

    /* Optional label child (after the parts, like checkbox cb_emit_text ordering). */
    if (label != NULL) {
        nt_ui_label_style_t lbl = (nt_ui_label_style_t){.font_id = 0, .font_size = 16, .color = {255.0F, 255.0F, 255.0F, 255.0F}};
        nt_ui_label(ctx, slider_make_data(NULL, label_layer, -1.0F), label, &lbl);
    }

    nt_ui_clay_priv_close_element();
}

/* Effective hit pad: style pad, with the CROSS-axis components auto-grown so the thumb's overhang
 * past the track is always clickable even at zero style pad. Horizontal grows top/bottom by
 * (thumb_h-track_h)/2; vertical grows left/right by (thumb_w-track_w)/2. Derived, no style requirement. */
static void slider_effective_pad(const nt_ui_slider_style_t *style, int16_t out[4]) {
    const bool vertical = (style->orientation == NT_UI_SLIDER_VERTICAL);
    const float overhang = (vertical ? (style->thumb_w - style->track_w) : (style->thumb_h - style->track_h)) * 0.5F;
    int16_t grow = 0;
    if (overhang > 0.0F) {
        grow = (int16_t)ceilf(overhang);
    }
    out[0] = style->hit_padding_lrtb[0];
    out[1] = style->hit_padding_lrtb[1];
    out[2] = style->hit_padding_lrtb[2];
    out[3] = style->hit_padding_lrtb[3];
    /* Grow the cross axis: left/right for vertical, top/bottom for horizontal. */
    const int lo = vertical ? 0 : 2;
    const int hi = vertical ? 1 : 3;
    out[lo] = (int16_t)((style->hit_padding_lrtb[lo] > grow) ? style->hit_padding_lrtb[lo] : grow);
    out[hi] = (int16_t)((style->hit_padding_lrtb[hi] > grow) ? style->hit_padding_lrtb[hi] : grow);
}

/* Shared [0,1]-fraction core for float + int. step_frac quantizes onto the 0,step,2*step,... grid
 * (0 = continuous) BEFORE anim/view/compose, so thumb + fill snap WITH the value. Returns the new
 * clamped fraction; *changed when it differs from in_frac. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static float slider_core(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *label, float in_frac, float min, float max, float step_frac,
                         nt_ui_slider_style_t *style, const Clay_ElementDeclaration *decl, bool enabled, bool *changed) {
    // #region entry asserts
    NT_ASSERT(ctx != NULL && "nt_ui_slider: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_slider: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_slider: style must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_slider: id 0 is the no-widget sentinel");
    NT_ASSERT(isfinite(style->track_w) && style->track_w > 0.0F && isfinite(style->track_h) && style->track_h > 0.0F && "nt_ui_slider: style.track_w/track_h must be finite > 0");
    NT_ASSERT(isfinite(style->thumb_w) && style->thumb_w >= 0.0F && isfinite(style->thumb_h) && style->thumb_h >= 0.0F && "nt_ui_slider: style.thumb_w/thumb_h must be finite >= 0");
    NT_ASSERT(isfinite(style->state_speed) && style->state_speed >= 0.0F && "nt_ui_slider: style.state_speed must be finite >= 0");
    NT_ASSERT(isfinite(style->value_speed) && style->value_speed >= 0.0F && "nt_ui_slider: style.value_speed must be finite >= 0");
    NT_ASSERT(isfinite(min) && isfinite(max) && min != max && "nt_ui_slider: min must differ from max");
    for (int i = 0; i < 4; ++i) {
        assert_cell_valid(&style->states[i]);
    }
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_slider: decl->id must be 0 (id is the explicit param)");
        NT_ASSERT(decl->image.imageData == NULL && "nt_ui_slider: decl->image.imageData must be NULL (style controls track art)");
        NT_ASSERT(decl->backgroundColor.a == 0.0F && "nt_ui_slider: decl->backgroundColor must be zero (style controls tint)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_slider: decl->userData must be NULL (data param controls)");
    }
    if (data != NULL) {
        NT_ASSERT((data->flags & (NT_UI_ELEM_FLAG_HAS_TRANSFORM | NT_UI_ELEM_FLAG_HAS_OPACITY)) == 0U && "nt_ui_slider: data->flags must not set HAS_TRANSFORM/HAS_OPACITY (widget owns these)");
    }
    NT_ASSERT(isfinite(step_frac) && step_frac >= 0.0F && "nt_ui_slider: step_frac must be finite >= 0");
    // #endregion
    // #region axis guard (orientation = AXIS, fill_direction = anchor within it)
    NT_ASSERT((style->orientation == NT_UI_SLIDER_HORIZONTAL || style->orientation == NT_UI_SLIDER_VERTICAL) && "nt_ui_slider: style.orientation must be HORIZONTAL or VERTICAL");
    /* Coerce a bad/unknown anchor in a LOCAL effective direction; never write back into the caller's
     * (maybe shared/static) style — that would corrupt other widgets across frames. The local default,
     * not the NT_ASSERT (gone in OFF), is the real guard against rendering the wrong axis/anchor. */
    const bool vertical = (style->orientation == NT_UI_SLIDER_VERTICAL);
    /* Supported anchors: vertical -> BOTTOM_UP/TOP_DOWN; horizontal -> LTR only. Horizontal RTL is
     * rejected (fill right-anchors while drag/thumb/thumb_pos stay LTR — inconsistent); any UNKNOWN enum
     * value also fails the explicit test and coerces to the axis default. */
    const bool bad_fill = vertical ? !(style->fill_direction == NT_UI_FILL_BOTTOM_UP || style->fill_direction == NT_UI_FILL_TOP_DOWN) : (style->fill_direction != NT_UI_FILL_LTR);
    nt_ui_fill_direction_t effective_fill_direction = style->fill_direction;
    if (bad_fill) {
        effective_fill_direction = vertical ? NT_UI_FILL_BOTTOM_UP : NT_UI_FILL_LTR;
    }
    NT_ASSERT(!bad_fill && "nt_ui_slider: unsupported fill_direction for orientation (horizontal: LTR; vertical: BOTTOM_UP/TOP_DOWN)");
    // #endregion
    // #region interaction
    int16_t pad[4];
    slider_effective_pad(style, pad);
    nt_ui_interaction_t in;
    if (enabled) {
        in = nt_ui_step_interaction_padded(ctx, id, pad);
    } else {
        in = (nt_ui_interaction_t){0};
        nt_ui_block_pointer(ctx, id, pad); /* inert occluder: disabled slider still blocks input behind it */
        nt_ui_debug_record_disabled_zone(ctx, id, pad);
    }
    // #endregion
    // #region drag math (press-ON-thumb grab vs track jump)
    float frac = nt_ui_clampf(in_frac, 0.0F, 1.0F);
    if (enabled && (in.pressed_now || in.pressed)) {
        frac = slider_resolve_drag(ctx, id, &in, style, effective_fill_direction, frac, min, max);
    } else {
        /* Release OR disabled-mid-drag: drop the cell so re-enable can't resume a stale grab. */
        nt_ui_state_clear(ctx, slider_drag_id(id));
    }
    /* Snap the fraction onto the value grid BEFORE it feeds anim/view/compose so the thumb + fill
     * land on the same tick as the emitted value (no continuous-thumb / snapped-value mismatch). */
    if (step_frac > 0.0F) {
        frac = nt_ui_clampf(roundf(frac / step_frac) * step_frac, 0.0F, 1.0F);
    }
    // #endregion
    // #region one anim call (state group + value_t)
    /* VISUAL pressed only while held AND over the widget: dragging off un-presses (re-presses on
     * return). Capture/value semantics above use in.pressed untouched — this is the state-cell pick only. */
    const bool pressed_visual = in.pressed && in.hovered;
    int state = NT_UI_SLIDER_IDLE;
    if (!enabled) {
        state = NT_UI_SLIDER_DISABLED;
    } else if (pressed_visual) {
        state = NT_UI_SLIDER_PRESSED;
    } else if (in.hovered) {
        state = NT_UI_SLIDER_HOVER;
    }
    const nt_ui_slider_cell_t *cell = &style->states[state];
    nt_ui_anim_target_t tgt = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = cell->opacity, .value_t = frac};
    /* value_speed 0 during drag = 1:1 snap; eased only on a game-driven change. */
    const float vspeed = in.pressed ? 0.0F : style->value_speed;
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, id, &tgt, style->state_speed, vspeed);
    /* During a drag vspeed=0 so eased_frac == the snapped frac (thumb/fill/bubble agree on the tick);
     * a game-driven change keeps the smooth ease toward the already-quantized target. */
    const float eased_frac = nt_ui_clampf(a->value_t, 0.0F, 1.0F);
    // #endregion

    /* Persist the eased fraction + axis-aware thumb geometry so const-ctx thumb_pos can recover it. */
    nt_ui_slider_view_t *view = (nt_ui_slider_view_t *)nt_ui_state(ctx, slider_view_id(id), (uint32_t)sizeof(nt_ui_slider_view_t), NT_UI_STATE_TAG('s', 'l', 'v', 'w'));
    view->fraction = eased_frac;
    view->track_w = style->track_w;
    view->track_h = style->track_h;
    view->thumb_w = style->thumb_w;
    view->thumb_h = style->thumb_h;
    view->orientation = (uint8_t)style->orientation;
    view->invert = (uint8_t)(vertical && (effective_fill_direction == NT_UI_FILL_BOTTOM_UP)); /* matches the emit invert */

    slider_compose(ctx, data, label_layer, id, label, cell, &style->states[NT_UI_SLIDER_IDLE], style, effective_fill_direction, eased_frac, a->opacity, decl, enabled);

    /* Changed when the drag moved the (snapped) fraction off the incoming game value. */
    *changed = enabled && (fabsf(frac - in_frac) > 1e-6F);
    return frac;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool nt_ui_slider_float(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *label, float *value, float min, float max, float step,
                        nt_ui_slider_style_t *style, const Clay_ElementDeclaration *decl, bool enabled) {
    NT_ASSERT(value != NULL && "nt_ui_slider_float: value must be non-NULL");
    NT_ASSERT(isfinite(min) && isfinite(max) && min < max && "nt_ui_slider_float: min must be < max (reversed range inverts clamps)");
    NT_ASSERT(isfinite(step) && step >= 0.0F && "nt_ui_slider_float: step must be finite >= 0");
    const bool out_of_range = (*value < min) || (*value > max); /* clamp-and-writeback on first frame */
    const float in_frac = slider_value_to_frac(nt_ui_clampf(*value, min, max), min, max);
    const float step_frac = (step > 0.0F) ? (step / (max - min)) : 0.0F;
    bool changed = false;
    const float out_frac = slider_core(ctx, data, label_layer, id, label, in_frac, min, max, step_frac, style, decl, enabled, &changed);
    /* Write back on a drag OR an out-of-range incoming *value (Unity property-clamp). An in-range
     * off-grid value is left alone unless the user drags — the game owns its own precision. */
    if (changed || out_of_range) {
        const float q = slider_quantize(min + (out_frac * (max - min)), min, max, step);
        if (fabsf(q - *value) > 1e-6F) {
            *value = q;
            return true;
        }
    }
    return false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
bool nt_ui_slider_int(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *label, int *value, int min, int max, int step, nt_ui_slider_style_t *style,
                      const Clay_ElementDeclaration *decl, bool enabled) {
    NT_ASSERT(value != NULL && "nt_ui_slider_int: value must be non-NULL");
    NT_ASSERT(min < max && "nt_ui_slider_int: min must be < max (reversed range inverts clamps)");
    NT_ASSERT(step >= 0 && "nt_ui_slider_int: step must be >= 0");
    const float fmin = (float)min;
    const float fmax = (float)max;
    const bool out_of_range = (*value < min) || (*value > max); /* clamp-and-writeback on first frame */
    const float in_frac = slider_value_to_frac((float)nt_ui_clampi(*value, min, max), fmin, fmax);
    const float fstep = (step > 0) ? (float)step : 1.0F; /* int slider always quantizes to >= 1 */
    const float step_frac = fstep / (fmax - fmin);
    bool changed = false;
    const float out_frac = slider_core(ctx, data, label_layer, id, label, in_frac, fmin, fmax, step_frac, style, decl, enabled, &changed);
    /* Write back on a drag OR an out-of-range incoming *value (clamp to range on first frame). */
    if (changed || out_of_range) {
        const float raw = fmin + (out_frac * (fmax - fmin));
        const int qi = (int)lrintf(slider_quantize(raw, fmin, fmax, fstep));
        if (qi != *value) {
            *value = qi;
            return true;
        }
    }
    return false;
}

nt_ui_slider_thumb_t nt_ui_slider_thumb_pos(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "nt_ui_slider_thumb_pos: ctx must be non-NULL");
    NT_ASSERT(id != 0U && "nt_ui_slider_thumb_pos: id must be non-zero");
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, id);
    /* The view cell is a mutable cache; the find is logically const (read-only). */
    const nt_ui_slider_view_t *view = (const nt_ui_slider_view_t *)nt_ui_state_find((nt_ui_context_t *)ctx, slider_view_id(id));
    if (!bb.found || view == NULL) {
        return (nt_ui_slider_thumb_t){0.0F, 0.0F, false};
    }
    /* Thumb center = track_origin + pos_frac*(track - thumb) + thumb/2 along the AXIS, centered on the
     * cross axis (matches the emit attach points). Vertical BOTTOM_UP measures pos from the far edge. */
    if (view->orientation == NT_UI_SLIDER_VERTICAL) {
        const float usable_h = (view->track_h - view->thumb_h > 0.0F) ? (view->track_h - view->thumb_h) : 0.0F;
        const float pos_frac = (view->invert != 0U) ? (1.0F - view->fraction) : view->fraction;
        return (nt_ui_slider_thumb_t){.x = bb.x + (bb.width * 0.5F), .y = bb.y + (pos_frac * usable_h) + (view->thumb_h * 0.5F), .found = true};
    }
    const float usable_w = (view->track_w - view->thumb_w > 0.0F) ? (view->track_w - view->thumb_w) : 0.0F;
    return (nt_ui_slider_thumb_t){.x = bb.x + (view->fraction * usable_w) + (view->thumb_w * 0.5F), .y = bb.y + (bb.height * 0.5F), .found = true};
}

nt_ui_slider_style_t nt_ui_slider_style_defaults(void) {
    nt_ui_slider_style_t s;
    memset(&s, 0, sizeof s); /* memset, not = {0}: emscripten -Werror rejects {0} on aggregate-first */
    for (int i = 0; i < 4; ++i) {
        s.states[i] = (nt_ui_slider_cell_t){.track_tint = 0xFFFFFFFFU, .fill_tint = 0xFFFFFFFFU, .thumb_tint = 0xFFFFFFFFU, .opacity = 1.0F};
    }
    s.states[NT_UI_SLIDER_DISABLED].opacity = 0.5F;
    s.track_w = 200.0F;
    s.track_h = 16.0F;
    s.thumb_w = 20.0F;
    s.thumb_h = 24.0F;
    s.fill_mode = NT_UI_FILL_STRETCH;
    s.fill_direction = NT_UI_FILL_LTR;
    s.orientation = NT_UI_SLIDER_HORIZONTAL; /* explicit; vertical opts in via the style */
    s.state_speed = 14.0F;
    s.value_speed = 12.0F;
    return s;
}
