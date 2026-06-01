/* Phase 56 ext rework: nt_ui_inspector public API + post-walk overlay.
 *
 * The verbatim Clay-debug-view emit_layout body lives in nt_ui.c (it must
 * touch Clay private types: Clay_Context fields, Clay__GetHashMapItem,
 * Clay__ElementIsOffscreen, Clay__HashString, the layoutElementTreeRoots /
 * layoutElements / reusableElementIndexBuffer arrays, debugSelectedElementId,
 * etc. Those symbols are file-static or compile-time-private to the
 * CLAY_IMPLEMENTATION TU). This TU exposes the three public functions and
 * owns the post-walk single-element hit-zone overlay (which can stay
 * Clay-agnostic). The internal entry point nt_ui_internal_inspector_overlay
 * lives in nt_ui_internal.h and is implemented in nt_ui.c for the engine to
 * keep one TU per private surface.
 *
 * Vendored Clay is zlib licensed (deps/clay/LICENSE -- (c) 2024 Nic Barker).
 * The emit_layout body in nt_ui.c is a renamed-and-adapted verbatim copy of
 * Clay__RenderDebugView / Clay__RenderDebugLayoutElementsList /
 * Clay__RenderDebugViewElementConfigHeader / Clay__RenderDebugViewColor /
 * Clay__RenderDebugViewCornerRadius (deps/clay/clay.h:3341-3800). The
 * CLAY({...}) emits are reproduced byte-for-byte where Clay's public API
 * permits; private accessors are adapted in place with comments. */

#include "ui/nt_ui_inspector.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_internal.h"

// #region metrics
/* Phase 56 ext (REVIEW-2 P3-1): default metrics preserve the previous hardcoded
 * shape verbatim -- panel_width 400 (NT_UI_INSPECTOR_PANEL_WIDTH), row_height
 * 30 (CDV_ROW_HEIGHT), font_size 16 (literal in CLAY_TEXT_CONFIG sites),
 * outer_padding 10 (CDV_OUTER_PADDING), indent_width 16 (CDV_INDENT_WIDTH).
 * A game with NO call to nt_ui_inspector_set_metrics sees the identical
 * inspector that shipped pre-fix. */
const nt_ui_inspector_metrics_t NT_UI_INSPECTOR_METRICS_DEFAULT = {
    .panel_width = 400.0F,
    .row_height = 30.0F,
    .font_size = 16U,
    .outer_padding = 10U,
    .indent_width = 16U,
};

/* 5 sequential NT_ASSERTs trip the cognitive-complexity threshold; each is a
 * straight precondition check on a distinct field. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_inspector_set_metrics(nt_ui_context_t *ctx, const nt_ui_inspector_metrics_t *metrics) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_set_metrics: ctx must be non-NULL");
    NT_ASSERT(metrics != NULL && "nt_ui_inspector_set_metrics: metrics must be non-NULL");
    NT_ASSERT(metrics->panel_width > 0.0F && "nt_ui_inspector_set_metrics: panel_width must be > 0");
    NT_ASSERT(metrics->row_height > 0.0F && "nt_ui_inspector_set_metrics: row_height must be > 0");
    NT_ASSERT(metrics->font_size > 0U && "nt_ui_inspector_set_metrics: font_size must be > 0");
    ctx->inspector_metrics = *metrics;
}
// #endregion

// #region toggle + getters
void nt_ui_inspector_set_active(nt_ui_context_t *ctx, bool on) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_set_active: ctx must be non-NULL");
    ctx->inspector_active = on;
    if (!on) {
        /* Clear focus + selection + collapsed-set on disable so a re-enable
         * starts clean (the user expects the next open to show the full tree). */
        ctx->inspector_highlight_id = 0U;
        ctx->inspector_selected_id = 0U;
        ctx->inspector_collapsed_count = 0U;
    }
}

