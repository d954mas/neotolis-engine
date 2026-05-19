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
 * remainder of the arena is owned by Clay (via Clay_Initialize). */
struct nt_ui_context {
    Clay_Context *clay;                  /* returned by Clay_Initialize */
    Clay_RenderCommandArray frozen_cmds; /* set by nt_ui_end, read by walk */
    bool in_frame;                       /* between begin and end */
    nt_font_t fonts[NT_UI_MAX_FONTS];    /* per-ctx font registry (D-52-15) */
    Clay_Arena clay_arena;               /* opaque arena descriptor */
    void *arena_base;                    /* caller's arena pointer */
    size_t arena_size;                   /* caller's arena size */
};

#endif /* NT_UI_INTERNAL_H */
