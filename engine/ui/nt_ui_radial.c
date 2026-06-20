#include "ui/nt_ui_radial.h"

#include <math.h>

#include "core/nt_assert.h"
#include "ui/nt_ui_clay_impl.h" /* nt_ui_internal_get_inframe_ctx */
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_radial(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, float angle_start, float angle_end, const nt_ui_radial_style_t *style, const Clay_ElementDeclaration *decl) {
    NT_ASSERT(ctx != NULL && "nt_ui_radial: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_radial: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_radial: style must be non-NULL");
    NT_ASSERT(style->material.id != 0 && "nt_ui_radial: style.material must be a valid radial material");
    NT_ASSERT(isfinite(angle_start) && isfinite(angle_end) && "nt_ui_radial: angles must be finite");
    NT_ASSERT(isfinite(style->inner_radius_norm) && style->inner_radius_norm >= 0.0F && style->inner_radius_norm < 1.0F && "nt_ui_radial: inner_radius_norm must be finite in [0,1)");
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_radial: decl->id must be 0 (id auto-assigned by Clay)");
        NT_ASSERT(decl->image.imageData == NULL && "nt_ui_radial: decl->image.imageData must be NULL (widget controls image)");
        NT_ASSERT(decl->backgroundColor.a == 0.0F && "nt_ui_radial: decl->backgroundColor must be zero (style->color_packed controls)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_radial: decl->userData must be NULL (data param controls)");
    }

    /* Custom block, attr_map order [a_radial, a_layout]:
     *   a_radial @ 0..3 = {angle_start, angle_end, inner_radius_norm, 0}
     *   a_layout @ 4..7 = {0,0,0,0} placeholders — the walker fills a_layout by NAME
     *                     (aspect = bbox w/h, bbox px size). The material must declare
     *                     attr_map [a_radial, a_layout].
     * Flat radial rasterizes as a clean white-pixel bbox quad (GEOMETRY mode) — the SDF
     * fs derives its local coord from gl_VertexID&3, so no region UV is needed. */
    const float blk[8] = {angle_start, angle_end, style->inner_radius_norm, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F};
    const nt_ui_image_custom_t img = {
        .atlas = ctx->atlas,
        .region_index = ctx->white_region,
        .material = style->material,
        .custom_attrs = blk,
        .custom_bytes = (uint8_t)sizeof blk,
        .geom_mode = NT_UI_IMAGE_GEOM_GEOMETRY,
        .slice9_scale = 1.0F,
        .color_packed = style->color_packed,
    };
    nt_ui_image_custom(ctx, data, &img, decl);
}

void nt_ui_radial_fill(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, float angle_start, float fill, float sweep_total, const nt_ui_radial_style_t *style,
                       const Clay_ElementDeclaration *decl) {
    NT_ASSERT(isfinite(angle_start) && isfinite(fill) && isfinite(sweep_total) && "nt_ui_radial_fill: angle_start/fill/sweep_total must be finite");
    const float angle_end = nt_ui_radial_fill_to_end(angle_start, fill, sweep_total);
    nt_ui_radial(ctx, data, angle_start, angle_end, style, decl);
}
