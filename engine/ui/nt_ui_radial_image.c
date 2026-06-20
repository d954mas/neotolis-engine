#include "ui/nt_ui_radial_image.h"

#include <math.h>

#include "core/nt_assert.h"
#include "ui/nt_ui_clay_impl.h" /* nt_ui_internal_get_inframe_ctx */
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_radial_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, float angle_start, float angle_end, const nt_ui_radial_image_style_t *style,
                        const Clay_ElementDeclaration *decl) {
    NT_ASSERT(ctx != NULL && "nt_ui_radial_image: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_radial_image: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_radial_image: style must be non-NULL");
    NT_ASSERT(region != NULL && region->atlas.id != 0 && "nt_ui_radial_image: invalid atlas handle");
    NT_ASSERT(style->material.id != 0 && "nt_ui_radial_image: style.material must be a valid radial-image material");
    NT_ASSERT(isfinite(angle_start) && isfinite(angle_end) && "nt_ui_radial_image: angles must be finite");
    NT_ASSERT(isfinite(style->inner_radius_norm) && style->inner_radius_norm >= 0.0F && style->inner_radius_norm < 1.0F && "nt_ui_radial_image: inner_radius_norm must be finite in [0,1)");
    NT_ASSERT(isfinite(style->slice9_scale) && style->slice9_scale > 0.0F && "nt_ui_radial_image: style.slice9_scale must be finite > 0");
    NT_ASSERT(isfinite(style->tint_strength) && style->tint_strength >= 0.0F && style->tint_strength <= 1.0F && "nt_ui_radial_image: tint_strength must be finite in [0,1]");
    /* slice9 unsupported: the reveal fs normalizes the atlas UV against the region's
     * UV rect, which assumes a LINEAR mapping over the quad. slice9's per-patch UV is
     * non-linear → the ring/reveal would deform. Any rectangular (non-slice9) region
     * is supported; a real geometry-local coord is the future path for slice9. */
    NT_ASSERT(!(style->flags & NT_UI_IMAGE_SLICE9_OVERRIDE) && style->slice9_lrtb[0] == 0 && style->slice9_lrtb[1] == 0 && style->slice9_lrtb[2] == 0 && style->slice9_lrtb[3] == 0 &&
              "nt_ui_radial_image: slice9 is unsupported (non-linear UV); rectangular regions only");
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

    /* Per-widget TINT color -> a_tint (0..1 floats). mode/dim stay material-level. */
    const Clay_Color tint_rgb = nt_ui_unpack_abgr(style->tint_color_packed);

    /* 48 B custom block, byte-for-byte the walker's expected layout:
     *   a_radial @ 0..3 = {angle_start, angle_end, inner_radius_norm, aspect-placeholder}
     *   a_tint   @ 4..7 = {r, g, b, tint_strength} in 0..1
     *   a_uvrect @ 8..11 = region min/max atlas UV placeholder
     * The walker overwrites a_radial.w (aspect_slot = 3) with the real bbox w/h and
     * a_uvrect (uvrect_slot = 8) with the region's UV bounds at emit. REGION mode =
     * the textured emit_region/emit_slice9 path (origin/flip honored; slice9 asserted
     * unset above). */
    const float blk[12] = {
        angle_start, angle_end, style->inner_radius_norm, 1.0F, tint_rgb.r / 255.0F, tint_rgb.g / 255.0F, tint_rgb.b / 255.0F, style->tint_strength, 0.0F, 0.0F, 0.0F, 0.0F,
    };
    const nt_ui_image_custom_t img = {
        .atlas = region->atlas,
        .region_index = region->region,
        .material = style->material,
        .custom_attrs = blk,
        .custom_bytes = (uint8_t)sizeof blk,
        .aspect_slot = 3,
        .uvrect_slot = 8,
        .geom_mode = NT_UI_IMAGE_GEOM_REGION,
        .flip_bits = style->flip_bits,
        .flags = style->flags,
        .origin_x = style->origin_x,
        .origin_y = style->origin_y,
        .slice9_scale = style->slice9_scale,
        .color_packed = style->color_packed,
    };
    /* slice9 asserted unset (rectangular regions only); slice9_lrtb stays zero. */
    nt_ui_image_custom(ctx, data, &img, decl);
}

void nt_ui_radial_image_fill(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, float angle_start, float fill, float sweep_total,
                             const nt_ui_radial_image_style_t *style, const Clay_ElementDeclaration *decl) {
    NT_ASSERT(isfinite(angle_start) && isfinite(fill) && isfinite(sweep_total) && "nt_ui_radial_image_fill: angle_start/fill/sweep_total must be finite");
    const float angle_end = nt_ui_radial_fill_to_end(angle_start, fill, sweep_total);
    nt_ui_radial_image(ctx, data, region, angle_start, angle_end, style, decl);
}
