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
#define NT_UI_DROPDOWN_CHEVRON_SALT 0x44DB0000U

static inline uint32_t dropdown_popup_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_POPUP_SALT); }
static inline uint32_t dropdown_scroll_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_SCROLL_SALT); }
static inline uint32_t dropdown_panel_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_PANEL_SALT); }
static inline uint32_t dropdown_chevron_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_CHEVRON_SALT); }

/* Immediate-combo row ids. The INTERACTIVE row folds (combo_id, key) ONLY — KEY-STABLE: inserting/hiding/
 * reordering a row during a press/release must NOT shift the interactive/anim/Clay/selection identity of a
 * fixed-key row (the same stability the menu's mix(scope,key) row id gives). The non-interactive LABEL cell
 * folds (combo_id, row_idx) only — key-independent so the _test_row_label_bbox probe can reproduce it from
 * the idx alone (row_idx is unique per frame, positional). fmix, NEVER additive base+index. */
static inline uint32_t combo_row_id(uint32_t combo_id, uint32_t key) {
    uint32_t h = combo_id * 0x9E3779B1U;
    h = (h ^ ((key + 1U) * 0x85EBCA6BU));
    h = (h ^ (h >> 13)) * 0xC2B2AE35U;
    h = (h ^ (h >> 15)) * 0x165667B1U;
    h = h ^ (h >> 16);
    return (h != 0U) ? h : 1U;
}
static inline uint32_t combo_row_label_id(uint32_t combo_id, uint32_t row_idx) {
    uint32_t h = combo_id * 0xC2B2AE35U;
    h = (h ^ ((row_idx + 1U) * 0x27D4EB2FU));
    h = (h ^ (h >> 15)) * 0x85EBCA6BU;
    h = h ^ (h >> 16);
    return (h != 0U) ? h : 1U;
}

/* DEBUG-only duplicate-key guard: row ids are key-stable (mix(combo_id,key)), so two selectables sharing a
 * key alias the SAME interactive/anim/Clay/selection id. Scan the first-N rows this frame for a collision
 * (fail-early, mirrors the menu's duplicate-sibling-key assert). Compiles out in NT_ASSERT_OFF builds. */
static inline void combo_dup_key_check(nt_ui_context_t *ctx, uint32_t row_id) {
#if NT_ASSERT_MODE != NT_ASSERT_OFF
    const uint16_t n = ctx->pending_combo.dup_key_count;
    for (uint16_t k = 0; k < n; ++k) {
        NT_ASSERT(ctx->pending_combo.dup_key_ids[k] != row_id && "nt_ui_combo: duplicate selectable key (keys must be unique within one combo list)");
    }
    if (n < NT_UI_COMBO_DUP_KEY_WINDOW) {
        ctx->pending_combo.dup_key_ids[n] = row_id;
        ctx->pending_combo.dup_key_count = (uint16_t)(n + 1U);
    }
#else
    (void)ctx;
    (void)row_id;
#endif
}

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

/* Whether the chevron will actually draw: a non-zero box AND a resolvable ref. Single predicate so the
 * custom-trigger path can gate the GROW spacer on the same condition (no spacer when no chevron). The
 * resolve is idempotent/caching, so calling it here and again in dropdown_declare_chevron is free. */
static bool dropdown_has_chevron(nt_ui_dropdown_style_t *style) {
    if (style->chevron_size == 0U) {
        return false;
    }
    nt_atlas_resolve_ref(&style->chevron);
    return style->chevron.atlas.id != 0U && style->chevron.region != NT_ATLAS_INVALID_REGION;
}

/* The chevron sprite at the trigger's right edge. Eased open-rotation: 0 closed -> ~180deg open (the
 * down chevron flips to point up). Drawn on the fill layer; tinted; no-op when no ref / chevron_size 0.
 * chevron_id 0 -> anonymous (plain trigger); a derived id lets the custom-trigger path expose a bbox probe. */
