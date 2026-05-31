#include "ui/nt_ui_debug.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/nt_assert.h"
#include "core/nt_builtins.h" /* sinf/cosf/sqrtf -> __builtin_* (CRT dllimport fix) */
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_internal.h"

/* Phase 56 ext: hit-zone debug overlay. Recording is per-frame ctx state
 * (filled inside nt_ui_get_interaction_padded when ctx->debug_recording is
 * true); drawing is decoupled and called after nt_ui_walk by the game. */

// #region toggle + getters
void nt_ui_debug_set_recording(nt_ui_context_t *ctx, bool on) {
    NT_ASSERT(ctx != NULL && "nt_ui_debug_set_recording: ctx must be non-NULL");
    ctx->debug_recording = on;
}

bool nt_ui_debug_get_recording(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_debug_get_recording: ctx must be non-NULL");
    return ctx->debug_recording;
}

uint32_t nt_ui_debug_get_zone_count(const nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_debug_get_zone_count: ctx must be non-NULL");
    return ctx->debug_zone_count;
}
// #endregion

// #region color helpers
/* 0xAABBGGRR packed -- matches nt_sprite_renderer convention. */
#define DEBUG_COLOR_IDLE 0x40C0C0C0U     /* light gray, 25% alpha */
#define DEBUG_COLOR_HOVER 0x6033CCFFU    /* yellow-ish, 38% alpha */
#define DEBUG_COLOR_PRESSED 0x603333FFU  /* red-ish, 38% alpha */
#define DEBUG_COLOR_CAPTURED 0x6033FFFFU /* cyan-ish, 38% alpha */
#define DEBUG_COLOR_DISABLED 0x40FF6633U /* blue tint, 25% alpha */
#define DEBUG_OUTLINE_COLOR 0xFFFFFFFFU  /* opaque white outline for visual bbox */

static uint32_t color_for_state(uint16_t flags) {
    if (flags & NT_UI_DEBUG_FLAG_DISABLED) {
        return DEBUG_COLOR_DISABLED;
    }
    if (flags & NT_UI_DEBUG_FLAG_PRESSED) {
        return DEBUG_COLOR_PRESSED;
    }
    if (flags & NT_UI_DEBUG_FLAG_CAPTURED) {
        return DEBUG_COLOR_CAPTURED;
    }
    if (flags & NT_UI_DEBUG_FLAG_HOVERED) {
        return DEBUG_COLOR_HOVER;
    }
    return DEBUG_COLOR_IDLE;
}
// #endregion

// #region forward-transform a 2D point through one accum level (Clay Y-down)
/* Mirrors compose_transform_level's per-level math from nt_ui.c
 * (NON-negated rotation, NO Y-flip -- those Clay-Y-down conventions are what
 * was RECORDED at query time). The walker's GL Y-flip is applied ONCE per
 * emitted point in project_to_world below, AFTER all accum levels have been
 * walked through. Order matters: doing the flip per-level would re-flip the
 * rotation pivot. */
static void apply_level(float *x, float *y, const nt_ui_transform_t *t, float cx, float cy) {
    const float sx = t->scale_x;
    const float sy = t->scale_y;
    const float cr = cosf(t->rotation);
    const float sr = sinf(t->rotation);
    const float dx = *x - cx;
    const float dy = *y - cy;
    *x = cx + (cr * sx * dx) - (sr * sy * dy) + t->offset_x;
    *y = cy + (sr * sx * dx) + (cr * sy * dy) + t->offset_y;
}

/* Project a Clay-space (layout-bbox) point through:
 *   1) the recorded accum stack (Clay Y-down, NON-negated rotation), then
 *   2) the walker's GL Y-flip: world_y = vy + vh - clay_y.
 * Step (2) makes the overlay land at the EXACT same screen coordinates as
 * the widgets dispatch_command renders (engine/ui/nt_ui.c). Pitfall 2 fix. */
static void project_to_world(const nt_ui_debug_zone_t *z, float vy, float vh, float x, float y, float *out_x, float *out_y) {
    float wx = x;
    float wy = y;
    for (uint32_t k = 0; k < z->accum_depth; ++k) {
        apply_level(&wx, &wy, &z->accum[k], z->center_x, z->center_y);
    }
    *out_x = wx;
    *out_y = vy + vh - wy; /* Clay Y-down -> GL Y-up */
}
// #endregion

// #region polygon emit helpers
static const float s_identity_mat[16] = {
    1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
};

