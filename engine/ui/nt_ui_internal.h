#ifndef NT_UI_INTERNAL_H
#define NT_UI_INTERNAL_H

/* Concrete layout of opaque nt_ui_context_t. */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "font/nt_font.h"
#include "ui/nt_ui.h"

/* Side-channel transform/opacity marker (not a Clay element). */
typedef struct {
    uint8_t type;
    uint32_t before_clay_idx;
    union {
        nt_ui_transform_t transform; /* PUSH_TRANSFORM only */
        float opacity;               /* PUSH_OPACITY only */
    };
} nt_ui_marker_t;

/* Lives at arena head; hot fields first. Per-ctx -- no module globals. */
struct nt_ui_context {
    Clay_Context *clay;
    Clay_RenderCommandArray frozen_cmds; /* set by end, read by walk */
    bool in_frame;
    bool debug_overlay; /* applied to Clay before BeginLayout in nt_ui_begin */

    /* Walker bindings -- nt_ui_walk asserts each is non-zero at entry. */
    nt_resource_t atlas;
    uint32_t white_region;
    nt_material_t sprite_material;
    nt_material_t text_material;
    nt_ui_custom_handler_t custom_fn;
    void *custom_user;

    /* Side-channel markers: push/pop transform/opacity without Clay elements.
     * Markers record before_clay_idx = number of Clay elements declared before
     * this marker. Walker pre-pass interleaves markers with Clay commands. */
    nt_ui_marker_t *markers; /* allocated from arena at create_context */
    uint32_t marker_count;
    uint32_t max_markers;

    /* Per-walk metrics. Walker writes; nt_ui_get_last_walk_* reads. */
    uint32_t last_walk_draw_call_delta;
    uint32_t last_walk_command_count;
    /* Phase 55: CPU timing (D-55-01, D-55-02) */
    float last_layout_ms;
    float last_walk_ms;
    /* Phase 55: per-type element counters (D-55-03) */
    uint32_t last_walk_rect_count;
    uint32_t last_walk_image_count;
    uint32_t last_walk_text_count;
    uint32_t last_walk_border_count;
    /* Phase 55: scissor/marker counters (D-55-05, D-55-06) */
    uint32_t last_walk_scissor_count;
    uint32_t last_walk_max_scissor_depth;
    uint32_t last_walk_transform_pushes;
    uint32_t last_walk_opacity_pushes;
#ifdef NT_TEST_ACCESS
    uint32_t test_last_walk_unlayered_count;
#endif

    uint32_t max_elements;

    nt_font_t fonts[NT_UI_MAX_FONTS];

    Clay_Arena clay_arena;
};

#endif /* NT_UI_INTERNAL_H */
