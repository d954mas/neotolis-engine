#ifndef NT_UI_VLIST_H
#define NT_UI_VLIST_H

/* Code-first virtualized-list clipper (== ImGui ImGuiListClipper, no fn-ptr indirection).
 * vlist_begin owns ONE internal nt_ui_scroll (one Clay clip), derives a {first,last} window
 * from the scroll pos + viewport + item_extent, and emits a LEADING spacer; the game loops
 * first..last; vlist_end emits the TRAILING spacer and closes the scroll. Leading+trailing
 * spacers size the content to count*extent so the existing scrollbar geometry stays correct.
 * A 10k-row list costs ~the visible count. Fixed item_extent, both axes (1-D). */

#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "ui/nt_ui.h"        /* nt_ui_element_data_t */
#include "ui/nt_ui_scroll.h" /* nt_ui_scroll_style_t */

typedef struct nt_ui_context nt_ui_context_t;

/* Scroll/window axis (D-70-08). Y = vertical list (default), X = horizontal. */
typedef enum { NT_UI_AXIS_Y = 0, NT_UI_AXIS_X } nt_ui_axis_t;

/* Inclusive visible window. EMPTY when first > last (e.g. count == 0): a
 * `for (i = r.first; i <= r.last; ++i)` loop body never runs. */
typedef struct {
    uint32_t first, last;
} nt_ui_vlist_range_t;

typedef struct {
    nt_ui_scroll_style_t scroll; /* the owned scroll's tunables (one clip) */
    int32_t overscan;            /* extra rows rendered each side of the viewport (hides recycle pop) */
    float gap;                   /* folded into the per-row stride (extent = item_extent + gap) */
} nt_ui_vlist_style_t;
_Static_assert(sizeof(nt_ui_vlist_style_t) == 96, "nt_ui_vlist_style_t stable ABI (88B scroll + i32 overscan + f32 gap)");

/* Valid baseline: y-only-derived scroll defaults, 2-row overscan, no gap. */
nt_ui_vlist_style_t nt_ui_vlist_style_defaults(void);

/* Collision-safe per-item id: fmix folding base_id + index into one widely-spread id so
 * per-item retained state survives recycle and never aliases a neighbor's Clay anon-child
 * space (D-70-09). NEVER base_id+index / nt_ui_derived_id — additive ids collide
 * (memory clay_additive_id_collision). Pure: same (base,index) -> same id every frame. */
static inline uint32_t nt_ui_vlist_item_id_of(uint32_t base_id, uint32_t index) {
    uint32_t h = base_id * 0x9E3779B1U;
    h = (h ^ ((index + 1U) * 0x85EBCA6BU));
    h = (h ^ (h >> 13)) * 0xC2B2AE35U;
    h = h ^ (h >> 16);
    return (h != 0U) ? h : 1U; /* 0 = "no widget" sentinel */
}

/* Opens the owned scroll (one Clay clip), emits the leading spacer, returns the visible
 * window. `id` is the vlist identity (also the scroll id). `count` rows of `item_extent`
 * along `axis`. `style` NULL -> defaults. `decl` supplies the scroll container's sizing/
 * padding (id/clip/userData are engine-owned). Must be balanced with nt_ui_vlist_end. */
nt_ui_vlist_range_t nt_ui_vlist_begin(nt_ui_context_t *ctx, const nt_ui_element_data_t *data, uint32_t id, uint32_t count, float item_extent, nt_ui_axis_t axis, const nt_ui_vlist_style_t *style,
                                      const Clay_ElementDeclaration *decl);

/* Emits the trailing spacer and closes the scroll. */
void nt_ui_vlist_end(nt_ui_context_t *ctx);

/* Per-item id for the row currently being emitted (reads the active-vlist base id). Use for
 * any per-row widget's id so its retained state follows the item across recycle. */
uint32_t nt_ui_vlist_item_id(nt_ui_context_t *ctx, uint32_t index);

#ifdef NT_TEST_ACCESS
/* Pure window math probe: derives {first,last} from a scroll pos (Clay negative-down sign),
 * viewport, item_extent, count, overscan — no Clay frame needed. Bounds-safe: count==0 ->
 * empty (first>last); item_extent<=0 / viewport<=0 -> {0,0}; last never exceeds count-1. */
nt_ui_vlist_range_t nt_ui_vlist_test_window(float pos_on_axis, float viewport, float item_extent, uint32_t count, int overscan);
#endif

#endif /* NT_UI_VLIST_H */
