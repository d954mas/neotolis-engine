#include "ui/nt_ui_inspector.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "core/nt_builtins.h" /* sinf/cosf -> __builtin_* (Win CRT dllimport fix) */
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_debug.h"
#include "ui/nt_ui_internal.h"

/* Phase 56 ext (CHUNK E): nt_ui_inspector.
 *
 * Port path chosen: HYBRID. We do NOT copy Clay's ~500-line
 * Clay__RenderDebugView -- that would double-port Clay state and drift on
 * Clay upgrades. Instead we walk Clay's internal layoutElements + hashmap
 * arrays directly (1 read-only loop) and render our own sidebar via the
 * sprite + text renderers, mirroring nt_ui_debug_draw_hit_zones's binding
 * contract. Sidebar layout, widget-type column, and hit-zone column are
 * 100% ours. Clay-internal dependency surface is one cursor iteration. */

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

// #region sidebar layout constants
#define SIDEBAR_W 360.0F            /* px on the right edge */
#define SIDEBAR_PAD 8.0F            /* inner padding */
#define ROW_H 16.0F                 /* one row of text */
#define HEADER_H 26.0F              /* top header bar */
#define BG_COLOR 0xE01A1A22U        /* 0xAABBGGRR, dark slate, semi-opaque */
#define HEADER_COLOR 0xFF2A2A38U    /* dark blue-gray */
#define ROW_ALT_COLOR 0x40FFFFFFU   /* alternate-row tint */
#define HIT_BADGE_COLOR 0x80FFCC33U /* yellow-ish, "has hit-zone" tag */
#define TEXT_COLOR_TITLE {1.0F, 0.95F, 0.6F, 1.0F}
#define TEXT_COLOR_BODY {1.0F, 1.0F, 1.0F, 1.0F}
#define TEXT_COLOR_TAG {0.7F, 0.9F, 1.0F, 1.0F}
// #endregion

// #region widget type -> short tag
static const char *widget_tag_str(nt_ui_widget_type_t t) {
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
        return "(plain)";
    }
}
// #endregion

