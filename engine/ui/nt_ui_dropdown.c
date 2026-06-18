#include "ui/nt_ui_dropdown.h"

#include <math.h>
#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_state.h"

const nt_ui_widget_def_t NT_UI_DROPDOWN_DEF = {
    .name = "nt_dropdown",
    .pill_color = 0xFF50A0E0U,
    ._reserved = 0U,
};

/* Sibling cell ids derived from the dropdown id: the popup list + the scroll wrapper each need a stable
 * non-aliasing id. Sparse high-bit salts (never a real low id). */
#define NT_UI_DROPDOWN_POPUP_SALT 0x44D90000U
#define NT_UI_DROPDOWN_SCROLL_SALT 0x44D50000U
#define NT_UI_DROPDOWN_PANEL_SALT 0x44DA0000U
#define NT_UI_DROPDOWN_ROW_SALT 0x44D70000U
#define NT_UI_DROPDOWN_LABEL_SALT 0x44DB0000U

static inline uint32_t dropdown_popup_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_POPUP_SALT); }
static inline uint32_t dropdown_scroll_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_SCROLL_SALT); }
static inline uint32_t dropdown_panel_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_PANEL_SALT); }
static inline uint32_t dropdown_row_id(uint32_t id, int idx) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_ROW_SALT + (uint32_t)idx); }
static inline uint32_t dropdown_row_label_id(uint32_t id, int idx) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_LABEL_SALT + (uint32_t)idx); }

#ifdef NT_TEST_ACCESS
static uint8_t s_last_side; /* edge-flip probe (NT_TEST_ACCESS) */
#endif

