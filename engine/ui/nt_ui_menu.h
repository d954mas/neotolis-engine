#ifndef NT_UI_MENU_H
#define NT_UI_MENU_H

/* Context-menu + recursive submenus (WGT-04/05) built ON popup-core. Each menu level is a popup-core
 * floating (z-band per depth); a parent item carrying a `submenu` pointer opens its child as a nested
 * popup fly-out. The novel piece is the mouse-aim triangle (CITED: Amazon mega-dropdown / Dear ImGui
 * BeginMenuEx): while the cursor travels diagonally toward an open submenu it stays open even when the
 * path crosses sibling items (Pitfall 3 — never collapse on raw hover-loss). Per-level edge-flip,
 * nested dismiss from the deepest level up, and keyboard-nav across levels round it out.
 *
 * Model D: the GAME owns the open `bool` and the chosen-item id sink; the menu only signals. */

#include <stdbool.h>
#include <stdint.h>

#include "ui/nt_ui.h" /* nt_ui_layer_t, nt_ui_widget_def_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_MENU_DEF;

/* Tuning (A1 — tuning-only, final values settle at the visual-QA gate). AIM_FALLBACK is how long the
 * cursor may sit OFF the aim triangle before the hovered sibling wins; OPEN_DELAY gates the submenu
 * open so a quick pass-over does not flash every child. */
#define NT_UI_MENU_AIM_FALLBACK_SECS 0.12F
#define NT_UI_MENU_OPEN_DELAY_SECS 0.20F

/* Submenu nesting cap. T-65-10: a runaway tree (cyclic submenu pointer, pathological depth) would
 * exhaust the popup z-band / stack; NT_ASSERT fires BEFORE the push (fail-early, no fallback). */
#define NT_UI_MENU_MAX_DEPTH 8

/* One menu item. Recursion is via `submenu` (NULL = leaf). The game builds a static const tree; the
 * menu never mutates it. ABI guarded portably (two pointers + 16B of scalars/pad) so the guard holds
 * on both 64-bit native and 32-bit wasm. */
typedef struct nt_ui_menu_item nt_ui_menu_item_t;
struct nt_ui_menu_item {
    const char *label;                /* non-NULL when this slot is a real item (NULL = separator) */
    const nt_ui_menu_item_t *submenu; /* child level (NULL = leaf / separator) */
    uint32_t id;                      /* game-chosen selection id reported on activate (leaf only) */
    uint32_t submenu_count;           /* number of items in `submenu` (0 = leaf) */
    bool enabled;                     /* disabled item: greyed, not selectable, opens no submenu */
    uint8_t _pad[7];
};
_Static_assert(sizeof(nt_ui_menu_item_t) == 2 * sizeof(void *) + 16, "nt_ui_menu_item_t stable ABI (2 ptr + 2 u32 + 1 bool + 7 pad)");

/* Visual knobs. Colors are 0xAABBGGRR. Item rows are plain rects + labels (no button widget) so the
 * hover-intent owns the open/keep/switch decision. */
typedef struct {
    uint32_t bg_color;         /* panel background */
    uint32_t item_hover_color; /* hovered/focused row highlight */
    uint32_t text_color;       /* enabled item text */
    uint32_t text_disabled;    /* disabled item text */
    float font_size;           /* px; asserted > 0 */
    uint16_t item_height;      /* px row height */
    uint16_t min_width;        /* px panel min width */
    uint16_t pad;              /* px inner padding */
    uint16_t font_id;          /* label font */
    nt_ui_layer_t layer;       /* draw layer */
    uint8_t _pad[3];
} nt_ui_menu_style_t;
_Static_assert(sizeof(nt_ui_menu_style_t) == 32, "nt_ui_menu_style_t stable ABI (4 u32 + 1 float + 4 u16 + 1 u8 layer + 3 pad)");

/* Valid baseline style (dark). */
nt_ui_menu_style_t nt_ui_menu_style_defaults(void);

/* Live menu state the game owns and persists across frames. `open` is set by an open helper (or the
 * game directly); the menu clears it on dismiss. `chosen_id` latches the activated leaf id (0 = none
 * yet) — the game reads + clears it. anchor_x/y is the screen-space open point (the cursor at open). */
typedef struct {
    float anchor_x, anchor_y; /* open point (UI space) — root menu top-left attaches here */
    uint32_t chosen_id;       /* latched activated leaf id; 0 = none. Game reads + clears. */
    bool open;                /* game-owned open flag; menu clears on dismiss */
    uint8_t _pad[3];
} nt_ui_menu_state_t;
_Static_assert(sizeof(nt_ui_menu_state_t) == 16, "nt_ui_menu_state_t stable ABI (2 float + 1 u32 + 1 bool + 3 pad)");

/* Open trigger: arms the menu at the cursor when the bound widget area was right-clicked OR long-pressed
 * this frame. `id` is the menu's stable id (drives every level's salted state). Returns true if it armed
 * this frame (so the caller can stop other handling). long_press_secs <= 0 disables the long-press path
 * (right-click only). Reads NT_BUTTON_RIGHT for the desktop trigger and an events long-press gesture for
 * touch (Plan 01). */
bool nt_ui_menu_open_trigger(nt_ui_context_t *ctx, uint32_t id, nt_ui_menu_state_t *st, float long_press_secs);

/* Declare the menu: renders the root as a popup-core floating at the anchor and recursively declares
 * each open submenu as a nested popup-core fly-out, driven by the mouse-aim hover-intent. Esc /
 * outside-click dismisses the deepest open level up the chain; keyboard arrows navigate. On a leaf
 * activate it latches st->chosen_id and dismisses the whole chain (Model D). id/items/st/style non-NULL;
 * NT_ASSERT on a malformed tree (NULL label with a submenu count, depth past the cap). */
void nt_ui_menu(nt_ui_context_t *ctx, uint32_t id, const nt_ui_menu_item_t *items, uint32_t count, nt_ui_menu_state_t *st, const nt_ui_menu_style_t *style);

#ifdef NT_TEST_ACCESS
/* Pure barycentric point-in-triangle (CITED RESEARCH §submenu). No GL, no ctx — directly unit-tested. */
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
#endif

#endif /* NT_UI_MENU_H */
