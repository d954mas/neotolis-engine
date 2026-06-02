#ifndef NT_UI_ANIM_H
#define NT_UI_ANIM_H

/* Per-id animation cache. Open addressing with linear probing (max 4 probes):
 * lookup tries slot[h], slot[h+1], ... up to NT_UI_ANIM_PROBE_MAX. Eviction
 * (= snap reseed) only when all probes are occupied by other ids. */

#include <stdbool.h>
#include <stdint.h>

typedef struct nt_ui_context nt_ui_context_t;

#ifndef NT_UI_ANIM_SLOTS
#define NT_UI_ANIM_SLOTS 64 /* power-of-2 for the slot mask; ~28 B/slot */
#endif
_Static_assert((NT_UI_ANIM_SLOTS & (NT_UI_ANIM_SLOTS - 1)) == 0, "NT_UI_ANIM_SLOTS must be power-of-2 (slot = id & (N-1))");

#ifndef NT_UI_ANIM_PROBE_MAX
#define NT_UI_ANIM_PROBE_MAX 4 /* linear probes per lookup; 4 keeps lookup cache-friendly */
#endif

/* Smoothed per-id visual fields. */
typedef struct {
    uint32_t id; /* Clay element id keyed here; 0 = empty slot */
    float scale; /* eased render scale */
    float off_x; /* eased offset */
    float off_y;
    float opacity; /* eased opacity */
    float tint_t;  /* eased state-tint blend factor 0..1 (game maps to a color) */
    bool valid;
} nt_ui_anim_interaction_t;

/* Target the widget wants this frame; cur eases toward it. */
typedef struct {
    float scale, off_x, off_y, opacity, tint_t;
} nt_ui_anim_target_t;

/* Ease ctx->anim[slot(id)] toward target. transition_speed==0 → instant.
 * Returns the post-ease smoothed slot. */
const nt_ui_anim_interaction_t *nt_ui_anim(nt_ui_context_t *ctx, uint32_t id, const nt_ui_anim_target_t *target, float transition_speed);

#endif /* NT_UI_ANIM_H */
