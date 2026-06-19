#ifndef NT_UI_MENU_H
#define NT_UI_MENU_H

/* Context-menu + recursive submenus built ON popup-core. Each menu level is a popup-core floating
 * (z-band per depth); a parent item carrying a `submenu` pointer opens its child as a nested popup
 * fly-out. The mouse-aim triangle keeps an open submenu open while the cursor travels diagonally toward
 * it even when the path crosses sibling items (never collapse on raw hover-loss). Per-level edge-flip,
 * nested dismiss from the deepest level up, and keyboard-nav across levels round it out.
 *
 * The GAME owns the open `bool` and the chosen-item id sink; the menu only signals. */

#include <stdbool.h>
#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "ui/nt_ui.h"       /* nt_ui_layer_t, nt_ui_widget_def_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_MENU_DEF;

/* AIM_FALLBACK is how long the cursor may sit OFF the aim triangle before the hovered sibling wins;
 * OPEN_DELAY gates the submenu open so a quick pass-over does not flash every child. */
#define NT_UI_MENU_AIM_FALLBACK_SECS 0.12F
#define NT_UI_MENU_OPEN_DELAY_SECS 0.20F

/* Submenu nesting cap: a runaway tree (cyclic submenu pointer, pathological depth) would exhaust the
 * popup z-band / stack; NT_ASSERT fires BEFORE the push (fail-early, no fallback). */
#define NT_UI_MENU_MAX_DEPTH 8

/* Visual knobs. Colors are 0xAABBGGRR. Item rows are plain rects + labels (no button widget) so the
 * hover-intent owns the open/keep/switch decision. The panel is an OPTIONAL slice9 sprite (panel_bg
 * ref) with a flat bg_color fallback; the submenu marker is an OPTIONAL arrow sprite with a ">" text
 * fallback; rows reserve a leading icon gutter (icon_size). layer comes from the call (data->layer),
 * NOT the style — mirrors checkbox/dropdown. */
typedef struct {
    nt_atlas_region_ref_t panel_bg;  /* optional panel slice9 art; atlas.id==0 = flat bg_color */
    nt_atlas_region_ref_t arrow;     /* optional submenu marker sprite; atlas.id==0 = ">" text fallback */
    nt_atlas_region_ref_t checkmark; /* optional selected-item checkmark sprite; atlas.id==0 = "✓" text fallback */
    uint32_t bg_color;               /* flat panel background fallback */
    uint32_t item_hover_color;       /* hovered/focused row highlight (eased in) */
    uint32_t text_color;             /* enabled item text */
    uint32_t text_disabled;          /* disabled item text */
    uint32_t shortcut_text;          /* right-aligned shortcut-column text color */
    uint32_t panel_tint;             /* multiplies the panel slice9 art; 0xFFFFFFFF = no tint */
    uint32_t arrow_tint;             /* multiplies the arrow sprite; 0xFFFFFFFF = no tint */
    uint32_t check_tint;             /* multiplies the checkmark sprite; 0xFFFFFFFF = no tint */
    uint32_t separator_color;        /* NULL-label separator divider color */
    float font_size;                 /* px; asserted > 0 */
    float slice9_scale;              /* multiplies the panel art's baked slice9 borders; > 0 */
    float state_speed;               /* eases the row hover/focus highlight (0 = instant) */
    float open_ease_speed;           /* popup open/close tween speed (0 = snap; game opts into a tween) */
    uint16_t item_height;            /* px row height */
    uint16_t min_width;              /* px panel min width */
    uint16_t pad;                    /* px inner padding */
    uint16_t font_id;                /* label font */
    uint16_t icon_size;              /* px leading icon gutter (0 = no gutter / text-only rows) */
    uint16_t arrow_size;             /* px submenu marker sprite box (used only when arrow ref is set) */
    uint16_t check_size;             /* px checkmark sprite box (used only when the checkmark ref is set) */
    uint16_t separator_height;       /* px NULL-label separator divider thickness */
} nt_ui_menu_style_t;
_Static_assert(sizeof(nt_ui_menu_style_t) == 120, "nt_ui_menu_style_t stable ABI (3 ref + 9 u32 + 4 float + 8 u16 + 4 tail pad)");

/* Valid baseline style (dark). */
nt_ui_menu_style_t nt_ui_menu_style_defaults(void);

