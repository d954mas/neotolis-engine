#ifndef NT_UI_MENU_H
#define NT_UI_MENU_H

/* Context menu with recursive submenus on popup-core, declared every frame; one popup level per depth.
 * A mouse-aim corridor keeps an open submenu while the cursor travels toward it across sibling rows. */

#include <stdbool.h>
#include <stdint.h>

#include "atlas/nt_atlas.h" /* nt_atlas_region_ref_t */
#include "ui/nt_ui.h"       /* nt_ui_layer_t, nt_ui_widget_def_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_MENU_DEF;

/* AIM_FALLBACK is how long the cursor may sit OFF the aim triangle before the hovered sibling wins. */
#define NT_UI_MENU_AIM_FALLBACK_SECS 0.12F

/* Submenu nesting cap: a parent row declared on the deepest level asserts (fail-early, no fallback), so a
 * runaway tree never exhausts the popup z-band / stack. Build-overridable for parity with every other cap
 * in the subsystem. */
#ifndef NT_UI_MENU_MAX_DEPTH
#define NT_UI_MENU_MAX_DEPTH 8
#endif

/* Visual knobs. Colors are 0xAABBGGRR. Item rows are plain rects + labels (no button widget) so the
 * hover-intent owns the open/keep/switch decision. The panel is an OPTIONAL slice9 sprite (panel_bg
 * ref) with a flat bg_color fallback; the submenu marker is an OPTIONAL arrow sprite with a ">" text
 * fallback; rows reserve a leading icon gutter (icon_size). layer comes from the call (data->layer for
 * fills, label_layer for text), NOT the style — mirrors checkbox/dropdown/tabbar. */
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
    uint32_t separator_color;        /* separator divider and section-header text color */
    float font_size;                 /* px; asserted > 0 */
    float slice9_scale;              /* multiplies the panel art's baked slice9 borders; > 0 */
    float state_speed;               /* eases the row hover/focus highlight (0 = instant) */
    uint16_t item_height;            /* px row height */
    uint16_t min_width;              /* px panel min width */
    uint16_t pad;                    /* px inner padding */
    uint16_t font_id;                /* label font */
    uint16_t icon_size;              /* px leading icon gutter (0 = no gutter / text-only rows) */
    uint16_t arrow_size;             /* px submenu marker sprite box (used only when arrow ref is set) */
    uint16_t check_size;             /* px checkmark sprite box (used only when the checkmark ref is set) */
    uint16_t separator_height;       /* px separator divider thickness */
} nt_ui_menu_style_t;
_Static_assert(sizeof(nt_ui_menu_style_t) == 112, "nt_ui_menu_style_t stable ABI (3 ref + 9 u32 + 3 float + 8 u16)");

/* Valid baseline style (dark). */
nt_ui_menu_style_t nt_ui_menu_style_defaults(void);

/* Options of a plain or parent row (item_ex / submenu_begin_ex). `icon` is the leading-gutter sprite
 * (atlas.id==0 = aligned empty gutter). `disabled` is negative-sense — unlike the positional `bool enabled`
 * on button/checkbox/slider — because a struct field can't default to true, so `{0}` is a normal row. */
typedef struct {
    nt_atlas_region_ref_t icon; /* optional leading-gutter sprite (atlas.id==0 = aligned empty gutter) */
    const char *shortcut;       /* optional right-aligned shortcut text (e.g. "Ctrl+S"); NULL = none */
    bool disabled;              /* greyed + non-selectable when true (default false = enabled) */
    bool selected;              /* checkmark (toggle/radio menu items); a parent row asserts it is false */
    uint8_t _pad[2];
} nt_ui_menu_item_opts_t;
/* 64-bit: 16 ref + 8 ptr + 4 bools/pad = 28 -> 32 (8-align). wasm 32-bit: 16 + 4 + 4 = 24. Both = a
 * multiple of the 8-byte ref alignment; express portably as 16 + the (ptr+4) cell rounded up to 8. */
_Static_assert(sizeof(nt_ui_menu_item_opts_t) == 16 + (((sizeof(void *) + 4) + 7) / 8 * 8), "nt_ui_menu_item_opts_t stable ABI (16 ref + ptr + 2 bool + pad, 8-aligned)");

/* The mouse-aim corridor guarding one open child level. apex: the cursor when another row of the parent's
 * level was first hovered; timer: time since then, reset over the child. depth 0 = not primed. */
typedef struct {
    float apex_x, apex_y;
    float timer;
    uint8_t depth; /* the guarded child level */
} nt_ui_menu_aim_t;

/* One declaration's scratch (menu_begin..menu_end), reset by nt_ui_menu_begin. */
typedef struct {
    nt_ui_context_t *ui; /* the core ctx between begin and end; NULL outside */
    nt_ui_menu_style_t *style;
    uint32_t menu_id;
    uint32_t nav_first, nav_last; /* first/last enabled row on the navigated level */
    uint32_t nav_prev, nav_next;  /* enabled rows before/after the focused one there */
    uint32_t hover_id;            /* row under the pointer (0 = none) */
    uint32_t click_id;            /* parent to open in menu_end: a click, or a key next to a sibling's open child */
    uint32_t item_id;             /* the open custom row (item_begin..item_end); 0 = none */
    uint8_t depth;                /* level being declared */
    uint8_t nav_depth;            /* the focused row's level at begin: the one keys navigate */
    uint8_t declared_depth;       /* deepest level declared: open parents past it are gone */
    uint8_t hover_depth;
    uint8_t click_depth;
    uint8_t fill_layer;
    uint8_t label_layer;
    bool nav_passed;       /* the navigated level has declared the focused row */
    bool focus_first;      /* a keyboard-opened child gives focus to its first enabled row */
    bool hover_opens;      /* hover_id is a parent */
    bool focus_seen;       /* the focused row is declared and enabled this frame */
    bool focus_opens;      /* the focused row is a parent */
    bool top;              /* the menu is the top overlay: it reads keys and the outside press */
    bool activated;        /* a row activated: the chain closes */
    bool item_enabled;     /* the open custom row is enabled */
    bool item_activatable; /* the open custom row has an action of its own */
} nt_ui_menu_frame_t;

