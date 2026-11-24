#ifndef NT_TEST_HELPER_UI_FX_FRAME_H
#define NT_TEST_HELPER_UI_FX_FRAME_H

/* Frame, pointer and render-command helpers shared by the nt_ui tests. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "clay.h"
#include "input/nt_input.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_FX_VIEW_W 800.0F
#define UI_FX_VIEW_H 600.0F
#define UI_FX_DT (1.0F / 60.0F) /* ui_fx_frame_begin's frame time; frame-count math derives from it */

/* An active pointer at (x, y) with the given left-button state. */
static inline nt_pointer_t ui_fx_pointer(float x, float y, bool down, bool pressed, bool released) {
    nt_pointer_t p = {.x = x, .y = y, .active = true};
    p.buttons[NT_BUTTON_LEFT] = (nt_button_state_t){.is_down = down, .is_pressed = pressed, .is_released = released};
    return p;
}

/* One 800x600 frame: publishes the input fed since the last frame, then begins with `count` pointers. */
static inline void ui_fx_frame_begin_ex(nt_ui_context_t *ctx, float dt, const nt_pointer_t *pointers, uint32_t count, uint32_t primary) {
    nt_input_poll();
    nt_ui_begin(ctx, &(nt_ui_frame_desc_t){.layout_w = UI_FX_VIEW_W, .layout_h = UI_FX_VIEW_H, .dt = dt, .pointers = pointers, .pointer_count = count, .primary = primary});
}

/* ui_fx_frame_begin_ex at 60 Hz with one pointer; NULL is an idle pointer at the origin. */
static inline void ui_fx_frame_begin(nt_ui_context_t *ctx, const nt_pointer_t *p) {
    const nt_pointer_t idle = {.active = true};
    ui_fx_frame_begin_ex(ctx, UI_FX_DT, p != NULL ? p : &idle, 1U, 0U);
}

/* The RECTANGLE command `id` emitted in the last ended frame, or NULL. Commands are in paint order, so a
 * later pointer paints above; zIndex is the band Clay stamped from the owning floating root. */
static inline const Clay_RenderCommand *ui_fx_find_rect(const nt_ui_context_t *ctx, uint32_t id) {
    for (int32_t i = 0; i < ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &ctx->frozen_cmds.internalArray[i];
        if (c->id == id && c->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
            return c;
        }
    }
    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* NT_TEST_HELPER_UI_FX_FRAME_H */