// #region helpers: emit screen rect (axis-aligned, world space)
static void emit_rect(nt_resource_t atlas, uint32_t region, float x, float y_top, float w, float h, uint32_t color) {
    /* y_top is the GL-Y-up TOP edge; sprite renderer's quad uses height
     * downward from origin. We emit a quad anchored at (x, y_top - h)
     * with size (w, h), which paints from y_top-h up to y_top. */
    const float identity_mat[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    const float verts[4][2] = {
        {x, y_top},
        {x + w, y_top},
        {x + w, y_top - h},
        {x, y_top - h},
    };
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    nt_sprite_renderer_emit_geometry(atlas, region, verts, 4U, indices, 6U, identity_mat, color);
}
// #endregion

// #region helper: does this id have a recorded debug zone? (for hit column)
static const nt_ui_debug_zone_t *find_zone_for_id(const nt_ui_context_t *ctx, uint32_t id) {
    for (uint32_t i = 0; i < ctx->debug_zone_count; ++i) {
        if (ctx->debug_zones[i].id == id) {
            return &ctx->debug_zones[i];
        }
    }
    return NULL;
}
// #endregion

// #region helper: draw one text row at (x, baseline_y)
static void draw_text_row(nt_material_t text_mat, nt_font_t font, float x, float baseline_y, float size, const float color[4], const char *s, size_t n) {
    if (size <= 0.0F || n == 0U) {
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

// #region public draw
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_inspector_draw(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_font_t font, float label_size) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_draw: ctx must be non-NULL");
    NT_ASSERT(target != NULL && "nt_ui_inspector_draw: target must be non-NULL (same one passed to nt_ui_walk)");
    if (!ctx->inspector_active) {
        return;
    }
    /* Same binding gate as nt_ui_debug_draw_hit_zones. */
    if (ctx->atlas.id == 0U || ctx->sprite_material.id == 0U) {
        return;
    }
    if (!nt_resource_is_ready(ctx->atlas)) {
        return;
    }

    /* === Step 1: hit-zone overlay (REUSE existing helper, Y-flip lives there).
     * Inspector implies overlay -- the F3 wiring auto-flips debug_recording. */
    nt_ui_debug_draw_hit_zones(ctx, target, NT_UI_DEBUG_HIT_HOVER, font, label_size);

    /* === Step 2: sidebar background pinned at the right edge in world space.
     * target->viewport[2/3] are the LOGICAL width/height; we draw in world
     * coords identical to dispatch_command's Y-flip (vy + vh - clay_y), so
     * the sidebar lives at x in [vw - SIDEBAR_W, vw] and y in [0, vh]. */
    const float vw = target->viewport[2];
    const float vh = target->viewport[3];
    const float side_x = vw - SIDEBAR_W;
    const float side_y_top = vh; /* GL-Y-up top */

    nt_sprite_renderer_set_material(ctx->sprite_material);

    /* Sidebar background */
    emit_rect(ctx->atlas, ctx->white_region, side_x, side_y_top, SIDEBAR_W, vh, BG_COLOR);
    /* Header strip */
    emit_rect(ctx->atlas, ctx->white_region, side_x, side_y_top, SIDEBAR_W, HEADER_H, HEADER_COLOR);

    /* === Step 3: walk Clay's layout elements via the internal accessor
     * (Clay_Context internals live in nt_ui.c -- inspector stays Clay-agnostic). */
    const int32_t total = nt_ui_internal_get_layout_element_count(ctx);

    const bool can_text = (ctx->text_material.id != 0U) && (font.id != 0U) && (label_size > 0.0F);

    /* Header text */
    if (can_text) {
        const float title_color[4] = TEXT_COLOR_TITLE;
        char header[64];
        const int n = snprintf(header, sizeof header, "nt_ui_inspector  elems=%d zones=%u", total, ctx->debug_zone_count);
        if (n > 0) {
            draw_text_row(ctx->text_material, font, side_x + SIDEBAR_PAD, side_y_top - HEADER_H + 6.0F, label_size, title_color, header, (size_t)n);
        }
    }

    const int32_t max_rows = (int32_t)((vh - HEADER_H - SIDEBAR_PAD) / ROW_H);
    const int32_t shown = (total < max_rows) ? total : max_rows;

    for (int32_t i = 0; i < shown; ++i) {
        const nt_ui_inspector_element_view_t view = nt_ui_internal_get_layout_element_view(ctx, i);
        const uint32_t id = view.id;
        const nt_ui_widget_type_t tag = nt_ui_widget_lookup(ctx, id);
        const nt_ui_debug_zone_t *zone = find_zone_for_id(ctx, id);

        /* Alt-row banding via translucent overlay. */
        const float row_y_top = side_y_top - HEADER_H - ((float)i * ROW_H);
        if ((i & 1) == 0) {
            emit_rect(ctx->atlas, ctx->white_region, side_x, row_y_top, SIDEBAR_W, ROW_H, ROW_ALT_COLOR);
        }
        /* "has hit-zone" badge: small yellow strip on the far right. */
        if (zone != NULL) {
            emit_rect(ctx->atlas, ctx->white_region, side_x + SIDEBAR_W - 6.0F, row_y_top, 4.0F, ROW_H, HIT_BADGE_COLOR);
        }

        if (!can_text) {
            continue;
        }
        char buf[96];
        int n;
        if (zone != NULL) {
            /* width/height from PADDED layout bbox so the user sees the hit area. */
            const float hw = zone->layout_r - zone->layout_l;
            const float hh = zone->layout_b - zone->layout_t;
            n = snprintf(buf, sizeof buf, "%s id=%08X hit=%.0fx%.0f", widget_tag_str(tag), id, (double)hw, (double)hh);
        } else {
            n = snprintf(buf, sizeof buf, "%s id=%08X", widget_tag_str(tag), id);
        }
        if (n <= 0) {
            continue;
        }
        const float body_color[4] = TEXT_COLOR_BODY;
        draw_text_row(ctx->text_material, font, side_x + SIDEBAR_PAD, row_y_top - ROW_H + 3.0F, label_size, body_color, buf, (size_t)n);
    }

    /* Flush so the sidebar paints in the current pass, not the next walker
     * flush. Same convention as nt_ui_debug_draw_hit_zones. */
    nt_sprite_renderer_flush();
    if (can_text) {
        nt_text_renderer_flush();
    }
}
// #endregion
