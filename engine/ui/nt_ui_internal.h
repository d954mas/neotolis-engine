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

/* Hit-test clip stack capacity. Real UIs nest 1-3 levels; 16 = 5x headroom. */
#ifndef NT_UI_CLIP_STACK_CAP
#define NT_UI_CLIP_STACK_CAP 16
#endif

typedef struct {
    /* Layout-space rect (Clay Y-down). */
    float x, y, w, h;
    /* Affine snapshot at push time. Same math as ui_hit_test (compose_transform_level
     * walking the accum_stack at the clip rect's center). */
    float accum_a, accum_b, accum_c, accum_d, accum_tx, accum_ty;
} nt_ui_clip_entry_t;

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
    /* Declaration-time transform stack snapshot at query time. */
    nt_ui_transform_t accum[NT_UI_TRANSFORM_STACK_DEPTH_CAP];
    uint32_t accum_depth;
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

/* Side-channel transform/opacity marker (not a Clay element). */
typedef struct {
    uint8_t type;
    uint32_t before_clay_idx;
    union {
        nt_ui_transform_t transform; /* PUSH_TRANSFORM only */
        float opacity;               /* PUSH_OPACITY only */
    };
} nt_ui_marker_t;

/* Walker pre-pass per-command baked transform. Preallocated in ctx so
 * the walker hot path never hits the scratch arena. */
typedef struct {
    float a, b, c, d, tx, ty;
    float scale_x, scale_y, rotation, opacity;
} nt_ui_baked_xform_t;

/* Lives at arena head; hot fields first. Per-ctx -- no module globals. */
struct nt_ui_context {
    Clay_Context *clay;
    Clay_RenderCommandArray frozen_cmds; /* set by end, read by walk */
    bool in_frame;

    /* Per-frame pointer snapshot read by get_interaction during declaration. */
    nt_pointer_t frame_pointers[NT_INPUT_MAX_POINTERS];
    uint32_t frame_pointer_count;
    float frame_dt; /* dt passed to begin; anim cache lerp uses it. */

    /* Declaration-time transform stack for the hit-test. push/pop_transform
     * maintain it live; get_interaction inverse-transforms the pointer here.
     * Rotation/scale center resolved per-query from prev-frame bbox center. */
    nt_ui_transform_t accum_stack[NT_UI_TRANSFORM_STACK_DEPTH_CAP];
    uint32_t accum_depth;

    /* Declaration-time clip stack. push_clip captures the current transform
     * accum at the clip rect's center; ui_hit_test walks this BEFORE the
     * widget inverse-affine. Reset depth=0 each begin; end asserts balanced. */
    nt_ui_clip_entry_t clip_stack[NT_UI_CLIP_STACK_CAP];
    uint32_t clip_depth;

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

    /* Side-channel markers: push/pop transform/opacity without Clay elements.
     * Markers record before_clay_idx = number of Clay elements declared before
     * this marker. Walker pre-pass interleaves markers with Clay commands. */
    nt_ui_marker_t *markers; /* allocated from arena at create_context */
    uint32_t marker_count;
    uint32_t max_markers;

    /* Walker pre-pass scratch — preallocated, max_elements-sized so the hot
     * path never touches the per-frame scratch arena. */
    nt_ui_baked_xform_t *walker_baked;
    int32_t *walker_sorted;

    /* Per-walk metrics. Walker writes; nt_ui_get_last_walk_* reads. */
    uint32_t last_walk_draw_call_delta;
    uint32_t last_walk_command_count;
    /* CPU timing (ms). */
    float last_layout_ms;
    float last_walk_ms;
    /* Per-type render-command counts, counted pre-emit. */
    uint32_t last_walk_rect_command_count;
    uint32_t last_walk_image_command_count;
    uint32_t last_walk_text_command_count;
    uint32_t last_walk_border_command_count;
    /* Scissor + marker push counts. */
    uint32_t last_walk_scissor_command_count;
    uint32_t last_walk_max_scissor_depth;
    uint32_t last_walk_transform_pushes;
    uint32_t last_walk_opacity_pushes;
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

/* Declaration-time transform composer — single source of truth shared by
 * walker, hit-test, and inspector hover. local = T(O) * T(C) * R(θ)*S * T(-C). */
void nt_ui_internal_compose_transform_level(const nt_ui_transform_t *t, float cx, float cy, float *a, float *b, float *c, float *d, float *tx, float *ty);

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