static void dropdown_declare_chevron(nt_ui_context_t *ctx, uint8_t fill_layer, uint32_t chevron_id, nt_ui_dropdown_style_t *style, float open_t) {
    if (!dropdown_has_chevron(style)) {
        return;
    }
    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_dropdown: scratch alloc failed (chevron payload)");
    *p = (nt_ui_image_payload_t){.atlas = style->chevron.atlas, .region_index = style->chevron.region, .origin_x = 0.5F, .origin_y = 0.5F, .slice9_scale = 1.0F};
    /* Eased rotation tied to the open amount so the affordance reads the open/close reveal. */
    const nt_ui_transform_t chev_t = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .rotation_z = open_t * 3.14159265F};
    const nt_ui_element_data_t *chev_data = nt_ui_make_element_data_xform(fill_layer, NULL, &chev_t, 1.0F);
    CLAY({.id = (Clay_ElementId){.id = chevron_id},
          .layout = {.sizing = {CLAY_SIZING_FIXED((float)style->chevron_size), CLAY_SIZING_FIXED((float)style->chevron_size)}},
          .image = (Clay_ImageElementConfig){.imageData = p},
          .backgroundColor = nt_ui_unpack_tint(style->chevron_tint),
          .userData = (void *)chev_data}) {}
}

/* Shared row emit for BOTH the plain selectable and the custom selectable_begin: query interaction,
 * per-state pick (selected -> pressed -> hover -> idle), ease the scale/opacity via nt_ui_anim, resolve
 * the per-state bg, then OPEN the row element (LEFT open for the caller's children). Returns the prev-frame
 * interaction so the caller can act on `in.clicked` (custom path) or feed it to a later step (plain path).
 * The caller balances the open element with nt_ui_clay_priv_close_element. */
static nt_ui_interaction_t combo_emit_row_decl(nt_ui_context_t *ctx, uint8_t fill_layer, uint32_t row_id, bool selected, nt_ui_dropdown_style_t *style) {
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
    return in;
}

/* One list row: the shared row rect + an engine column (optional leading icon gutter + left label), closed
 * here. Returns clicked (the canonical mutating step_interaction). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) — icon-gutter CLAY nesting, not deep control flow
static bool dropdown_declare_row(nt_ui_context_t *ctx, uint8_t fill_layer, uint8_t label_layer, uint32_t row_id, uint32_t label_id, const char *label, const nt_atlas_region_ref_t *icon, bool selected,
                                 nt_ui_dropdown_style_t *style) {
    (void)combo_emit_row_decl(ctx, fill_layer, row_id, selected, style); /* opens the row element; the engine column follows */
    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(style->row_text)};
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

/* ---- Immediate combo (begin/selectable/end) ---- */

/* Combo render layers come from the CALL (data->layer for fills, label_layer for text), NOT the style —
 * mirrors menu/checkbox/tabbar so the game owns the render band. The scrollbar floats on the SAME fill
 * layer as the panel (nt_ui_scroll draws its bar on the container's data->layer); floating-over-same-layer
 * keeps it above the panel fill at ANY base, so no fixed +1 offset is needed. data NULL => fills fall to 0. */
#define NT_UI_COMBO_TRIGGER_H 32.0F /* default trigger control height: combo_begin owns sizing (no caller decl), the list anchors to trigger bottom */

/* Emit the trigger element + harvest the toggle. When `leave_open` the element stays OPEN (the game declares
 * the preview content) and combo_preview_end appends the chevron tail + closes + does the ONE step; otherwise
 * emit the preview label + chevron inline here, close, and do the step (toggling *open on a click). The
 * trigger interior is fed a single `preview` string (the game-owned current-selection label). */