/* Filled quad (4 verts, 2 tris). Vertices already in world space. */
static void emit_filled_quad(nt_resource_t atlas, uint32_t region, const float v[4][2], uint32_t color) {
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    nt_sprite_renderer_emit_geometry(atlas, region, v, 4U, indices, 6U, s_identity_mat, color);
}

/* Thin rotated-rectangle outline as 4 quads, one per edge. For each edge
 * (c[i] -> c[(i+1)%4]) build a thin quad inset toward the polygon centroid
 * by `thickness` pixels along the unit inward perpendicular. */
static void emit_outline(nt_resource_t atlas, uint32_t region, const float c[4][2], float thickness, uint32_t color) {
    /* Polygon centroid for direction-flip test. */
    float cx = 0.0F;
    float cy = 0.0F;
    for (uint32_t i = 0; i < 4U; ++i) {
        cx += c[i][0];
        cy += c[i][1];
    }
    cx *= 0.25F;
    cy *= 0.25F;

    for (uint32_t i = 0; i < 4U; ++i) {
        const uint32_t j = (i + 1U) & 3U;
        const float ax = c[i][0];
        const float ay = c[i][1];
        const float bx = c[j][0];
        const float by = c[j][1];
        /* Edge perpendicular (CCW rotation of edge vector). */
        float nx = -(by - ay);
        float ny = bx - ax;
        const float len = sqrtf((nx * nx) + (ny * ny));
        if (len < 1e-5F) {
            continue;
        }
        nx /= len;
        ny /= len;
        /* Flip to point inward (toward centroid). */
        const float mx = (ax + bx) * 0.5F;
        const float my = (ay + by) * 0.5F;
        if (((cx - mx) * nx) + ((cy - my) * ny) < 0.0F) {
            nx = -nx;
            ny = -ny;
        }
        const float t = thickness;
        const float quad[4][2] = {
            {ax, ay},
            {bx, by},
            {bx + (nx * t), by + (ny * t)},
            {ax + (nx * t), ay + (ny * t)},
        };
        emit_filled_quad(atlas, region, quad, color);
    }
}
// #endregion

// #region mode filter
static bool zone_passes_mode(const nt_ui_debug_zone_t *z, nt_ui_debug_hit_mode_t mode) {
    switch (mode) {
    case NT_UI_DEBUG_HIT_OFF:
        return false;
    case NT_UI_DEBUG_HIT_HOVER:
        return (z->state_flags & NT_UI_DEBUG_FLAG_HOVERED) != 0U;
    case NT_UI_DEBUG_HIT_CAPTURED:
        return (z->state_flags & NT_UI_DEBUG_FLAG_CAPTURED) != 0U;
    case NT_UI_DEBUG_HIT_ALL:
        return true;
    default:
        return false;
    }
}
// #endregion

// #region label rendering
/* Compose a short status label for one zone and draw it at the GL-Y-up
 * top-left corner of the padded zone (post-transform + Y-flip). The corner
 * is the visible TOP of the box in GL coords (largest y of the 4 corners).
 * Text grows UPWARD from baseline; baseline sits just below the top edge so
 * the glyph body lands inside the box. font_size <= 0 skips. DISABLED zones
 * get a "disabl" tag (state filter under mode=ALL). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void draw_zone_label(const nt_ui_debug_zone_t *z, const float corner[2], nt_material_t text_mat, nt_font_t font, float size) {
    if (size <= 0.0F) {
        return;
    }
    char buf[64];
    const char *state_str = "idle";
    if (z->state_flags & NT_UI_DEBUG_FLAG_DISABLED) {
        state_str = "disabl";
    } else if (z->state_flags & NT_UI_DEBUG_FLAG_PRESSED) {
        state_str = "press";
    } else if (z->state_flags & NT_UI_DEBUG_FLAG_CAPTURED) {
        state_str = "capt";
    } else if (z->state_flags & NT_UI_DEBUG_FLAG_HOVERED) {
        state_str = "hover";
    }
    const int n = snprintf(buf, sizeof buf, "id=%08X %s", z->id, state_str);
    if (n <= 0) {
        return;
    }
    /* GL Y-up: corner[1] is the TOP of the box; subtract size+2 so the
     * baseline sits inside the box and text grows back up toward the top edge. */
    const float baseline_y = corner[1] - size - 2.0F;
    const float model[16] = {
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, corner[0] + 2.0F, baseline_y, 0.0F, 1.0F,
    };
    const float color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    nt_text_renderer_set_material(text_mat);
    nt_text_renderer_set_font(font);
    nt_text_renderer_draw_n(buf, (size_t)n, model, size, color, 0.0F, 0.0F);
}
// #endregion

