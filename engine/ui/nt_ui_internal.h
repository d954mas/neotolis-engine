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

/* Hit-zone debug overlay cap. At-cap is silently saturated (observability path). */
#ifndef NT_UI_DEBUG_ZONE_CAP
#define NT_UI_DEBUG_ZONE_CAP 64
#endif

/* Widget_registry capacity. Direct-mapped (id mod cap); collisions replace.
 * Power-of-two for the cheap modulo. */
#ifndef NT_UI_WIDGET_REGISTRY_CAP
#define NT_UI_WIDGET_REGISTRY_CAP 1024
#endif
_Static_assert((NT_UI_WIDGET_REGISTRY_CAP & (NT_UI_WIDGET_REGISTRY_CAP - 1)) == 0, "NT_UI_WIDGET_REGISTRY_CAP must be a power of two");

/* Inspector collapsed-element set. Persistent across frames; linear scan. */
#ifndef NT_UI_INSPECTOR_COLLAPSED_CAP
#define NT_UI_INSPECTOR_COLLAPSED_CAP 128
#endif

/* DFS depth budget for the post-EndLayout build pass. Matches inspector's STACK_CAP. */
#ifndef NT_UI_TREE_DFS_DEPTH_CAP
#define NT_UI_TREE_DFS_DEPTH_CAP 256
#endif

typedef struct {
    uint32_t id;                   /* 0 = slot empty */
    const nt_ui_widget_def_t *def; /* NULL = slot empty (kept in sync with id==0) */
    uint8_t has_padding;           /* 0 = none recorded, 1 = hit_padding_lrtb is valid */
    uint8_t _pad[3];
    int16_t hit_padding_lrtb[4]; /* layout-space padding {l,r,t,b}; matches nt_ui_button_style_t */
} nt_ui_widget_slot_t;

typedef struct {
    uint32_t id;
    /* Padded layout-space bbox (l/t/r/b), Clay Y-down. l<r, t<b. */
    float layout_l, layout_t, layout_r, layout_b;
    /* Exact visual bbox (unpadded), so the overlay can outline padding distinctly. */
    float visual_l, visual_t, visual_r, visual_b;
    /* Composed affine snapshot — matches tree_baked[] layout. */
    float aff_a, aff_b, aff_c, aff_d, aff_tx, aff_ty;
    /* VISUAL bbox center — used by both rotation and inverse-affine so the
     * draw path matches the hit-test math. */
    float center_x, center_y;
    /* bit0 hovered, bit1 pressed, bit2 captured, bit3 disabled-heuristic. */
    uint16_t state_flags;
} nt_ui_debug_zone_t;

#define NT_UI_DEBUG_FLAG_HOVERED (1U << 0)
#define NT_UI_DEBUG_FLAG_PRESSED (1U << 1)
#define NT_UI_DEBUG_FLAG_CAPTURED (1U << 2)
#define NT_UI_DEBUG_FLAG_DISABLED (1U << 3)

/* Per-element composed accum. Same struct used twice: tree_baked (indexed by
 * layout idx, drives walker) and hit_baked (indexed by Clay hashmap slot, drives
 * hit-test). nt_ui_min_arena_size + create_context both count
 * `sizeof(nt_ui_baked_xform_t) * max_elements` × 2; if this struct grows, both
 * formulas + the offset cascade in nt_ui_create_context must update together. */
typedef struct {
    float a, b, c, d, tx, ty;
    float opacity;
    float _pad;
} nt_ui_baked_xform_t;
_Static_assert(sizeof(nt_ui_baked_xform_t) == 32, "nt_ui_baked_xform_t fixed at 32B (a,b,c,d,tx,ty,opacity, pad to 8B alignment)");

/* DFS frame for build_tree's traversal — full affine + opacity, no decomposition. */
typedef struct {
    int32_t elem_idx;
    float a, b, c, d, tx, ty;
    float opacity;
    int32_t children_cursor;
} nt_ui_dfs_frame_t;
/* 4 (elem_idx) + 24 (a..ty) + 4 (opacity) + 4 (cursor) = 36B. Aligned to 4B = 36B. */
_Static_assert(sizeof(nt_ui_dfs_frame_t) == 36, "nt_ui_dfs_frame_t fixed at 36B");