bool nt_ui_inspector_is_active(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_is_active: ctx must be non-NULL");
    return ctx->inspector_active;
}

bool nt_ui_inspector_pointer_consumed(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_pointer_consumed: ctx must be non-NULL");
    /* inspector_pointer_consumed is only meaningful while the inspector is active;
     * the and-with-active is defensive against stale state when the toggle flips. */
    return ctx->inspector_active && ctx->inspector_pointer_consumed;
}
// #endregion

// #region emit_layout (forwarder -- body in nt_ui.c)
/* The real implementation needs Clay private types (Clay_Context fields,
 * Clay__GetHashMapItem, etc.) which only the CLAY_IMPLEMENTATION TU can see.
 * Expose a thin internal accessor and forward to it -- the public symbol
 * stays in this header, the body stays where Clay's private surface lives. */
void nt_ui_inspector_emit_layout(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_emit_layout: ctx must be non-NULL");
    nt_ui_internal_emit_inspector_layout_extern(ctx);
}
// #endregion

// #region overlay_draw helpers (axis-aligned world-space quads + text)
static const float s_identity_mat[16] = {
    1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
};

/* GPU scissor clip (Phase 56 ext REVIEW-2 P2-1): the post-walk overlay clips
 * its highlight + label against the inspector sidebar via GPU scissor (set up
 * in overlay_draw before the emit, cleared after). The walker's per-element
 * Clay-driven scissor stack has already been torn down by walk exit, so the
 * overlay simply enables scissor over the GAME area [0, 0, panel_left_x,
 * screen_h] and disables it afterwards -- restoring the disabled state the
 * walker left.
 *
 * Phase 56 ext (REVIEW-2 P3-1): panel width comes from
 * ctx->inspector_metrics.panel_width (set via nt_ui_inspector_set_metrics,
 * default 400). The same metric drives the Clay debug-view emit in nt_ui.c
 * AND the input-consume gate in nt_ui_begin -- one source of truth so the
 * three sites can never drift.
 *
 * Replaces the previous CPU-clip implementation: every overlay_emit_rect call
 * used to clamp its width against panel_left_x in two paths (axis-aligned
 * fallback) but the transformed path bypassed it entirely -- rotated/scaled
 * highlights bled OVER the sidebar. GPU scissor handles both paths uniformly. */

/* Emit a filled rect with GL Y-up coords. (x, y_top) = top-left in GL space;
 * the quad paints downward from y_top by `h`. Sidebar clipping is now handled
 * by GPU scissor configured in overlay_draw -- this emit is unclipped. */
static void overlay_emit_rect(nt_resource_t atlas, uint32_t region, float x, float y_top, float w, float h, uint32_t color) {
    if (w <= 0.0F || h <= 0.0F) {
        return;
    }
    const float verts[4][2] = {
        {x, y_top},
        {x + w, y_top},
        {x + w, y_top - h},
        {x, y_top - h},
    };
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    nt_sprite_renderer_emit_geometry(atlas, region, verts, 4U, indices, 6U, s_identity_mat, color);
}

/* Thin 4-edge outline of an axis-aligned bbox in GL Y-up. Sidebar clipping is
 * handled by GPU scissor configured in overlay_draw -- this emit is unclipped. */
static void overlay_emit_outline(nt_resource_t atlas, uint32_t region, float x, float y_top, float w, float h, float t, uint32_t color) {
    overlay_emit_rect(atlas, region, x, y_top, w, t, color);         /* top edge */
    overlay_emit_rect(atlas, region, x, y_top - h + t, w, t, color); /* bottom edge */
    overlay_emit_rect(atlas, region, x, y_top, t, h, color);         /* left edge */
    overlay_emit_rect(atlas, region, x + w - t, y_top, t, h, color); /* right edge */
}