// #region public draw
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_debug_draw_hit_zones(nt_ui_context_t *ctx, const nt_ui_target_t *target, nt_ui_debug_hit_mode_t mode, nt_font_t font, float label_size) {
    NT_ASSERT(ctx != NULL && "nt_ui_debug_draw_hit_zones: ctx must be non-NULL");
    NT_ASSERT(target != NULL && "nt_ui_debug_draw_hit_zones: target must be non-NULL (same one passed to nt_ui_walk)");
    if (mode == NT_UI_DEBUG_HIT_OFF || ctx->debug_zone_count == 0U) {
        return;
    }
    /* Need atlas + sprite material to draw quads. text material+font drives
     * the label; missing label deps degrade to rects only. */
    if (ctx->atlas.id == 0U || ctx->sprite_material.id == 0U) {
        return; /* silent skip: required bindings missing */
    }
    if (!nt_resource_is_ready(ctx->atlas)) {
        return;
    }
    nt_sprite_renderer_set_material(ctx->sprite_material);

    const bool can_label = (ctx->text_material.id != 0U) && (font.id != 0U) && (label_size > 0.0F);
    /* Walker's logical viewport y/h drives the Y-flip (engine/ui/nt_ui.c
     * dispatch_command: world_y = vy + vh - sbb.y - sbb.height). The overlay
     * emits POINTS (corner-by-corner) so the per-point flip is
     * world_y = vy + vh - clay_y. */
    const float vy = target->viewport[1];
    const float vh = target->viewport[3];

    for (uint32_t i = 0; i < ctx->debug_zone_count; ++i) {
        const nt_ui_debug_zone_t *z = &ctx->debug_zones[i];
        if (!zone_passes_mode(z, mode)) {
            continue;
        }
        /* Project four corners of the PADDED layout bbox through the snapshot
         * accum stack + walker's Y-flip so the overlay matches the rendered widget. */
        float pad_corners[4][2] = {
            {z->layout_l, z->layout_t},
            {z->layout_r, z->layout_t},
            {z->layout_r, z->layout_b},
            {z->layout_l, z->layout_b},
        };
        for (uint32_t k = 0; k < 4U; ++k) {
            project_to_world(z, vy, vh, pad_corners[k][0], pad_corners[k][1], &pad_corners[k][0], &pad_corners[k][1]);
        }
        /* Filled fill for the padded zone. */
        const uint32_t fill = color_for_state(z->state_flags);
        emit_filled_quad(ctx->atlas, ctx->white_region, pad_corners, fill);

        /* Outline the EXACT visual bbox so padding is visually distinct. */
        float vis_corners[4][2] = {
            {z->visual_l, z->visual_t},
            {z->visual_r, z->visual_t},
            {z->visual_r, z->visual_b},
            {z->visual_l, z->visual_b},
        };
        for (uint32_t k = 0; k < 4U; ++k) {
            project_to_world(z, vy, vh, vis_corners[k][0], vis_corners[k][1], &vis_corners[k][0], &vis_corners[k][1]);
        }
        emit_outline(ctx->atlas, ctx->white_region, vis_corners, 2.0F, DEBUG_OUTLINE_COLOR);

        /* Label anchored at the GL-Y-up top-left corner of the padded zone.
         * After Y-flip the SMALLEST layout-y becomes the LARGEST GL-y, so
         * pick the corner with max y for the label baseline.
         * Text grows UPWARD from baseline (font convention), so baseline = top_y - 2
         * places the label just below the top edge of the box, growing up into it. */
        if (can_label) {
            float top_x = pad_corners[0][0];
            float top_y = pad_corners[0][1];
            for (uint32_t k = 1; k < 4U; ++k) {
                if (pad_corners[k][1] > top_y) {
                    top_y = pad_corners[k][1];
                    top_x = pad_corners[k][0];
                }
            }
            const float label_corner[2] = {top_x, top_y};
            draw_zone_label(z, label_corner, ctx->text_material, font, label_size);
        }
    }
    /* Flush so the overlay lands in the current pass, not the next walk's. */
    nt_sprite_renderer_flush();
    if (can_label) {
        nt_text_renderer_flush();
    }
}
// #endregion
