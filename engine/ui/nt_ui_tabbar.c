#include "ui/nt_ui_tabbar.h"

#include <math.h>
#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"

const nt_ui_widget_def_t NT_UI_TABBAR_DEF = {
    .name = "nt_tabbar",
    .pill_color = 0xFF40C0A0U,
    ._reserved = 0U,
};

nt_ui_tabbar_style_t nt_ui_tabbar_style_defaults(void) {
    /* Flat-color baseline: no atlas art, eased states, dark vertical nav that looks good out of the box. */
    const nt_ui_tab_state_t idle = {.fill = 0U, /* transparent: the bar bg shows through unselected tabs */
                                    .bg_tint = 0xFFFFFFFFU,
                                    .scale = 1.0F,
                                    .opacity = 1.0F};
    const nt_ui_tab_state_t hover = {.fill = 0xFF3A3F46U, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
    const nt_ui_tab_state_t selected = {.fill = 0xFF2E629EU, .bg_tint = 0xFFFFFFFFU, .scale = 1.03F, .opacity = 1.0F};
    return (nt_ui_tabbar_style_t){
        .idle = idle,
        .hover = hover,
        .selected = selected,
        .bar_bg = 0xFF22262AU,
        .accent = 0xFFE0B050U,
        .text = 0xFFC8C8C8U,
        .text_selected = 0xFFFFFFFFU,
        .font_size = 14.0F,
        .slice9_scale = 1.0F,
        .state_speed = 16.0F,
        .value_speed = 14.0F,
        .tab_extent = 38U,
        .accent_px = 3U,
        .corner_radius = 6U,
        .pad = 8U,
        .gap = 6U,
        .font_id = 0U,
        .dir = NT_UI_TABBAR_VERTICAL,
    };
}

/* Fail early on out-of-range state values — a silent "almost works" would otherwise leak. */
static void assert_state_valid(const nt_ui_tab_state_t *st) {
    NT_ASSERT(isfinite(st->scale) && st->scale > 0.0F && "nt_ui_tabbar: state.scale must be finite > 0");
    NT_ASSERT(isfinite(st->opacity) && st->opacity >= 0.0F && st->opacity <= 1.0F && "nt_ui_tabbar: state.opacity must be finite in [0,1]");
}

/* One tab: a full-extent click target with an eased leading accent bar (selected) + a label. The eased
 * state transform (scale/opacity) rides the tab's data->layer xform; the bg is slice9 art when supplied,
 * else the flat `fill` color. Returns clicked. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — CLAY macro + per-state pick + accent ease, not real nesting
static bool tabbar_declare_tab(nt_ui_context_t *ctx, uint8_t fill_layer, uint8_t label_layer, uint32_t tab_id, const char *label, bool active, nt_ui_tabbar_style_t *style) {
    const nt_ui_interaction_t in = nt_ui_query_interaction(ctx, tab_id);
    const bool horizontal = (style->dir == NT_UI_TABBAR_HORIZONTAL);

    /* Priority: selected -> hover -> idle. Non-const so the resolve memoizes into the style. */
    nt_ui_tab_state_t *st = &style->idle;
    if (active) {
        st = &style->selected;
    } else if (in.hovered) {
        st = &style->hover;
    }

    // #region eased state + accent value
    /* ONE anim call per tab: scale/opacity at state_speed + accent grow-in (value_t -> 1 when selected)
     * at value_speed. The eased state transform rides the tab's own data->layer xform channel. */
    nt_ui_anim_target_t tgt = {
        .scale_x = st->scale,
        .scale_y = st->scale,
        .scale_z = 1.0F,
        .off_x = 0.0F,
        .off_y = 0.0F,
        .off_z = 0.0F,
        .rot_x = 0.0F,
        .rot_y = 0.0F,
        .rot_z = 0.0F,
        .opacity = st->opacity,
        .tint_t = 0.0F,
        .value_t = active ? 1.0F : 0.0F,
    };
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, tab_id, &tgt, style->state_speed, style->value_speed);
    const nt_ui_transform_t tab_t = {.scale_x = a->scale_x, .scale_y = a->scale_y, .scale_z = 1.0F};
    const nt_ui_element_data_t *tab_data = nt_ui_make_element_data_xform(fill_layer, NULL, &tab_t, a->opacity);
    // #endregion

    const uint32_t txt = active ? style->text_selected : style->text;
    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(txt)};

    // #region resolve bg ref + flat fallback
    /* Resolve STYLE-OWNED refs in place FIRST (memoizes), then inherit by value. Atomic ref: a non-idle
     * state with atlas.id==0 inherits idle's whole ref; idle no-art -> flat `fill` color. */
    nt_atlas_resolve_ref(&st->bg);
    nt_atlas_resolve_ref(&style->idle.bg);
    const nt_atlas_region_ref_t bg = nt_ui_ref_or(st->bg, style->idle.bg);
    const bool has_art = (bg.atlas.id != 0U && bg.region != NT_ATLAS_INVALID_REGION);
    // #endregion

    /* Accent: eased grow-in on the leading edge (LEFT for vertical, TOP for horizontal). Width rides
     * value_t so it animates in/out; round to whole px (Clay border widths are integral). */
    const float accent_eased = (float)style->accent_px * a->value_t;
    const uint16_t a_px = (uint16_t)lroundf(accent_eased);
    const Clay_Color accent_col = (a_px > 0U) ? nt_ui_unpack_abgr(style->accent) : (Clay_Color){0};
    Clay_BorderWidth bw = {0};
    if (horizontal) {
        bw.top = a_px;
    } else {
        bw.left = a_px;
    }
    /* Pad the label past the full accent edge so it never jumps as the accent eases in. */
    Clay_Padding pad = {.left = style->pad, .right = style->pad, .top = style->pad, .bottom = style->pad};
    if (horizontal) {
        pad.top = (uint16_t)(style->pad + style->accent_px);
    } else {
        pad.left = (uint16_t)(style->pad + style->accent_px);
    }

    Clay_ElementDeclaration decl = {
        .id = (Clay_ElementId){.id = tab_id},
        .layout = {.sizing =
                       horizontal ? (Clay_Sizing){CLAY_SIZING_FIXED((float)style->tab_extent), CLAY_SIZING_GROW(0)} : (Clay_Sizing){CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((float)style->tab_extent)},
                   .padding = pad,
                   .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
        .cornerRadius = CLAY_CORNER_RADIUS((float)style->corner_radius),
        .border = {.color = accent_col, .width = bw},
        .userData = (void *)tab_data,
    };
    if (has_art) {
        /* slice9 atlas bg: tint multiplies the art; no flat backgroundColor (the IMAGE carries it). */
        nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
        NT_ASSERT(p != NULL && "nt_ui_tabbar: scratch alloc failed (image_payload)");
        *p = (nt_ui_image_payload_t){.atlas = bg.atlas, .region_index = bg.region, .slice9_scale = style->slice9_scale};
        decl.image = (Clay_ImageElementConfig){.imageData = p};
        decl.backgroundColor = nt_ui_unpack_tint(st->bg_tint);
    } else {
        decl.backgroundColor = (st->fill != 0U) ? nt_ui_unpack_abgr(st->fill) : (Clay_Color){0};
    }

    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(decl);
    {
        nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), (label != NULL) ? label : "", &lbl);
    }
    nt_ui_clay_priv_close_element();
    return nt_ui_step_interaction(ctx, tab_id).clicked;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — CLAY macro expands to a for-loop; metric counts that + the tab loop + asserts, not real nesting
