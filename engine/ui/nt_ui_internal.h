#ifndef NT_UI_INTERNAL_H
#define NT_UI_INTERNAL_H

/* Concrete layout of opaque nt_ui_context_t. */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "font/nt_font.h"
#include "ui/nt_ui.h"

/* Lives at arena head; hot fields first. Per-ctx -- no module globals. */
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
#ifdef NT_TEST_ACCESS
    uint32_t test_last_walk_unlayered_count;
#endif

    /* Layer-sort scratch buffer (uint16 indices into frozen_cmds). Lives
     * in the same caller-owned arena right after the ctx struct, sized to
     * fit one whole segment worst case (== max_elements). */
    uint16_t *sorted;
    uint32_t max_elements;

    nt_font_t fonts[NT_UI_MAX_FONTS];

    Clay_Arena clay_arena;
};

#endif /* NT_UI_INTERNAL_H */