nt_ui_dropdown_style_t nt_ui_dropdown_style_defaults(void) {
    /* Flat-color baseline: no atlas art (every bg.atlas.id stays 0), eased states, polished out of box.
     * The trigger reads as a button; rows highlight on hover; the selected row carries a distinct fill. */
    const nt_ui_dd_state_t trig_idle = {.fill = 0xFF3A3A3AU, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
    const nt_ui_dd_state_t trig_hover = {.fill = 0xFF464646U, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
    const nt_ui_dd_state_t trig_pressed = {.fill = 0xFF2E2E2EU, .bg_tint = 0xFFFFFFFFU, .scale = 0.98F, .opacity = 1.0F};
    const nt_ui_dd_state_t row_idle = {.fill = 0U, /* transparent: the panel bg shows through unselected rows */
                                       .bg_tint = 0xFFFFFFFFU,
                                       .scale = 1.0F,
                                       .opacity = 1.0F};
    const nt_ui_dd_state_t row_hover = {.fill = 0xFF4A4A4AU, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
    const nt_ui_dd_state_t row_pressed = {.fill = 0xFF565E78U, .bg_tint = 0xFFFFFFFFU, .scale = 0.99F, .opacity = 1.0F};
    const nt_ui_dd_state_t row_selected = {.fill = 0xFF505A78U, .bg_tint = 0xFFFFFFFFU, .scale = 1.0F, .opacity = 1.0F};
    return (nt_ui_dropdown_style_t){
        .trigger_idle = trig_idle,
        .trigger_hover = trig_hover,
        .trigger_pressed = trig_pressed,
        .row_idle = row_idle,
        .row_hover = row_hover,
        .row_pressed = row_pressed,
        .row_selected = row_selected,
        .panel_fill = 0xFF2A2A2AU,
        .panel_tint = 0xFFFFFFFFU,
        .trigger_text = 0xFFE8E8E8U,
        .row_text = 0xFFE0E0E0U,
        .chevron_tint = 0xFFC8C8C8U,
        .list_scroll = nt_ui_scroll_style_defaults(), /* atlas-free baseline; game wires bar sprites (consistent with button art refs) */
        .font_size = 14.0F,
        .slice9_scale = 1.0F,
        .state_speed = 16.0F,
        .value_speed = 14.0F,
        .open_ease_speed = 0.0F, /* snap-open by default; the game opts into a tween */
        .row_height = 28U,
        .min_width = 160U,
        .pad = 6U,
        .font_id = 0U,
        .max_visible_rows = 6U,
        .icon_size = 0U,     /* no icon gutter by default (text-only) */
        .chevron_size = 14U, /* drawn only when style->chevron has a ref */
        .panel_corner_radius = 6U,
    };
}

/* Fail early on out-of-range state values — a silent "almost works" would otherwise leak. */
static void assert_state_valid(const nt_ui_dd_state_t *st) {
    NT_ASSERT(isfinite(st->scale) && st->scale > 0.0F && "nt_ui_dropdown: state.scale must be finite > 0");
    NT_ASSERT(isfinite(st->opacity) && st->opacity >= 0.0F && st->opacity <= 1.0F && "nt_ui_dropdown: state.opacity must be finite in [0,1]");
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat assert chain, not real nesting
static void assert_style_valid(const nt_ui_dropdown_style_t *style) {
    NT_ASSERT(style->font_size > 0.0F && "nt_ui_dropdown: font_size > 0");
    NT_ASSERT(isfinite(style->slice9_scale) && style->slice9_scale > 0.0F && "nt_ui_dropdown: slice9_scale must be finite > 0");
    NT_ASSERT(isfinite(style->state_speed) && style->state_speed >= 0.0F && "nt_ui_dropdown: state_speed must be finite >= 0");
    NT_ASSERT(isfinite(style->value_speed) && style->value_speed >= 0.0F && "nt_ui_dropdown: value_speed must be finite >= 0");
    assert_state_valid(&style->trigger_idle);
    assert_state_valid(&style->trigger_hover);
    assert_state_valid(&style->trigger_pressed);
    assert_state_valid(&style->row_idle);
    assert_state_valid(&style->row_hover);
    assert_state_valid(&style->row_pressed);
    assert_state_valid(&style->row_selected);
}

/* The label shown on the trigger: the selected entry, or the placeholder when -1 / out of range. */
static const char *dropdown_trigger_text(const char *const *labels, int count, int selected, const char *placeholder) {
    if (selected >= 0 && selected < count && labels[selected] != NULL) {
        return labels[selected];
    }
    return (placeholder != NULL) ? placeholder : "";
}

/* Resolve a per-state bg ref + the atomic-inherit from idle; reports whether art should be emitted.
 * Mirrors nt_ui_tabbar: resolve STYLE-OWNED refs in place FIRST (memoizes), then inherit by value. */
static bool dropdown_resolve_bg(nt_ui_dd_state_t *st, nt_ui_dd_state_t *idle, nt_atlas_region_ref_t *out) {
    nt_atlas_resolve_ref(&st->bg);
    nt_atlas_resolve_ref(&idle->bg);
    const nt_atlas_region_ref_t bg = nt_ui_ref_or(st->bg, idle->bg);
    *out = bg;
    return (bg.atlas.id != 0U && bg.region != NT_ATLAS_INVALID_REGION);
}

/* Apply a resolved per-state look (slice9 art tinted, or flat fill) onto a decl. */
static void dropdown_apply_bg(Clay_ElementDeclaration *decl, bool has_art, const nt_atlas_region_ref_t *bg, const nt_ui_dd_state_t *st, float slice9_scale) {
    if (has_art) {
        nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
        NT_ASSERT(p != NULL && "nt_ui_dropdown: scratch alloc failed (image_payload)");
        *p = (nt_ui_image_payload_t){.atlas = bg->atlas, .region_index = bg->region, .slice9_scale = slice9_scale};
        decl->image = (Clay_ImageElementConfig){.imageData = p};
        decl->backgroundColor = nt_ui_unpack_tint(st->bg_tint);
    } else {
        decl->backgroundColor = (st->fill != 0U) ? nt_ui_unpack_abgr(st->fill) : (Clay_Color){0};
    }
}

/* The chevron sprite at the trigger's right edge. Eased open-rotation: 0 closed -> ~180deg open (the
 * down chevron flips to point up). Drawn on the fill layer; tinted; no-op when no ref / chevron_size 0. */
static void dropdown_declare_chevron(nt_ui_context_t *ctx, uint8_t fill_layer, nt_ui_dropdown_style_t *style, float open_t) {
    if (style->chevron_size == 0U) {
        return;
    }
    nt_atlas_resolve_ref(&style->chevron);
    if (style->chevron.atlas.id == 0U || style->chevron.region == NT_ATLAS_INVALID_REGION) {
        return;
    }
    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_dropdown: scratch alloc failed (chevron payload)");
    *p = (nt_ui_image_payload_t){.atlas = style->chevron.atlas, .region_index = style->chevron.region, .origin_x = 0.5F, .origin_y = 0.5F, .slice9_scale = 1.0F};
    /* Eased rotation tied to the open amount so the affordance reads the open/close reveal. */
    const nt_ui_transform_t chev_t = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .rotation_z = open_t * 3.14159265F};
    const nt_ui_element_data_t *chev_data = nt_ui_make_element_data_xform(fill_layer, NULL, &chev_t, 1.0F);
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED((float)style->chevron_size), CLAY_SIZING_FIXED((float)style->chevron_size)}},
          .image = (Clay_ImageElementConfig){.imageData = p},
          .backgroundColor = nt_ui_unpack_tint(style->chevron_tint),
          .userData = (void *)chev_data}) {}
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — per-state pick + bg branch + chevron, not deep nesting
bool nt_ui_dropdown_trigger(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *const *labels, int count, int selected, const char *placeholder,
                            nt_ui_dropdown_style_t *style, const Clay_ElementDeclaration *decl, bool *open) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_dropdown_trigger: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(id != 0U && labels != NULL && style != NULL && open != NULL && "nt_ui_dropdown_trigger: id non-zero, pointers non-NULL");
    NT_ASSERT(count >= 0 && selected >= -1 && selected < count && "nt_ui_dropdown_trigger: selected in [-1,count)");
    assert_style_valid(style);

    const uint8_t fill_layer = (data != NULL) ? data->layer : 0U; /* fill + chevron on data->layer, label on label_layer */

    const nt_ui_interaction_t in = nt_ui_query_interaction(ctx, id);

    /* Priority: pressed (held + over) -> hover -> idle. Non-const so the resolve memoizes into the style. */
    nt_ui_dd_state_t *st = &style->trigger_idle;
    if (in.pressed && in.hovered) {
        st = &style->trigger_pressed;
    } else if (in.hovered) {
        st = &style->trigger_hover;
    }

    /* ONE anim call: scale/opacity at state_speed + the chevron open-rotation (value_t -> 1 when open)
     * at value_speed. The eased state transform rides the trigger's own data->layer xform channel. */
    const nt_ui_anim_target_t tgt = {.scale_x = st->scale, .scale_y = st->scale, .scale_z = 1.0F, .opacity = st->opacity, .value_t = *open ? 1.0F : 0.0F};
    /* `a` may alias the shared ctx->anim_snap (no-slot fast path): read every field below BEFORE any other
     * nt_ui_anim call. The nested label/chevron emitted further down must NOT call nt_ui_anim. */
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, id, &tgt, style->state_speed, style->value_speed);
    const nt_ui_transform_t trig_t = {.scale_x = a->scale_x, .scale_y = a->scale_y, .scale_z = 1.0F};
    const nt_ui_element_data_t *trig_data = nt_ui_make_element_data_xform(fill_layer, NULL, &trig_t, a->opacity);

    nt_atlas_region_ref_t bg;
    const bool has_art = dropdown_resolve_bg(st, &style->trigger_idle, &bg);

    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(style->trigger_text)};
    /* The caller owns sizing/padding via `decl`; the engine owns .id/.image/.backgroundColor/.userData so
     * the trigger bbox is queryable for the list anchor. A {0}/NULL decl is a FIT trigger. */
    Clay_ElementDeclaration d = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    d.id = (Clay_ElementId){.id = id};
    d.layout.layoutDirection = CLAY_LEFT_TO_RIGHT; /* label grows; chevron pinned right */
    d.layout.childGap = style->pad;
    if (d.layout.childAlignment.y == 0) {
        d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    }
    if (d.layout.padding.left == 0 && d.layout.padding.right == 0) {
        d.layout.padding.left = style->pad;
        d.layout.padding.right = style->pad;
    }
    dropdown_apply_bg(&d, has_art, &bg, st, style->slice9_scale);
    d.userData = (void *)trig_data;

    bool toggled = false;
    /* Runtime-built decl: use the priv open/configure/close pattern (CLAY() brace-inits a wrapper from a
     * compound literal and cannot take a variable decl). */
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(d);
    /* Label grows to push the chevron to the right edge. */
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), dropdown_trigger_text(labels, count, selected, placeholder), &lbl);
    }
    dropdown_declare_chevron(ctx, fill_layer, style, a->value_t);
    nt_ui_clay_priv_close_element();

    if (nt_ui_step_interaction(ctx, id).clicked) {
        *open = !*open;
        toggled = true;
    }
    return toggled;
}

