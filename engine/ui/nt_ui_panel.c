#include "ui/nt_ui_panel.h"

#include <string.h>

#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_internal.h"

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_panel_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_resource_t atlas, uint32_t region_index, const nt_ui_image_style_t *style) {
    NT_ASSERT(ctx != NULL && "nt_ui_panel_begin: ctx must be non-NULL");
    NT_ASSERT(style != NULL && "nt_ui_panel_begin: style must be non-NULL");
    NT_ASSERT(atlas.id != 0 && "nt_ui_panel_begin: invalid atlas handle");

    /* Allocate payload for Clay IMAGE */
    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_panel_begin: scratch alloc failed");
    *p = (nt_ui_image_payload_t){
        .atlas = atlas,
        .region_index = region_index,
        .flip_bits = style->flip_bits,
    };
    memcpy(p->slice9_override, style->slice9_lrtb, sizeof(p->slice9_override));

    /* Unpack tint */
    Clay_Color tint = {0};
    if (style->color_packed != 0xFFFFFFFF) {
        tint.r = (float)(style->color_packed & 0xFFU);
        tint.g = (float)((style->color_packed >> 8) & 0xFFU);
        tint.b = (float)((style->color_packed >> 16) & 0xFFU);
        tint.a = (float)((style->color_packed >> 24) & 0xFFU);
    }

    /* Open Clay IMAGE container -- children go between _begin and _end */
    Clay__OpenElement();
    Clay__ConfigureOpenElement((Clay_ElementDeclaration){
        .backgroundColor = tint,
        .image = {.imageData = p},
        .userData = (void *)data,
    });
}

void nt_ui_panel_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_panel_end: ctx must be non-NULL");
    Clay__CloseElement();
    (void)ctx;
}

void nt_ui_group_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data) {
    NT_ASSERT(ctx != NULL && "nt_ui_group_begin: ctx must be non-NULL");

    /* Transparent RECT so Clay emits a render command with bbox for deferred center. */
    Clay__OpenElement();
    Clay__ConfigureOpenElement((Clay_ElementDeclaration){
        .backgroundColor = {0, 0, 0, 0},
        .userData = (void *)data,
    });
}

void nt_ui_group_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_group_end: ctx must be non-NULL");
    Clay__CloseElement();
    (void)ctx;
}
