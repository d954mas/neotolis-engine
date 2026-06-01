/* Clay debug view design ported from deps/clay/clay.h (zlib license,
 * (c) 2024 Nic Barker). The colors, row heights, indent widths, pill shapes,
 * element-info attribute rows, and overall layout are reproduced as
 * faithfully as practical for a post-walk overlay.
 *
 * Clay's `Clay__RenderDebugView` (clay.h ~3392-3800) runs INSIDE the
 * declaration phase and emits CLAY({...}) macros that participate in the
 * same layout solve as the user's UI. Our inspector runs AFTER nt_ui_walk
 * (parallel to nt_stats_draw) and emits sprite + text commands directly --
 * a "call site verbatim port" is not possible without re-architecting the
 * inspector to run between begin/end. The VISUAL DESIGN, however, is a
 * 1:1 port: same color palette (CLAY__DEBUGVIEW_COLOR_1..4), same row
 * cadence (CLAY__DEBUGVIEW_ROW_HEIGHT, OUTER_PADDING, INDENT_WIDTH), same
 * info-pane attribute layout (Bounding Box / Sizing / Padding / Child
 * Alignment / Colors), and the same per-config-type pill colors from
 * Clay__DebugGetElementConfigTypeLabel.
 *
 * Extensions:
 *   1) Widget-type pill -- our nt_ui widgets register a tag in
 *      ctx->widget_registry; the row's tail shows button / image / label /
 *      panel / group when applicable.
 *   2) Hit-zone overlay -- drawn at the END of the inspector by calling
 *      nt_ui_debug_draw_hit_zones, gated on ctx->debug_recording. This
 *      re-couples the two debug aids: F3 alone toggles both. */

#include "ui/nt_ui_inspector.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "core/nt_builtins.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_debug.h"
#include "ui/nt_ui_internal.h"

// #region toggle + getters
void nt_ui_inspector_set_active(nt_ui_context_t *ctx, bool on) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_set_active: ctx must be non-NULL");
    ctx->inspector_active = on;
}

bool nt_ui_inspector_is_active(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_is_active: ctx must be non-NULL");
    return ctx->inspector_active;
}
// #endregion

// #region clay debug view palette + metrics (ported verbatim from clay.h:3113-3121)
/* CLAY__DEBUGVIEW_COLOR_1 = {58, 56, 52, 255}  -- row alt 1 */
/* CLAY__DEBUGVIEW_COLOR_2 = {62, 60, 58, 255}  -- row alt 2 / header bg */
/* CLAY__DEBUGVIEW_COLOR_3 = {141, 133, 135, 255} -- separators / dim text */
/* CLAY__DEBUGVIEW_COLOR_4 = {238, 226, 231, 255} -- body text */
/* Colors here are 0xAABBGGRR packed for nt_sprite_renderer. */
#define CDV_COLOR_1 0xFF383834U
#define CDV_COLOR_2 0xFF3A3C3EU
#define CDV_COLOR_3 0xFF87858DU
#define CDV_COLOR_4 0xFFE7E2EEU
#define CDV_COLOR_SEL_ROW 0xFF4E5066U /* CLAY__DEBUGVIEW_COLOR_SELECTED_ROW (102,80,78) */
#define CDV_PANEL_BORDER 0xFF87858DU
#define CDV_HIT_HINT 0xC033CC33U /* light green tag for "widget" rows */

#define CDV_ROW_H 24.0F         /* CLAY__DEBUGVIEW_ROW_HEIGHT 30 -- shrunk to fit our font */
#define CDV_OUTER_PADDING 10.0F /* CLAY__DEBUGVIEW_OUTER_PADDING */
#define CDV_INDENT_WIDTH 16.0F  /* CLAY__DEBUGVIEW_INDENT_WIDTH */
#define CDV_PANEL_W 400.0F      /* Clay__debugViewWidth */
#define CDV_INFO_PANE_H 280.0F  /* Clay's info-pane is FIXED(300); our font is smaller so 280 fits */
#define CDV_TYPE_PILL_W 56.0F
#define CDV_TYPE_PILL_H 16.0F
// #endregion

