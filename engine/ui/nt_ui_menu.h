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

/* One menu item. Recursion is via `submenu` (NULL = leaf). The game builds a static const tree; the
 * menu never mutates it. ABI guarded portably (two pointers + 32B of ref/scalars/pad) so the guard
 * holds on both 64-bit native and 32-bit wasm. `icon` is an OPTIONAL leading-gutter sprite (atlas.id==0
 * = no icon -> aligned empty space when style->icon_size > 0; unified icon model). */
typedef struct nt_ui_menu_item nt_ui_menu_item_t;
struct nt_ui_menu_item {
    const char *label;                /* non-NULL when this slot is a real item (NULL = separator) */
    const nt_ui_menu_item_t *submenu; /* child level (NULL = leaf / separator) */
    nt_atlas_region_ref_t icon;       /* optional leading icon (atlas.id==0 = aligned empty gutter) */
    uint32_t id;                      /* game-chosen selection id reported on activate (leaf only) */
    uint32_t submenu_count;           /* number of items in `submenu` (0 = leaf) */
    bool enabled;                     /* disabled item: greyed, not selectable, opens no submenu */
    uint8_t _pad[7];
};
_Static_assert(sizeof(nt_ui_menu_item_t) == 2 * sizeof(void *) + 32, "nt_ui_menu_item_t stable ABI (2 ptr + 16 ref + 2 u32 + 1 bool + 7 pad)");

/* Visual knobs. Colors are 0xAABBGGRR. Item rows are plain rects + labels (no button widget) so the
 * hover-intent owns the open/keep/switch decision. The panel is an OPTIONAL slice9 sprite (panel_bg
 * ref) with a flat bg_color fallback; the submenu marker is an OPTIONAL arrow sprite with a ">" text
 * fallback; rows reserve a leading icon gutter (icon_size). layer comes from the call (data->layer),
 * NOT the style — mirrors checkbox/dropdown. */
typedef struct {
    nt_atlas_region_ref_t panel_bg; /* optional panel slice9 art; atlas.id==0 = flat bg_color */
    nt_atlas_region_ref_t arrow;    /* optional submenu marker sprite; atlas.id==0 = ">" text fallback */
    uint32_t bg_color;              /* flat panel background fallback */
    uint32_t item_hover_color;      /* hovered/focused row highlight (eased in) */
    uint32_t text_color;            /* enabled item text */
    uint32_t text_disabled;         /* disabled item text */
    uint32_t panel_tint;            /* multiplies the panel slice9 art; 0xFFFFFFFF = no tint */
    uint32_t arrow_tint;            /* multiplies the arrow sprite; 0xFFFFFFFF = no tint */
    uint32_t separator_color;       /* NULL-label separator divider color */
    float font_size;                /* px; asserted > 0 */
    float slice9_scale;             /* multiplies the panel art's baked slice9 borders; > 0 */
    float state_speed;              /* eases the row hover/focus highlight (0 = instant) */
    float open_ease_speed;          /* popup open/close tween speed (0 = snap; game opts into a tween) */
    uint16_t item_height;           /* px row height */
    uint16_t min_width;             /* px panel min width */
    uint16_t pad;                   /* px inner padding */
    uint16_t font_id;               /* label font */
    uint16_t icon_size;             /* px leading icon gutter (0 = no gutter / text-only rows) */
    uint16_t arrow_size;            /* px submenu marker sprite box (used only when arrow ref is set) */
    uint16_t separator_height;      /* px NULL-label separator divider thickness */
    uint8_t _pad[6];
} nt_ui_menu_style_t;
_Static_assert(sizeof(nt_ui_menu_style_t) == 96, "nt_ui_menu_style_t stable ABI (2 ref + 7 u32 + 4 float + 7 u16 + 6 pad)");

/* Valid baseline style (dark). */
nt_ui_menu_style_t nt_ui_menu_style_defaults(void);

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

/* Declare the menu: renders the root as a popup-core floating at the anchor and recursively declares
 * each open submenu as a nested popup-core fly-out, driven by the mouse-aim hover-intent. Esc /
 * outside-click dismisses the deepest open level up the chain; keyboard arrows navigate. On a leaf
 * activate it latches st->chosen_id and dismisses the whole chain. id/items/st/style non-NULL;
 * NT_ASSERT on a malformed tree (NULL label with a submenu count, depth past the cap). A NULL-label item
 * is a non-interactive separator (a thin divider, skipped by hover/click/keyboard-nav).
 * A SINGLE root-level full-viewport occluder sits under the whole menu stack: it absorbs the dismiss
 * click and gates base UI while the menu is open. NO per-level catchers: a per-level catcher sits ABOVE
 * ancestor panels and would trap the user in the deepest level.
 * style is mutated in place to memoize resolved atlas refs (panel_bg / arrow).
 * Layers: every level's panel + row fills + icons + arrow draw on data->layer (also the popup panel
 * layer), item text on label_layer (split to batch). data may be NULL (fills fall to layer 0). */
void nt_ui_menu(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const nt_ui_menu_item_t *items, uint32_t count, nt_ui_menu_state_t *st,
                nt_ui_menu_style_t *style);

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

/* Scope-stack id derivation probe: mix(scope_id, key, idx) — drives the sibling/scope distinctness test. */
uint32_t nt_ui_menu_test_item_id(uint32_t scope_id, uint32_t key, uint32_t idx);
/* Prev-frame frame-record focus probe: the recorded item id at rt->focus[depth] (1-frame latency). */
uint32_t nt_ui_menu_test_focus_item_id(uint32_t menu_id, uint8_t depth);
#endif

#endif /* NT_UI_MENU_H */
