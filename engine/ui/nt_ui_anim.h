#ifndef NT_UI_ANIM_H
#define NT_UI_ANIM_H

/* Per-id animation cache. Open-addressed linear probe up to NT_UI_ANIM_PROBE_MAX. */

#include <stdbool.h>
#include <stdint.h>

typedef struct nt_ui_context nt_ui_context_t;

#ifndef NT_UI_ANIM_SLOTS
#define NT_UI_ANIM_SLOTS 64 /* power-of-2 for the slot mask; ~28 B/slot */
#endif
_Static_assert((NT_UI_ANIM_SLOTS & (NT_UI_ANIM_SLOTS - 1)) == 0, "NT_UI_ANIM_SLOTS must be power-of-2 (slot = id & (N-1))");

#ifndef NT_UI_ANIM_PROBE_MAX
#define NT_UI_ANIM_PROBE_MAX 4
#endif

typedef struct {
    uint32_t id; /* 0 = empty slot */
    float scale;
    float off_x;
    float off_y;
    float opacity;
    float tint_t; /* 0..1 (game maps to a color) */
    bool valid;
} nt_ui_anim_interaction_t;

typedef struct {
    float scale, off_x, off_y, opacity, tint_t;
} nt_ui_anim_target_t;

/* transition_speed==0 → instant; returns the post-ease slot. */
const nt_ui_anim_interaction_t *nt_ui_anim(nt_ui_context_t *ctx, uint32_t id, const nt_ui_anim_target_t *target, float transition_speed);

#endif /* NT_UI_ANIM_H */
