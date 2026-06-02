#ifndef NT_UI_ANIM_H
#define NT_UI_ANIM_H

/* Direct-mapped per-id animation cache. Fixed power-of-2 array, key = Clay
 * uint32 id (already a hash), replace-on-collision, no LRU. Widgets set a
 * target each frame; the cache eases the stored value toward it. */

#include <stdbool.h>
#include <stdint.h>

typedef struct nt_ui_context nt_ui_context_t;

#ifndef NT_UI_ANIM_SLOTS
#define NT_UI_ANIM_SLOTS 64 /* power-of-2 for the slot mask; ~28 B/slot */
#endif
_Static_assert((NT_UI_ANIM_SLOTS & (NT_UI_ANIM_SLOTS - 1)) == 0, "NT_UI_ANIM_SLOTS must be power-of-2 (slot = id & (N-1))");

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
