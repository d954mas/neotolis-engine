#include "ui/nt_ui_tabbar.h"

#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_label.h"

const nt_ui_widget_def_t NT_UI_TABBAR_DEF = {
    .name = "nt_tabbar",
    .pill_color = 0xFF40C0A0U,
    ._reserved = 0U,
};

nt_ui_tabbar_style_t nt_ui_tabbar_style_defaults(void) {
    return (nt_ui_tabbar_style_t){
        .bar_bg = 0xFF22262AU,
        .tab_bg = 0U, /* transparent: the bar bg shows through unselected tabs */
        .tab_selected = 0xFF2E629EU,
        .tab_hover = 0xFF3A3F46U,
        .accent = 0xFFE0B050U,
        .text = 0xFFC8C8C8U,
        .text_selected = 0xFFFFFFFFU,
        .font_size = 14.0F,
        .tab_extent = 38U,
        .accent_px = 3U,
        .pad = 8U,
        .gap = 6U,
        .font_id = 0U,
        .dir = NT_UI_TABBAR_VERTICAL,
        .layer = 0U,
    };
}

/* One tab: a full-extent click target with a leading accent bar (active only) + a label. Plain rect +
 * step_interaction (no button-style construction). Returns clicked. */
static bool tabbar_declare_tab(nt_ui_context_t *ctx, uint32_t tab_id, const char *label, bool active, const nt_ui_tabbar_style_t *style) {
    const nt_ui_interaction_t in = nt_ui_query_interaction(ctx, tab_id);
    const bool horizontal = (style->dir == NT_UI_TABBAR_HORIZONTAL);

    uint32_t bg = style->tab_bg;
    if (active) {
        bg = style->tab_selected;
    } else if (in.hovered) {
        bg = style->tab_hover;
    }
    const uint32_t txt = active ? style->text_selected : style->text;
    const nt_ui_label_style_t lbl = {.font_id = style->font_id, .font_size = style->font_size, .color = nt_ui_unpack_abgr(txt)};

    /* The tab is a single rect (label directly inside) with a leading-edge accent BORDER on the active
     * tab — no nested child elements, so numerically-adjacent tab ids cannot collide on Clay's auto-id
     * for unnamed children. Vertical bar -> accent on the LEFT edge; horizontal -> on the TOP edge. */
    const Clay_Color accent_col = active ? nt_ui_unpack_abgr(style->accent) : (Clay_Color){0};
    const uint16_t a = active ? style->accent_px : 0U;
    Clay_BorderWidth bw = {0};
    if (horizontal) {
        bw.top = a;
    } else {
        bw.left = a;
    }
    /* Pad the label past the accent edge so it never sits under the border. */
    Clay_Padding pad = {.left = style->pad, .right = style->pad, .top = style->pad, .bottom = style->pad};
    if (horizontal) {
        pad.top = (uint16_t)(style->pad + style->accent_px);
    } else {
        pad.left = (uint16_t)(style->pad + style->accent_px);
    }
    CLAY({
        .id = (Clay_ElementId){.id = tab_id},
        .layout = {.sizing =
                       horizontal ? (Clay_Sizing){CLAY_SIZING_FIXED((float)style->tab_extent), CLAY_SIZING_GROW(0)} : (Clay_Sizing){CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((float)style->tab_extent)},
                   .padding = pad,
                   .childAlignment = {CLAY_ALIGN_X_LEFT, CLAY_ALIGN_Y_CENTER}},
        .backgroundColor = (bg != 0U) ? nt_ui_unpack_abgr(bg) : (Clay_Color){0},
        .cornerRadius = CLAY_CORNER_RADIUS(6),
        .border = {.color = accent_col, .width = bw},
        .userData = (void *)nt_ui_make_element_data(style->layer, NULL),
    }) {
        nt_ui_label(ctx, nt_ui_make_element_data(style->layer, NULL), (label != NULL) ? label : "", &lbl);
    }
    return nt_ui_step_interaction(ctx, tab_id).clicked;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — CLAY macro expands to a for-loop; metric counts that + the tab loop + asserts, not real nesting
int nt_ui_tabbar(nt_ui_context_t *ctx, uint32_t base_id, const char *const *labels, int count, int *active, const nt_ui_tabbar_style_t *style) {
    NT_ASSERT(ctx != NULL && ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_tabbar: call between nt_ui_begin/end on the active ctx");
    NT_ASSERT(base_id != 0U && labels != NULL && active != NULL && style != NULL && "nt_ui_tabbar: base_id non-zero, pointers non-NULL");
    NT_ASSERT(count >= 0 && "nt_ui_tabbar: count must be >= 0");                                         /* T-65-15 */
    NT_ASSERT(*active >= -1 && (count == 0 || *active < count) && "nt_ui_tabbar: active in [-1,count)"); /* T-65-15 */
    NT_ASSERT(style->font_size > 0.0F && "nt_ui_tabbar: font_size > 0");

    const bool horizontal = (style->dir == NT_UI_TABBAR_HORIZONTAL);
    int clicked = -1;
    /* The container has no explicit id (base_id is reserved for the tabs: base_id + i). */
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(style->pad),
                     .layoutDirection = horizontal ? CLAY_LEFT_TO_RIGHT : CLAY_TOP_TO_BOTTOM,
                     .childGap = style->gap},
          .backgroundColor = (style->bar_bg != 0U) ? nt_ui_unpack_abgr(style->bar_bg) : (Clay_Color){0},
          .userData = (void *)nt_ui_make_element_data(style->layer, NULL)}) {
        for (int i = 0; i < count; ++i) {
            NT_ASSERT(labels[i] != NULL && "nt_ui_tabbar: label entry must be non-NULL"); /* T-65-16 */
            /* Salt per-tab ids from base_id + i (mirrors the showcase s_id_tab_btn_base + i). */
            if (tabbar_declare_tab(ctx, base_id + (uint32_t)i, labels[i], i == *active, style)) {
                *active = i; /* Model D: latch the game-owned active index */
                clicked = i;
            }
        }
    }
    return clicked;
}
