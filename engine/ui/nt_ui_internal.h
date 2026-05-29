#ifndef NT_UI_INTERNAL_H
#define NT_UI_INTERNAL_H

/* Concrete layout of opaque nt_ui_context_t. */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "font/nt_font.h"
#include "input/nt_input.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_anim.h"

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

    /* Phase 56: frame pointer snapshot for engine-owned hit-test (D-56-04/19).
     * Copied each begin so get_interaction (called later in declaration) reads it. */
    nt_pointer_t frame_pointers[NT_INPUT_MAX_POINTERS];
    uint32_t frame_pointer_count;
    float frame_dt; /* dt passed to begin; anim cache lerp uses it (D-56-15) */

    /* Phase 56: declaration-time transform stack for the hit-test (Option A,
     * D-56-07). push/pop_transform maintain this live (mirrors the walk-local
     * nt_ui_walker_state_t but ctx-resident) so get_interaction can inverse-
     * transform the pointer at call time. The scale/rotation center is resolved
     * per-query from the widget's prev-frame bbox center (Clay_GetElementData).
     * Reset to depth 0 each nt_ui_begin. Kept separate from anim[] below. */
    nt_ui_transform_t accum_stack[NT_UI_TRANSFORM_STACK_DEPTH_CAP];
    uint32_t accum_depth;

    /* Phase 56: per-pointer capture state machine (D-56-04). v1.8 drives the
     * primary pointer (index 0); the array is multitouch-ready for v1.9.
     * capture_seen[] tracks which captures get_interaction touched this frame;
     * nt_ui_begin clears orphaned captures (queried-then-abandoned) using it.
     * pointer_over_any feeds nt_ui_wants_pointer (D-56-08). */
    nt_ui_capture_t captures[NT_INPUT_MAX_POINTERS];
    uint8_t capture_seen[NT_INPUT_MAX_POINTERS];
    bool pointer_over_any;

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
    /* Phase 55: per-type render-command counts, counted pre-emit (D-55-03). */
    uint32_t last_walk_rect_command_count;
    uint32_t last_walk_image_command_count;
    uint32_t last_walk_text_command_count;
    uint32_t last_walk_border_command_count;
    /* Phase 55: scissor command count + marker push counts (D-55-05, D-55-06) */
    uint32_t last_walk_scissor_command_count;
    uint32_t last_walk_max_scissor_depth;
    uint32_t last_walk_transform_pushes;
    uint32_t last_walk_opacity_pushes;
#ifdef NT_TEST_ACCESS
    uint32_t test_last_walk_unlayered_count;
#endif

    uint32_t max_elements;

    nt_font_t fonts[NT_UI_MAX_FONTS];

    nt_ui_anim_interaction_t anim[NT_UI_ANIM_SLOTS]; /* Phase 56 direct-mapped state-anim cache (D-56-15) */

    Clay_Arena clay_arena;
};

#endif /* NT_UI_INTERNAL_H */
