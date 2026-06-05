#ifndef NT_UI_ANIM_H
#define NT_UI_ANIM_H

/* Per-id animation cache. Open-addressed linear probe up to NT_UI_ANIM_PROBE_MAX. */

#include <stdbool.h>
#include <stdint.h>

typedef struct nt_ui_context nt_ui_context_t;

#ifndef NT_UI_ANIM_SLOTS
#define NT_UI_ANIM_SLOTS 64 /* power-of-2 for the slot mask; ~52 B/slot post-3D-extension */
#endif
_Static_assert((NT_UI_ANIM_SLOTS & (NT_UI_ANIM_SLOTS - 1)) == 0, "NT_UI_ANIM_SLOTS must be power-of-2 (slot = id & (N-1))");

#ifndef NT_UI_ANIM_PROBE_MAX
#define NT_UI_ANIM_PROBE_MAX 4
#endif

/* All 9 nt_ui_transform_t fields are eased independently for 3D widget animation (card-flip,
 * world-mounted HP-bar slide). Euler rotation lerps suffer gimbal-lock wobble on sustained
 * 2-axis tweens (>~30° simultaneous rot_x + rot_y); single-axis is precise. For complex
 * orientation animations the game should drive rotation via a quaternion path itself and
 * feed the resulting Euler to nt_ui_anim only as the final state. */
typedef struct {
    uint32_t id; /* 0 = empty slot */
    float scale_x, scale_y, scale_z;
    float off_x, off_y, off_z;
    float rot_x, rot_y, rot_z;
    float opacity;
    float tint_t; /* 0..1 (game maps to a color) */
    bool valid;
} nt_ui_anim_interaction_t;

typedef struct {
    float scale_x, scale_y, scale_z;
    float off_x, off_y, off_z;
    float rot_x, rot_y, rot_z;
    float opacity;
    float tint_t;
} nt_ui_anim_target_t;

/* transition_speed==0 → instant; returns the post-ease slot. */
const nt_ui_anim_interaction_t *nt_ui_anim(nt_ui_context_t *ctx, uint32_t id, const nt_ui_anim_target_t *target, float transition_speed);

#endif /* NT_UI_ANIM_H */
