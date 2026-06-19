#ifndef NT_UI_INTERNAL_H
#define NT_UI_INTERNAL_H

/* Concrete layout of opaque nt_ui_context_t. */

#include <stdbool.h>
#include <stdint.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "font/nt_font.h"
#include "input/nt_input.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_inspector.h"
#include "ui/nt_ui_menu.h" /* NT_UI_MENU_MAX_DEPTH + nt_ui_menu_state_t for the immediate menu depth stack */
#include "ui/nt_ui_state.h"

/* Depth (not count) — independent of max_elements; deep nests are rare. */
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
    /* Full composed mat4 (column-major); overlay extracts the top-left 2×2 + col3 (a=m[0],
     * b=m[4], c=m[1], d=m[5], tx=m[12], ty=m[13]) for the planar highlight box. For 3D ctx
     * widgets with rotation_x/y or offset_z this is a 2D-projection approximation — overlay
     * still draws something, just not the rotated-out-of-plane outline. Full 3D-pose overlay
     * would need view_proj-projected corners; not implemented (debug-only path). */
    float m[16];
    /* Used by both rotation pivot and inverse-affine so draw matches hit-test. */
    float center_x, center_y;
    /* bit0 hovered, bit1 pressed, bit2 captured, bit3 disabled-heuristic. */
    uint16_t state_flags;
} nt_ui_debug_zone_t;

#define NT_UI_DEBUG_FLAG_HOVERED (1U << 0)
#define NT_UI_DEBUG_FLAG_PRESSED (1U << 1)
#define NT_UI_DEBUG_FLAG_CAPTURED (1U << 2)
#define NT_UI_DEBUG_FLAG_DISABLED (1U << 3)

/* Column-major mat4 storage. 2D consumers extract a/b/c/d/tx/ty via the helpers below.
 * If this grows, nt_ui_min_arena_size + create_context's offset cascade must update together. */
typedef struct {
    float m[16];
    float opacity;
    uint16_t hierarchy_depth;
    int16_t zindex; /* effective Clay zIndex (the owning floating tree-root's); 0 for base content. */
    float _pad[2];
} nt_ui_baked_xform_t;
_Static_assert(sizeof(nt_ui_baked_xform_t) == 80, "nt_ui_baked_xform_t fixed at 80B");

typedef struct {
    int32_t elem_idx;
    float m[16];
    float opacity;
    uint16_t hierarchy_depth;
    uint16_t _reserved;
    int32_t children_cursor;
} nt_ui_dfs_frame_t;
_Static_assert(sizeof(nt_ui_dfs_frame_t) == 80, "nt_ui_dfs_frame_t fixed at 80B");

/* 2D affine extraction from column-major mat4 (matches [a b tx; c d ty]·[x;y;1] applied to (x,y,0,1)):
 *   a = m[0], b = m[4], c = m[1], d = m[5], tx = m[12], ty = m[13]
 * Callers (walker, hit-test, debug_zone fill) inline this indexing directly. */

/* Packed 0xAABBGGRR -> Clay_Color (0..255), literal (no sentinel). Callers that
 * treat 0xFFFFFFFF as "no tint" must guard it before calling. */
static inline Clay_Color nt_ui_unpack_abgr(uint32_t packed) {
    return (Clay_Color){
        .r = (float)(packed & 0xFFU),
        .g = (float)((packed >> 8) & 0xFFU),
        .b = (float)((packed >> 16) & 0xFFU),
        .a = (float)((packed >> 24) & 0xFFU),
    };
}

/* 0xAABBGGRR -> Clay_Color with the IMAGE "no tint" sentinel: 0xFFFFFFFF -> {0}
 * (the walker maps a {0,0,0,0} backgroundColor back to white). Text must NOT use this. */
static inline Clay_Color nt_ui_unpack_tint(uint32_t packed) { return (packed == 0xFFFFFFFFU) ? (Clay_Color){0} : nt_ui_unpack_abgr(packed); }

/* Atomic ref inherit: copies the WHOLE ref (incl. name_hash + memoized region) if its atlas is set,
 * else `fallback`. Keyed on atlas.id (region 0 is a valid index, so it can't double as "unset"). */
