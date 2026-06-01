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
#include "core/nt_builtins.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_internal.h"

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

/* CPU clip: width of the inspector sidebar panel (mirrors CDV_PANEL_WIDTH in
 * nt_ui.c). The post-walk overlay clips its rect width against
 * `panel_left_x = viewport.w - CDV_PANEL_WIDTH` so the highlight + label do
 * NOT visually overlap the sidebar -- the panel is rendered by nt_ui_walk
 * earlier in the pass, and this overlay is the only thing drawn after the
 * walk, so without clipping the highlight peeks over the panel (user-visible
 * regression for any widget near the screen's right edge). 400 is the
 * file-static CDV_PANEL_WIDTH in engine/ui/nt_ui.c; if either changes the
 * other must follow (a single shared constant would be cleaner long-term but
 * needs the internal header surface). */
#define NTINSP_OVERLAY_PANEL_W 400.0F

/* Clamp a rect's right edge against `panel_left_x`. Returns the clipped width
 * (or 0 when the entire rect is inside the panel). Top/bottom/y stay as-is. */
static float overlay_clip_w_to_panel(float x, float w, float panel_left_x) {
    if (x >= panel_left_x) {
        return 0.0F;
    }
    if ((x + w) > panel_left_x) {
        return panel_left_x - x;
    }
    return w;
}

/* Emit a filled rect with GL Y-up coords. (x, y_top) = top-left in GL space;
 * the quad paints downward from y_top by `h`. Clips against the inspector
 * sidebar so the overlay stays UNDER the panel visually. */
static void overlay_emit_rect(nt_resource_t atlas, uint32_t region, float x, float y_top, float w, float h, float panel_left_x, uint32_t color) {
    const float clip_w = overlay_clip_w_to_panel(x, w, panel_left_x);
    if (clip_w <= 0.0F || h <= 0.0F) {
        return;
    }
    const float verts[4][2] = {
        {x, y_top},
        {x + clip_w, y_top},
        {x + clip_w, y_top - h},
        {x, y_top - h},
    };
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    nt_sprite_renderer_emit_geometry(atlas, region, verts, 4U, indices, 6U, s_identity_mat, color);
}

/* Thin 4-edge outline of an axis-aligned bbox in GL Y-up. Clips against the
 * inspector sidebar; the right edge silently vanishes when the bbox extends
 * past the panel left -- visually correct since the panel covers the rest. */
static void overlay_emit_outline(nt_resource_t atlas, uint32_t region, float x, float y_top, float w, float h, float t, float panel_left_x, uint32_t color) {
    overlay_emit_rect(atlas, region, x, y_top, w, t, panel_left_x, color);         /* top edge */
    overlay_emit_rect(atlas, region, x, y_top - h + t, w, t, panel_left_x, color); /* bottom edge */
    overlay_emit_rect(atlas, region, x, y_top, t, h, panel_left_x, color);         /* left edge */
    /* Right edge: only emit when the visual bbox right is left of the panel. */
    if ((x + w - t) < panel_left_x) {
        overlay_emit_rect(atlas, region, x + w - t, y_top, t, h, panel_left_x, color);
    }
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
    /* Clay Y-down -> GL Y-up: world_y(top) = vy+vh - clay_y(top). */
    const float gl_x = info.bbox_x;
    const float gl_y_top = vy + vh - info.bbox_y;
    const float w = info.bbox_w;
    const float h = info.bbox_h;

    /* CPU clip boundary for "stay under the inspector panel". Panel is a
     * right-attached float of width CDV_PANEL_WIDTH (400) at the layout-space
     * origin, so its left edge sits at viewport.x + viewport.w - panel_w in
     * the SAME logical space the overlay draws into. When the panel is NOT
     * visible (inspector inactive should have early-returned above, but
     * defensive), the clip degenerates to the viewport right edge. */
    const float panel_left_x = vx + vw - NTINSP_OVERLAY_PANEL_W;

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
        overlay_emit_rect(ctx->atlas, ctx->white_region, pad_x, pad_y_top, pad_w, pad_h, panel_left_x, 0x6033FFFFU);
        /* Thin yellow outline tracing the padded edge so the touch-target is
         * unambiguous. */
        overlay_emit_outline(ctx->atlas, ctx->white_region, pad_x, pad_y_top, pad_w, pad_h, 1.0F, panel_left_x, 0xFF00FFFFU);
    }
    /* Filled translucent highlight (matches Clay__debugViewHighlightColor = {168,66,28,100}). */
    overlay_emit_rect(ctx->atlas, ctx->white_region, gl_x, gl_y_top, w, h, panel_left_x, 0x641C42A8U);
    /* Bright opaque outline for clarity. */
    overlay_emit_outline(ctx->atlas, ctx->white_region, gl_x, gl_y_top, w, h, 2.0F, panel_left_x, 0xFFFFFFFFU);

    /* Id label anchored at the top-left corner of the highlight rectangle.
     * Skipped when the visual bbox starts inside the panel footprint -- the
     * label would never be readable (panel covers it). */
    if (ctx->text_material.id != 0U && font.id != 0U && label_size > 0.0F && gl_x < panel_left_x) {
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
}
// #endregion