int nt_ui_tabbar(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t base_id, const char *const *labels, int count, int *active, nt_ui_tabbar_style_t *style) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_tabbar: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(base_id != 0U && labels != NULL && active != NULL && style != NULL && "nt_ui_tabbar: base_id non-zero, pointers non-NULL");
    NT_ASSERT(count >= 0 && "nt_ui_tabbar: count must be >= 0");                                         /* T-65-15 */
    NT_ASSERT(*active >= -1 && (count == 0 || *active < count) && "nt_ui_tabbar: active in [-1,count)"); /* T-65-15 */
    NT_ASSERT(style->font_size > 0.0F && "nt_ui_tabbar: font_size > 0");
    NT_ASSERT(isfinite(style->slice9_scale) && style->slice9_scale > 0.0F && "nt_ui_tabbar: slice9_scale must be finite > 0");
    NT_ASSERT(isfinite(style->state_speed) && style->state_speed >= 0.0F && "nt_ui_tabbar: state_speed must be finite >= 0");
    NT_ASSERT(isfinite(style->value_speed) && style->value_speed >= 0.0F && "nt_ui_tabbar: value_speed must be finite >= 0");
    assert_state_valid(&style->idle);
    assert_state_valid(&style->hover);
    assert_state_valid(&style->selected);

    const bool horizontal = (style->dir == NT_UI_TABBAR_HORIZONTAL);
    const uint8_t fill_layer = (data != NULL) ? data->layer : 0U; /* fills on data->layer, text on label_layer (batch split) */
    int clicked = -1;
    /* The container has no explicit id (base_id is reserved for the tabs: base_id + i). */
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(style->pad),
                     .layoutDirection = horizontal ? CLAY_LEFT_TO_RIGHT : CLAY_TOP_TO_BOTTOM,
                     .childGap = style->gap},
          .backgroundColor = (style->bar_bg != 0U) ? nt_ui_unpack_abgr(style->bar_bg) : (Clay_Color){0},
          .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)}) {
        for (int i = 0; i < count; ++i) {
            NT_ASSERT(labels[i] != NULL && "nt_ui_tabbar: label entry must be non-NULL"); /* T-65-16 */
            /* Salt per-tab ids from base_id + i (mirrors the showcase s_id_tab_btn_base + i). */
            if (tabbar_declare_tab(ctx, fill_layer, label_layer, base_id + (uint32_t)i, labels[i], i == *active, style)) {
                *active = i; /* Model D: latch the game-owned active index */
                clicked = i;
            }
        }
    }
    return clicked;
}