/* One list row: a fixed-height per-state rect + an optional leading icon gutter + label. Eased
 * hover/press/selected via nt_ui_anim through the fill-layer xform channel. Returns clicked. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — per-state pick + bg branch + icon gutter, not deep nesting
static bool dropdown_declare_row(nt_ui_context_t *ctx, uint8_t fill_layer, uint8_t label_layer, uint32_t row_id, uint32_t label_id, const char *label, const nt_atlas_region_ref_t *icon, bool selected,
                                 nt_ui_dropdown_style_t *style) {
    const nt_ui_interaction_t in = nt_ui_query_interaction(ctx, row_id);

    /* Priority: selected -> pressed (held + over) -> hover -> idle. Non-const so the resolve memoizes. */
    nt_ui_dd_state_t *st = &style->row_idle;
    if (selected) {
        st = &style->row_selected;
    } else if (in.pressed && in.hovered) {
        st = &style->row_pressed;
    } else if (in.hovered) {
        st = &style->row_hover;
    }

    const nt_ui_anim_target_t tgt = {.scale_x = st->scale, .scale_y = st->scale, .scale_z = 1.0F, .opacity = st->opacity};
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, row_id, &tgt, style->state_speed, 0.0F);
    const nt_ui_transform_t row_t = {.scale_x = a->scale_x, .scale_y = a->scale_y, .scale_z = 1.0F};
    const nt_ui_element_data_t *row_data = nt_ui_make_element_data_xform(fill_layer, NULL, &row_t, a->opacity);

    nt_atlas_region_ref_t bg;
    const bool has_art = dropdown_resolve_bg(st, &style->row_idle, &bg);

    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(style->row_text)};

    Clay_ElementDeclaration d = {
        .id = (Clay_ElementId){.id = row_id},
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((float)style->row_height)},
                   .padding = {.left = style->pad, .right = style->pad},
                   .childGap = style->pad,
                   .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
        .userData = (void *)row_data,
    };
    dropdown_apply_bg(&d, has_art, &bg, st, style->slice9_scale);

    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(d);
    {
        /* Icon gutter: reserve icon_size px so text stays aligned; draw the icon if its ref is set, else
         * leave the gutter empty (OS-menu icon-column behavior). icon_size==0 -> no gutter at all. */
        if (style->icon_size > 0U) {
            const float gut = (float)style->icon_size;
            CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(gut), CLAY_SIZING_FIXED(gut)}, .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}}}) {
                if (icon != NULL && icon->atlas.id != 0U) {
                    nt_atlas_region_ref_t ic = *icon; /* by-value copy: per-row refs aren't style-owned, can't memoize */
                    nt_atlas_resolve_ref(&ic);
                    if (ic.region != NT_ATLAS_INVALID_REGION) {
                        nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
                        NT_ASSERT(p != NULL && "nt_ui_dropdown: scratch alloc failed (icon payload)");
                        *p = (nt_ui_image_payload_t){.atlas = ic.atlas, .region_index = ic.region, .origin_x = 0.5F, .origin_y = 0.5F, .slice9_scale = 1.0F};
                        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}}, .image = (Clay_ImageElementConfig){.imageData = p}, .userData = NT_UI_CLAY_DATA(fill_layer)}) {}
                    }
                }
            }
        }
        /* Label cell carries a stable id so tests can probe its aligned x position (icon-gutter probe). */
        CLAY({.id = (Clay_ElementId){.id = label_id}, .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
            nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), (label != NULL) ? label : "", &lbl);
        }
    }
    nt_ui_clay_priv_close_element();
    return nt_ui_step_interaction(ctx, row_id).clicked;
}

