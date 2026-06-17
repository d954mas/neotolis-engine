#include "ui/nt_ui_dropdown.h"

#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
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

static inline uint32_t dropdown_popup_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_POPUP_SALT); }
static inline uint32_t dropdown_scroll_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_SCROLL_SALT); }
static inline uint32_t dropdown_panel_id(uint32_t id) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_PANEL_SALT); }
static inline uint32_t dropdown_row_id(uint32_t id, int idx) { return nt_ui_derived_id(id, NT_UI_DROPDOWN_ROW_SALT + (uint32_t)idx); }

#ifdef NT_TEST_ACCESS
static uint8_t s_last_side; /* edge-flip probe (NT_TEST_ACCESS) */
#endif

nt_ui_dropdown_style_t nt_ui_dropdown_style_defaults(void) {
    return (nt_ui_dropdown_style_t){
        .trigger_bg = 0xFF3A3A3AU,
        .trigger_text = 0xFFE8E8E8U,
        .panel_bg = 0xFF2A2A2AU,
        .row_text = 0xFFE0E0E0U,
        .row_hover_bg = 0xFF4A4A4AU,
        .row_selected_bg = 0xFF505A78U,
        .font_size = 14.0F,
        .row_height = 28U,
        .min_width = 160U,
        .pad = 6U,
        .font_id = 0U,
        .max_visible_rows = 6U,
    };
}

/* The label shown on the trigger: the selected entry, or the placeholder when -1 / out of range. */
static const char *dropdown_trigger_text(const char *const *labels, int count, int selected, const char *placeholder) {
    if (selected >= 0 && selected < count && labels[selected] != NULL) {
        return labels[selected];
    }
    return (placeholder != NULL) ? placeholder : "";
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — complexity is the validation assert chain, not control flow
bool nt_ui_dropdown_trigger(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *const *labels, int count, int selected, const char *placeholder,
                            const nt_ui_dropdown_style_t *style, const Clay_ElementDeclaration *decl, bool *open) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_dropdown_trigger: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(id != 0U && labels != NULL && style != NULL && open != NULL && "nt_ui_dropdown_trigger: id non-zero, pointers non-NULL");
    NT_ASSERT(count >= 0 && selected >= -1 && selected < count && "nt_ui_dropdown_trigger: selected in [-1,count)"); /* T-65-15 */
    NT_ASSERT(style->font_size > 0.0F && "nt_ui_dropdown_trigger: font_size > 0");

    const uint8_t fill_layer = (data != NULL) ? data->layer : 0U; /* fill on data->layer, label on label_layer (batch split) */
    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(style->trigger_text)};
    /* The caller owns sizing/padding via `decl`; the engine owns .id/.backgroundColor/.userData so the
     * trigger bbox is queryable for the list anchor. A {0}/NULL decl is a FIT trigger. */
    Clay_ElementDeclaration d = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    d.id = (Clay_ElementId){.id = id};
    d.backgroundColor = nt_ui_unpack_abgr(style->trigger_bg);
    if (d.layout.childAlignment.y == 0) {
        d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    }
    if (d.layout.padding.left == 0 && d.layout.padding.right == 0) {
        d.layout.padding.left = style->pad;
        d.layout.padding.right = style->pad;
    }
    d.userData = (void *)nt_ui_make_element_data(fill_layer, NULL);

    bool toggled = false;
    /* Runtime-built decl: use the priv open/configure/close pattern (CLAY() brace-inits a wrapper from a
     * compound literal and cannot take a variable decl). */
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(d);
    nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), dropdown_trigger_text(labels, count, selected, placeholder), &lbl);
    nt_ui_clay_priv_close_element();
    const nt_ui_interaction_t in = nt_ui_step_interaction(ctx, id);
    if (in.clicked) {
        *open = !*open;
        toggled = true;
    }
    return toggled;
}

/* One list row: a fixed-height rect + label, highlighted when hovered or it is the current selection.
 * Plain rect + step_interaction (no button-style construction); returns clicked. */
static bool dropdown_declare_row(nt_ui_context_t *ctx, uint8_t fill_layer, uint8_t label_layer, uint32_t row_id, const char *label, bool selected, const nt_ui_dropdown_style_t *style) {
    const nt_ui_interaction_t in = nt_ui_query_interaction(ctx, row_id);
    uint32_t bg = 0U;
    if (in.hovered) {
        bg = style->row_hover_bg;
    } else if (selected) {
        bg = style->row_selected_bg;
    }
    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(style->row_text)};
    CLAY({
        .id = (Clay_ElementId){.id = row_id},
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((float)style->row_height)}, .padding = {.left = style->pad, .right = style->pad}, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = (bg != 0U) ? nt_ui_unpack_abgr(bg) : (Clay_Color){0},
        .userData = (void *)nt_ui_make_element_data(fill_layer, NULL),
    }) {
        nt_ui_label(ctx, nt_ui_make_element_data(label_layer, NULL), (label != NULL) ? label : "", &lbl);
    }
    return nt_ui_step_interaction(ctx, row_id).clicked;
}