// #region clay element-config-type pill colors (verbatim from clay.h:3130 switch)
/* Clay__DebugGetElementConfigTypeLabel returns these (alpha 90 in source). */
static uint32_t cdv_pill_color_for_bit(uint8_t bit) {
    /* 0xAABBGGRR. Alpha 0x80 (~128) -- Clay's debug view uses 90/255. */
    switch (bit) {
    case 0: /* Shared (243,134,48) */
        return 0x803086F3U;
    case 1: /* Text (105,210,231) */
        return 0x80E7D269U;
    case 2: /* Aspect (101,149,194) */
        return 0x80C29565U;
    case 3: /* Image (121,189,154) */
        return 0x809ABD79U;
    case 4: /* Floating (250,105,0) */
        return 0x800069FAU;
    case 5: /* Clip (242,196,90) */
        return 0x805AC4F2U;
    case 6: /* Border (108,91,123) */
        return 0x807B5B6CU;
    case 7: /* Custom (11,72,107) */
        return 0x806B480BU;
    default:
        return CDV_COLOR_3;
    }
}

static const char *cdv_pill_label_for_bit(uint8_t bit) {
    switch (bit) {
    case 0:
        return "Shr";
    case 1:
        return "Txt";
    case 2:
        return "Asp";
    case 3:
        return "Img";
    case 4:
        return "Flt";
    case 5:
        return "Scr";
    case 6:
        return "Bdr";
    case 7:
        return "Cus";
    default:
        return "?";
    }
}
// #endregion

// #region widget type tag (engine extension)
static const char *widget_tag_short(nt_ui_widget_type_t t) {
    switch (t) {
    case NT_UI_WIDGET_BUTTON:
        return "button";
    case NT_UI_WIDGET_IMAGE:
        return "image";
    case NT_UI_WIDGET_LABEL:
        return "label";
    case NT_UI_WIDGET_PANEL:
        return "panel";
    case NT_UI_WIDGET_GROUP:
        return "group";
    case NT_UI_WIDGET_NONE:
    default:
        return NULL;
    }
}
// #endregion

// #region emit helpers (axis-aligned world-space quads + text)
static const float s_identity_mat[16] = {
    1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
};

/* Emit a filled rect with GL Y-up coords. (x, y_top) = top-left in GL space;
 * the quad paints downward from y_top by `h`. */
static void cdv_emit_rect(nt_resource_t atlas, uint32_t region, float x, float y_top, float w, float h, uint32_t color) {
    const float verts[4][2] = {
        {x, y_top},
        {x + w, y_top},
        {x + w, y_top - h},
        {x, y_top - h},
    };
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    nt_sprite_renderer_emit_geometry(atlas, region, verts, 4U, indices, 6U, s_identity_mat, color);
}

static void cdv_draw_text(nt_material_t text_mat, nt_font_t font, float x, float baseline_y, float size, const float color[4], const char *s, size_t n) {
    if (size <= 0.0F || n == 0U || s == NULL) {
        return;
    }
    const float model[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, x, baseline_y, 0.0F, 1.0F,
    };
    nt_text_renderer_set_material(text_mat);
    nt_text_renderer_set_font(font);
    nt_text_renderer_draw_n(s, n, model, size, color, 0.0F, 0.0F);
}
// #endregion

// #region row + pill emit
static void unpack_color(uint32_t packed, float out[4]) {
    /* 0xAABBGGRR -> RGBA floats 0..1. */
    out[0] = (float)((packed >> 0U) & 0xFFU) / 255.0F;
    out[1] = (float)((packed >> 8U) & 0xFFU) / 255.0F;
    out[2] = (float)((packed >> 16U) & 0xFFU) / 255.0F;
    out[3] = (float)((packed >> 24U) & 0xFFU) / 255.0F;
}

/* Emit a config-type pill: small rounded-ish rect with the 3-char label
 * (we approximate Clay's CLAY_CORNER_RADIUS(4) with a plain rect -- our
 * white_region is a solid sample and corner rounding requires a fan). */
