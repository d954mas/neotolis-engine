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
    /* Id recycle modulus: per-row id keys on (index % id_ring), so distinct ids per list are bounded
     * by id_ring, NOT the row count — a 10k list never saturates Clay's PERSISTENT element hashmap
     * (one permanent slot per distinct id ever declared). MUST exceed the max simultaneously-visible
     * window (begin hard-clamps the window to id_ring-1 so visible rows never alias). Default 256 ~ 256
     * hashmap slots/list, far beyond any real viewport. 0/1 disables recycling (absolute ids; can
     * saturate). */
    uint32_t id_ring;
} nt_ui_vlist_style_t;
_Static_assert(sizeof(nt_ui_vlist_style_t) == 104, "nt_ui_vlist_style_t stable ABI (88B scroll + i32 overscan + f32 gap + u32 id_ring + 4B tail pad, 8B aligned)");

/* Valid baseline: y-only-derived scroll defaults, 2-row overscan, no gap, id_ring 256. */
nt_ui_vlist_style_t nt_ui_vlist_style_defaults(void);

/* Recycling contract (id_ring): per-row ids are RECYCLED by a frame-stable ring (slot = index %
 * id_ring), so the same screen slot reuses one id as you scroll. Consequences for the game:
 *   - Dispatch per-row ACTIONS by the ABSOLUTE loop index, NEVER by the id (the id is shared by
 *     every row index that lands on the same slot).
 *   - Keep per-row PERSISTENT values game-owned (arrays/bitsets keyed by absolute index, re-fed each
 *     frame) — never hang them off the recycled id.
 *   - Transient UI state (hover / press anim / focus / capture) follows the screen SLOT, not the row
 *     content — standard immediate-mode virtualization.
 * id_ring MUST exceed the max visible window (begin clamps to be safe). */

/* Collision-safe per-item id: maps index onto a bounded ring slot (index % ring, ring>1) then fmixes
 * base_id + slot into one widely-spread id so per-slot retained state never aliases a neighbor's Clay
 * anon-child space (D-70-09). NEVER base_id+index / nt_ui_derived_id — additive ids collide (memory
 * clay_additive_id_collision). ring<=1 disables recycling (slot = index). Pure: same (base,index,ring)
 * -> same id every frame, and id_of(base,i,ring) == id_of(base,i+ring,ring). */
static inline uint32_t nt_ui_vlist_item_id_of(uint32_t base_id, uint32_t index, uint32_t ring) {
    const uint32_t slot = (ring > 1U) ? (index % ring) : index;
    uint32_t h = base_id * 0x9E3779B1U;
    h = (h ^ ((slot + 1U) * 0x85EBCA6BU));
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

/* Per-item id for the row currently being emitted (reads the active-vlist base id + id_ring). Use for
 * any per-row widget's id; the id RECYCLES by the ring (see the recycling contract above) so transient
 * UI state follows the screen SLOT — dispatch actions / store values by the absolute index, not the id. */
uint32_t nt_ui_vlist_item_id(nt_ui_context_t *ctx, uint32_t index);

#ifdef NT_TEST_ACCESS
/* Pure window math probe: derives {first,last} from a scroll pos (Clay negative-down sign),
 * viewport, item_extent, count, overscan — no Clay frame needed. Bounds-safe: count==0 ->
 * empty (first>last); item_extent<=0 / viewport<=0 -> {0,0}; last never exceeds count-1. */
nt_ui_vlist_range_t nt_ui_vlist_test_window(float pos_on_axis, float viewport, float item_extent, uint32_t count, int overscan);
#endif

#endif /* NT_UI_VLIST_H */
