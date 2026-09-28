#include "ui/nt_ui_radial_image.h"

#include <math.h>

#include "core/nt_assert.h"
#include "ui/nt_ui_clay_impl.h" /* nt_ui_internal_get_inframe_ctx */
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"

#define RADIAL_IMAGE_ASPECT_OFFSET 52U

const nt_vertex_layout_t NT_UI_RADIAL_IMAGE_VERTEX_LAYOUT = {.stride = 64,
                                                             .attr_count = 7,
                                                             .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
                                                                       {.location = 2, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 16},
                                                                       {.location = 3, .type = NT_VERTEX_UINT16, .count = 2, .normalized = true, .offset = 12},
                                                                       {.location = 4, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 20},
                                                                       {.location = 5, .type = NT_VERTEX_FLOAT, .count = 4, .offset = 36},
                                                                       {.location = 6, .type = NT_VERTEX_FLOAT, .count = 2, .offset = NT_UI_RADIAL_IMAGE_SOURCE_UV_OFFSET},
                                                                       {.location = 7, .type = NT_VERTEX_FLOAT, .count = 1, .offset = RADIAL_IMAGE_ASPECT_OFFSET}}};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_radial_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, float angle_start, float angle_end, const nt_ui_radial_image_style_t *style,
                        const Clay_ElementDeclaration *decl) {
    NT_ASSERT(ctx != NULL && "nt_ui_radial_image: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_radial_image: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_radial_image: style must be non-NULL");
    NT_ASSERT(region != NULL && region->atlas.id != 0 && "nt_ui_radial_image: invalid atlas handle");
    NT_ASSERT(style->material.id != 0 && "nt_ui_radial_image: style.material must be a valid radial-image material");
    NT_ASSERT(nt_material_vertex_layout_equals(nt_material_get_info(style->material), &NT_UI_RADIAL_IMAGE_VERTEX_LAYOUT) &&
              nt_material_get_info(style->material)->source_uv_offset == NT_UI_RADIAL_IMAGE_SOURCE_UV_OFFSET &&
              "nt_ui_radial_image: material needs NT_UI_RADIAL_IMAGE_VERTEX_LAYOUT and its source UV offset");
    NT_ASSERT(isfinite(angle_start) && isfinite(angle_end) && "nt_ui_radial_image: angles must be finite");
    NT_ASSERT(isfinite(style->inner_radius_norm) && style->inner_radius_norm >= 0.0F && style->inner_radius_norm < 1.0F && "nt_ui_radial_image: inner_radius_norm must be finite in [0,1)");
    NT_ASSERT(isfinite(style->slice9_scale) && style->slice9_scale > 0.0F && "nt_ui_radial_image: style.slice9_scale must be finite > 0");
    NT_ASSERT(isfinite(style->tint_strength) && style->tint_strength >= 0.0F && style->tint_strength <= 1.0F && "nt_ui_radial_image: tint_strength must be finite in [0,1]");
    /* Slice9 patches use stretched geometry; the source-coordinate producer only
     * covers the original atlas region vertices. */
    NT_ASSERT(!(style->flags & NT_UI_IMAGE_SLICE9_OVERRIDE) && style->slice9_lrtb[0] == 0 && style->slice9_lrtb[1] == 0 && style->slice9_lrtb[2] == 0 && style->slice9_lrtb[3] == 0 &&
              "nt_ui_radial_image: slice9 is unsupported; rectangular regions only");
    if (style->flags & NT_UI_IMAGE_ORIGIN_OVERRIDE) {
        NT_ASSERT(isfinite(style->origin_x) && isfinite(style->origin_y) && "nt_ui_radial_image: ORIGIN_OVERRIDE -> style.origin_{x,y} must be finite");
    }
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_radial_image: decl->id must be 0 (id auto-assigned by Clay)");
        NT_ASSERT(decl->image.imageData == NULL && "nt_ui_radial_image: decl->image.imageData must be NULL (atlas+region controls image)");
        NT_ASSERT(decl->backgroundColor.a == 0.0F && "nt_ui_radial_image: decl->backgroundColor must be zero (style->color_packed controls)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_radial_image: decl->userData must be NULL (data param controls)");
    }

    /* No-art terminal: resolve lazily, memoize into *region; skip the emit on an
     * unresolved/no-art ref (mirror nt_ui_fill.c). */
    nt_atlas_resolve_ref(region);
    if (region->region == NT_ATLAS_INVALID_REGION) {
        return;
    }
    if (nt_resource_is_ready(region->atlas)) {
        const nt_texture_region_t *resolved = nt_atlas_get_region(region->atlas, region->region);
        NT_ASSERT((resolved->slice9_lrtb[0] | resolved->slice9_lrtb[1] | resolved->slice9_lrtb[2] | resolved->slice9_lrtb[3]) == 0 && "nt_ui_radial_image: baked slice9 is unsupported");
    }

    /* Per-widget TINT color -> a_tint (0..1 floats). mode/dim stay material-level. */
    const Clay_Color tint_rgb = nt_ui_unpack_abgr(style->tint_color_packed);

    /* The walker writes a_aspect; the sprite renderer writes a_source_uv per vertex. */
    const float blk[11] = {angle_start, angle_end, style->inner_radius_norm, 0.0F, tint_rgb.r / 255.0F, tint_rgb.g / 255.0F, tint_rgb.b / 255.0F, style->tint_strength, 0.0F, 0.0F, 0.0F};
    const nt_ui_image_custom_t img = {
        .atlas = region->atlas,
        .region_index = region->region,
        .material = style->material,
        .custom_attrs = blk,
        .custom_bytes = (uint8_t)sizeof blk,
        .aspect_offset = RADIAL_IMAGE_ASPECT_OFFSET,
        /* a_tint.w is the TINT reveal strength, not alpha. Real alpha fades via color_packed ->
         * a_color (the walker's backgroundColor.a path), never through a_tint. */
        .flip_bits = style->flip_bits,
        .flags = style->flags,
        .origin_x = style->origin_x,
        .origin_y = style->origin_y,
        .slice9_scale = style->slice9_scale,
        .color_packed = style->color_packed,
    };
    nt_ui_image_custom(ctx, data, &img, decl);
}

void nt_ui_radial_image_fill(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, float angle_start, float fill, float sweep_total,
                             const nt_ui_radial_image_style_t *style, const Clay_ElementDeclaration *decl) {
    NT_ASSERT(isfinite(angle_start) && isfinite(fill) && isfinite(sweep_total) && "nt_ui_radial_image_fill: angle_start/fill/sweep_total must be finite");
    const float clamped_fill = fminf(fmaxf(fill, 0.0F), 1.0F);
    const float angle_end = angle_start + (clamped_fill * sweep_total);
    nt_ui_radial_image(ctx, data, region, angle_start, angle_end, style, decl);
}