static float cdv_emit_pill(nt_resource_t atlas, uint32_t white_region, nt_material_t text_mat, nt_font_t font, float pill_x, float pill_y_top, uint32_t color, const char *label, float size) {
    cdv_emit_rect(atlas, white_region, pill_x, pill_y_top, CDV_TYPE_PILL_W, CDV_TYPE_PILL_H, color);
    if (label != NULL && size > 0.0F) {
        float tc[4];
        unpack_color(CDV_COLOR_4, tc);
        const size_t llen = strlen(label);
        cdv_draw_text(text_mat, font, pill_x + 4.0F, pill_y_top - CDV_TYPE_PILL_H + 3.0F, size, tc, label, llen);
    }
    return pill_x + CDV_TYPE_PILL_W + 4.0F;
}
// #endregion

// #region public draw
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_inspector_draw(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_font_t font, float label_size) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_draw: ctx must be non-NULL");
    NT_ASSERT(target != NULL && "nt_ui_inspector_draw: target must be non-NULL (same one passed to nt_ui_walk)");
    if (!ctx->inspector_active) {
        return;
    }
    if (ctx->atlas.id == 0U || ctx->sprite_material.id == 0U) {
        return;
    }
    if (!nt_resource_is_ready(ctx->atlas)) {
        return;
    }

    /* Panel is pinned to the RIGHT edge of the logical viewport (Clay's debug
     * view attaches LEFT_CENTER of panel to RIGHT_CENTER of root via floating
     * config -- the panel sits flush with the right edge of the UI). */
    const float vw = target->viewport[2];
    const float vh = target->viewport[3];
    const float panel_x = vw - CDV_PANEL_W;
    const float panel_y_top = vh;

    nt_sprite_renderer_set_material(ctx->sprite_material);

    /* Panel background (CLAY__DEBUGVIEW_COLOR_1 full body). */
    cdv_emit_rect(ctx->atlas, ctx->white_region, panel_x, panel_y_top, CDV_PANEL_W, vh, CDV_COLOR_1);

    /* Header bar: COLOR_2 strip with "Clay Debug Tools" text + a small "x"
     * pill on the right (visual only -- the close button doesn't fire in
     * a post-walk overlay because input is owned by the game, not Clay). */
    cdv_emit_rect(ctx->atlas, ctx->white_region, panel_x, panel_y_top, CDV_PANEL_W, CDV_ROW_H, CDV_COLOR_2);
    /* Separator line at the bottom of the header. */
    cdv_emit_rect(ctx->atlas, ctx->white_region, panel_x, panel_y_top - CDV_ROW_H, CDV_PANEL_W, 1.0F, CDV_COLOR_3);
    /* Vertical border at the left edge of the panel (matches Clay's floating
     * panel border-bottom: { color = COLOR_3 } visual rhythm). */
    cdv_emit_rect(ctx->atlas, ctx->white_region, panel_x, panel_y_top, 1.0F, vh, CDV_PANEL_BORDER);

    const bool can_text = (ctx->text_material.id != 0U) && (font.id != 0U) && (label_size > 0.0F);

    /* Header text (CLAY_STRING("Clay Debug Tools") in clay.h:3436). */
    if (can_text) {
        float c4[4];
        unpack_color(CDV_COLOR_4, c4);
        const char *title = "nt_ui_inspector (Clay-styled)";
        cdv_draw_text(ctx->text_material, font, panel_x + CDV_OUTER_PADDING, panel_y_top - CDV_ROW_H + 6.0F, label_size, c4, title, strlen(title));
    }

    /* Element list area (between header and the info pane). */
    const float list_y_top = panel_y_top - CDV_ROW_H - 1.0F;
    const float info_pane_y_top = CDV_INFO_PANE_H;
    const float list_y_bottom = info_pane_y_top;
    const float list_h = list_y_top - list_y_bottom;

    /* Collect DFS pre-order rows. Cap at what fits in the list area. */
    enum { MAX_ROWS_CAP = 192 };
    nt_ui_inspector_tree_row_t rows[MAX_ROWS_CAP];
    const int32_t total_rows = nt_ui_internal_collect_tree_rows(ctx, rows, (int32_t)MAX_ROWS_CAP);
    const int32_t visible_rows = (int32_t)(list_h / CDV_ROW_H);
    const int32_t shown = (total_rows < visible_rows) ? total_rows : visible_rows;

    /* Draw rows. Mirrors Clay__RenderDebugLayoutElementsList layout:
     *   - alternating row backgrounds (COLOR_1 / COLOR_2)
     *   - 16 px indent per depth level via CDV_INDENT_WIDTH
     *   - small dot or +/- collapse marker (we draw the dot variant)
     *   - element id string
     *   - one pill per attached config type (Shr/Txt/Asp/Img/Flt/Scr/Bdr/Cus)
     *   - tail: our widget-type tag (button/image/label/panel/group) */
    for (int32_t r = 0; r < shown; ++r) {
        const nt_ui_inspector_tree_row_t *row = &rows[r];
        const float row_y_top = list_y_top - ((float)r * CDV_ROW_H);

        /* Alternate row background. */
        const uint32_t row_bg = ((r & 1) == 0) ? CDV_COLOR_1 : CDV_COLOR_2;
        cdv_emit_rect(ctx->atlas, ctx->white_region, panel_x + 1.0F, row_y_top, CDV_PANEL_W - 2.0F, CDV_ROW_H, row_bg);

        const float indent_x = panel_x + CDV_OUTER_PADDING + ((float)row->depth * CDV_INDENT_WIDTH);

        /* Small dot for collapse marker -- COLOR_3, 8x8 centered in a 16x16 cell. */
        cdv_emit_rect(ctx->atlas, ctx->white_region, indent_x + 4.0F, row_y_top - 4.0F, 8.0F, 8.0F, CDV_COLOR_3);

        const float cursor_x = indent_x + CDV_INDENT_WIDTH + 4.0F;

        /* Element id string (or hex id fallback when empty -- e.g. anonymous
         * CLAY({...}) blocks). Text color dims to COLOR_3 if offscreen,
         * COLOR_4 otherwise, mirroring Clay's behavior at clay.h:3227.
         * Pills below are right-aligned from the panel edge, so we do NOT
         * track an advance cursor (no per-row text measurement -- inspector
         * is observability; the right-edge anchor keeps it cheap). */
        if (can_text) {
            float id_color[4];
            unpack_color(row->offscreen ? CDV_COLOR_3 : CDV_COLOR_4, id_color);
            if (row->id_string_len > 0U) {
                cdv_draw_text(ctx->text_material, font, cursor_x, row_y_top - CDV_ROW_H + 6.0F, label_size, id_color, row->id_string, (size_t)row->id_string_len);
            } else {
                char hexbuf[16];
                const int hn = snprintf(hexbuf, sizeof hexbuf, "#%08X", row->id);
                if (hn > 0) {
                    cdv_draw_text(ctx->text_material, font, cursor_x, row_y_top - CDV_ROW_H + 6.0F, label_size, id_color, hexbuf, (size_t)hn);
                }
            }
        }

        /* Config-type pills (right-aligned). Each pill is CDV_TYPE_PILL_W + 4 gap.
         * Count attached configs, lay them out right-to-left from the panel edge. */
        uint8_t pill_count = 0U;
        for (uint8_t b = 0U; b < 8U; ++b) {
            if (row->config_mask & (uint8_t)(1U << b)) {
                pill_count++;
            }
        }
        /* Reserve the rightmost area for our widget-type tag (if any). */
        const nt_ui_widget_type_t wtype = nt_ui_widget_lookup(ctx, row->id);
        const char *wtag = widget_tag_short(wtype);

        float pill_x_right = panel_x + CDV_PANEL_W - CDV_OUTER_PADDING;
        if (wtag != NULL) {
            const float wtag_w = CDV_TYPE_PILL_W;
            const float pill_y_top = row_y_top - 4.0F;
            cdv_emit_rect(ctx->atlas, ctx->white_region, pill_x_right - wtag_w, pill_y_top, wtag_w, CDV_TYPE_PILL_H, CDV_HIT_HINT);
            if (can_text) {
                float wc[4];
                unpack_color(CDV_COLOR_4, wc);
                cdv_draw_text(ctx->text_material, font, pill_x_right - wtag_w + 4.0F, pill_y_top - CDV_TYPE_PILL_H + 3.0F, label_size, wc, wtag, strlen(wtag));
            }
            pill_x_right -= wtag_w + 4.0F;
        }
        for (uint8_t b = 0U; b < 8U && pill_count > 0U; ++b) {
            if (!(row->config_mask & (uint8_t)(1U << b))) {
                continue;
            }
            const float pill_y_top = row_y_top - 4.0F;
            pill_x_right -= CDV_TYPE_PILL_W;
            (void)cdv_emit_pill(ctx->atlas, ctx->white_region, ctx->text_material, font, pill_x_right, pill_y_top, cdv_pill_color_for_bit(b), cdv_pill_label_for_bit(b), label_size);
            pill_x_right -= 4.0F;
            pill_count--;
        }

        /* TEXT element body line ("\"...\""), mirroring clay.h:3265. */
        if (row->is_text && row->text_chars != NULL && row->text_len > 0U && can_text) {
            const float body_y_top = row_y_top - CDV_ROW_H;
            const uint32_t body_bg = ((r & 1) == 1) ? CDV_COLOR_1 : CDV_COLOR_2;
            cdv_emit_rect(ctx->atlas, ctx->white_region, panel_x + 1.0F, body_y_top, CDV_PANEL_W - 2.0F, CDV_ROW_H, body_bg);
            char buf[80];
            const uint32_t shown_len = (row->text_len > 40U) ? 40U : row->text_len;
            const int bn = snprintf(buf, sizeof buf, "\"%.*s%s\"", (int)shown_len, row->text_chars, (row->text_len > 40U) ? "..." : "");
            if (bn > 0) {
                float bc[4];
                unpack_color(CDV_COLOR_4, bc);
                cdv_draw_text(ctx->text_material, font, indent_x + CDV_INDENT_WIDTH + 16.0F, body_y_top - CDV_ROW_H + 6.0F, label_size, bc, buf, (size_t)bn);
            }
        }
    }

    /* === Element-info pane (mirrors clay.h:3477 onward).
     * Pinned at the BOTTOM of the panel, COLOR_2 background, FIXED(300) tall
     * (Clay's value). We show info for either the first widget-tagged row,
     * or row[0] if no widget is registered, so the user always sees something. */
    cdv_emit_rect(ctx->atlas, ctx->white_region, panel_x, info_pane_y_top, CDV_PANEL_W, CDV_INFO_PANE_H, CDV_COLOR_2);
    /* Top separator (CLAY({ .sizing = {.width = GROW(0), .height = FIXED(1)} }, .backgroundColor = COLOR_3 } at clay.h:3475). */
    cdv_emit_rect(ctx->atlas, ctx->white_region, panel_x, info_pane_y_top, CDV_PANEL_W, 1.0F, CDV_COLOR_3);

    uint32_t selected_id = 0U;
    for (int32_t r = 0; r < shown; ++r) {
        if (nt_ui_widget_lookup(ctx, rows[r].id) != NT_UI_WIDGET_NONE) {
            selected_id = rows[r].id;
            break;
        }
    }
    if (selected_id == 0U && total_rows > 0) {
        selected_id = rows[0].id;
    }
    nt_ui_inspector_element_info_t info = nt_ui_internal_get_element_info(ctx, selected_id);

    if (can_text) {
        float c3[4];
        float c4[4];
        unpack_color(CDV_COLOR_3, c3);
        unpack_color(CDV_COLOR_4, c4);
        float ipy = info_pane_y_top - CDV_OUTER_PADDING - label_size;
        const float ipx = panel_x + CDV_OUTER_PADDING;
        char line[160];
        int n;

        /* Header: "Layout Config" + id_string (clay.h:3485-3494). */
        n = snprintf(line, sizeof line, "Layout Config");
        if (n > 0) {
            cdv_draw_text(ctx->text_material, font, ipx, ipy, label_size, c4, line, (size_t)n);
        }
        if (info.found && info.id_string_len > 0U) {
            cdv_draw_text(ctx->text_material, font, ipx + 140.0F, ipy, label_size, c3, info.id_string, (size_t)info.id_string_len);
        }
        ipy -= CDV_ROW_H;

        if (!info.found) {
            n = snprintf(line, sizeof line, "(no element selected)");
            if (n > 0) {
                cdv_draw_text(ctx->text_material, font, ipx, ipy, label_size, c3, line, (size_t)n);
            }
        } else {
            /* Bounding Box (clay.h:3500-3511). */
            n = snprintf(line, sizeof line, "Bounding Box");
            if (n > 0) {
                cdv_draw_text(ctx->text_material, font, ipx, ipy, label_size, c3, line, (size_t)n);
            }
            ipy -= CDV_ROW_H;
            n = snprintf(line, sizeof line, "{ x: %d, y: %d, w: %d, h: %d }", (int)info.bbox_x, (int)info.bbox_y, (int)info.bbox_w, (int)info.bbox_h);
            if (n > 0) {
                cdv_draw_text(ctx->text_material, font, ipx + CDV_INDENT_WIDTH, ipy, label_size, c4, line, (size_t)n);
            }
            ipy -= CDV_ROW_H;

            /* Layout Direction (clay.h:3513-3515). */
            n = snprintf(line, sizeof line, "Layout Direction: %s", (info.layout_direction == 0U) ? "LEFT_TO_RIGHT" : "TOP_TO_BOTTOM");
            if (n > 0) {
                cdv_draw_text(ctx->text_material, font, ipx, ipy, label_size, c4, line, (size_t)n);
            }
            ipy -= CDV_ROW_H;

            /* Padding (clay.h:3527-3538). */
            n = snprintf(line, sizeof line, "Padding { l:%u r:%u t:%u b:%u }  childGap: %u", info.padding_l, info.padding_r, info.padding_t, info.padding_b, info.child_gap);
            if (n > 0) {
                cdv_draw_text(ctx->text_material, font, ipx, ipy, label_size, c4, line, (size_t)n);
            }
            ipy -= CDV_ROW_H;

            /* Child Alignment (clay.h:3543-3562). */
            const char *ax_s = "RIGHT";
            if (info.child_align_x == 0U) {
                ax_s = "LEFT";
            } else if (info.child_align_x == 1U) {
                ax_s = "CENTER";
            }
            const char *ay_s = "BOTTOM";
            if (info.child_align_y == 0U) {
                ay_s = "TOP";
            } else if (info.child_align_y == 1U) {
                ay_s = "CENTER";
            }
            n = snprintf(line, sizeof line, "Child Alignment { x: %s, y: %s }", ax_s, ay_s);
            if (n > 0) {
                cdv_draw_text(ctx->text_material, font, ipx, ipy, label_size, c4, line, (size_t)n);
            }
            ipy -= CDV_ROW_H;

            /* SHARED config: Background color + corner radius (clay.h:3568-3577). */
            if (info.config_mask & (1U << 0U)) {
                n = snprintf(line, sizeof line, "Bg: { r:%d g:%d b:%d a:%d }", (int)info.bg_r, (int)info.bg_g, (int)info.bg_b, (int)info.bg_a);
                if (n > 0) {
                    cdv_draw_text(ctx->text_material, font, ipx, ipy, label_size, c4, line, (size_t)n);
                }
                /* Color swatch (CSS3 padding 0 -- just a small box next to the line). */
                float swatch[4];
                swatch[0] = info.bg_r / 255.0F;
                swatch[1] = info.bg_g / 255.0F;
                swatch[2] = info.bg_b / 255.0F;
                swatch[3] = info.bg_a / 255.0F;
                const uint32_t swatch_packed =
                    ((uint32_t)(swatch[3] * 255.0F) << 24U) | ((uint32_t)(swatch[2] * 255.0F) << 16U) | ((uint32_t)(swatch[1] * 255.0F) << 8U) | (uint32_t)(swatch[0] * 255.0F);
                cdv_emit_rect(ctx->atlas, ctx->white_region, ipx + 220.0F, ipy + label_size, 14.0F, 14.0F, swatch_packed);
                ipy -= CDV_ROW_H;
            }
            /* TEXT config: font + color + alignment (clay.h:3580-3617). */
            if (info.config_mask & (1U << 1U)) {
                const char *ta = "RIGHT";
                if (info.text_align == 0U) {
                    ta = "LEFT";
                } else if (info.text_align == 1U) {
                    ta = "CENTER";
                }
                n = snprintf(line, sizeof line, "Text { size:%u fontId:%u align:%s }", info.text_font_size, info.text_font_id, ta);
                if (n > 0) {
                    cdv_draw_text(ctx->text_material, font, ipx, ipy, label_size, c4, line, (size_t)n);
                }
                ipy -= CDV_ROW_H;
                n = snprintf(line, sizeof line, "Text Color { r:%d g:%d b:%d a:%d }", (int)info.text_color_r, (int)info.text_color_g, (int)info.text_color_b, (int)info.text_color_a);
                if (n > 0) {
                    cdv_draw_text(ctx->text_material, font, ipx, ipy, label_size, c4, line, (size_t)n);
                }
                ipy -= CDV_ROW_H;
            }

            /* Widget tag row (engine extension). */
            nt_ui_widget_type_t selwt = nt_ui_widget_lookup(ctx, selected_id);
            const char *wtag = widget_tag_short(selwt);
            n = snprintf(line, sizeof line, "Widget: %s", (wtag != NULL) ? wtag : "(plain Clay)");
            if (n > 0) {
                cdv_draw_text(ctx->text_material, font, ipx, ipy, label_size, c4, line, (size_t)n);
            }
        }
    }

    /* Footer counters strip across the bottom: total elements / total
     * widget tags / hit-zones currently recorded. Always visible -- the
     * user reads these to verify the registry is being populated. */
    if (can_text) {
        float c3[4];
        unpack_color(CDV_COLOR_3, c3);
        char footer[96];
        uint32_t widget_count = 0U;
        for (uint32_t i = 0U; i < (uint32_t)NT_UI_WIDGET_REGISTRY_CAP; ++i) {
            if (ctx->widget_registry[i].id != 0U) {
                widget_count++;
            }
        }
        const int fn = snprintf(footer, sizeof footer, "elems=%d  widgets=%u  hit-zones=%u", (int)total_rows, widget_count, ctx->debug_zone_count);
        if (fn > 0) {
            cdv_draw_text(ctx->text_material, font, panel_x + CDV_OUTER_PADDING, 4.0F, label_size, c3, footer, (size_t)fn);
        }
    }

    /* Flush so the panel paints in this pass. Same convention as
     * nt_stats_draw and nt_ui_debug_draw_hit_zones. */
    nt_sprite_renderer_flush();
    if (can_text) {
        nt_text_renderer_flush();
    }

    /* === Re-coupling: F3 alone now drives the hit-zone overlay. The
     * inspector composes hit-zone visualization on top of its own panel
     * when ctx->debug_recording is on. nt_ui_debug_draw_hit_zones gates
     * internally on (mode != OFF) and on the bindings being ready, so
     * passing NT_UI_DEBUG_HIT_HOVER here means "show hovered zones while
     * inspector is active". The game's F2 cycles a separate s_dbg_mode
     * which it forwards by calling nt_ui_debug_draw_hit_zones itself (no
     * change there); the inspector adds the default-HOVER coverage when
     * recording is on. */
    if (ctx->debug_recording) {
        nt_ui_debug_draw_hit_zones(ctx, target, NT_UI_DEBUG_HIT_HOVER, font, label_size);
    }
}
// #endregion
