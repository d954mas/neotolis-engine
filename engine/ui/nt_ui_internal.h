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
#include "ui/nt_ui_inspector.h"

/* At-cap is silently saturated (observability path). */
#ifndef NT_UI_DEBUG_ZONE_CAP
#define NT_UI_DEBUG_ZONE_CAP 64
#endif

/* Direct-mapped (id mod cap); power-of-two for the cheap modulo. */
#ifndef NT_UI_WIDGET_REGISTRY_CAP
#define NT_UI_WIDGET_REGISTRY_CAP 1024
#endif
_Static_assert((NT_UI_WIDGET_REGISTRY_CAP & (NT_UI_WIDGET_REGISTRY_CAP - 1)) == 0, "NT_UI_WIDGET_REGISTRY_CAP must be a power of two");

/* Persistent across frames; linear scan. */
#ifndef NT_UI_INSPECTOR_COLLAPSED_CAP
#define NT_UI_INSPECTOR_COLLAPSED_CAP 128
#endif

#ifndef NT_UI_TREE_DFS_DEPTH_CAP
#define NT_UI_TREE_DFS_DEPTH_CAP 256
#endif

typedef struct {
    uint32_t id;                   /* 0 = slot empty */
    const nt_ui_widget_def_t *def; /* NULL = slot empty (kept in sync with id==0) */
    uint8_t has_padding;
    uint8_t _pad[3];
    int16_t hit_padding_lrtb[4];
} nt_ui_widget_slot_t;

typedef struct {
    uint32_t id;
    /* Padded layout-space bbox (l/t/r/b), Clay Y-down. */
    float layout_l, layout_t, layout_r, layout_b;
    /* Exact visual bbox (unpadded). */
    float visual_l, visual_t, visual_r, visual_b;
    float aff_a, aff_b, aff_c, aff_d, aff_tx, aff_ty;
    /* Used by both rotation pivot and inverse-affine so draw matches hit-test. */
    float center_x, center_y;
    /* bit0 hovered, bit1 pressed, bit2 captured, bit3 disabled-heuristic. */
    uint16_t state_flags;
} nt_ui_debug_zone_t;

#define NT_UI_DEBUG_FLAG_HOVERED (1U << 0)
#define NT_UI_DEBUG_FLAG_PRESSED (1U << 1)
#define NT_UI_DEBUG_FLAG_CAPTURED (1U << 2)
#define NT_UI_DEBUG_FLAG_DISABLED (1U << 3)

/* If this grows, nt_ui_min_arena_size + create_context's offset cascade must update together. */
typedef struct {
    float a, b, c, d, tx, ty;
    float opacity;
    float _pad;
} nt_ui_baked_xform_t;
_Static_assert(sizeof(nt_ui_baked_xform_t) == 32, "nt_ui_baked_xform_t fixed at 32B");

typedef struct {
    int32_t elem_idx;
    float a, b, c, d, tx, ty;
    float opacity;
    int32_t children_cursor;
} nt_ui_dfs_frame_t;
_Static_assert(sizeof(nt_ui_dfs_frame_t) == 36, "nt_ui_dfs_frame_t fixed at 36B");

/* Identity baked xform — DFS seed + walker OOB fallback. */
static inline nt_ui_baked_xform_t nt_ui_internal_identity_baked(void) {
    return (nt_ui_baked_xform_t){
        .a = 1.0F,
        .b = 0.0F,
        .c = 0.0F,
        .d = 1.0F,
        .tx = 0.0F,
        .ty = 0.0F,
        .opacity = 1.0F,
        ._pad = 0.0F,
    };
}

/* Returns false on singular affine (det == 0). */
static inline bool nt_ui_internal_point_in_inverse_transformed_bbox(float px, float py, float a, float b, float c, float d, float tx, float ty, const Clay_BoundingBox *bbox) {
    const float det = (a * d) - (b * c);
    if (det == 0.0F) {
        return false;
    }
    const float inv_a = d / det;
    const float inv_b = -b / det;
    const float inv_c = -c / det;
    const float inv_d = a / det;
    const float rx = px - tx;
    const float ry = py - ty;
    const float lx = (inv_a * rx) + (inv_b * ry);
    const float ly = (inv_c * rx) + (inv_d * ry);
    return (lx >= bbox->x) && (lx < bbox->x + bbox->width) && (ly >= bbox->y) && (ly < bbox->y + bbox->height);
}