static inline nt_atlas_region_ref_t nt_ui_ref_or(nt_atlas_region_ref_t ref, nt_atlas_region_ref_t fallback) { return (ref.atlas.id != 0U) ? ref : fallback; }

/* Shared scalar clamps for the widget TUs (fill/progress/scroll/slider). */
static inline float nt_ui_clampf(float v, float lo, float hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static inline int nt_ui_clampi(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

/* Identity baked xform — DFS seed + walker OOB fallback. */
static inline nt_ui_baked_xform_t nt_ui_internal_identity_baked(void) {
    nt_ui_baked_xform_t bx = {
        .m = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F}, .opacity = 1.0F, .hierarchy_depth = 0U, .zindex = 0, ._pad = {0.0F, 0.0F}};
    return bx;
}

/* Per-frame record of an interactive widget (one per step_interaction), used next frame by the
 * front-most hot-widget resolve. The transform/bbox are re-fetched by id at resolve time. */
typedef struct {
    uint32_t id;
    int16_t pad[4]; /* hit padding L/R/T/B */
} nt_ui_interactive_t;

/* Wheel-routing candidate: every scroll container/pane declared this frame appends one. The bbox is
 * NOT stored — nt_ui_end re-fetches it from the just-solved layout so the resolve and next frame's
 * consume read the same layout snapshot. Ownership is decided purely by bbox containment; per-axis
 * gating lives in the consume step. The winner per pointer lands in wheel_owner[]. */
typedef struct {
    uint32_t id;
    uint16_t depth; /* scroll-nesting depth; deeper (inner) wins */
} nt_ui_wheel_candidate_t;

/* Deep scroll nesting is rare; a small cap keeps the list in BSS (no heap in hot path).
 * The DEBUG_TOOLS inspector registers 2 of these (tree + selection panes), so the effective
 * game budget is this minus 2 when the inspector is active. */
#ifndef NT_UI_WHEEL_CANDIDATES
#define NT_UI_WHEEL_CANDIDATES 24
#endif

/* Nested-modal cap (fail-early assert on overflow; depth is a counter, no heap).
 * Depth drives the z-band stride*(depth+1) and close-signal top-targeting.
 * Build-overridable; raising it requires stride*depth to still fit int16 (see _Static_assert). */
#ifndef NT_UI_MODAL_MAX_DEPTH
#define NT_UI_MODAL_MAX_DEPTH 16
#endif

/* Per-level immediate-menu nav frame-record cap: a small cap keeps the per-level nav record in BSS
 * (no heap in hot path). Items past this in one level trip a fail-early assert. */
#ifndef NT_UI_MENU_MAX_ITEMS_PER_LEVEL
#define NT_UI_MENU_MAX_ITEMS_PER_LEVEL 64
#endif

/* One recorded immediate-menu row per level this frame; keyboard nav (menu_end) iterates the PREVIOUS
 * frame's per-level slice (D-236-06, 1-frame latency). Separators are never recorded (skipped by nav). */
typedef struct {
    uint32_t id;     /* the row's derived scope-stack id */
    uint16_t idx;    /* the row's running layout index at its level */
    uint8_t enabled; /* 0 = disabled: nav skips it */
    uint8_t has_sub; /* 1 = parent (submenu) row */
} nt_ui_menu_record_t;

/* App-wide gesture defaults. Per-ctx, settable via nt_ui_set_gesture_constants. Touch-friendly baseline
 * (looser than ImGui's mouse 6px/0.3s) since the engine targets mobile/WASM; mouse-only apps can tighten. */
#ifndef NT_UI_GESTURE_DBL_WINDOW_SECS
#define NT_UI_GESTURE_DBL_WINDOW_SECS 0.50F
#endif
#ifndef NT_UI_GESTURE_MOVE_RADIUS_PX
#define NT_UI_GESTURE_MOVE_RADIUS_PX 16.0F
#endif

/* Lives at arena head; hot fields first. Per-ctx — no module globals. */
struct nt_ui_context {
    Clay_Context *clay;
    Clay_RenderCommandArray frozen_cmds;
    bool in_frame;
    /* Orphan-focus cleanup (mirrors capture_seen): set to 1 the frame the focused field runs; if the
     * focused field is not re-declared next frame, nt_ui_begin clears focused_input_id (else a vanished
     * field gates global Esc forever). Reset to 0 each begin. Placed here to reuse in_frame's pad. */
    uint8_t focused_input_seen;

    nt_pointer_t frame_pointers[NT_INPUT_MAX_POINTERS];
    uint32_t frame_pointer_count;
    float frame_dt;

    /* capture_seen[] tracks who touched the capture this frame — orphans cleared on begin. */
    nt_ui_capture_t captures[NT_INPUT_MAX_POINTERS];
    /* Per-pointer front-most interactive widget + game-fed occlusion cutoff (3D), resolved lazily once
     * per frame from prev-frame layout (3D: nearest ray-t within cutoff; 2D: top zIndex). Ordered here
     * by alignment (after the 4B-aligned captures) to avoid adding struct padding. */
    nt_ui_hot_t pointer_hot[NT_INPUT_MAX_POINTERS];
    float pointer_occlusion[NT_INPUT_MAX_POINTERS]; /* max world ray distance; default +inf = no cutoff */
    /* Exclusive wheel routing (innermost-wins): the scroll id allowed to consume this pointer's wheel
     * this frame (0 = free). Resolved at the end of the previous frame from the candidate list below,
     * so it carries the same 1-frame lag as every prev-frame bbox hit-test. Frame 1: all 0. */
    uint32_t wheel_owner[NT_INPUT_MAX_POINTERS];
    /* This frame's wheel candidates; cleared in begin, appended by every scroll_begin/pane, resolved
     * in end (which re-fetches + sanity-checks each bbox). wheel_depth: live nesting counter (begin++/end--). */
    nt_ui_wheel_candidate_t wheel_candidates[NT_UI_WHEEL_CANDIDATES];
    /* Top-modal targeting with the standard 1-frame IM lag: _cur tracks the DEEPEST modal id seen
     * this frame; nt_ui_begin commits it into _prev, which the Esc/backdrop close-scan targets so
     * only the genuinely-topmost modal consumes the event (same-frame nesting can't be known at
     * the inner begin's return, so we use last frame's resolved top). */
    uint32_t modal_top_id_prev;
    uint32_t modal_top_id_cur;
    /* Keyboard-focus arbiter: the input field that eats typed chars + editing keys.
     * 0 = none. A press inside a field sets it; Esc clears it; Tab moves it to the next
     * field declared this frame. Survives across frames (not reset by begin). */
    uint32_t focused_input_id;
    /* Tab focus-advance bookkeeping, scoped to one frame: _seek set the frame a focused field
     * sees Tab; the NEXT field declared that frame claims focus and clears _seek. _first_id
     * tracks the earliest field so Tab off the last field wraps to the first. */
    uint32_t focus_tab_seek;
    uint32_t focus_first_id;
    uint32_t wheel_candidate_count;
    /* Per-depth modal z-band stride; resolved + validated in create_context (> 0). */
    int16_t modal_zband_stride;
    uint16_t wheel_depth;
    uint8_t active_modal_depth;
    uint8_t modal_max_depth_cur; /* deepest active_modal_depth reached this frame (drives modal_top_id_cur) */
    uint8_t capture_seen[NT_INPUT_MAX_POINTERS];
    bool pointer_over_any;
    bool hot_resolved; /* gates the once-per-frame lazy hot resolve */
    /* Modal-presence across frames: _cur set when any modal declares a backdrop this frame;
     * nt_ui_begin commits it into _prev. nt_ui_modal_active reads _prev so the game can gate its
     * next-frame hotkeys (the live depth is always 0 by nt_ui_end, can't be queried after). */
    bool modal_present_cur;
    bool modal_present_prev;

    /* Buttons do not nest (asserted). */
    struct {
        bool active;
        bool clicked;
    } pending_button;

    /* Tab-bar begin/end core. The bar holds the per-call style ptr + layers + base_id so tab_begin can
     * emit the per-state bg + accent without re-passing them; the tab sub-state carries the open tab's id
     * (for tab_end's step_interaction) + its click result. Bars/tabs do not nest (asserted). */
    struct {
        const void *style; /* nt_ui_tabbar_style_t* (void to keep the internal header widget-agnostic) */
        uint32_t base_id;
        uint8_t fill_layer;
        uint8_t label_layer;
        bool active;
    } pending_tabbar;
    struct {
        uint32_t id; /* the open tab's id (mixed hash of base_id+index; see tabbar_tab_id, NOT additive) */
        bool active; /* a tab element is open (between tab_begin/tab_end) */
    } pending_tab;

    /* Immediate-menu begin/end DEPTH stack (menus nest where tabs don't). The scope stack derives each
     * row's id via mix(scope_id[depth], key) (position-stable, no idx); a submenu pushes its own row id as the
     * child scope (ImGui PushID), so keys need only be unique among siblings. `st`/`menu_id`/layers are
     * stashed at begin so the per-call item/submenu functions need not re-pass them. `chosen` accumulates
     * a leaf activation this frame (INTERNAL close-chain signal only; the game sees activation via the row's
     * inline bool return, not a public sink). */
    struct {
        const void *style; /* nt_ui_menu_style_t* (void to keep the internal header widget-agnostic) */
        nt_ui_menu_state_t *st;
        uint32_t menu_id;
        uint32_t scope_id[NT_UI_MENU_MAX_DEPTH];    /* mix base per level; level 0 = menu_id */
        uint16_t item_idx[NT_UI_MENU_MAX_DEPTH];    /* per-level running layout index (separators advance it) */
        uint32_t chosen;                            /* leaf id activated this frame (0 = none) */
        int16_t pending_open[NT_UI_MENU_MAX_DEPTH]; /* running idx of a row whose submenu a click opens this frame (-1 none) */
        /* Hover-open (Bug 1): per-depth the running idx of a parent row hovered THIS frame (-1 none), and a
         * flag that an enabled LEAF (non-parent) row is hovered. menu_end resolves these against the mouse-
         * aim corridor: a hovered parent opens (next frame, 1-frame IM lag); a hovered leaf collapses the
         * open child once the corridor releases. Keyboard-open stays authoritative (set directly in nav). */
        int16_t hover_parent[NT_UI_MENU_MAX_DEPTH]; /* running idx of a hovered parent at this depth (-1 none) */
        uint8_t hover_leaf[NT_UI_MENU_MAX_DEPTH];   /* 1 = an enabled non-parent row is hovered at this depth */
        uint8_t level_side[NT_UI_MENU_MAX_DEPTH];   /* each level's resolved popup side (mouse-aim corridor near-edge pick) */
        uint8_t fill_layer;
        uint8_t label_layer;
        uint8_t depth;
        bool active;     /* between menu_begin/menu_end (true even for a CLOSED present-only menu) */
        bool open_frame; /* this frame's menu is OPEN: false = present-only, item/submenu calls no-op */
    } pending_menu;
    struct {
        uint32_t id;      /* the open custom-content row's id (for item_end's step_interaction) */
        bool activatable; /* false = an interactive child owns the click (the row never latches activate) */
        bool active;      /* a custom-content row element is open (between item_begin/item_end) */
    } pending_menu_item;

    /* Immediate-combo begin/selectable/end. Combos do NOT nest (single level, asserted). combo_begin
     * stashes the per-call style ptr + the game-owned open ptr + the combo id + layers; each
     * combo_selectable derives its row id via mix(id, key, row_idx), the running row_idx folding the
     * per-frame row order. The GAME owns int *selected and writes it on the selectable's clicked return
     * (Model-D); the combo only clears *open on a row click. trigger_open marks a custom preview element
     * open between combo_preview_begin/end; row_open marks a custom selectable open between
     * selectable_begin/end (its id + click latch deferred to selectable_end). */
    struct {
        const void *style;    /* nt_ui_dropdown_style_t* (void to keep the internal header widget-agnostic) */
        uint32_t id;          /* the combo id (scope for the row fmix) */
        bool *open;           /* game-owned open flag; a selectable click clears it (Model-D) */
        uint32_t row_id;      /* the open custom selectable's row id (for selectable_end's step + click latch) */
        float trigger_open_t; /* eased open amount stashed by the custom trigger; combo_preview_end's chevron rotation */
        uint16_t row_idx;     /* per-frame running row index (the selectable fmix fold) */
        uint8_t fill_layer;
        uint8_t label_layer;
        uint8_t scrolls;      /* list body wraps nt_ui_scroll (vs a plain panel) — combo_end closes the right nesting */
        uint8_t active;       /* a combo list is open between combo_begin/combo_end */
        uint8_t trigger_open; /* a custom trigger element is open between combo_preview_begin/end */
        uint8_t row_open;     /* a custom selectable element is open between selectable_begin/end */
    } pending_combo;

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

    /* Double-buffered interactive-widget registry (always-on). step_interaction appends to _cur this
     * frame; the hot resolve reads _prev (last frame). Buffers swap each begin. Arena-allocated. */
    nt_ui_interactive_t *interactive_prev;
    nt_ui_interactive_t *interactive_cur;

    uint32_t current_generation;
    uint32_t frame_counter; /* ++ each nt_ui_begin; STABLE for the whole begin->end->post-end-query window (current_generation bumps mid-end, so it can't gate post-end reads) */
    uint32_t interactive_prev_count;
    uint32_t interactive_cur_count;

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

    /* 3D-mode flag — copied from create_desc; gates raycast vs inverse-affine hit-test. */
    bool use_raycast_input;
    /* Set by nt_ui_set_view_proj; reset by nt_ui_begin so a forgotten setter trips the assert
     * inside ui_hit_test instead of silently raycasting through a stale frame's camera. */
    bool view_proj_set;
    float element_depth_bias_ndc;
    /* App-wide gesture constants (nt_ui_events). Set in create_context to the #define defaults,
     * overridable via nt_ui_set_gesture_constants. Grouped with the float block to avoid padding. */
    float gesture_dbl_window_secs;
    float gesture_move_radius_px;
    float view_proj[16];
    float inv_view_proj[16];
#if NT_UI_DEBUG_TOOLS
    /* Inspector renders at pixel coords even in 3D ctx — own ortho (built per begin from screen dims). */
    float inspector_view_proj[16];
    float inv_inspector_view_proj[16];
#endif

    nt_font_t fonts[NT_UI_MAX_FONTS];

    nt_ui_anim_interaction_t anim[NT_UI_ANIM_SLOTS];
    nt_ui_anim_interaction_t anim_snap; /* nt_ui_anim no-slot fast path (both speeds 0): snapped target, consumes no pool slot */
    /* Monotonic; nonzero delta across frames means raise NT_UI_ANIM_SLOTS. */
    uint32_t anim_collision_count;

    /* Generic per-id retained-state pool. Arena auto-sizes via sizeof(struct nt_ui_context);
     * create_context's memset zero-inits it (same as anim[]). */
    nt_ui_state_cell_t state_pool[NT_UI_STATE_SLOTS];

    /* Immediate-menu per-level nav frame record (live, this frame). menu_end navs the deepest open level
     * against the record just built this frame; OPENING a deeper level only sets open_path so the new
     * level is declared (and navigable) NEXT frame — the 1-frame latency of D-236-06. No per-frame memcpy. */
    nt_ui_menu_record_t frame_record[NT_UI_MENU_MAX_DEPTH][NT_UI_MENU_MAX_ITEMS_PER_LEVEL];
    uint16_t frame_record_count[NT_UI_MENU_MAX_DEPTH];

#if NT_UI_DEBUG_TOOLS
    /* Per-slot layer cache: 3D ctx hit-test branches inspector vs game view_proj on this.
     * Only the inspector-overlay path needs it, so gated under DEBUG_TOOLS. */
    uint8_t *hit_layer;

    /* Arena-allocated, cap = max_elements (worst-case = all interactive). */
    nt_ui_debug_zone_t *debug_zones;
    uint32_t debug_zone_cap;
    uint32_t debug_zone_count;
    bool debug_recording;

    /* Arena-allocated, cap = next_pow2(max_elements * 2) for linear-probing hash
     * (50% load factor). mask = cap - 1; bucket = id & mask + linear probe. */
    nt_ui_widget_slot_t *widget_registry;
    uint32_t widget_registry_cap;
    uint32_t widget_registry_mask;

    /* highlight_id resets each begin; selected_id persists across frames. */
    bool inspector_active;
    uint32_t inspector_highlight_id;
    uint32_t inspector_selected_id;
    /* Pointer inside sidebar footprint — gates step_interaction to zeroed return. */
    bool inspector_pointer_consumed;
    /* Prev-frame tree truncation state — warn only on the OFF->ON edge, not every frame. */
    bool inspector_was_truncated;

    /* Arena-allocated, cap = max_elements (user can't collapse more nodes than exist). */
    uint32_t *inspector_collapsed_ids;
    uint32_t inspector_collapsed_cap;
    uint32_t inspector_collapsed_count;

    nt_ui_inspector_metrics_t inspector_metrics;
    /* Optional overlay materials (typically depth_test=false); 0 = fall back to the game's sprite/text material. */
    nt_material_t inspector_sprite_material;
    nt_material_t inspector_text_material;
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

/* 3D-ctx inspector viewport pick: last-recorded zone the cursor ray hits, game view_proj (record/step
 * order, not camera distance). Scope: recorded debug_zones only (interactive widgets) — non-interactive
 * elements are tree-selectable, not scene-hover-pickable. 0 = none. */
uint32_t nt_ui_internal_pick_zone_3d(const nt_ui_context_t *ctx, float px, float py);

void nt_ui_internal_project_layout_to_world(const nt_ui_debug_zone_t *z, float vy, float vh, float x, float y, float *out_x, float *out_y);

void nt_ui_internal_emit_filled_quad(nt_resource_t atlas, uint32_t region, const float v[4][2], uint32_t color);

void nt_ui_internal_emit_outline(nt_resource_t atlas, uint32_t region, const float c[4][2], float thickness, uint32_t color);

/* 3D-ctx variants: corners stay in the element's Clay-layout space and `model` (the recorded world
 * mat4) maps them into the scene. Caller binds the perspective view_proj so the debug overlay lands
 * on the element in 3D. The 2D-ctx helpers above pre-project to screen and emit under identity. */
void nt_ui_internal_emit_filled_quad_m(nt_resource_t atlas, uint32_t region, const float v[4][2], const float model[16], uint32_t color);

void nt_ui_internal_emit_outline_m(nt_resource_t atlas, uint32_t region, const float c[4][2], float thickness, const float model[16], uint32_t color);

/* (x,y) top-left, (wp,hp) size in logical layout pixels. Caller wraps in scissor_enabled(true/false). */
void nt_ui_internal_apply_scissor_logical_to_physical(const nt_ui_target_t *target, int x, int y, int wp, int hp);

/* Scroll offset for an engine-internal clip pane (inspector) keyed by Clay element id; drives
 * the custom scroll integrator off prev-frame dims + wheel. Feed the result to clip.childOffset.
 * `tag` keeps each pane's state cell distinct (NT_UI_STATE_TAG). */
Clay_Vector2 nt_ui_internal_scroll_pane_offset(nt_ui_context_t *ctx, uint32_t id, uint32_t tag, bool scroll_x, bool scroll_y);

/* End-of-frame wheel-owner resolve: picks, per pointer, the winning scroll candidate (max depth,
 * then min bbox area, then latest declaration) into ctx->wheel_owner[] for NEXT frame's consume. */
void nt_ui_internal_resolve_wheel_owners(nt_ui_context_t *ctx);

/* Prev-frame transform-aware point-in-bbox test (same 1-frame IM lag as step/query), with optional
 * L/R/T/B padding. Sets the Clay current-ctx internally. Used by the modal close-on-backdrop guard. */
bool nt_ui_internal_hit_test_padded(nt_ui_context_t *ctx, uint32_t id, float px, float py, const int16_t pad_lrtb[4]);

#endif /* NT_UI_INTERNAL_H */