/* Header-only helper — identity baked xform (passed to dfs seed + walker OOB). */
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

/* Inverse-affine pointer test against axis-aligned bbox in the element's own
 * frame. Returns false on singular affine (det == 0). Shared by hit-test paths. */
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

/* Lives at arena head; hot fields first. Per-ctx -- no module globals. */
struct nt_ui_context {
    Clay_Context *clay;
    Clay_RenderCommandArray frozen_cmds; /* set by end, read by walk */
    bool in_frame;

    /* Per-frame pointer snapshot read by get_interaction during declaration. */
    nt_pointer_t frame_pointers[NT_INPUT_MAX_POINTERS];
    uint32_t frame_pointer_count;
    float frame_dt; /* dt passed to begin; anim cache lerp uses it. */

    /* Per-pointer capture state machine. v1.8 drives pointers[0].
     * capture_seen[] tracks which captures get_interaction touched this frame
     * — orphans cleared on nt_ui_begin. pointer_over_any feeds wants_pointer. */
    nt_ui_capture_t captures[NT_INPUT_MAX_POINTERS];
    uint8_t capture_seen[NT_INPUT_MAX_POINTERS];
    bool pointer_over_any;

    /* Carries get_interaction result from button_begin to button_end.
     * Single slot — buttons do not nest (asserted). */
    struct {
        bool active;
        bool clicked;
    } pending_button;

    /* Walker bindings -- nt_ui_walk asserts each is non-zero at entry. */
    nt_resource_t atlas;
    uint32_t white_region;
    nt_material_t sprite_material;
    nt_material_t text_material;
    nt_ui_custom_handler_t custom_fn;
    void *custom_user;

    /* Per-element composed accum + clip parent, indexed by Clay LAYOUT element index.
     * Walker reads tree_baked[c->nt_layout_index] (hot path — no hashmap lookup).
     * tree_root_for_elem maps element idx → tree root idx (-1 if not a root).
     * tree_dfs_stack is the iterative DFS scratch (NT_UI_TREE_DFS_DEPTH_CAP frames).
     * Live only between EndLayout and the next BeginLayout. */
    nt_ui_baked_xform_t *tree_baked;
    int32_t *tree_root_for_elem;
    nt_ui_dfs_frame_t *tree_dfs_stack;

    /* Per-id snapshot indexed by Clay's PERSISTENT hashmap slot. Survives the
     * next BeginLayout — hit-test in frame N+1 reads frame N's data even if the
     * widget moved to a different layout idx (e.g., game called step_interaction
     * AFTER its CLAY({.id=X}) declaration shifted X's layout idx).
     * hit_generation pins each slot to the build_tree pass that wrote it; a
     * stale id (declared once, never re-declared) reads != current_generation
     * and is rejected by hit-test even though Clay's hashmap still has it. */
    nt_ui_baked_xform_t *hit_baked;
    uint32_t *hit_clip_parent_id;
    uint32_t *hit_generation;
    uint32_t current_generation;

    /* Per-walk metrics. Walker writes; nt_ui_get_last_walk_* reads. */
    uint32_t last_walk_draw_call_delta;
    uint32_t last_walk_command_count;
    /* CPU timing (ms). */
    float last_layout_ms;
    float last_build_tree_ms; /* build_tree pass timing. */
    float last_walk_ms;
    /* Per-type render-command counts, counted pre-emit. */
    uint32_t last_walk_rect_command_count;
    uint32_t last_walk_image_command_count;
    uint32_t last_walk_text_command_count;
    uint32_t last_walk_border_command_count;
    /* Scissor counts. */
    uint32_t last_walk_scissor_command_count;
    uint32_t last_walk_max_scissor_depth;
#ifdef NT_TEST_ACCESS
    uint32_t test_last_walk_unlayered_count;
#endif

