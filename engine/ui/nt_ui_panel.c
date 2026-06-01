#include "ui/nt_ui_panel.h"

#include <string.h>

#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_internal.h"

/* Phase 56 ext (descriptor refactor): static descriptors consumed by the
 * inspector. Orange pill for panel (0xFF4678B4 -- R=180,G=120,B=70); olive
 * pill for group (0xFF5AA0A0 -- R=160,G=160,B=90). Preserved from the
 * pre-refactor cdv_widget_color switch. */
const nt_ui_widget_def_t NT_UI_PANEL_DEF = {
    .name = "panel",
    .pill_color = 0xFF4678B4U,
    ._reserved = 0U,
};
const nt_ui_widget_def_t NT_UI_GROUP_DEF = {
    .name = "group",
    .pill_color = 0xFF5AA0A0U,
    ._reserved = 0U,
};

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
        .origin_x = style->origin_x,
        .origin_y = style->origin_y,
        .flip_bits = style->flip_bits,
        .flags = style->flags,
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

    /* Phase 56 ext (CHUNK E): tag this Clay element so nt_ui_inspector shows
     * "panel" in the tree. Clay auto-assigns the id; fetch it post-open via
     * the internal accessor (Clay__GetOpenLayoutElement is private). */
    nt_ui_widget_register(ctx, nt_ui_internal_current_open_element_id(), &NT_UI_PANEL_DEF, NULL);
}

void nt_ui_panel_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_panel_end: ctx must be non-NULL");
    Clay__CloseElement();
    (void)ctx;
}

void nt_ui_group_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data) {
    NT_ASSERT(ctx != NULL && "nt_ui_group_begin: ctx must be non-NULL");

    /* CUSTOM anchor: zero draw calls, zero darkening. type=NONE tells the
     * walker to skip the handler call but still use the bbox for deferred
     * center resolution. */
    nt_ui_custom_data_t *anchor = NT_MEM_SCRATCH_ALLOC(nt_ui_custom_data_t);
    NT_ASSERT(anchor != NULL && "nt_ui_group_begin: scratch alloc failed");
    *anchor = (nt_ui_custom_data_t){.type = NT_UI_CUSTOM_TYPE_NONE, .data = NULL};

    Clay__OpenElement();
    Clay__ConfigureOpenElement((Clay_ElementDeclaration){
        .custom = {.customData = anchor},
        .userData = (void *)data,
    });

    /* Phase 56 ext (CHUNK E): tag this Clay element so nt_ui_inspector shows
     * "group" in the tree. Clay auto-assigns the id; fetch it post-open via
     * the internal accessor (Clay__GetOpenLayoutElement is private). */
    nt_ui_widget_register(ctx, nt_ui_internal_current_open_element_id(), &NT_UI_GROUP_DEF, NULL);
}

void nt_ui_group_end(nt_ui_context_t *ctx) {
    NT_ASSERT(ctx != NULL && "nt_ui_group_end: ctx must be non-NULL");
    Clay__CloseElement();
    (void)ctx;
}
