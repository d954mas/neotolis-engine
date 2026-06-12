/* Public API + post-walk overlay; emit_layout body lives in nt_ui_clay_impl.c. */

#include "ui/nt_ui_inspector.h"

#if NT_UI_DEBUG_TOOLS

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "math/nt_math.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_state.h"

// #region metrics
/* Defaults match Clay's debug-view literals. */
const nt_ui_inspector_metrics_t NT_UI_INSPECTOR_METRICS_DEFAULT = {
    .panel_width = 400.0F,
    .row_height = 30.0F,
    .font_size = 16U,
    .outer_padding = 10U,
    .indent_width = 16U,
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_inspector_set_metrics(nt_ui_context_t *ctx, const nt_ui_inspector_metrics_t *metrics) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_set_metrics: ctx must be non-NULL");
    NT_ASSERT(metrics != NULL && "nt_ui_inspector_set_metrics: metrics must be non-NULL");
    NT_ASSERT(metrics->panel_width > 0.0F && "nt_ui_inspector_set_metrics: panel_width must be > 0");
    NT_ASSERT(metrics->row_height > 0.0F && "nt_ui_inspector_set_metrics: row_height must be > 0");
    NT_ASSERT(metrics->font_size > 0U && "nt_ui_inspector_set_metrics: font_size must be > 0");
    ctx->inspector_metrics = *metrics;
}

void nt_ui_inspector_set_materials(nt_ui_context_t *ctx, nt_material_t sprite, nt_material_t text) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_set_materials: ctx must be non-NULL");
    NT_ASSERT(!ctx->in_frame && "nt_ui_inspector_set_materials: must be called outside begin/end");
    /* 0 handles fall back to the game's sprite/text material at walk time. */
    ctx->inspector_sprite_material = sprite;
    ctx->inspector_text_material = text;
}
// #endregion

// #region toggle + getters
void nt_ui_inspector_set_active(nt_ui_context_t *ctx, bool on) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_set_active: ctx must be non-NULL");
    ctx->inspector_active = on;
    if (!on) {
        /* Start clean on re-enable. */
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
    /* Combined check guards against stale state when toggle flips. */
    return ctx->inspector_active && ctx->inspector_pointer_consumed;
}
// #endregion

// #region emit_layout
void nt_ui_inspector_emit_layout(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_inspector_emit_layout: ctx must be non-NULL");
    nt_ui_internal_emit_inspector_layout_extern(ctx);
}

/* "UI memory" sidebar line: state-pool occupancy + anim collisions.
 * Formats into caller buf; the Clay row is emitted from the sidebar layout. */
const char *nt_ui_internal_format_mem_line(const nt_ui_context_t *ctx, char *buf, size_t buf_size) {
    NT_ASSERT(ctx != NULL && buf != NULL && buf_size > 0U);
    (void)snprintf(buf, buf_size, "UI mem: %u/%u slots  %u B  anim_coll:%u", nt_ui_state_used_slots(ctx), (uint32_t)NT_UI_STATE_SLOTS, nt_ui_state_used_bytes(ctx),
                   nt_ui_get_anim_collision_count(ctx));
    return buf;
}
// #endregion

// #region overlay_draw helpers
/* GL Y-up; (x, y_top) is the top-left. */
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
    nt_sprite_renderer_emit_geometry(atlas, region, verts, 4U, indices, 6U, NT_MATH_MAT4_IDENTITY, color);
}

