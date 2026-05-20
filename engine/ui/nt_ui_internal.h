#ifndef NT_UI_INTERNAL_H
#define NT_UI_INTERNAL_H

/*
 * Concrete struct nt_ui_context layout. The public header forward-declares
 * the type as opaque; only nt_ui.c and NT_TEST_ACCESS test TUs see this.
 */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "font/nt_font.h"
#include "ui/nt_ui.h"

/* Lives in the first ~256 bytes of the caller-owned arena (Clay owns the
 * rest). Hot fields first for cache locality. All walker state is per-ctx
 * -- no module globals -- so multi-context UIs are correct without state
 * swaps between walks. */
struct nt_ui_context {
    Clay_Context *clay;
    Clay_RenderCommandArray frozen_cmds; /* set by end, read by walk */
    bool in_frame;

    /* Walker bindings -- nt_ui_walk asserts each is non-zero at entry. */
    nt_resource_t atlas;
    uint32_t white_region;
    nt_material_t sprite_material;
    nt_material_t text_material;
    nt_ui_custom_handler_t custom_fn;
    void *custom_user;

    /* Per-walk metrics. Walker writes; nt_ui_get_last_walk_* reads. */
    uint32_t last_walk_draw_call_delta;
    uint32_t last_walk_element_count;

    nt_font_t fonts[NT_UI_MAX_FONTS];

    Clay_Arena clay_arena;
};

#endif /* NT_UI_INTERNAL_H */