/* Lives at arena head; hot fields first. Per-ctx — no module globals. */
struct nt_ui_context {
    Clay_Context *clay;
    Clay_RenderCommandArray frozen_cmds;
    bool in_frame;

    nt_pointer_t frame_pointers[NT_INPUT_MAX_POINTERS];
    uint32_t frame_pointer_count;
    float frame_dt;

    /* capture_seen[] tracks who touched the capture this frame — orphans cleared on begin. */
    nt_ui_capture_t captures[NT_INPUT_MAX_POINTERS];
    uint8_t capture_seen[NT_INPUT_MAX_POINTERS];
    bool pointer_over_any;

    /* Buttons do not nest (asserted). */
    struct {
        bool active;
        bool clicked;
    } pending_button;

    /* nt_ui_walk asserts each is non-zero at entry. */
    nt_resource_t atlas;
    uint32_t white_region;
    nt_material_t sprite_material;
    nt_material_t text_material;
    nt_ui_custom_handler_t custom_fn;
    void *custom_user;

    /* Indexed by Clay layout element index — hot path, no hashmap lookup.
     * Live only between EndLayout and the next BeginLayout. */
    nt_ui_baked_xform_t *tree_baked;
    int32_t *tree_root_for_elem;
    nt_ui_dfs_frame_t *tree_dfs_stack;

    /* Indexed by Clay's PERSISTENT hashmap slot — survives BeginLayout (slot stable across frames).
     * hit_generation rejects stale ids Clay still holds but that weren't re-declared this frame. */
    nt_ui_baked_xform_t *hit_baked;
    uint32_t *hit_clip_parent_id;
    uint32_t *hit_generation;
    uint32_t current_generation;

    /* Per-walk metrics. */
    uint32_t last_walk_draw_call_delta;
    uint32_t last_walk_command_count;
    float last_layout_ms;
    float last_build_tree_ms;
    float last_walk_ms;
    uint32_t last_walk_rect_command_count;
    uint32_t last_walk_image_command_count;
    uint32_t last_walk_text_command_count;
    uint32_t last_walk_border_command_count;
    uint32_t last_walk_scissor_command_count;
    uint32_t last_walk_max_scissor_depth;
#ifdef NT_TEST_ACCESS
    uint32_t test_last_walk_unlayered_count;
#endif

    uint32_t max_elements;

    /* nt_mem_scratch_used snapshot at nt_ui_end; nt_ui_walk asserts it hasn't shrunk
     * (caller reset scratch between end and walk → dangling payloads in Clay). */
    size_t scratch_used_at_end;

    nt_font_t fonts[NT_UI_MAX_FONTS];

    nt_ui_anim_interaction_t anim[NT_UI_ANIM_SLOTS];
    /* Monotonic; nonzero delta across frames means raise NT_UI_ANIM_SLOTS. */
    uint32_t anim_collision_count;

#if NT_UI_DEBUG_TOOLS
    /* At-cap pushes silently dropped (observability). */
    nt_ui_debug_zone_t debug_zones[NT_UI_DEBUG_ZONE_CAP];
    uint32_t debug_zone_count;
    bool debug_recording;

    nt_ui_widget_slot_t widget_registry[NT_UI_WIDGET_REGISTRY_CAP];

    /* highlight_id resets each begin; selected_id persists across frames. */
    bool inspector_active;
    uint32_t inspector_highlight_id;
    uint32_t inspector_selected_id;
    /* Pointer inside sidebar footprint — gates step_interaction to zeroed return. */
    bool inspector_pointer_consumed;

    uint32_t inspector_collapsed_ids[NT_UI_INSPECTOR_COLLAPSED_CAP];
    uint32_t inspector_collapsed_count;

    nt_ui_inspector_metrics_t inspector_metrics;
#endif /* NT_UI_DEBUG_TOOLS */

    Clay_Arena clay_arena;
};

