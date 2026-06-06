#include "ui/nt_ui_debug_hit_zones.h"

#if NT_UI_DEBUG_TOOLS

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/nt_assert.h"
#include "core/nt_builtins.h"
#include "math/nt_math.h"
#include "renderers/nt_sprite_renderer.h"
#include "renderers/nt_text_renderer.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_internal.h"

/* Recording filled inside step_interaction_padded; drawing called by game after nt_ui_walk. */

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
/* 0xAABBGGRR packed — matches nt_sprite_renderer convention. */
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

// #region zone helpers
/* Composed affine + walker Y-flip into world space. Extracts the 2D affine subset (top-left 2×2 +
 * col3) from the recorded mat4 — accurate for 2D ctx + planar 3D widgets; rotation_x/y/offset_z
 * widgets get a 2D-projection approximation for the highlight outline. */
void nt_ui_internal_project_layout_to_world(const nt_ui_debug_zone_t *z, float vy, float vh, float x, float y, float *out_x, float *out_y) {
    const float wx = (z->m[0] * x) + (z->m[4] * y) + z->m[12];
    const float wy = (z->m[1] * x) + (z->m[5] * y) + z->m[13];
    *out_x = wx;
    *out_y = vy + vh - wy;
}

/* Linear scan beats a hash at the 64-zone cap. */
const nt_ui_debug_zone_t *nt_ui_internal_find_debug_zone(const nt_ui_context_t *ctx, uint32_t id) {
    NT_ASSERT(ctx != NULL && "find_debug_zone: ctx must be non-NULL");
    if (id == 0U) {
        return NULL;
    }
    for (uint32_t i = 0; i < ctx->debug_zone_count; ++i) {
        if (ctx->debug_zones[i].id == id) {
            return &ctx->debug_zones[i];
        }
    }
    return NULL;
}
// #endregion

// #region polygon emit helpers
/* `model` maps the quad corners into render space (identity = corners already there). */
void nt_ui_internal_emit_filled_quad_m(nt_resource_t atlas, uint32_t region, const float v[4][2], const float model[16], uint32_t color) {
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    nt_sprite_renderer_emit_geometry(atlas, region, v, 4U, indices, 6U, model, color);
}

/* 4 inset quads; inset direction = unit perpendicular toward centroid. Inset runs in the corners'
 * own space, so `model` (set for 3D ctx) scales the outline thickness with the element. */
void nt_ui_internal_emit_outline_m(nt_resource_t atlas, uint32_t region, const float c[4][2], float thickness, const float model[16], uint32_t color) {
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
        /* CCW perpendicular. */
        float nx = -(by - ay);
        float ny = bx - ax;
        const float len = sqrtf((nx * nx) + (ny * ny));
        if (len < 1e-5F) {
            continue;
        }
        nx /= len;
        ny /= len;
        /* Flip inward. */
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
        nt_ui_internal_emit_filled_quad_m(atlas, region, quad, model, color);
    }
}

/* Vertices already in world space (2D-ctx pre-projected path). */
void nt_ui_internal_emit_filled_quad(nt_resource_t atlas, uint32_t region, const float v[4][2], uint32_t color) { nt_ui_internal_emit_filled_quad_m(atlas, region, v, NT_MATH_MAT4_IDENTITY, color); }

