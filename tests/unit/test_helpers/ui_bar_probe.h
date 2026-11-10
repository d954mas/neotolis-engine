#ifndef NT_UI_BAR_PROBE_H
#define NT_UI_BAR_PROBE_H

/* Reads a scrollbar back from what the last frame produced: its declared element and its render
   commands. The bar's id is the track; the thumb is the image drawn right after it. */

#include "clay.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_clay_impl.h"
#include "ui/nt_ui_internal.h"

typedef struct {
    bool drawn; /* the track reached the render commands */
    float track_len;
    float thumb_len;
    float thumb_off; /* along the track, from its start */
    float opacity;
    uint8_t layer;
} ui_bar_probe_t;

/* True when the bar's element was declared in the last finished frame. */
static inline bool ui_bar_declared(const nt_ui_context_t *ctx, uint32_t bar_id) {
    const int32_t slot = nt_ui_clay_priv_hashmap_slot_for_id(ctx->clay, bar_id);
    return slot >= 0 && ctx->hit_generation[slot] == ctx->current_generation;
}

static inline ui_bar_probe_t ui_bar_probe(const nt_ui_context_t *ctx, uint32_t bar_id, int axis) {
    ui_bar_probe_t out = {0};
    const Clay_RenderCommandArray *arr = &ctx->frozen_cmds;
    for (int32_t i = 0; i < arr->length; ++i) {
        const Clay_RenderCommand *track = &arr->internalArray[i];
        if (track->id != bar_id || track->commandType != CLAY_RENDER_COMMAND_TYPE_IMAGE) {
            continue;
        }
        const nt_ui_element_data_t *data = (const nt_ui_element_data_t *)track->userData;
        out.drawn = true;
        out.track_len = (axis == 1) ? track->boundingBox.height : track->boundingBox.width;
        out.opacity = data->opacity;
        out.layer = data->layer;
        for (int32_t k = i + 1; k < arr->length; ++k) {
            const Clay_RenderCommand *thumb = &arr->internalArray[k];
            if (thumb->commandType == CLAY_RENDER_COMMAND_TYPE_IMAGE) {
                out.thumb_len = (axis == 1) ? thumb->boundingBox.height : thumb->boundingBox.width;
                out.thumb_off = (axis == 1) ? (thumb->boundingBox.y - track->boundingBox.y) : (thumb->boundingBox.x - track->boundingBox.x);
                break;
            }
        }
        break;
    }
    return out;
}

#endif /* NT_UI_BAR_PROBE_H */
