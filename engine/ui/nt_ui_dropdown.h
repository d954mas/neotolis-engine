#ifndef NT_UI_DROPDOWN_H
#define NT_UI_DROPDOWN_H

/* Dropdown / combobox (WGT-01) built ON popup-core (Plan 03) + the Phase-59 scroll wrapper. A trigger
 * button shows the current selection; clicking it toggles a game-owned `bool open`. The open list is a
 * popup-core floating anchored to the trigger's bottom-left with edge-flip up near the bottom border;
 * its rows are nt_ui_buttons. A row click writes the row index into the game-owned `int *selected` and
 * raises close. A long list is wrapped in nt_ui_scroll_begin/end (NOT a raw Clay .clip, which leaks a
 * scroll-container pool slot -> type=7 crash, Pitfall 7).
 *
 * Model D: the GAME owns `int *selected` and `bool *open`; the widget only signals. */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "ui/nt_ui.h" /* nt_ui_layer_t, nt_ui_widget_def_t, nt_ui_element_data_t */

typedef struct nt_ui_context nt_ui_context_t;

extern const nt_ui_widget_def_t NT_UI_DROPDOWN_DEF;

/* Visual knobs. Colors are 0xAABBGGRR. The list panel + rows are plain rects + labels; the trigger
 * border/fill double as the row look so the popup reads as one widget. */
typedef struct {
    uint32_t trigger_bg;       /* trigger button fill */
    uint32_t trigger_text;     /* trigger label color */
    uint32_t panel_bg;         /* list panel background */
    uint32_t row_text;         /* enabled row text */
    uint32_t row_hover_bg;     /* hovered/selected row highlight */
    uint32_t row_selected_bg;  /* current selection row fill */
    float font_size;           /* px; asserted > 0 */
    uint16_t row_height;       /* px list row height */
    uint16_t min_width;        /* px list panel min width */
    uint16_t pad;              /* px inner padding */
    uint16_t font_id;          /* label font */
    uint16_t max_visible_rows; /* rows shown before the list scrolls (0 = no cap, never scrolls) */
    uint8_t _pad[2];           /* layer comes from the call (data->layer), NOT the style — mirrors checkbox */
} nt_ui_dropdown_style_t;
_Static_assert(sizeof(nt_ui_dropdown_style_t) == 40, "nt_ui_dropdown_style_t stable ABI (6 u32 + 1 float + 5 u16 + 2 pad)");

/* Valid baseline style (dark). */
nt_ui_dropdown_style_t nt_ui_dropdown_style_defaults(void);

/* Trigger button: shows the label of the current *selected (or `placeholder` when out of range / -1)
 * and toggles *open on click. Declared like any other widget inside the layout. id/labels/selected/
 * open/style non-NULL; count >= 0; *selected in [-1,count). Returns true on the frame it toggled.
 * Layers: the trigger fill draws on data->layer, its label on label_layer (split to batch). data may be
 * NULL (fill falls to layer 0). */
bool nt_ui_dropdown_trigger(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *const *labels, int count, int selected, const char *placeholder,
                            const nt_ui_dropdown_style_t *style, const Clay_ElementDeclaration *decl, bool *open);

/* The open list: a popup-core floating anchored to the trigger's bbox (queried by `id`) with edge-flip
 * up near the bottom border. Rows are nt_ui_buttons; a row click sets *selected and clears *open. A list
 * taller than style->max_visible_rows is wrapped in nt_ui_scroll (GC'd; no clip leak). Call every frame
 * AFTER the trigger; it self-balances when fully closed (no end needed by the caller). id/labels/
 * selected/open/style non-NULL; *selected in [-1,count). Returns true if a selection was made this frame.
 * Layers: the panel + row fills draw on data->layer (also the popup panel layer), row text on label_layer
 * (split to batch). data may be NULL (fills fall to layer 0). */
bool nt_ui_dropdown_list(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint8_t label_layer, uint32_t id, const char *const *labels, int count, int *selected,
                         const nt_ui_dropdown_style_t *style, bool *open);

#ifdef NT_TEST_ACCESS
/* The popup side chosen for the list on the last nt_ui_dropdown_list call (edge-flip probe). */
uint8_t nt_ui_dropdown_test_last_side(void);
/* The scroll id the list used for its long-list wrapper (0 if the list did not scroll). */
uint32_t nt_ui_dropdown_test_scroll_id(uint32_t dropdown_id);
#endif

#endif /* NT_UI_DROPDOWN_H */
