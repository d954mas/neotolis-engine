#include "ui/nt_ui_image.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_clay_internal.h"
#include "ui/nt_ui_internal.h"

const nt_ui_widget_def_t NT_UI_IMAGE_DEF = {
    .name = "nt_image",
    .pill_color = 0xFFB45A78U,
    ._reserved = 0U,
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_resource_t atlas, uint32_t region_index, const nt_ui_image_style_t *style, const Clay_ElementDeclaration *decl) {
    NT_ASSERT(ctx != NULL && "nt_ui_image: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_image: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_image: style must be non-NULL");
    NT_ASSERT(atlas.id != 0 && "nt_ui_image: invalid atlas handle");
    NT_ASSERT(isfinite(style->slice9_scale) && style->slice9_scale > 0.0F && "nt_ui_image: style.slice9_scale must be finite > 0");
    if (style->flags & NT_UI_IMAGE_ORIGIN_OVERRIDE) {
        NT_ASSERT(isfinite(style->origin_x) && isfinite(style->origin_y) && "nt_ui_image: ORIGIN_OVERRIDE -> style.origin_{x,y} must be finite");
    }
    if (decl != NULL) {
        NT_ASSERT(decl->image.imageData == NULL && "nt_ui_image: decl->image.imageData must be NULL (atlas+region controls image)");
        NT_ASSERT(decl->backgroundColor.a == 0.0F && "nt_ui_image: decl->backgroundColor must be zero (style->color_packed controls)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_image: decl->userData must be NULL (data param controls)");
    }

    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_image: scratch alloc failed");
    *p = (nt_ui_image_payload_t){
        .atlas = atlas,
        .region_index = region_index,
        .origin_x = style->origin_x,
        .origin_y = style->origin_y,
        .slice9_scale = style->slice9_scale,
        .flip_bits = style->flip_bits,
        .flags = style->flags,
    };
    memcpy(p->slice9_override, style->slice9_lrtb, sizeof(p->slice9_override));

    /* 0xFFFFFFFF = untinted (pass {0,0,0,0}). Transparency lives in opacity. */
    Clay_Color tint = {0};
    if (style->color_packed != 0xFFFFFFFF) {
        tint.r = (float)(style->color_packed & 0xFFU);
        tint.g = (float)((style->color_packed >> 8) & 0xFFU);
        tint.b = (float)((style->color_packed >> 16) & 0xFFU);
        tint.a = (float)((style->color_packed >> 24) & 0xFFU);
    }

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

    CLAY(final) { nt_ui_widget_register(ctx, nt_ui_internal_current_open_element_id(), &NT_UI_IMAGE_DEF, NULL); }
}
