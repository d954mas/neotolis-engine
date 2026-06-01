#include "ui/nt_ui_image.h"

#include <string.h>

#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "ui/nt_ui_internal.h"

/* Phase 56 ext (descriptor refactor): static descriptor consumed by the
 * inspector. Purple pill (0xFFB45A78 -- R=120,G=90,B=180; preserved from
 * the pre-refactor cdv_widget_color switch). */
const nt_ui_widget_def_t NT_UI_IMAGE_DEF = {
    .name = "nt_image",
    .pill_color = 0xFFB45A78U,
    ._reserved = 0U,
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_ui_image(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, nt_resource_t atlas, uint32_t region_index, const nt_ui_image_style_t *style) {
    NT_ASSERT(ctx != NULL && "nt_ui_image: ctx must be non-NULL");
    NT_ASSERT(style != NULL && "nt_ui_image: style must be non-NULL");
    NT_ASSERT(atlas.id != 0 && "nt_ui_image: invalid atlas handle");

    /* Allocate payload from scratch arena */
    nt_ui_image_payload_t *p = NT_MEM_SCRATCH_ALLOC(nt_ui_image_payload_t);
    NT_ASSERT(p != NULL && "nt_ui_image: scratch alloc failed");
    *p = (nt_ui_image_payload_t){
        .atlas = atlas,
        .region_index = region_index,
        .origin_x = style->origin_x,
        .origin_y = style->origin_y,
        .flip_bits = style->flip_bits,
        .flags = style->flags,
    };
    memcpy(p->slice9_override, style->slice9_lrtb, sizeof(p->slice9_override));

    /* Unpack tint to Clay_Color. 0xFFFFFFFF = untinted (pass {0,0,0,0}). */
    Clay_Color tint = {0};
    if (style->color_packed != 0xFFFFFFFF) {
        tint.r = (float)(style->color_packed & 0xFFU);
        tint.g = (float)((style->color_packed >> 8) & 0xFFU);
        tint.b = (float)((style->color_packed >> 16) & 0xFFU);
        tint.a = (float)((style->color_packed >> 24) & 0xFFU);
    }

    CLAY({
        .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
        .backgroundColor = tint,
        .image = {.imageData = p},
        .userData = (void *)data,
    }) {
        /* Phase 56 ext (CHUNK E): tag this Clay element so nt_ui_inspector
         * shows "image" in the tree. Clay auto-assigns the id (no game-side
         * id needed); we read it via the internal accessor (the just-opened
         * element is the current top of openLayoutElementStack). */
        nt_ui_widget_register(ctx, nt_ui_internal_current_open_element_id(), &NT_UI_IMAGE_DEF, NULL, data);
    }
}
