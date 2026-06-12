#ifndef NT_UI_STATE_H
#define NT_UI_STATE_H

/* Generic per-widget-id retained-state pool. Direct-mapped, linear-probe, NO
 * eviction: a cell dies only via clear / clear_all / context destroy, so a cell
 * the game stored a pointer in is never silently reclaimed (leak-safe escape hatch).
 * Returned pointer is NOT valid across frames — re-acquire by id each frame. Stores
 * view/interaction state only; logical values stay game-owned. */

#include <stdbool.h>
#include <stdint.h>

typedef struct nt_ui_context nt_ui_context_t;

/* Derive a sibling cell id from a widget's base id (drag/view/scrollbar sub-cells).
 * Folds the base==salt case to salt so every derived id stays nonzero (id 0 is the
 * empty-slot sentinel). Salts are sparse high-bit constants, never a real low id. */
static inline uint32_t nt_ui_derived_id(uint32_t base, uint32_t salt) {
    const uint32_t id = base ^ salt;
    return (id != 0U) ? id : salt;
}

#ifndef NT_UI_STATE_SLOTS
#define NT_UI_STATE_SLOTS 64 /* power-of-2; slot = id & (N-1) */
#endif
#ifndef NT_UI_STATE_PAYLOAD_MAX
#define NT_UI_STATE_PAYLOAD_MAX 64 /* bytes; > this => game stores its own pointer in the cell */
#endif
#ifndef NT_UI_STATE_PROBE_MAX
#define NT_UI_STATE_PROBE_MAX 4
#endif
_Static_assert((NT_UI_STATE_SLOTS & (NT_UI_STATE_SLOTS - 1)) == 0, "NT_UI_STATE_SLOTS must be power-of-2 (slot = id & (N-1))");

typedef struct {
    uint32_t id;   /* 0 = empty slot */
    uint32_t size; /* re-acquire asserts size match */
    uint32_t tag;  /* owner widget tag; re-acquire asserts match (cross-widget aliasing trap) */
    uint8_t payload[NT_UI_STATE_PAYLOAD_MAX];
} nt_ui_state_cell_t;

/* 4-char owner tag (e.g. NT_UI_STATE_TAG('s','c','r','l')); folds the widget identity
 * into the cell so two different widgets colliding on one id+size trap, not alias silently. */
#define NT_UI_STATE_TAG(a, b, c, d) (((uint32_t)(uint8_t)(a)) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

/* Get-or-create. Cell is ZEROED on create (zero must be a valid initial state). `tag`
 * identifies the owning widget — re-acquire asserts both size AND tag match, so two
 * widgets that hash to the same id trap instead of aliasing. Overflow / oversize assert. */
void *nt_ui_state(nt_ui_context_t *ctx, uint32_t id, uint32_t size, uint32_t tag);

/* NULL if absent — no create, no assert on a miss. */
void *nt_ui_state_find(nt_ui_context_t *ctx, uint32_t id);

/* True when a live cell exists under `id` with exactly `tag` — cheap widget-kind probe
 * that works in ALL builds (the debug widget registry is DEBUG_TOOLS-only). */
bool nt_ui_state_has_tag(const nt_ui_context_t *ctx, uint32_t id, uint32_t tag);

/* Explicit free; the game frees any owned pointer BEFORE this. id==0 is a no-op. */
void nt_ui_state_clear(nt_ui_context_t *ctx, uint32_t id);

/* Bulk free — screen transitions. */
void nt_ui_state_clear_all(nt_ui_context_t *ctx);

/* Occupancy for the inspector "UI memory" line. */
uint32_t nt_ui_state_used_slots(const nt_ui_context_t *ctx);
uint32_t nt_ui_state_used_bytes(const nt_ui_context_t *ctx); /* sum of live cell sizes */

#endif /* NT_UI_STATE_H */
