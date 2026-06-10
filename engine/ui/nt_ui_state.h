#ifndef NT_UI_STATE_H
#define NT_UI_STATE_H

/* Generic per-widget-id retained-state pool (#190). Direct-mapped, linear-probe,
 * NO eviction — a cell dies only via clear / clear_all / context destroy. That
 * no-eviction discipline keeps the D-59-09 game-owned-pointer escape hatch
 * leak-safe (a cell the game stored a pointer in is never silently reclaimed).
 *
 * Contract: the returned pointer is NOT valid across frames — re-acquire by id
 * each frame. Stores view/interaction state only (scroll offset/velocity, custom
 * widget userdata); logical values stay game-owned (Model D). */

#include <stdint.h>

typedef struct nt_ui_context nt_ui_context_t;

#ifndef NT_UI_STATE_SLOTS
#define NT_UI_STATE_SLOTS 64 /* power-of-2; slot = id & (N-1) */
#endif
#ifndef NT_UI_STATE_PAYLOAD_MAX
#define NT_UI_STATE_PAYLOAD_MAX 64 /* bytes; > this => game stores its own pointer in the cell (D-59-09) */
#endif
#ifndef NT_UI_STATE_PROBE_MAX
#define NT_UI_STATE_PROBE_MAX 4
#endif
_Static_assert((NT_UI_STATE_SLOTS & (NT_UI_STATE_SLOTS - 1)) == 0, "NT_UI_STATE_SLOTS must be power-of-2 (slot = id & (N-1))");

typedef struct {
    uint32_t id;   /* 0 = empty slot */
    uint32_t size; /* re-acquire asserts size match (D-59-08) */
    uint8_t payload[NT_UI_STATE_PAYLOAD_MAX];
} nt_ui_state_cell_t;

/* Get-or-create. Cell is ZEROED on create (zero must be a valid initial state).
 * Re-acquire with the same id asserts size matches. Overflow / oversize assert. */
void *nt_ui_state(nt_ui_context_t *ctx, uint32_t id, uint32_t size);

/* NULL if absent — no create, no assert on a miss. */
void *nt_ui_state_find(nt_ui_context_t *ctx, uint32_t id);

/* Explicit free; the game frees any owned pointer BEFORE this. id==0 is a no-op. */
void nt_ui_state_clear(nt_ui_context_t *ctx, uint32_t id);

/* Bulk free — screen transitions. */
void nt_ui_state_clear_all(nt_ui_context_t *ctx);

/* Occupancy for the inspector "UI memory" line. */
uint32_t nt_ui_state_used_slots(const nt_ui_context_t *ctx);
uint32_t nt_ui_state_used_bytes(const nt_ui_context_t *ctx); /* sum of live cell sizes */

#endif /* NT_UI_STATE_H */
