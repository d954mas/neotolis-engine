#ifndef NT_UI_TABBAR_H
#define NT_UI_TABBAR_H

/* Reusable tab-bar (WGT-02). A column (or row) of full-width click targets; the active tab carries an
 * accent bar + a selected fill, hovered tabs lighten. A click sets the game-owned `int *active` (Model D
 * — the widget never owns the index). No popup, no gesture cell: a tab is a plain click. Self-contained
 * so a vitrine can dogfood it (the showcase's ad-hoc tab_row migrates onto this).
 *
 * Lifted from examples/ui_showcase tab_row: transparent full-row button, 3px accent on the active row,
 * hover/selected bg from the style, click sets the active index. */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "ui/nt_ui.h" /* nt_ui_layer_t, nt_ui_widget_def_t, nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_TABBAR_DEF;

/* Layout direction of the bar. Vertical = a left-side nav list (the showcase look); horizontal = a
 * top tab strip. The accent bar sits on the leading edge (left for vertical, top for horizontal). */
typedef enum { NT_UI_TABBAR_VERTICAL = 0, NT_UI_TABBAR_HORIZONTAL } nt_ui_tabbar_dir_t;

/* Visual knobs. Colors are 0xAABBGGRR. */
typedef struct {
    uint32_t bar_bg;        /* the bar container background (0 = transparent) */
    uint32_t tab_bg;        /* unselected tab fill */
    uint32_t tab_selected;  /* selected tab fill */
    uint32_t tab_hover;     /* hovered tab fill */
    uint32_t accent;        /* active-tab accent bar color */
    uint32_t text;          /* unselected tab text */
    uint32_t text_selected; /* selected tab text */
    float font_size;        /* px; asserted > 0 */
    uint16_t tab_extent;    /* px height (vertical) or width (horizontal) of each tab */
    uint16_t accent_px;     /* px accent-bar thickness on the leading edge */
    uint16_t pad;           /* px inner padding */
    uint16_t gap;           /* px gap between tabs */
    uint16_t font_id;       /* label font */
    uint8_t dir;            /* nt_ui_tabbar_dir_t */
    nt_ui_layer_t layer;    /* draw layer */
} nt_ui_tabbar_style_t;
_Static_assert(sizeof(nt_ui_tabbar_style_t) == 44, "nt_ui_tabbar_style_t stable ABI (7 u32 + 1 float + 5 u16 + 1 u8 dir + 1 u8 layer)");

/* Valid baseline style (dark, vertical). */
nt_ui_tabbar_style_t nt_ui_tabbar_style_defaults(void);

/* Declare the tab-bar: a container holding `count` full-extent tabs. A click on tab i sets *active = i
 * (Model D). base_id salts each tab's id (base_id + i). The bar grows to fill its parent on the cross
 * axis. ctx/labels/active/style non-NULL; count >= 0; *active in [0,count) when count > 0 (or -1 = none).
 * Returns the index clicked this frame, or -1 if no tab was clicked. */
int nt_ui_tabbar(nt_ui_context_t *ctx, uint32_t base_id, const char *const *labels, int count, int *active, const nt_ui_tabbar_style_t *style);

#endif /* NT_UI_TABBAR_H */