    uint32_t max_elements;

    nt_font_t fonts[NT_UI_MAX_FONTS];

    nt_ui_anim_interaction_t anim[NT_UI_ANIM_SLOTS]; /* direct-mapped state-anim cache */

#if NT_UI_DEBUG_TOOLS
    /* Hit-zone debug overlay. OFF by default; game opts in via debug_set_recording.
     * Zones cleared each nt_ui_begin; at-cap pushes silently dropped. */
    nt_ui_debug_zone_t debug_zones[NT_UI_DEBUG_ZONE_CAP];
    uint32_t debug_zone_count;
    bool debug_recording;

    /* Per-frame widget-tag registry. Cleared each begin (id=0 every slot). */
    nt_ui_widget_slot_t widget_registry[NT_UI_WIDGET_REGISTRY_CAP];

    /* When active, nt_ui_end emits the Clay debug view into the layout pass.
     * highlight_id resets each begin and is recomputed during emit_layout;
     * selected_id persists across frames (sidebar click). */
    bool inspector_active;
    uint32_t inspector_highlight_id;
    uint32_t inspector_selected_id;
    /* True when the pointer is inside the sidebar footprint — gates user-widget
     * step_interaction to a zeroed return and makes wants_pointer report true. */
    bool inspector_pointer_consumed;

    /* Persistent collapsed-id set; toggled by the per-row dot click. */
    uint32_t inspector_collapsed_ids[NT_UI_INSPECTOR_COLLAPSED_CAP];
    uint32_t inspector_collapsed_count;

    /* Runtime inspector sizing — read by the layout emit (nt_ui_clay_internal.c)
     * and the post-walk overlay scissor (nt_ui_inspector.c). */
    nt_ui_inspector_metrics_t inspector_metrics;
#endif /* NT_UI_DEBUG_TOOLS */

    Clay_Arena clay_arena;
};

/* Thin accessors to Clay's layoutElements for the inspector TU. */
typedef struct nt_ui_inspector_element_view {
    uint32_t id; /* Clay-assigned id, 0 if invalid index */
    float x, y;  /* layout bbox top-left (Clay Y-down) */
    float w, h;
} nt_ui_inspector_element_view_t;

int32_t nt_ui_internal_get_layout_element_count(const nt_ui_context_t *ctx);
nt_ui_inspector_element_view_t nt_ui_internal_get_layout_element_view(const nt_ui_context_t *ctx, int32_t index);

/* Id of the currently-open Clay element (top of openLayoutElementStack).
 * Returns 0 if no ctx is in-frame. */
uint32_t nt_ui_internal_current_open_element_id(void);

/* Id of the LAST layout element added — call IMMEDIATELY after CLAY_TEXT
 * (the text leaf is added at the array tail but not pushed on the open stack). */
uint32_t nt_ui_internal_last_emitted_element_id(void);

/* DFS pre-order tree row for the inspector — flat shape so the inspector
 * TU never touches Clay_LayoutElement directly. id_string is borrowed from
 * Clay's layoutElementIdStrings (lifetime through next nt_ui_begin). */
typedef struct nt_ui_inspector_tree_row {
    const char *id_string;  /* NUL-not-guaranteed; see id_string_len. */
    const char *text_chars; /* NULL unless is_text. */
    uint32_t id;
    float bbox_x, bbox_y, bbox_w, bbox_h; /* Clay Y-down layout bbox */
    uint16_t id_string_len;
    uint16_t text_len;
    uint8_t depth;
    uint8_t config_mask; /* bit0=Shared bit1=Text bit2=Aspect bit3=Image bit4=Floating bit5=Clip bit6=Border bit7=Custom */
    uint8_t offscreen;
    uint8_t is_text; /* element has CLAY__ELEMENT_CONFIG_TYPE_TEXT */
} nt_ui_inspector_tree_row_t;

/* Fill out[] with up to out_cap pre-order rows; returns count. ctx must have
 * just completed nt_ui_end (Clay_EndLayout invoked). Restores Clay context on exit. */