static void combo_open_trigger(nt_ui_context_t *ctx, uint8_t fill_layer, uint8_t label_layer, uint32_t id, const char *preview, nt_ui_dropdown_style_t *style, bool *open, bool leave_open) {
    const nt_ui_interaction_t in = nt_ui_query_interaction(ctx, id);

    nt_ui_dd_state_t *st = &style->trigger_idle;
    if (in.pressed && in.hovered) {
        st = &style->trigger_pressed;
    } else if (in.hovered) {
        st = &style->trigger_hover;
    }

    const nt_ui_anim_target_t tgt = {.scale_x = st->scale, .scale_y = st->scale, .scale_z = 1.0F, .opacity = st->opacity, .value_t = *open ? 1.0F : 0.0F};
    const nt_ui_anim_interaction_t *a = nt_ui_anim(ctx, id, &tgt, style->state_speed, style->value_speed);
    const nt_ui_transform_t trig_t = {.scale_x = a->scale_x, .scale_y = a->scale_y, .scale_z = 1.0F};
    const nt_ui_element_data_t *trig_data = nt_ui_make_element_data_xform(fill_layer, NULL, &trig_t, a->opacity);

    nt_atlas_region_ref_t bg;
    const bool has_art = dropdown_resolve_bg(st, &style->trigger_idle, &bg);

    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(style->trigger_text)};
    /* combo_begin owns sizing (no caller decl): FIT(min=min_width) wide, a fixed control height. The list
     * anchors to this trigger's bottom edge, so a stable height keeps the open list placement predictable. */
    Clay_ElementDeclaration d = {.id = (Clay_ElementId){.id = id},
                                 .layout = {.sizing = {CLAY_SIZING_FIT(.min = (float)style->min_width), CLAY_SIZING_FIXED(NT_UI_COMBO_TRIGGER_H)},
                                            .layoutDirection = CLAY_LEFT_TO_RIGHT,
                                            .childGap = style->pad,
                                            .padding = {.left = style->pad, .right = style->pad},
                                            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}};
    dropdown_apply_bg(&d, has_art, &bg, st, style->slice9_scale);
    d.userData = (void *)trig_data;

    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(d);
    if (leave_open) {
        /* Stash the eased open amount so combo_preview_end draws the chevron WITHOUT re-stepping anim. */
        ctx->pending_combo.trigger_open_t = a->value_t;
        return; /* the game declares the trigger content; combo_preview_end appends the chevron, closes + steps */
    }
    /* Label grows to push the chevron to the right edge (mirror the data-form trigger). */
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
        nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), (preview != NULL) ? preview : "", &lbl);
    }
    dropdown_declare_chevron(ctx, fill_layer, 0U, style, a->value_t);
    nt_ui_clay_priv_close_element();
    if (nt_ui_step_interaction(ctx, id).clicked) {
        *open = !*open;
    }
}

/* Apply the list-panel slice9 art (tinted) or the flat panel_fill onto a decl. IMAGE bg can't round, so
 * cornerRadius is only set in the flat branch. Shared by the scroll-wrapper + plain-panel list bodies. */
static void combo_apply_panel_bg(Clay_ElementDeclaration *decl, const nt_ui_dropdown_style_t *style, bool panel_art) {
    if (panel_art) {
        nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
        NT_ASSERT(p != NULL && "nt_ui_dropdown: scratch alloc failed (panel payload)");
        *p = (nt_ui_image_payload_t){.atlas = style->panel_bg.atlas, .region_index = style->panel_bg.region, .slice9_scale = style->slice9_scale};
        decl->image = (Clay_ImageElementConfig){.imageData = p};
        decl->backgroundColor = nt_ui_unpack_tint(style->panel_tint);
    } else {
        decl->backgroundColor = (style->panel_fill != 0U) ? nt_ui_unpack_abgr(style->panel_fill) : (Clay_Color){0};
        decl->cornerRadius = CLAY_CORNER_RADIUS((float)style->panel_corner_radius);
    }
}

/* The combo list body: ALWAYS wrap the rows in nt_ui_scroll — a raw .clip leaks a scroll-container pool
 * slot per open. When max_visible_rows > 0 the viewport FITs short lists and caps+scrolls long ones (the immediate API
 * discovers the row count during the frame, so the cap is a height bound, not a count branch). Otherwise a
 * plain panel. Leaves the inner row container OPEN for the selectables. */