/* One menu, owned by the game and kept across frames: one per logical menu and UI context. The game opens
 * it with nt_ui_menu_open and may close it by clearing `open`; the menu clears `open` on dismiss and on
 * activation. The other fields are the menu's own; all-zero is the valid closed state. */
typedef struct {
    float anchor_x, anchor_y; /* open point (UI space) — root menu top-left attaches here */
    uint32_t focus;           /* focused row id (0 = none) */
    /* Open parent row id per level; the first 0 ends the chain, and its length is the open depth. */
    uint32_t open_path[NT_UI_MENU_MAX_DEPTH];
    nt_ui_menu_aim_t aim;
    uint8_t focus_depth; /* the focused row's level */
    bool open;           /* the menu is shown; cleared on dismiss */
    bool arming;         /* opened since the last end: the next declaration ignores the press and last frame's hits */
    bool kbd_mode;       /* a menu key was pressed: the pointer takes focus back once it moves or presses */
    nt_ui_menu_frame_t frame;
} nt_ui_menu_t;

/* Declare the rows and call nt_ui_menu_end only when begin returns true (the menu is open). Fills, icons and
 * sprite markers draw on data->layer (NULL = layer 0), text on label_layer. Keys must be unique among
 * siblings; navigation rules: spec chapter UI, section "Immediate menu + combo". */
bool nt_ui_menu_begin(nt_ui_menu_t *menu, nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t menu_id, nt_ui_menu_style_t *style);
bool nt_ui_menu_item(nt_ui_menu_t *menu, uint32_t key, const char *label);                                 /* plain item; true when activated (click or Enter) */
bool nt_ui_menu_item_ex(nt_ui_menu_t *menu, uint32_t key, const char *label, nt_ui_menu_item_opts_t opts); /* rich row; true when activated */
/* Custom-content row: item_begin opens the row element, the game declares its body, item_end closes it
 * and returns whether the row was activated (mouse or Enter), like item/item_ex. A row that is not
 * `activatable` only hosts a control: it hovers but never captures or activates. A forgotten item_end trips
 * the next row, submenu_end or menu_end assert. */
void nt_ui_menu_item_begin(nt_ui_menu_t *menu, uint32_t key, bool enabled, bool activatable);
bool nt_ui_menu_item_end(nt_ui_menu_t *menu);
bool nt_ui_menu_submenu_begin(nt_ui_menu_t *menu, uint32_t key, const char *label); /* true ONLY when open -> declare body */
/* Rich submenu parent (icon/shortcut/disabled), mirroring item -> item_ex. A disabled parent renders greyed
 * and never opens. */
bool nt_ui_menu_submenu_begin_ex(nt_ui_menu_t *menu, uint32_t key, const char *label, nt_ui_menu_item_opts_t opts);
void nt_ui_menu_submenu_end(nt_ui_menu_t *menu);
void nt_ui_menu_separator(nt_ui_menu_t *menu);                         /* non-interactive divider (no focus) */
void nt_ui_menu_separator_text(nt_ui_menu_t *menu, const char *label); /* optional section header */
void nt_ui_menu_end(nt_ui_menu_t *menu);

/* Opens the menu with its root at (x, y) in UI space and starts its navigation fresh; the press of this
 * frame (usually the one that opened it) does not dismiss it. When to open is the game's call: a
 * right-click (nt_ui_pointer_pos gives the point), a long-press on its widget, a button. */
void nt_ui_menu_open(nt_ui_menu_t *menu, float x, float y);

#ifdef NT_TEST_ACCESS
/* Pure barycentric point-in-triangle. No GL, no ctx — directly unit-tested. */
bool nt_ui_menu_test_point_in_tri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy);

/* The x of the submenu edge the corridor aims at: the edge facing the apex. */
float nt_ui_menu_test_aim_near_x(float sub_x, float sub_w, float apex_x);

/* The hover-intent decision for the corridor `aim` toward the open submenu rect: true KEEPS the submenu
 * (the cursor aims at it or sits over it), false allows a sibling switch once the off-corridor dwell
 * passed AIM_FALLBACK. An unprimed aim latches its apex at the cursor and keeps. */
bool nt_ui_menu_test_hover_intent(nt_ui_menu_aim_t *aim, float mouse_x, float mouse_y, float sub_x, float sub_y, float sub_w, float sub_h, float dt);

/* A level's panel id, for prev-frame bbox queries. */
uint32_t nt_ui_menu_test_panel_id(uint32_t menu_id, uint8_t depth);

/* A row's sub-element ids (submenu arrow, icon gutter, shortcut text, selected checkmark), derived from
 * the row id, for prev-frame bbox queries. */
uint32_t nt_ui_menu_test_arrow_id(uint32_t row_id);
uint32_t nt_ui_menu_test_icon_id(uint32_t row_id);
uint32_t nt_ui_menu_test_shortcut_id(uint32_t row_id);
uint32_t nt_ui_menu_test_check_id(uint32_t row_id);

/* Row id probe: the id of `key` under `scope_id` (the menu id at the root, the parent row id below it). */
uint32_t nt_ui_menu_test_item_id(uint32_t scope_id, uint32_t key);
#endif

#endif /* NT_UI_MENU_H */