int32_t nt_ui_internal_collect_tree_rows(const nt_ui_context_t *ctx, nt_ui_inspector_tree_row_t *out, int32_t out_cap);

/* Layout config of a single element (for the element-info pane). Looked up by
 * the Clay-assigned id (e.g. from a tree row). found = false if unknown. */
typedef struct nt_ui_inspector_element_info {
    bool found;
    /* Bounding box (Clay Y-down). */
    float bbox_x, bbox_y, bbox_w, bbox_h;
    /* Layout config bits. */
    uint8_t layout_direction; /* 0=LTR 1=TTB (matches Clay_LayoutDirection enum) */
    uint16_t padding_l, padding_r, padding_t, padding_b;
    uint16_t child_gap;
    uint8_t child_align_x; /* 0=L 1=C 2=R */
    uint8_t child_align_y; /* 0=T 1=C 2=B */
    /* Element-id string (borrowed). */
    const char *id_string;
    uint16_t id_string_len;
    uint8_t config_mask;
    /* For SHARED config: background RGBA + corner-radius. */
    float bg_r, bg_g, bg_b, bg_a;
    float corner_tl, corner_tr, corner_bl, corner_br;
    /* For TEXT config: font_size, color, alignment label. */
    uint16_t text_font_size;
    uint16_t text_font_id;
    float text_color_r, text_color_g, text_color_b, text_color_a;
    uint8_t text_align; /* 0=LEFT 1=CENTER 2=RIGHT */
} nt_ui_inspector_element_info_t;

nt_ui_inspector_element_info_t nt_ui_internal_get_element_info(const nt_ui_context_t *ctx, uint32_t id);

/* Composes tree_baked + hit_baked + hit_clip_parent_id for the current frame.
 * Floating root seeds: parentId=ROOT → identity, =PARENT → lexical parent,
 * =ELEMENT_WITH_ID → arbitrary. Also marks synthetic SCISSOR_START commands
 * with nt_layout_index = -1 so the walker reads identity. Caller (nt_ui_end)
 * must run inside the Clay current-ctx scope. */
void nt_ui_internal_build_tree(nt_ui_context_t *ctx);

#ifdef NT_TEST_ACCESS
const nt_ui_baked_xform_t *nt_ui_internal_test_get_tree_baked(const nt_ui_context_t *ctx, int32_t elem_idx);
int32_t nt_ui_internal_test_get_tree_baked_count(const nt_ui_context_t *ctx);
int32_t nt_ui_internal_test_get_tree_root_for_elem(const nt_ui_context_t *ctx, int32_t elem_idx);
#endif

/* Shared overlay helpers used by both nt_ui_debug_draw_hit_zones and
 * nt_ui_inspector_overlay_draw — keeps the Y-flip + per-level accum
 * convention as one source of truth.
 *   - project_layout_to_world: project through z's accum (NON-negated rotation,
 *     no per-level flip — matches what step_interaction recorded), then apply
 *     the walker's single GL Y-flip.
 *   - emit_filled_quad / emit_outline: sprite emits in WORLD space. */
const nt_ui_debug_zone_t *nt_ui_internal_find_debug_zone(const nt_ui_context_t *ctx, uint32_t id);

void nt_ui_internal_project_layout_to_world(const nt_ui_debug_zone_t *z, float vy, float vh, float x, float y, float *out_x, float *out_y);

void nt_ui_internal_emit_filled_quad(nt_resource_t atlas, uint32_t region, const float v[4][2], uint32_t color);

void nt_ui_internal_emit_outline(nt_resource_t atlas, uint32_t region, const float c[4][2], float thickness, uint32_t color);

/* Logical->physical scissor (shared with the walker so they agree on every
 * target shape). (x,y) top-left, (wp,hp) size — all in logical layout pixels.
 * Caller must wrap in scissor_enabled(true)/scissor_enabled(false). */
void nt_ui_internal_apply_scissor_logical_to_physical(const nt_ui_target_t *target, int x, int y, int wp, int hp);

#endif /* NT_UI_INTERNAL_H */
