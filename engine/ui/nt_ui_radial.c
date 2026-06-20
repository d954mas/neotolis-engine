#include "ui/nt_ui_radial.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_clay_impl.h"
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

    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_radial: scratch alloc failed");
    /* Walker emit_radial reads ctx->atlas/white_region directly; the payload mirrors
     * them so the shared IMAGE asserts (non-zero atlas handle) hold. */
    *p = (nt_ui_image_payload_t){
        .atlas = ctx->atlas,
        .region_index = ctx->white_region,
        .slice9_scale = 1.0F,
        .flags = (uint8_t)NT_UI_IMAGE_FLAG_RADIAL,
        .material = style->material,
        /* radial.w (aspect) is a placeholder; the walker overwrites it with the
         * real bbox w/h at emit (correct under any sizing). */
        .radial = {angle_start, angle_end, style->inner_radius_norm, 1.0F},
    };

    /* Transparency lives in opacity (element_data), not tint alpha. */
    const Clay_Color tint = nt_ui_unpack_tint(style->color_packed);

    Clay_ElementDeclaration final;
    if (decl != NULL) {
        final = *decl;
    } else {
        final = (Clay_ElementDeclaration){.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}}};
    }
    final.image = (Clay_ImageElementConfig){.imageData = p};
    final.backgroundColor = tint;
    final.userData = (void *)data;

    CLAY(final) { nt_ui_widget_register(ctx, nt_ui_internal_current_open_element_id(), &NT_UI_IMAGE_DEF, NULL); }
}

void nt_ui_radial_fill(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, float angle_start, float fill, float sweep_total, const nt_ui_radial_style_t *style,
                       const Clay_ElementDeclaration *decl) {
    NT_ASSERT(isfinite(angle_start) && isfinite(fill) && isfinite(sweep_total) && "nt_ui_radial_fill: angle_start/fill/sweep_total must be finite");
    const float angle_end = nt_ui_radial_fill_to_end(angle_start, fill, sweep_total);
    nt_ui_radial(ctx, data, angle_start, angle_end, style, decl);
}