/* Immediate-item options (D-236-03): the full OS-menu row field set. `icon` is the leading-gutter sprite
 * (atlas.id==0 = aligned empty gutter). A {0} init reads enabled=false + activatable=false — wrong for a
 * normal row, so prefer nt_ui_menu_item_opts_defaults() (enabled+activatable true) and opt OUT, not IN. */
typedef struct {
    nt_atlas_region_ref_t icon; /* optional leading-gutter sprite (atlas.id==0 = aligned empty gutter) */
    const char *shortcut;       /* optional right-aligned shortcut text (e.g. "Ctrl+S"); NULL = none */
    bool enabled;               /* greyed + non-selectable when false */
    bool selected;              /* checkmark (toggle/radio menu items) */
    bool activatable;           /* false = an interactive child owns the click (§7 opt-out, item_begin only) */
    uint8_t _pad[1];
} nt_ui_menu_item_opts_t;
/* 64-bit: 16 ref + 8 ptr + 4 bools/pad = 28 -> 32 (8-align). wasm 32-bit: 16 + 4 + 4 = 24. Both = a
 * multiple of the 8-byte ref alignment; express portably as 16 + the (ptr+4) cell rounded up to 8. */
_Static_assert(sizeof(nt_ui_menu_item_opts_t) == 16 + (((sizeof(void *) + 4) + 7) / 8 * 8), "nt_ui_menu_item_opts_t stable ABI (16 ref + ptr + 3 bool + pad, 8-aligned)");

/* Safe baseline: a normal enabled, activatable row (no icon/shortcut/check). Callers opt OUT (clear
 * enabled/activatable), never IN — a zeroed opts would otherwise silently disable + de-activate the row. */
static inline nt_ui_menu_item_opts_t nt_ui_menu_item_opts_defaults(void) { return (nt_ui_menu_item_opts_t){.enabled = true, .activatable = true}; }

/* Live menu state the game owns and persists across frames. `open` is set by an open helper (or the
 * game directly); the menu clears it on dismiss. Setting open false then true directly reopens cleanly
 * (the menu resets its runtime chain on the closed frame). `chosen_id` latches the activated leaf id
 * (0 = none yet) — the game reads + clears it. anchor_x/y is the screen-space open point (cursor at open). */
typedef struct {
    float anchor_x, anchor_y; /* open point (UI space) — root menu top-left attaches here */
    uint32_t chosen_id;       /* latched activated leaf id; 0 = none. Game reads + clears. */
    bool open;                /* game-owned open flag; menu clears on dismiss */
    uint8_t _pad[3];
} nt_ui_menu_state_t;
_Static_assert(sizeof(nt_ui_menu_state_t) == 16, "nt_ui_menu_state_t stable ABI (2 float + 1 u32 + 1 bool + 3 pad)");

/* Immediate-mode menu (begin/end). The game discovers the tree during the frame: open with menu_begin,
 * declare rows via item / item_ex / item_begin..item_end / submenu_begin..submenu_end / separator, close
 * with menu_end. Ids derive via the scope stack (mix(scope,key,idx)); submenu_begin pushes its row id as
 * the child scope so keys need only be unique among siblings. Keyboard nav runs in menu_end against the
 * PREVIOUS frame's per-level record (1-frame latency). The game owns st (open + anchor + chosen sink). */
void nt_ui_menu_begin(nt_ui_context_t *ctx, uint32_t menu_id, nt_ui_menu_state_t *st, nt_ui_menu_style_t *style);
bool nt_ui_menu_item(nt_ui_context_t *ctx, uint32_t key, const char *label);                                 /* plain item; returns clicked */
bool nt_ui_menu_item_ex(nt_ui_context_t *ctx, uint32_t key, const char *label, nt_ui_menu_item_opts_t opts); /* rich row; returns clicked */
bool nt_ui_menu_item_begin(nt_ui_context_t *ctx, uint32_t key,
                           nt_ui_menu_item_opts_t opts); /* open a CUSTOM-content row; TRUE = declare body (menu open), guard with if. Activatable click latches in item_end. */
void nt_ui_menu_item_end(nt_ui_context_t *ctx);
bool nt_ui_menu_submenu_begin(nt_ui_context_t *ctx, uint32_t key, const char *label); /* true ONLY when open -> declare body */
void nt_ui_menu_submenu_end(nt_ui_context_t *ctx);
void nt_ui_menu_separator(nt_ui_context_t *ctx);                         /* non-interactive divider (no focus index) */
void nt_ui_menu_separator_text(nt_ui_context_t *ctx, const char *label); /* optional section header */
void nt_ui_menu_end(nt_ui_context_t *ctx);