void nt_ui_internal_emit_outline(nt_resource_t atlas, uint32_t region, const float c[4][2], float thickness, uint32_t color) {
    nt_ui_internal_emit_outline_m(atlas, region, c, thickness, NT_MATH_MAT4_IDENTITY, color);
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
/* `text_model` already places the baseline; caller picks screen (2D) or z->m-mapped (3D) space. */
static void draw_zone_label(const nt_ui_debug_zone_t *z, const float text_model[16], nt_material_t text_mat, nt_font_t font, float size) {
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
    const float color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    nt_text_renderer_set_material(text_mat);
    nt_text_renderer_set_font(font);
    nt_text_renderer_draw_n(buf, (size_t)n, text_model, size, color, 0.0F, 0.0F);
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
    /* Missing text mat / font degrades to rects only. */
    if (ctx->atlas.id == 0U || ctx->sprite_material.id == 0U) {
        return;
    }
    if (!nt_resource_is_ready(ctx->atlas)) {
        return;
    }
    /* 3D ctx: emit Clay-layout corners under each zone's world mat4 (caller binds the perspective
     * view_proj) and prefer the depth-off inspector materials so the overlay stays on top. */
    const bool is_3d = ctx->use_raycast_input;
    const nt_material_t smat = (is_3d && ctx->inspector_sprite_material.id != 0U) ? ctx->inspector_sprite_material : ctx->sprite_material;
    const nt_material_t tmat = (is_3d && ctx->inspector_text_material.id != 0U) ? ctx->inspector_text_material : ctx->text_material;
    nt_sprite_renderer_set_material(smat);

    const bool can_label = (tmat.id != 0U) && (font.id != 0U) && (label_size > 0.0F);
    const float vy = target->viewport[1];
    const float vh = target->viewport[3];

    for (uint32_t i = 0; i < ctx->debug_zone_count; ++i) {
        const nt_ui_debug_zone_t *z = &ctx->debug_zones[i];
        if (!zone_passes_mode(z, mode)) {
            continue;
        }
        float pad_corners[4][2] = {
            {z->layout_l, z->layout_t},
            {z->layout_r, z->layout_t},
            {z->layout_r, z->layout_b},
            {z->layout_l, z->layout_b},
        };
        float vis_corners[4][2] = {
            {z->visual_l, z->visual_t},
            {z->visual_r, z->visual_t},
            {z->visual_r, z->visual_b},
            {z->visual_l, z->visual_b},
        };
        const uint32_t fill = color_for_state(z->state_flags);

        if (is_3d) {
            nt_ui_internal_emit_filled_quad_m(ctx->atlas, ctx->white_region, pad_corners, z->m, fill);
            /* Outline visual bbox so padding is visually distinct. */
            nt_ui_internal_emit_outline_m(ctx->atlas, ctx->white_region, vis_corners, 2.0F, z->m, DEBUG_OUTLINE_COLOR);
            if (can_label) {
                /* Clay-px baseline at the top-left, mapped by z->m; col1 negated so glyphs read upright. */
                const float ox = z->visual_l + 2.0F;
                const float oy = z->visual_t + label_size + 2.0F;
                float text_model[16];
                for (int rr = 0; rr < 4; ++rr) {
                    text_model[rr] = z->m[rr];
                    text_model[4 + rr] = -z->m[4 + rr];
                    text_model[8 + rr] = z->m[8 + rr];
                    text_model[12 + rr] = (ox * z->m[rr]) + (oy * z->m[4 + rr]) + z->m[12 + rr];
                }
                draw_zone_label(z, text_model, tmat, font, label_size);
            }
            continue;
        }

        for (uint32_t k = 0; k < 4U; ++k) {
            nt_ui_internal_project_layout_to_world(z, vy, vh, pad_corners[k][0], pad_corners[k][1], &pad_corners[k][0], &pad_corners[k][1]);
        }
        nt_ui_internal_emit_filled_quad(ctx->atlas, ctx->white_region, pad_corners, fill);

        for (uint32_t k = 0; k < 4U; ++k) {
            nt_ui_internal_project_layout_to_world(z, vy, vh, vis_corners[k][0], vis_corners[k][1], &vis_corners[k][0], &vis_corners[k][1]);
        }
        nt_ui_internal_emit_outline(ctx->atlas, ctx->white_region, vis_corners, 2.0F, DEBUG_OUTLINE_COLOR);

        /* Label at corner with max y after Y-flip (GL-Y-up top-left). */
        if (can_label) {
            float top_x = pad_corners[0][0];
            float top_y = pad_corners[0][1];
            for (uint32_t k = 1; k < 4U; ++k) {
                if (pad_corners[k][1] > top_y) {
                    top_y = pad_corners[k][1];
                    top_x = pad_corners[k][0];
                }
            }
            /* GL Y-up: baseline sits inside the top edge. */
            const float text_model[16] = {
                1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, top_x + 2.0F, top_y - label_size - 2.0F, 0.0F, 1.0F,
            };
            draw_zone_label(z, text_model, tmat, font, label_size);
        }
    }
    nt_sprite_renderer_flush();
    if (can_label) {
        nt_text_renderer_flush();
    }
}
// #endregion

#endif /* NT_UI_DEBUG_TOOLS */
