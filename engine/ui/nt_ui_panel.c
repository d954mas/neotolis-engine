#include "ui/nt_ui_panel.h"

#include <math.h>
#include <string.h>

#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_clay_internal.h"
#include "ui/nt_ui_internal.h"

const nt_ui_widget_def_t NT_UI_PANEL_DEF = {
    .name = "nt_panel",
    .pill_color = 0xFF4678B4U,
    ._reserved = 0U,
};
const nt_ui_widget_def_t NT_UI_GROUP_DEF = {
    .name = "nt_group",
    .pill_color = 0xFF5AA0A0U,
    ._reserved = 0U,
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_panel_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_resource_t atlas, uint32_t region_index, const nt_ui_image_style_t *style, const Clay_ElementDeclaration *decl) {
    NT_ASSERT(ctx != NULL && "nt_ui_panel_begin: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_panel_begin: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    NT_ASSERT(style != NULL && "nt_ui_panel_begin: style must be non-NULL");
    NT_ASSERT(atlas.id != 0 && "nt_ui_panel_begin: invalid atlas handle");
    NT_ASSERT(isfinite(style->slice9_scale) && style->slice9_scale > 0.0F && "nt_ui_panel_begin: style.slice9_scale must be finite > 0");
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_panel_begin: decl->id must be 0 (panel id auto-assigned by Clay)");
        NT_ASSERT(decl->image.imageData == NULL && "nt_ui_panel_begin: decl->image.imageData must be NULL (atlas+region controls image)");
        NT_ASSERT(decl->backgroundColor.a == 0.0F && "nt_ui_panel_begin: decl->backgroundColor must be zero (style->color_packed controls)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_panel_begin: decl->userData must be NULL (data param controls)");
    }

    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_panel_begin: scratch alloc failed");
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

    Clay_Color tint = {0};
    if (style->color_packed != 0xFFFFFFFF) {
        tint.r = (float)(style->color_packed & 0xFFU);
        tint.g = (float)((style->color_packed >> 8) & 0xFFU);
        tint.b = (float)((style->color_packed >> 16) & 0xFFU);
        tint.a = (float)((style->color_packed >> 24) & 0xFFU);
    }

    Clay_ElementDeclaration final = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    final.backgroundColor = tint;
    final.image = (Clay_ImageElementConfig){.imageData = p};
    final.userData = (void *)data;
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(final);

    /* Clay auto-assigns the id; fetch post-open. */
    nt_ui_widget_register(ctx, nt_ui_internal_current_open_element_id(), &NT_UI_PANEL_DEF, NULL);
}

void nt_ui_panel_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_panel_end: ctx must be non-NULL");
    nt_ui_clay_priv_close_element();
    (void)ctx;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_group_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, const Clay_ElementDeclaration *decl) {
    NT_ASSERT(ctx != NULL && "nt_ui_group_begin: ctx must be non-NULL");
    NT_ASSERT(ctx->in_frame && ctx == nt_ui_internal_get_inframe_ctx() && "nt_ui_group_begin: must be called between nt_ui_begin and nt_ui_end on the active ctx");
    if (decl != NULL) {
        NT_ASSERT(decl->id.id == 0U && "nt_ui_group_begin: decl->id must be 0 (group id auto-assigned by Clay)");
        NT_ASSERT(decl->userData == NULL && "nt_ui_group_begin: decl->userData must be NULL (data param controls)");
    }

    /* type=NONE anchor — walker skips handler but keeps the bbox. */
    nt_ui_custom_data_t *anchor = NT_MEM_SCRATCH_ALLOC(nt_ui_custom_data_t);
    NT_ASSERT(anchor != NULL && "nt_ui_group_begin: scratch alloc failed");
    *anchor = (nt_ui_custom_data_t){.type = NT_UI_CUSTOM_TYPE_NONE, .data = NULL};

    Clay_ElementDeclaration final = (decl != NULL) ? *decl : (Clay_ElementDeclaration){0};
    final.custom = (Clay_CustomElementConfig){.customData = anchor};
    final.userData = (void *)data;
    nt_ui_clay_priv_open_element();
    nt_ui_clay_priv_configure_open_element(final);

    nt_ui_widget_register(ctx, nt_ui_internal_current_open_element_id(), &NT_UI_GROUP_DEF, NULL);
}

void nt_ui_group_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_group_end: ctx must be non-NULL");
    nt_ui_clay_priv_close_element();
    (void)ctx;
}