/* Open trigger: arms the menu at the cursor on a right-click OR a caller-supplied long-press this frame.
 * Does ONLY idempotent reads — NO mutating interaction step (so it never double-steps the caller's
 * widget; nt_ui_events is the one canonical mutating step per widget per frame). `menu_id` is the menu's
 * stable id (drives every level's salted state). `target_id` optionally binds the right-click to a widget:
 * 0 = arm anywhere (any right-click); non-zero = arm over that widget's bbox via a GEOMETRY hit-test (NOT
 * arbitrated hover, so a right-click RE-opens the menu through its own occluder; does not respect z-order).
 * `long_pressed` is the touch trigger: the
 * caller passes the long_pressed flag from ITS target widget's own nt_ui_events gesture step (this helper
 * owns no gesture timing). Returns true if it armed this frame (so the caller can stop other handling). */
bool nt_ui_menu_open_trigger(nt_ui_context_t *ctx, uint32_t menu_id, uint32_t target_id, bool long_pressed, nt_ui_menu_state_t *st);

#ifdef NT_TEST_ACCESS
/* Pure barycentric point-in-triangle. No GL, no ctx — directly unit-tested. */
bool nt_ui_menu_test_point_in_tri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy);

/* The aim-triangle corners for a submenu rect on a given side (RIGHT => near edge is the submenu LEFT;
 * LEFT => mirror to the submenu RIGHT). Writes {bx,by,cx,cy} = the two near corners. */
void nt_ui_menu_test_aim_corners(float sub_x, float sub_y, float sub_w, float sub_h, uint8_t side, float *bx, float *by, float *cx, float *cy);

/* Per-open-submenu hover-intent decision, driven through the state pool (salted by depth so level N
 * never aliases N+1). Feeds the current mouse + prev-frame mouse + the open submenu rect; returns true
 * to KEEP the submenu (cursor is aiming at it, switch_timer reset) or false to allow a sibling switch
 * once the off-triangle dwell passed AIM_FALLBACK. */
bool nt_ui_menu_test_hover_intent(nt_ui_context_t *ctx, uint32_t menu_id, uint8_t depth, float mouse_x, float mouse_y, float sub_x, float sub_y, float sub_w, float sub_h, uint8_t side, float dt);

/* Live switch-timer in the depth-salted hover-intent cell (0 if absent). */
float nt_ui_menu_test_switch_timer(const nt_ui_context_t *ctx, uint32_t menu_id, uint8_t depth);

/* Resolve a level's panel id / a row id so tests can query their prev-frame bbox via nt_ui_get_bbox
 * and drive the snapshot pointer at real row geometry. */
uint32_t nt_ui_menu_test_panel_id(uint32_t menu_id, uint8_t depth);
uint32_t nt_ui_menu_test_row_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx);

/* Sub-element ids so tests can query their prev-frame bbox: the submenu arrow marker cell, the icon
 * gutter cell, and the single root occluder. Drives the sprite-presence / icon-gutter / occluder probes. */
uint32_t nt_ui_menu_test_arrow_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx);
uint32_t nt_ui_menu_test_icon_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx);
uint32_t nt_ui_menu_test_occluder_id(uint32_t menu_id);
/* Rich-row sub-element ids (shortcut text cell / selected checkmark cell): drive the right-aligned
 * shortcut + checkmark CELL bbox probes (the rendered glyph is user-visual-QA only). */
uint32_t nt_ui_menu_test_shortcut_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx);
uint32_t nt_ui_menu_test_check_id(uint32_t menu_id, uint8_t depth, uint32_t item_idx);

/* Scope-stack id derivation probe: mix(scope_id, key, idx) — drives the sibling/scope distinctness test. */
uint32_t nt_ui_menu_test_item_id(uint32_t scope_id, uint32_t key, uint32_t idx);
/* Prev-frame frame-record focus probe: the recorded item id at rt->focus[depth] (1-frame latency). */
uint32_t nt_ui_menu_test_focus_item_id(uint32_t menu_id, uint8_t depth);
#endif

#endif /* NT_UI_MENU_H */