static void combo_open_body(nt_ui_context_t *ctx, uint8_t fill_layer, uint32_t id, nt_ui_dropdown_style_t *style, bool scrolls, bool panel_art) {
    const float panel_w = (float)style->min_width;
    if (scrolls) {
        const float view_h = ((float)style->max_visible_rows * (float)style->row_height) + (2.0F * (float)style->pad);
        nt_ui_scroll_style_t sst = style->list_scroll;
        Clay_ElementDeclaration scroll_decl = {.layout = {.sizing = {CLAY_SIZING_FIT(.min = panel_w), CLAY_SIZING_FIT(.max = view_h)}, .padding = CLAY_PADDING_ALL(style->pad)}};
        combo_apply_panel_bg(&scroll_decl, style, panel_art);
        nt_ui_scroll_begin(ctx, nt_ui_make_element_data(fill_layer, NULL), dropdown_scroll_id(id), &sst, &scroll_decl);
        nt_ui_clay_priv_open_element();
        nt_ui_clay_priv_configure_open_element((Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2}});
        return;
    }
    Clay_ElementDeclaration panel = {
        .id = (Clay_ElementId){.id = dropdown_panel_id(id)},
        .layout = {.sizing = {.width = CLAY_SIZING_FIT(.min = panel_w), .height = CLAY_SIZING_FIT(0)}, .padding = CLAY_PADDING_ALL(style->pad), .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2},
        .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)};
    combo_apply_panel_bg(&panel, style, panel_art);
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(panel);
}

/* Open the list popup + the inner row container after the trigger has emitted. Returns whether the body
 * should be declared this frame (the popup is visible). On true, pushes pending_combo so the per-row
 * selectables + combo_end find their bookkeeping. */
static bool combo_open_list(nt_ui_context_t *ctx, uint8_t fill_layer, uint8_t label_layer, uint32_t id, nt_ui_dropdown_style_t *style, bool *open) {
    nt_ui_popup_anchor_t anc = {.prefer_side = NT_UI_POPUP_BELOW};
    const nt_ui_bbox_t bb = nt_ui_get_bbox(ctx, id);
    if (bb.found) {
        anc.x = bb.x;
        anc.y = bb.y;
        anc.w = bb.width;
        anc.h = bb.height;
    }

    nt_ui_popup_style_t pst = nt_ui_popup_style_defaults();
    pst.ease_speed = style->open_ease_speed;
    pst.layer = fill_layer;

    nt_atlas_resolve_ref(&style->panel_bg);
    const bool panel_art = (style->panel_bg.atlas.id != 0U && style->panel_bg.region != NT_ATLAS_INVALID_REGION);

    if (!nt_ui_popup_visible(ctx, dropdown_popup_id(id), &pst, &anc, open)) {
        return false; /* popup self-balances when fully closed (no end needed) */
    }
#ifdef NT_TEST_ACCESS
    s_last_side = nt_ui_popup_test_last_side();
#endif

    const bool scrolls = (style->max_visible_rows > 0U);
    combo_open_body(ctx, fill_layer, id, style, scrolls, panel_art);

    ctx->pending_combo.style = style;
    ctx->pending_combo.id = id;
    ctx->pending_combo.open = open;
    ctx->pending_combo.row_idx = 0U;
#if NT_ASSERT_MODE != NT_ASSERT_OFF
    ctx->pending_combo.dup_key_count = 0U; /* fresh per-list dup-key window */
#endif
    ctx->pending_combo.fill_layer = fill_layer;
    ctx->pending_combo.label_layer = label_layer;
    ctx->pending_combo.scrolls = scrolls ? 1U : 0U;
    ctx->pending_combo.active = 1U;
    ctx->pending_combo.row_open = 0U;
    return true;
}

bool nt_ui_combo_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *preview, nt_ui_dropdown_style_t *style, bool *open) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_combo_begin: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(id != 0U && style != NULL && open != NULL && "nt_ui_combo_begin: id non-zero, pointers non-NULL");
    NT_ASSERT(!ctx->pending_combo.active && !ctx->pending_combo.trigger_open && "nt_ui_combo_begin: combos do not nest");
    assert_style_valid(style);

    const uint8_t fill_layer = (data != NULL) ? data->layer : 0U; /* fills on data->layer, text on label_layer (game-controlled render order) */
    combo_open_trigger(ctx, fill_layer, label_layer, id, preview, style, open, false);
    return combo_open_list(ctx, fill_layer, label_layer, id, style, open);
}