static void overlay_draw_text(nt_material_t text_mat, nt_font_t font, float x, float baseline_y, float size, const float color[4], const char *s, size_t n) {
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

// #region public overlay_draw
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_inspector_overlay_draw(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_font_t font, float label_size) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_overlay_draw: ctx must be non-NULL");
    NT_ASSERT(target != NULL && "nt_ui_inspector_overlay_draw: target must be non-NULL (same one passed to nt_ui_walk)");
    if (!ctx->inspector_active || ctx->inspector_highlight_id == 0U) {
        return;
    }
    if (ctx->atlas.id == 0U || ctx->sprite_material.id == 0U) {
        return;
    }
    if (!nt_resource_is_ready(ctx->atlas)) {
        return;
    }

    /* Look up the layout bbox via the engine-internal accessor. The bbox is
     * in Clay Y-down space; the walker's GL Y-flip per-corner gives the
     * matching GL-Y-up screen position. */
    nt_ui_inspector_element_info_t info = nt_ui_internal_get_element_info(ctx, ctx->inspector_highlight_id);
    if (!info.found) {
        return;
    }

    const float vx = target->viewport[0];
    const float vy = target->viewport[1];
    const float vw = target->viewport[2];
    const float vh = target->viewport[3];

    /* Phase 56 ext fix (REVIEW-2 P2-1): push GPU scissor over the GAME area
     * (left of the inspector sidebar) so BOTH overlay paths (transformed and
     * axis-aligned) clip the highlight at the panel's left edge. Pre-fix, the
     * axis-aligned path did this via per-rect CPU width clamps and the
     * transformed path had NO clipping -- rotated/scaled highlight bled OVER
     * the sidebar.
     *
     * Panel is a right-attached float of width ctx->inspector_metrics.panel_width
     * at layout-space origin, so its left edge sits at viewport.x + viewport.w -
     * panel_w in the same logical space the overlay draws into. Scissor rect
     * [0, 0, panel_left_x, screen_h] (logical, top-left convention -- the
     * shared logical-to-physical helper handles Y-flip + DIRECT/SCALED).
     *
     * The walker tore down its scissor stack on exit (engine/ui/nt_ui.c
     * scissor_pop:depth==0 -> set_scissor_enabled(false)), so the prior state
     * is "disabled" -- we restore to "disabled" at every exit path below. */
    const float panel_left_x = vx + vw - ctx->inspector_metrics.panel_width;
    const int scissor_x = (int)vx;
    const int scissor_y = (int)vy;
    const int scissor_w = (int)(panel_left_x - vx);
    const int scissor_h = (int)vh;
    if (scissor_w > 0 && scissor_h > 0) {
        nt_ui_internal_apply_scissor_logical_to_physical(target, scissor_x, scissor_y, scissor_w, scissor_h);
        nt_gfx_set_scissor_enabled(true);
    }

    /* Phase 56 ext fix (inspector overlay transform-aware): if the highlighted
     * id was queried via nt_ui_get_interaction_padded this frame, a debug zone
     * carries the declaration-time accum-transform snapshot. Use it to project
     * the visual + padded corners into world space so the overlay matches the
     * EXACT rendered position. Without this, push_transform-wrapped widgets
     * (e.g. ui_buttons_demo BAKED button) show the overlay at the layout
     * bbox while the widget renders at the transformed position. Falls back
     * to the axis-aligned bbox path when no zone is recorded (plain Clay
     * containers, non-interactive elements). */
    const nt_ui_debug_zone_t *z = nt_ui_internal_find_debug_zone(ctx, ctx->inspector_highlight_id);
    if (z != NULL && z->accum_depth > 0U) {
        nt_sprite_renderer_set_material(ctx->sprite_material);

        /* The recorded zone carries the visual bbox AND the padded layout bbox
         * (from the registered hit_padding_lrtb). Project them through the
         * same per-level accum math + Y-flip the debug overlay uses. */
        int16_t pad[4] = {0, 0, 0, 0};
        const bool has_pad = nt_ui_widget_get_hit_padding(ctx, ctx->inspector_highlight_id, pad) && (pad[0] > 0 || pad[1] > 0 || pad[2] > 0 || pad[3] > 0);

        if (has_pad) {
            float pad_corners[4][2] = {
                {z->layout_l, z->layout_t},
                {z->layout_r, z->layout_t},
                {z->layout_r, z->layout_b},
                {z->layout_l, z->layout_b},
            };
            for (uint32_t k = 0; k < 4U; ++k) {
                nt_ui_internal_project_layout_to_world(z, vy, vh, pad_corners[k][0], pad_corners[k][1], &pad_corners[k][0], &pad_corners[k][1]);
            }
            nt_ui_internal_emit_filled_quad(ctx->atlas, ctx->white_region, pad_corners, 0x6033FFFFU);
            nt_ui_internal_emit_outline(ctx->atlas, ctx->white_region, pad_corners, 1.0F, 0xFF00FFFFU);
        }

        /* Visual bbox highlight: filled translucent fill + opaque outline. */
        float vis_corners[4][2] = {
            {z->visual_l, z->visual_t},
            {z->visual_r, z->visual_t},
            {z->visual_r, z->visual_b},
            {z->visual_l, z->visual_b},
        };
        for (uint32_t k = 0; k < 4U; ++k) {
            nt_ui_internal_project_layout_to_world(z, vy, vh, vis_corners[k][0], vis_corners[k][1], &vis_corners[k][0], &vis_corners[k][1]);
        }
        nt_ui_internal_emit_filled_quad(ctx->atlas, ctx->white_region, vis_corners, 0x641C42A8U);
        nt_ui_internal_emit_outline(ctx->atlas, ctx->white_region, vis_corners, 2.0F, 0xFFFFFFFFU);

        /* Label at the GL-Y-up TOP corner of the visual quad (max y after
         * Y-flip = original top edge). Mirrors nt_ui_debug_draw_hit_zones's
         * label placement so the two overlays look consistent. */
        if (ctx->text_material.id != 0U && font.id != 0U && label_size > 0.0F) {
            float top_x = vis_corners[0][0];
            float top_y = vis_corners[0][1];
            for (uint32_t k = 1; k < 4U; ++k) {
                if (vis_corners[k][1] > top_y) {
                    top_y = vis_corners[k][1];
                    top_x = vis_corners[k][0];
                }
            }
            char buf[80];
            int n;
            if (info.id_string_len > 0U && info.id_string != NULL) {
                const int slen = (info.id_string_len > 48U) ? 48 : (int)info.id_string_len;
                n = snprintf(buf, sizeof buf, "id=%.*s", slen, info.id_string);
            } else {
                n = snprintf(buf, sizeof buf, "id=#%08X", ctx->inspector_highlight_id);
            }
            if (n > 0) {
                const float color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
                overlay_draw_text(ctx->text_material, font, top_x + 4.0F, top_y - label_size - 2.0F, label_size, color, buf, (size_t)n);
            }
        }

        nt_sprite_renderer_flush();
        if (ctx->text_material.id != 0U && font.id != 0U && label_size > 0.0F) {
            nt_text_renderer_flush();
        }
        /* Restore disabled scissor state (walker exit invariant). Flush BEFORE
         * the disable so the panel-clipped staging actually carries the scissor. */
        if (scissor_w > 0 && scissor_h > 0) {
            nt_gfx_set_scissor_enabled(false);
        }
        return;
    }

    /* Fallback: axis-aligned bbox in layout space. Used for plain Clay
     * elements with no recorded zone, or zones with no accum transform
     * (depth==0 -> identity, same screen position as the bbox). GPU scissor
     * (pushed above) handles the sidebar clip uniformly with the transformed
     * path -- no more per-rect CPU clamps.
     * Clay Y-down -> GL Y-up: world_y(top) = vy+vh - clay_y(top). */
    const float gl_x = info.bbox_x;
    const float gl_y_top = vy + vh - info.bbox_y;
    const float w = info.bbox_w;
    const float h = info.bbox_h;

    nt_sprite_renderer_set_material(ctx->sprite_material);

    /* Phase 56 ext fix: when the highlighted widget has a registered hit-zone
     * padding (button via nt_ui_widget_register with non-NULL pad_lrtb), draw
     * the padded hit area as a translucent fill UNDERNEATH the visual bbox
     * so the user sees both at once. When no padding is recorded, only the
     * visual highlight is drawn (same as before). The padded fill is drawn
     * first so the visual outline on top stays crisp. */
    int16_t pad[4] = {0, 0, 0, 0};
    if (nt_ui_widget_get_hit_padding(ctx, ctx->inspector_highlight_id, pad) && (pad[0] > 0 || pad[1] > 0 || pad[2] > 0 || pad[3] > 0)) {
        /* Padded extents in Clay layout space: visual bbox + pad on each side.
         * Layout space matches the recorded padding direction (left/right/top/bottom). */
        const float pl = (float)pad[0];
        const float pr = (float)pad[1];
        const float pt = (float)pad[2];
        const float pb = (float)pad[3];
        const float pad_x = gl_x - pl;
        const float pad_y_top = gl_y_top + pt; /* GL Y-up: top edge moves UP by pt */
        const float pad_w = w + pl + pr;
        const float pad_h = h + pt + pb;
        /* Translucent cyan fill for the padded hit area (matches debug overlay
         * idle color so the two systems stay visually consistent). */
        overlay_emit_rect(ctx->atlas, ctx->white_region, pad_x, pad_y_top, pad_w, pad_h, 0x6033FFFFU);
        /* Thin yellow outline tracing the padded edge so the touch-target is
         * unambiguous. */
        overlay_emit_outline(ctx->atlas, ctx->white_region, pad_x, pad_y_top, pad_w, pad_h, 1.0F, 0xFF00FFFFU);
    }
    /* Filled translucent highlight (matches Clay__debugViewHighlightColor = {168,66,28,100}). */
    overlay_emit_rect(ctx->atlas, ctx->white_region, gl_x, gl_y_top, w, h, 0x641C42A8U);
    /* Bright opaque outline for clarity. */
    overlay_emit_outline(ctx->atlas, ctx->white_region, gl_x, gl_y_top, w, h, 2.0F, 0xFFFFFFFFU);

    /* Id label anchored at the top-left corner of the highlight rectangle.
     * GPU scissor (set up above) crops the label glyphs against the panel's
     * left edge, so the label is always at most partially visible -- the
     * pre-fix early-skip-when-gl_x>=panel_left_x is no longer needed. */
    if (ctx->text_material.id != 0U && font.id != 0U && label_size > 0.0F) {
        char buf[80];
        int n;
        if (info.id_string_len > 0U && info.id_string != NULL) {
            const int slen = (info.id_string_len > 48U) ? 48 : (int)info.id_string_len;
            n = snprintf(buf, sizeof buf, "id=%.*s", slen, info.id_string);
        } else {
            n = snprintf(buf, sizeof buf, "id=#%08X", ctx->inspector_highlight_id);
        }
        if (n > 0) {
            const float color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
            /* GL Y-up: gl_y_top is the top edge; baseline sits just inside. */
            overlay_draw_text(ctx->text_material, font, gl_x + 4.0F, gl_y_top - label_size - 2.0F, label_size, color, buf, (size_t)n);
        }
    }

    nt_sprite_renderer_flush();
    if (ctx->text_material.id != 0U && font.id != 0U && label_size > 0.0F) {
        nt_text_renderer_flush();
    }
    /* Restore disabled scissor state (walker exit invariant). */
    if (scissor_w > 0 && scissor_h > 0) {
        nt_gfx_set_scissor_enabled(false);
    }
}
// #endregion
