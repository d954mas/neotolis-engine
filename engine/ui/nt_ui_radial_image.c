#include "ui/nt_ui_radial_image.h"

#include <math.h>
#include <stddef.h>

#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "ui/nt_ui_clay_impl.h" /* nt_ui_internal_get_inframe_ctx */
#include "ui/nt_ui_image.h"
#include "ui/nt_ui_internal.h"

#define RADIAL_TAIL_ATTR(loc, field, n) {.location = (loc), .type = NT_VERTEX_FLOAT, .count = (n), .offset = (uint16_t)(20U + offsetof(nt_ui_radial_image_tail_t, field))}

const nt_vertex_layout_t NT_UI_RADIAL_IMAGE_VERTEX_LAYOUT = {.stride = 20U + sizeof(nt_ui_radial_image_tail_t),
                                                             .attr_count = 7,
                                                             .attrs = {{.location = 0, .type = NT_VERTEX_FLOAT, .count = 3, .offset = 0},
                                                                       {.location = 2, .type = NT_VERTEX_UINT8, .count = 4, .normalized = true, .offset = 16},
                                                                       {.location = 3, .type = NT_VERTEX_UINT16, .count = 2, .normalized = true, .offset = 12},
                                                                       RADIAL_TAIL_ATTR(4, radial, 4),
                                                                       RADIAL_TAIL_ATTR(5, tint, 4),
                                                                       RADIAL_TAIL_ATTR(6, source_u, 3),
                                                                       RADIAL_TAIL_ATTR(7, source_v, 3)}};

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
    NT_ASSERT(isfinite(style->tint_strength) && style->tint_strength >= 0.0F && style->tint_strength <= 1.0F && "nt_ui_radial_image: tint_strength must be finite in [0,1]");
    /* Slice9 stretches patches, so angles in source-image space would distort; the other bits are engine-owned. */
    NT_ASSERT((style->flags & ~NT_UI_IMAGE_ORIGIN_OVERRIDE) == 0U && "nt_ui_radial_image: style.flags accepts only NT_UI_IMAGE_ORIGIN_OVERRIDE");
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

    struct nt_ui_radial_reveal *reveal = NT_MEM_SCRATCH_ALLOC(struct nt_ui_radial_reveal);
    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(reveal != NULL && p != NULL && "nt_ui_radial_image: scratch alloc failed");
    /* tint.w is the TINT reveal strength, not alpha; alpha fades through color_packed and opacity. */
    const Clay_Color tint_rgb = nt_ui_unpack_abgr(style->tint_color_packed);
    *reveal = (struct nt_ui_radial_reveal){
        .angle_start = angle_start,
        .angle_end = angle_end,
        .inner_radius_norm = style->inner_radius_norm,
        .tint = {tint_rgb.r / 255.0F, tint_rgb.g / 255.0F, tint_rgb.b / 255.0F, style->tint_strength},
    };
    /* The override flag with zero borders turns a baked nine-patch into a plain quad. */
    *p = (nt_ui_image_payload_t){
        .atlas = region->atlas,
        .region_index = region->region,
        .origin_x = style->origin_x,
        .origin_y = style->origin_y,
        .slice9_scale = 1.0F,
        .flip_bits = style->flip_bits,
        .flags = (uint8_t)(style->flags | NT_UI_IMAGE_SLICE9_OVERRIDE | NT_UI_IMAGE_RADIAL_REVEAL),
        .material = style->material,
        .radial = reveal,
    };

    Clay_ElementDeclaration final = decl != NULL ? *decl : (Clay_ElementDeclaration){.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}}};
    final.image = (Clay_ImageElementConfig){.imageData = p};
    final.backgroundColor = nt_ui_unpack_tint(style->color_packed);
    final.userData = (void *)data;
    CLAY(final) { nt_ui_widget_register(ctx, nt_ui_internal_current_open_element_id(), &NT_UI_IMAGE_DEF, NULL, true); }
}

void nt_ui_radial_image_fill(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, float angle_start, float fill, float sweep_total,
                             const nt_ui_radial_image_style_t *style, const Clay_ElementDeclaration *decl) {
    NT_ASSERT(isfinite(angle_start) && isfinite(fill) && isfinite(sweep_total) && "nt_ui_radial_image_fill: angle_start/fill/sweep_total must be finite");
    const float clamped_fill = fminf(fmaxf(fill, 0.0F), 1.0F);
    const float angle_end = angle_start + (clamped_fill * sweep_total);
    nt_ui_radial_image(ctx, data, region, angle_start, angle_end, style, decl);
}
