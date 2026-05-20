#ifndef NT_UI_INTERNAL_H
#define NT_UI_INTERNAL_H

/*
 * nt_ui internal definitions.
 *
 * This header exposes struct nt_ui_context to TU code that needs the
 * concrete layout (nt_ui.c itself, and tests compiled with
 * NT_UI_TEST_ACCESS). The public header (engine/ui/nt_ui.h) only
 * forward-declares the type as opaque.
 */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "font/nt_font.h"
#include "ui/nt_ui.h"

/* Concrete context layout (D-52-10 / D-52-15).
 *
 * Lives in the first ~256 bytes of the caller-provided arena. Hot fields
 * (clay, frozen_cmds, in_frame) come first for cache locality. The
 * remainder of the arena is owned by Clay (via Clay_Initialize).
 *
 * All walker state (atlas, materials, custom handler, walker stats) is
 * per-context -- no module globals. This makes "one ctx, many walks
 * against different targets" trivially correct and lets Phase 53 themes
 * own their state cleanly. */
struct nt_ui_context {
    /* --- Hot frame fields --- */
    Clay_Context *clay;                  /* returned by Clay_Initialize */
    Clay_RenderCommandArray frozen_cmds; /* set by nt_ui_end, read by walk */
    bool in_frame;                       /* between begin and end */

    /* --- Walker bindings (set by nt_ui_set_*; walker asserts non-zero) --- */
    nt_resource_t atlas;
    uint32_t white_region;
    nt_material_t sprite_material;
    nt_material_t text_material;
    nt_ui_custom_handler_t custom_fn;
    void *custom_user;

    /* --- Per-walk stats (D-52-20 / WALK-09); also routed to nt_stats. --- */
    uint32_t last_walk_draw_call_delta;
    uint32_t last_walk_element_count;

    /* --- Per-ctx font registry (D-52-15) --- */
    nt_font_t fonts[NT_UI_MAX_FONTS];

    /* --- Arena bookkeeping --- */
    Clay_Arena clay_arena; /* opaque arena descriptor */
    void *arena_base;      /* caller's arena pointer */
    size_t arena_size;     /* caller's arena size */
};

#endif /* NT_UI_INTERNAL_H */