/* Declare every row; latch a click into *selected and signal close. Used both inside the scroll wrapper
 * (long list) and directly (short list). Returns true if a row was selected this frame. */
static bool dropdown_declare_rows(nt_ui_context_t *ctx, uint8_t fill_layer, uint8_t label_layer, uint32_t id, const char *const *labels, const nt_atlas_region_ref_t *icons, int count, int *selected,
                                  nt_ui_dropdown_style_t *style, bool *open) {
    bool made = false;
    for (int i = 0; i < count; ++i) {
        NT_ASSERT(labels[i] != NULL && "nt_ui_dropdown: label entry must be non-NULL");
        const nt_atlas_region_ref_t *icon = (icons != NULL) ? &icons[i] : NULL;
        if (dropdown_declare_row(ctx, fill_layer, label_layer, dropdown_row_id(id, i), dropdown_row_label_id(id, i), labels[i], icon, i == *selected, style)) {
            *selected = i;
            *open = false;
            made = true;
        }
    }
    return made;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — scroll/non-scroll branch + validation asserts, not deep nesting
bool nt_ui_dropdown_list(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *const *labels, const nt_atlas_region_ref_t *icons, int count,
                         int *selected, nt_ui_dropdown_style_t *style, bool *open) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_dropdown_list: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(id != 0U && labels != NULL && selected != NULL && style != NULL && open != NULL && "nt_ui_dropdown_list: id non-zero, pointers non-NULL");
    NT_ASSERT(count >= 0 && *selected >= -1 && *selected < count && "nt_ui_dropdown_list: selected in [-1,count)");
    assert_style_valid(style);

    const uint8_t fill_layer = (data != NULL) ? data->layer : 0U; /* panel + row fills + icons on data->layer, row text on label_layer */

    /* Anchor the list to the trigger's bbox; default BELOW with edge-flip ABOVE near the bottom border. */
    nt_ui_popup_anchor_t anc = {.prefer_side = NT_UI_POPUP_BELOW};
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, id);
    if (bb.found) {
        anc.x = bb.x;
        anc.y = bb.y;
        anc.w = bb.width;
        anc.h = bb.height;
    }

    nt_ui_popup_style_t pst = nt_ui_popup_style_defaults();
    pst.ease_speed = style->open_ease_speed; /* game-controlled open tween (0 = snap) */
    pst.layer = fill_layer;                  /* widget-owned: the popup panel sits on the fill layer */

    /* Resolve the panel slice9 once: art-or-flat decides whether the panel can round (IMAGE bg can't). */
    nt_atlas_resolve_ref(&style->panel_bg);
    const bool panel_art = (style->panel_bg.atlas.id != 0U && style->panel_bg.region != NT_ATLAS_INVALID_REGION);

    const uint32_t popup_id = dropdown_popup_id(id);
    bool made = false;
    if (nt_ui_popup_visible(ctx, popup_id, &pst, &anc, open)) {
#ifdef NT_TEST_ACCESS
        s_last_side = nt_ui_popup_test_last_side(); /* the side popup-core chose this frame (edge-flip probe) */
#endif
        const bool scrolls = (style->max_visible_rows > 0U) && (count > (int)style->max_visible_rows);
        const float panel_w = (float)style->min_width;
        if (scrolls) {
            /* Long list: wrap the rows in nt_ui_scroll (GC'd cell) — NEVER a raw Clay .clip (a raw .clip
             * leaks a scroll-container pool slot). */
            const float view_h = ((float)style->max_visible_rows * (float)style->row_height) + (2.0F * (float)style->pad);
            /* Game fully owns the list's scroll feel + bar via the embedded list_scroll style (copy); the
             * dropdown still builds the scroll DECL itself (FIXED panel_w x view_h, padding, panel art bg). */
            nt_ui_scroll_style_t sst = style->list_scroll;
            /* scroll owns .id/.clip/.userData; the decl supplies only sizing/look. IMAGE panel art can't
             * round, so drop cornerRadius when art is present (rounding is baked into the sprite). */
            /* Width FIT(min=panel_w) matches the non-scroll branch so long labels don't clip; height stays
             * fixed at view_h (the vertical scroll viewport). */
            Clay_ElementDeclaration scroll_decl = {.layout = {.sizing = {CLAY_SIZING_FIT(.min = panel_w), CLAY_SIZING_FIXED(view_h)}, .padding = CLAY_PADDING_ALL(style->pad)}};
            if (panel_art) {
                nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
                NT_ASSERT(p != NULL && "nt_ui_dropdown: scratch alloc failed (panel payload)");
                *p = (nt_ui_image_payload_t){.atlas = style->panel_bg.atlas, .region_index = style->panel_bg.region, .slice9_scale = style->slice9_scale};
                scroll_decl.image = (Clay_ImageElementConfig){.imageData = p};
                scroll_decl.backgroundColor = nt_ui_unpack_tint(style->panel_tint);
            } else {
                scroll_decl.backgroundColor = (style->panel_fill != 0U) ? nt_ui_unpack_abgr(style->panel_fill) : (Clay_Color){0};
                scroll_decl.cornerRadius = CLAY_CORNER_RADIUS((float)style->panel_corner_radius);
            }
            nt_ui_scroll_begin(ctx, nt_ui_make_element_data(fill_layer, NULL), dropdown_scroll_id(id), &sst, &scroll_decl);
            {
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2}}) {
                    made = dropdown_declare_rows(ctx, fill_layer, label_layer, id, labels, icons, count, selected, style, open);
                }
            }
            nt_ui_scroll_end(ctx);
        } else {
            Clay_ElementDeclaration panel = {.id = (Clay_ElementId){.id = dropdown_panel_id(id)},
                                             .layout = {.sizing = {.width = CLAY_SIZING_FIT(.min = panel_w), .height = CLAY_SIZING_FIT(0)},
                                                        .padding = CLAY_PADDING_ALL(style->pad),
                                                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                                        .childGap = 2},
                                             .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)};
            if (panel_art) {
                nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
                NT_ASSERT(p != NULL && "nt_ui_dropdown: scratch alloc failed (panel payload)");
                *p = (nt_ui_image_payload_t){.atlas = style->panel_bg.atlas, .region_index = style->panel_bg.region, .slice9_scale = style->slice9_scale};
                panel.image = (Clay_ImageElementConfig){.imageData = p};
                panel.backgroundColor = nt_ui_unpack_tint(style->panel_tint);
            } else {
                panel.backgroundColor = (style->panel_fill != 0U) ? nt_ui_unpack_abgr(style->panel_fill) : (Clay_Color){0};
                panel.cornerRadius = CLAY_CORNER_RADIUS((float)style->panel_corner_radius);
            }
            nt_ui_clay_priv_open_element();
            nt_ui_clay_priv_configure_open_element(panel);
            made = dropdown_declare_rows(ctx, fill_layer, label_layer, id, labels, icons, count, selected, style, open);
            nt_ui_clay_priv_close_element();
        }
        nt_ui_popup_end(ctx);
    }
    return made;
}

#ifdef NT_TEST_ACCESS
uint8_t nt_ui_dropdown_test_last_side(void) { return s_last_side; }
uint32_t nt_ui_dropdown_test_scroll_id(uint32_t dropdown_id) { return dropdown_scroll_id(dropdown_id); }
nt_ui_bbox_t nt_ui_dropdown_test_row_label_bbox(const nt_ui_context_t *ctx, uint32_t dropdown_id, int idx) { return nt_ui_get_bbox(ctx, dropdown_row_label_id(dropdown_id, idx)); }
#endif