void nt_ui_combo_preview_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, nt_ui_dropdown_style_t *style, bool *open) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_combo_preview_begin: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(id != 0U && style != NULL && open != NULL && "nt_ui_combo_preview_begin: id non-zero, pointers non-NULL");
    NT_ASSERT(!ctx->pending_combo.active && !ctx->pending_combo.trigger_open && "nt_ui_combo_preview_begin: combos do not nest");
    assert_style_valid(style);

    const uint8_t fill_layer = (data != NULL) ? data->layer : 0U; /* fills on data->layer, text on label_layer (game-controlled render order) */
    combo_open_trigger(ctx, fill_layer, label_layer, id, NULL, style, open, true);
    /* The trigger element stays open for the game's content; stash what combo_preview_end needs. */
    ctx->pending_combo.id = id;
    ctx->pending_combo.style = style;
    ctx->pending_combo.open = open;
    ctx->pending_combo.fill_layer = fill_layer; /* combo_preview_end re-opens the list with the game's base layer */
    ctx->pending_combo.label_layer = label_layer;
    ctx->pending_combo.trigger_open = 1U;
}

bool nt_ui_combo_preview_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_combo_preview_end: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(ctx->pending_combo.trigger_open && "nt_ui_combo_preview_end without combo_preview_begin");

    const uint32_t id = ctx->pending_combo.id;
    nt_ui_dropdown_style_t *style = (nt_ui_dropdown_style_t *)ctx->pending_combo.style;
    bool *open = ctx->pending_combo.open;
    const uint8_t fill_layer = ctx->pending_combo.fill_layer;
    const uint8_t label_layer = ctx->pending_combo.label_layer;

    /* The chevron is the combo's OWN affordance (open/close indicator), independent of the game's preview
     * content — draw it in the custom trigger too. GROW spacer pushes it to the right edge past the game's
     * content (mirrors the plain trigger's [content GROW][chevron] tail). Both gated on the chevron actually
     * drawing: opting out (chevron_size 0 / no ref) emits NEITHER, so the game owns the trigger interior. */
    if (dropdown_has_chevron(style)) {
        CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {}
        dropdown_declare_chevron(ctx, fill_layer, dropdown_chevron_id(id), style, ctx->pending_combo.trigger_open_t);
    }

    nt_ui_clay_priv_close_element();
    if (nt_ui_step_interaction(ctx, id).clicked) {
        *open = !*open;
    }
    ctx->pending_combo.trigger_open = 0U;
    /* Open the list now that the trigger bbox is closed (matches combo_begin's order). */
    return combo_open_list(ctx, fill_layer, label_layer, id, style, open);
}

/* Shared row emit for the plain + iconed selectable: one engine-owned column ([icon gutter][label LEFT]),
 * so iconed and text-only rows align their labels at the SAME x (icon drawn in the gutter when set, else
 * the gutter is empty). `icon` may be NULL (text-only). */
static bool combo_emit_selectable(nt_ui_context_t *ctx, uint32_t key, const nt_atlas_region_ref_t *icon, const char *label, bool selected) {
    nt_ui_dropdown_style_t *style = (nt_ui_dropdown_style_t *)ctx->pending_combo.style;
    const uint16_t idx = ctx->pending_combo.row_idx++;
    const uint32_t row_id = combo_row_id(ctx->pending_combo.id, key); /* key-stable interactive identity */
    const uint32_t label_id = combo_row_label_id(ctx->pending_combo.id, idx);
    combo_dup_key_check(ctx, row_id);
    const bool clicked = dropdown_declare_row(ctx, ctx->pending_combo.fill_layer, ctx->pending_combo.label_layer, row_id, label_id, label, icon, selected, style);
    if (clicked) {
        *(ctx->pending_combo.open) = false; /* the game writes *selected; the combo clears *open (Model-D) */
    }
    return clicked;
}

bool nt_ui_combo_selectable(nt_ui_context_t *ctx, uint32_t key, const char *label, bool selected) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_combo_selectable: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(ctx->pending_combo.active && !ctx->pending_combo.row_open && "nt_ui_combo_selectable: call between combo_begin and combo_end, not inside a custom row");
    return combo_emit_selectable(ctx, key, NULL, label, selected);
}

bool nt_ui_combo_selectable_icon(nt_ui_context_t *ctx, uint32_t key, const nt_atlas_region_ref_t *icon, const char *label, bool selected) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_combo_selectable_icon: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(ctx->pending_combo.active && !ctx->pending_combo.row_open && "nt_ui_combo_selectable_icon: call between combo_begin and combo_end, not inside a custom row");
    return combo_emit_selectable(ctx, key, icon, label, selected);
}