typedef struct nt_ui_inspector_element_view {
    uint32_t id; /* Clay-assigned id, 0 if invalid index */
    float x, y;  /* layout bbox top-left (Clay Y-down) */
    float w, h;
} nt_ui_inspector_element_view_t;

int32_t nt_ui_internal_get_layout_element_count(const nt_ui_context_t *ctx);
nt_ui_inspector_element_view_t nt_ui_internal_get_layout_element_view(const nt_ui_context_t *ctx, int32_t index);

/* Top of openLayoutElementStack; 0 if no ctx is in-frame. */
uint32_t nt_ui_internal_current_open_element_id(void);

/* Call IMMEDIATELY after CLAY_TEXT — text leaf is appended but not pushed on the open stack. */
uint32_t nt_ui_internal_last_emitted_element_id(void);

/* Flat row borrowing id_string from Clay (valid through next nt_ui_begin). */
typedef struct nt_ui_inspector_tree_row {
    const char *id_string;
    const char *text_chars;
    uint32_t id;
    float bbox_x, bbox_y, bbox_w, bbox_h;
    uint16_t id_string_len;
    uint16_t text_len;
    uint8_t depth;
    uint8_t config_mask; /* bit0=Shared bit1=Text bit2=Aspect bit3=Image bit4=Floating bit5=Clip bit6=Border bit7=Custom */
    uint8_t offscreen;
    uint8_t is_text;
} nt_ui_inspector_tree_row_t;

/* ctx must have just completed nt_ui_end. */
int32_t nt_ui_internal_collect_tree_rows(const nt_ui_context_t *ctx, nt_ui_inspector_tree_row_t *out, int32_t out_cap);

typedef struct nt_ui_inspector_element_info {
    bool found;
    float bbox_x, bbox_y, bbox_w, bbox_h;
    uint8_t layout_direction; /* matches Clay_LayoutDirection */
    uint16_t padding_l, padding_r, padding_t, padding_b;
    uint16_t child_gap;
    uint8_t child_align_x;
    uint8_t child_align_y;
    const char *id_string;
    uint16_t id_string_len;
    uint8_t config_mask;
    float bg_r, bg_g, bg_b, bg_a;
    float corner_tl, corner_tr, corner_bl, corner_br;
    uint16_t text_font_size;
    uint16_t text_font_id;
    float text_color_r, text_color_g, text_color_b, text_color_a;
    uint8_t text_align; /* 0=LEFT 1=CENTER 2=RIGHT */
} nt_ui_inspector_element_info_t;

nt_ui_inspector_element_info_t nt_ui_internal_get_element_info(const nt_ui_context_t *ctx, uint32_t id);

/* Caller (nt_ui_end) must run inside the Clay current-ctx scope. */
void nt_ui_internal_build_tree(nt_ui_context_t *ctx);

#ifdef NT_TEST_ACCESS
const nt_ui_baked_xform_t *nt_ui_internal_test_get_tree_baked(const nt_ui_context_t *ctx, int32_t elem_idx);
int32_t nt_ui_internal_test_get_tree_baked_count(const nt_ui_context_t *ctx);
int32_t nt_ui_internal_test_get_tree_root_for_elem(const nt_ui_context_t *ctx, int32_t elem_idx);
#endif

/* Shared overlay helpers — single source of truth for the Y-flip + per-level accum convention. */
const nt_ui_debug_zone_t *nt_ui_internal_find_debug_zone(const nt_ui_context_t *ctx, uint32_t id);

void nt_ui_internal_project_layout_to_world(const nt_ui_debug_zone_t *z, float vy, float vh, float x, float y, float *out_x, float *out_y);

void nt_ui_internal_emit_filled_quad(nt_resource_t atlas, uint32_t region, const float v[4][2], uint32_t color);

void nt_ui_internal_emit_outline(nt_resource_t atlas, uint32_t region, const float c[4][2], float thickness, uint32_t color);

/* (x,y) top-left, (wp,hp) size in logical layout pixels. Caller wraps in scissor_enabled(true/false). */
void nt_ui_internal_apply_scissor_logical_to_physical(const nt_ui_target_t *target, int x, int y, int wp, int hp);

#endif /* NT_UI_INTERNAL_H */