static void overlay_emit_outline(nt_resource_t atlas, uint32_t region, float x, float y_top, float w, float h, float t, uint32_t color) {
    overlay_emit_rect(atlas, region, x, y_top, w, t, color);         /* top */
    overlay_emit_rect(atlas, region, x, y_top - h + t, w, t, color); /* bottom */
    overlay_emit_rect(atlas, region, x, y_top, t, h, color);         /* left */
    overlay_emit_rect(atlas, region, x + w - t, y_top, t, h, color); /* right */
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

    nt_ui_inspector_element_info_t info = nt_ui_internal_get_element_info(ctx, ctx->inspector_highlight_id);
    if (!info.found) {
        return;
    }

    const float vx = target->viewport[0];
    const float vy = target->viewport[1];
    const float vw = target->viewport[2];
    const float vh = target->viewport[3];

    /* GPU scissor over the game area so both overlay paths clip uniformly.
     * apply_scissor_logical_to_physical adds vx/vy itself — pass 0,0 relative. */
    const float panel_left_x = vx + vw - ctx->inspector_metrics.panel_width;
    const int scissor_x = 0;
    const int scissor_y = 0;
    const int scissor_w = (int)(panel_left_x - vx);
    const int scissor_h = (int)vh;
    if (scissor_w > 0 && scissor_h > 0) {
        nt_ui_internal_apply_scissor_logical_to_physical(target, scissor_x, scissor_y, scissor_w, scissor_h);
        nt_gfx_set_scissor_enabled(true);
    }

    /* Inspector text material when set, else the game's (mirrors draw_hit_zones). Gate can_label on
     * the resolved one so a depth-off-only inspector material still draws the label. */
    const nt_material_t tmat = (ctx->inspector_text_material.id != 0U) ? ctx->inspector_text_material : ctx->text_material;
    const bool can_label = tmat.id != 0U && font.id != 0U && label_size > 0.0F;
    const float white[4] = {1.0F, 1.0F, 1.0F, 1.0F};

    if (ctx->use_raycast_input) {
        /* 3D ctx: every walked element (not only interactive widgets) has its world mat4 snapshotted
         * in hit_baked, so fetch it by id and emit the element's Clay bbox under it. Caller binds the
         * perspective view_proj; the depth-off inspector material keeps the highlight on top. */
        const int32_t slot = nt_ui_clay_priv_hashmap_slot_for_id(ctx->clay, ctx->inspector_highlight_id);
        const bool slot_ok = (slot >= 0) && (slot < (int32_t)ctx->max_elements) && (ctx->hit_generation[slot] == ctx->current_generation);
        const float *m = slot_ok ? ctx->hit_baked[slot].m : NULL;
        /* Identity affine + zero translation = no 3D placement (e.g. the GROW root): its full-canvas
         * bbox would map to a screen-filling quad, so only highlight elements that carry a transform. */
        const bool placed = (m != NULL) && (m[0] != 1.0F || m[4] != 0.0F || m[1] != 0.0F || m[5] != 1.0F || m[12] != 0.0F || m[13] != 0.0F || m[14] != 0.0F);
        if (placed) {
            const nt_material_t smat = (ctx->inspector_sprite_material.id != 0U) ? ctx->inspector_sprite_material : ctx->sprite_material;
            nt_sprite_renderer_set_material(smat);

            const float vl = info.bbox_x;
            const float vt = info.bbox_y;
            const float vr = info.bbox_x + info.bbox_w;
            const float vb = info.bbox_y + info.bbox_h;

            int16_t pad[4] = {0, 0, 0, 0};
            if (nt_ui_widget_get_hit_padding(ctx, ctx->inspector_highlight_id, pad) && (pad[0] > 0 || pad[1] > 0 || pad[2] > 0 || pad[3] > 0)) {
                const float pad_corners[4][2] = {
                    {vl - (float)pad[0], vt - (float)pad[2]},
                    {vr + (float)pad[1], vt - (float)pad[2]},
                    {vr + (float)pad[1], vb + (float)pad[3]},
                    {vl - (float)pad[0], vb + (float)pad[3]},
                };
                nt_ui_internal_emit_filled_quad_m(ctx->atlas, ctx->white_region, pad_corners, m, 0x6033FFFFU);
                nt_ui_internal_emit_outline_m(ctx->atlas, ctx->white_region, pad_corners, 2.0F, m, 0xFF00FFFFU);
            }
            const float vis_corners[4][2] = {{vl, vt}, {vr, vt}, {vr, vb}, {vl, vb}};
            nt_ui_internal_emit_filled_quad_m(ctx->atlas, ctx->white_region, vis_corners, m, 0x641C42A8U);
            nt_ui_internal_emit_outline_m(ctx->atlas, ctx->white_region, vis_corners, 2.0F, m, 0xFFFFFFFFU);
            nt_sprite_renderer_flush();

            if (can_label) {
                char buf[80];
                int n;
                if (info.id_string_len > 0U && info.id_string != NULL) {
                    const int slen = (info.id_string_len > 48U) ? 48 : (int)info.id_string_len;
                    n = snprintf(buf, sizeof buf, "id=%.*s", slen, info.id_string);
                } else {
                    n = snprintf(buf, sizeof buf, "id=#%08X", ctx->inspector_highlight_id);
                }
                if (n > 0) {
                    /* Label in Clay px at the top-left, mapped by m onto the element; col1 negated so
                     * glyphs read upright (same col1-flip as emit_text; no text_scale to divide out
                     * here — label_size is already in renderer units). */
                    const float ox = vl + 2.0F;
                    const float oy = vt + label_size + 2.0F;
                    float tm[16];
                    for (int rr = 0; rr < 4; ++rr) {
                        tm[rr] = m[rr];
                        tm[4 + rr] = -m[4 + rr];
                        tm[8 + rr] = m[8 + rr];
                        tm[12 + rr] = (ox * m[rr]) + (oy * m[4 + rr]) + m[12 + rr];
                    }
                    nt_text_renderer_set_material(tmat);
                    nt_text_renderer_set_font(font);
                    nt_text_renderer_draw_n(buf, (size_t)n, tm, label_size, white, 0.0F, 0.0F);
                    nt_text_renderer_flush();
                }
            }
        }
        if (scissor_w > 0 && scissor_h > 0) {
            nt_gfx_set_scissor_enabled(false);
        }
        return;
    }

    /* 2D ctx: project the composed affine subset to screen and emit under identity. */
    const nt_ui_debug_zone_t *z = nt_ui_internal_find_debug_zone(ctx, ctx->inspector_highlight_id);
    const bool z_has_xform = (z != NULL) && (z->m[0] != 1.0F || z->m[4] != 0.0F || z->m[1] != 0.0F || z->m[5] != 1.0F || z->m[12] != 0.0F || z->m[13] != 0.0F);
    if (z_has_xform) {
        nt_sprite_renderer_set_material(ctx->sprite_material);
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

        /* Label at corner with max y (GL-Y-up TOP of projected quad). */
        if (can_label) {
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
                overlay_draw_text(tmat, font, top_x + 4.0F, top_y - label_size - 2.0F, label_size, white, buf, (size_t)n);
            }
        }
        nt_sprite_renderer_flush();
        nt_text_renderer_flush();
        if (scissor_w > 0 && scissor_h > 0) {
            nt_gfx_set_scissor_enabled(false);
        }
        return;
    }

    /* Fallback path: Clay Y-down → GL Y-up. */
    const float gl_x = info.bbox_x;
    const float gl_y_top = vy + vh - info.bbox_y;
    const float w = info.bbox_w;
    const float h = info.bbox_h;

    nt_sprite_renderer_set_material(ctx->sprite_material);

    /* Padded fill drawn UNDER the visual bbox so both are visible. */
    int16_t pad[4] = {0, 0, 0, 0};
    if (nt_ui_widget_get_hit_padding(ctx, ctx->inspector_highlight_id, pad) && (pad[0] > 0 || pad[1] > 0 || pad[2] > 0 || pad[3] > 0)) {
        const float pl = (float)pad[0];
        const float pr = (float)pad[1];
        const float pt = (float)pad[2];
        const float pb = (float)pad[3];
        const float pad_x = gl_x - pl;
        /* GL Y-up: top edge moves UP by pt. */
        const float pad_y_top = gl_y_top + pt;
        const float pad_w = w + pl + pr;
        const float pad_h = h + pt + pb;
        overlay_emit_rect(ctx->atlas, ctx->white_region, pad_x, pad_y_top, pad_w, pad_h, 0x6033FFFFU);
        overlay_emit_outline(ctx->atlas, ctx->white_region, pad_x, pad_y_top, pad_w, pad_h, 1.0F, 0xFF00FFFFU);
    }
    /* Matches Clay__debugViewHighlightColor. */
    overlay_emit_rect(ctx->atlas, ctx->white_region, gl_x, gl_y_top, w, h, 0x641C42A8U);
    overlay_emit_outline(ctx->atlas, ctx->white_region, gl_x, gl_y_top, w, h, 2.0F, 0xFFFFFFFFU);

    if (can_label) {
        char buf[80];
        int n;
        if (info.id_string_len > 0U && info.id_string != NULL) {
            const int slen = (info.id_string_len > 48U) ? 48 : (int)info.id_string_len;
            n = snprintf(buf, sizeof buf, "id=%.*s", slen, info.id_string);
        } else {
            n = snprintf(buf, sizeof buf, "id=#%08X", ctx->inspector_highlight_id);
        }
        if (n > 0) {
            overlay_draw_text(tmat, font, gl_x + 4.0F, gl_y_top - label_size - 2.0F, label_size, white, buf, (size_t)n);
        }
    }

    nt_sprite_renderer_flush();
    if (can_label) {
        nt_text_renderer_flush();
    }
    if (scissor_w > 0 && scissor_h > 0) {
        nt_gfx_set_scissor_enabled(false);
    }
}
// #endregion

#endif /* NT_UI_DEBUG_TOOLS */