bool nt_ui_combo_selectable_begin(nt_ui_context_t *ctx, uint32_t key, bool selected) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_combo_selectable_begin: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(ctx->pending_combo.active && !ctx->pending_combo.row_open && "nt_ui_combo_selectable_begin: call between combo_begin and combo_end, not nested");

    nt_ui_dropdown_style_t *style = (nt_ui_dropdown_style_t *)ctx->pending_combo.style;
    const uint16_t idx = ctx->pending_combo.row_idx++;
    const uint32_t row_id = combo_row_id(ctx->pending_combo.id, key); /* key-stable interactive identity */
    const uint32_t label_id = combo_row_label_id(ctx->pending_combo.id, idx);
    combo_dup_key_check(ctx, row_id);

    /* Shared per-state pick + anim + bg + open; the row element stays OPEN for the game's content. */
    const nt_ui_interaction_t in = combo_emit_row_decl(ctx, ctx->pending_combo.fill_layer, row_id, selected, style);

    /* No engine icon gutter here: a custom-content selectable is game-owned (the game declares its OWN
     * inline icon + label), mirroring nt_ui_menu_item_begin. The plain nt_ui_combo_selectable /
     * nt_ui_combo_selectable_icon paths keep the single engine gutter.
     * Zero-width anchor carrying the probe id at the row's left content edge; FIT(0) so it does NOT push
     * the game's content right (a GROW cell here would eat the row and float the content rightward). */
    CLAY({.id = (Clay_ElementId){.id = label_id}, .layout = {.sizing = {CLAY_SIZING_FIT(0), CLAY_SIZING_FIT(0)}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {}

    ctx->pending_combo.row_id = row_id;
    ctx->pending_combo.row_open = 1U;
    return in.clicked;
}

void nt_ui_combo_selectable_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_combo_selectable_end: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(ctx->pending_combo.row_open && "nt_ui_combo_selectable_end without combo_selectable_begin");

    const uint32_t row_id = ctx->pending_combo.row_id;
    nt_ui_clay_priv_close_element();
    if (nt_ui_step_interaction(ctx, row_id).clicked) {
        *(ctx->pending_combo.open) = false; /* the game writes *selected; the combo clears *open (Model-D) */
    }
    ctx->pending_combo.row_open = 0U;
}

void nt_ui_combo_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_combo_end: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(ctx->pending_combo.active && "nt_ui_combo_end without combo_begin");
    NT_ASSERT(!ctx->pending_combo.row_open && "nt_ui_combo_end with an open custom selectable (missing combo_selectable_end)");

    if (ctx->pending_combo.scrolls) {
        nt_ui_clay_priv_close_element(); /* the inner TOP_TO_BOTTOM container */
        nt_ui_scroll_end(ctx);
    } else {
        nt_ui_clay_priv_close_element(); /* the plain panel */
    }
    nt_ui_popup_end(ctx);
    ctx->pending_combo.active = 0U;
    ctx->pending_combo.style = NULL;
    ctx->pending_combo.open = NULL;
}

#ifdef NT_TEST_ACCESS
uint8_t nt_ui_dropdown_test_last_side(void) { return s_last_side; }
uint32_t nt_ui_dropdown_test_scroll_id(uint32_t dropdown_id) { return dropdown_scroll_id(dropdown_id); }
/* Combo rows key the label cell by (combo_id, row_idx); row_idx == idx for the in-order selectable feed. */
nt_ui_bbox_t nt_ui_dropdown_test_row_label_bbox(const nt_ui_context_t *ctx, uint32_t dropdown_id, int idx) { return nt_ui_get_bbox(ctx, combo_row_label_id(dropdown_id, (uint32_t)idx)); }
nt_ui_bbox_t nt_ui_dropdown_test_chevron_bbox(const nt_ui_context_t *ctx, uint32_t dropdown_id) { return nt_ui_get_bbox(ctx, dropdown_chevron_id(dropdown_id)); }
uint32_t nt_ui_dropdown_test_combo_row_id(uint32_t combo_id, uint32_t key) { return combo_row_id(combo_id, key); }
#endif