/* Declare every row; latch a click into *selected and signal close. Used both inside the scroll wrapper
 * (long list) and directly (short list). Returns true if a row was selected this frame. */
static bool dropdown_declare_rows(nt_ui_context_t *ctx, uint8_t fill_layer, uint8_t label_layer, uint32_t id, const char *const *labels, int count, int *selected, const nt_ui_dropdown_style_t *style,
                                  bool *open) {
    bool made = false;
    for (int i = 0; i < count; ++i) {
        NT_ASSERT(labels[i] != NULL && "nt_ui_dropdown: label entry must be non-NULL"); /* T-65-16 */
        if (dropdown_declare_row(ctx, fill_layer, label_layer, dropdown_row_id(id, i), labels[i], i == *selected, style)) {
            *selected = i;
            *open = false;
            made = true;
        }
    }
    return made;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — scroll/non-scroll branch + validation asserts, not deep nesting
bool nt_ui_dropdown_list(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *const *labels, int count, int *selected,
                         const nt_ui_dropdown_style_t *style, bool *open) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_dropdown_list: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(id != 0U && labels != NULL && selected != NULL && style != NULL && open != NULL && "nt_ui_dropdown_list: id non-zero, pointers non-NULL");
    NT_ASSERT(count >= 0 && *selected >= -1 && *selected < count && "nt_ui_dropdown_list: selected in [-1,count)"); /* T-65-15 */

    const uint8_t fill_layer = (data != NULL) ? data->layer : 0U; /* panel + row fills on data->layer, row text on label_layer */

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
    pst.ease_speed = 0.0F;  /* lists snap open; no spatial tween */
    pst.layer = fill_layer; /* the popup panel sits on the fill layer */

    const uint32_t popup_id = dropdown_popup_id(id);
    bool made = false;
    if (nt_ui_popup_visible(ctx, popup_id, &pst, &anc, open)) {
#ifdef NT_TEST_ACCESS
        s_last_side = nt_ui_popup_test_last_side(); /* the side popup-core chose this frame (edge-flip probe) */
#endif
        const bool scrolls = (style->max_visible_rows > 0U) && (count > (int)style->max_visible_rows);
        const float panel_w = (float)style->min_width;
        if (scrolls) {
            /* Long list: wrap the rows in nt_ui_scroll (GC'd cell) — NEVER a raw Clay .clip (Pitfall 7). */
            const float view_h = ((float)style->max_visible_rows * (float)style->row_height) + (2.0F * (float)style->pad);
            nt_ui_scroll_style_t sst = nt_ui_scroll_style_defaults();
            /* scroll owns .id/.clip/.userData; the decl supplies only sizing/look. */
            nt_ui_scroll_begin(ctx, nt_ui_make_element_data(fill_layer, NULL), dropdown_scroll_id(id), &sst,
                               &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(panel_w), CLAY_SIZING_FIXED(view_h)}, .padding = CLAY_PADDING_ALL(style->pad)},
                                                          .backgroundColor = nt_ui_unpack_abgr(style->panel_bg),
                                                          .cornerRadius = CLAY_CORNER_RADIUS(6)});
            {
                CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2}}) {
                    made = dropdown_declare_rows(ctx, fill_layer, label_layer, id, labels, count, selected, style, open);
                }
            }
            nt_ui_scroll_end(ctx);
        } else {
            CLAY({.id = (Clay_ElementId){.id = dropdown_panel_id(id)},
                  .layout = {.sizing = {.width = CLAY_SIZING_FIT(.min = panel_w), .height = CLAY_SIZING_FIT(0)},
                             .padding = CLAY_PADDING_ALL(style->pad),
                             .layoutDirection = CLAY_TOP_TO_BOTTOM,
                             .childGap = 2},
                  .backgroundColor = nt_ui_unpack_abgr(style->panel_bg),
                  .cornerRadius = CLAY_CORNER_RADIUS(6),
                  .userData = (void *)nt_ui_make_element_data(fill_layer, NULL)}) {
                made = dropdown_declare_rows(ctx, fill_layer, label_layer, id, labels, count, selected, style, open);
            }
        }
        nt_ui_popup_end(ctx);
    }
    return made;
}

#ifdef NT_TEST_ACCESS
uint8_t nt_ui_dropdown_test_last_side(void) { return s_last_side; }
uint32_t nt_ui_dropdown_test_scroll_id(uint32_t dropdown_id) { return dropdown_scroll_id(dropdown_id); }
#endif
