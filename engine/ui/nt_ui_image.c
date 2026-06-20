#include "ui/nt_ui_image.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "material/nt_material.h"
#include "memory/nt_mem_scratch.h"
#include "renderers/nt_sprite_renderer.h" /* NT_SPRITE_CUSTOM_STRIDE_MAX */
#include "resource/nt_resource.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"

const nt_ui_widget_def_t NT_UI_IMAGE_DEF = {
    .name = "nt_image",
    .pill_color = 0xFFB45A78U,
    ._reserved = 0U,
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_atlas_region_ref_t *region, const nt_ui_image_style_t *style, const Clay_ElementDeclaration *decl) {
    NT_ASSERT(ctx != NULL && "nt_ui_image: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_image: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_image: style must be non-NULL");
    NT_ASSERT(region != NULL && region->atlas.id != 0 && "nt_ui_image: invalid atlas handle");
    NT_ASSERT(isfinite(style->slice9_scale) && style->slice9_scale > 0.0F && "nt_ui_image: style.slice9_scale must be finite > 0");
    if (style->flags & NT_UI_IMAGE_ORIGIN_OVERRIDE) {
        NT_ASSERT(isfinite(style->origin_x) && isfinite(style->origin_y) && "nt_ui_image: ORIGIN_OVERRIDE -> style.origin_{x,y} must be finite");
    }
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_image: decl->id must be 0 (image id auto-assigned by Clay)");
        NT_ASSERT(decl->image.imageData == NULL && "nt_ui_image: decl->image.imageData must be NULL (atlas+region controls image)");
        NT_ASSERT(decl->backgroundColor.a == 0.0F && "nt_ui_image: decl->backgroundColor must be zero (style->color_packed controls)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_image: decl->userData must be NULL (data param controls)");
    }

    /* Late-bind: resolve lazily, memoize into *region. Unresolved (atlas not ready or bad name) -> skip emit. */
    nt_atlas_resolve_ref(region);
    if (region->region == NT_ATLAS_INVALID_REGION) {
        return;
    }

    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_image: scratch alloc failed");
    *p = (nt_ui_image_payload_t){
        .atlas = region->atlas,
        .region_index = region->region,
        .origin_x = style->origin_x,
        .origin_y = style->origin_y,
        .slice9_scale = style->slice9_scale,
        .flip_bits = style->flip_bits,
        .flags = style->flags,
    };
    memcpy(p->slice9_override, style->slice9_lrtb, sizeof(p->slice9_override));

    /* Transparency lives in opacity, not tint alpha. */
    const Clay_Color tint = nt_ui_unpack_tint(style->color_packed);

    /* decl == NULL falls back to GROW/GROW; decl != NULL is respected verbatim. */
    Clay_ElementDeclaration final;
    if (decl != NULL) {
        final = *decl;
    } else {
        final = (Clay_ElementDeclaration){.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}}};
    }
    final.image = (Clay_ImageElementConfig){.imageData = p};
    final.backgroundColor = tint;
    final.userData = (void *)data;

    CLAY(final) { nt_ui_widget_register(ctx, nt_ui_internal_current_open_element_id(), &NT_UI_IMAGE_DEF, NULL, true); }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_image_custom(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const nt_ui_image_custom_t *img, const Clay_ElementDeclaration *decl) {
    NT_ASSERT(ctx != NULL && "nt_ui_image_custom: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_image_custom: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(img != NULL && "nt_ui_image_custom: img must be non-NULL");
    NT_ASSERT(img->atlas.id != 0 && "nt_ui_image_custom: invalid atlas handle");
    NT_ASSERT(img->material.id != 0 && "nt_ui_image_custom: material must be valid (custom-attr path)");
    NT_ASSERT(img->custom_bytes > 0 && img->custom_bytes <= NT_SPRITE_CUSTOM_STRIDE_MAX && "nt_ui_image_custom: custom_bytes in (0, NT_SPRITE_CUSTOM_STRIDE_MAX]");
    NT_ASSERT(img->custom_attrs != NULL && "nt_ui_image_custom: custom_attrs must be non-NULL when custom_bytes > 0");
    NT_ASSERT(isfinite(img->slice9_scale) && img->slice9_scale > 0.0F && "nt_ui_image_custom: slice9_scale must be finite > 0");
    /* The block must fill exactly one FLOAT4 per declared material attr — set_custom_attrs asserts the same. */
    const nt_material_info_t *mi = nt_material_get_info(img->material);
    NT_ASSERT(mi != NULL && mi->ready && "nt_ui_image_custom: material must be ready");
    NT_ASSERT((uint32_t)mi->attr_map_count * 16U == (uint32_t)img->custom_bytes && "nt_ui_image_custom: custom_bytes must equal material attr_map_count*16");
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_image_custom: decl->id must be 0 (id auto-assigned by Clay)");
        NT_ASSERT(decl->image.imageData == NULL && "nt_ui_image_custom: decl->image.imageData must be NULL (atlas+region controls image)");
        NT_ASSERT(decl->backgroundColor.a == 0.0F && "nt_ui_image_custom: decl->backgroundColor must be zero (img->color_packed controls)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_image_custom: decl->userData must be NULL (data param controls)");
    }

    /* Custom block lives in its own scratch alloc (same frame lifetime as the payload);
     * the payload only carries a pointer, so plain images stay small. */
    nt_ui_image_custom_block_t *blk = NT_MEM_SCRATCH_ALLOC(nt_ui_image_custom_block_t);
    NT_ASSERT(blk != NULL && "nt_ui_image_custom: scratch alloc failed (block)");
    *blk = (nt_ui_image_custom_block_t){.custom_bytes = img->custom_bytes, .geom_mode = img->geom_mode};
    memcpy(blk->custom_attrs, img->custom_attrs, img->custom_bytes);

    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_image_custom: scratch alloc failed");
    *p = (nt_ui_image_payload_t){
        .atlas = img->atlas,
        .region_index = img->region_index,
        .origin_x = img->origin_x,
        .origin_y = img->origin_y,
        .slice9_scale = img->slice9_scale,
        .flip_bits = img->flip_bits,
        .flags = img->flags,
        .material = img->material,
        .custom = blk,
    };
    memcpy(p->slice9_override, img->slice9_lrtb, sizeof(p->slice9_override));

    /* Transparency lives in opacity (element_data), not tint alpha. */
    const Clay_Color tint = nt_ui_unpack_tint(img->color_packed);

    Clay_ElementDeclaration final;
    if (decl != NULL) {
        final = *decl;
    } else {
        final = (Clay_ElementDeclaration){.layout = {.sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)}}};
    }
    final.image = (Clay_ImageElementConfig){.imageData = p};
    final.backgroundColor = tint;
    final.userData = (void *)data;

    CLAY(final) { nt_ui_widget_register(ctx, nt_ui_internal_current_open_element_id(), &NT_UI_IMAGE_DEF, NULL, true); }
}
