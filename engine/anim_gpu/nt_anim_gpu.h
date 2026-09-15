#ifndef NT_ANIM_GPU_H
#define NT_ANIM_GPU_H

#include <stdint.h>

#include "graphics/nt_gfx.h"

/*
 * nt_anim_gpu — prepared GPU matrix source for one frame (spec 12).
 *
 * The staging and upload functions land with #476; this header pins the value
 * type the renderer and skin_comp already contract on.
 *
 * The texture is borrowed: a bank owns its texture, anim_gpu owns the dynamic
 * ones, and the binding neither owns nor frees anything. A binding is valid
 * until the context's next begin_frame or a graphics invalidation, so it is a
 * by-value frame result the game re-derives every frame, never stored state.
 * frame_epoch mirrors anim_gpu's counter so a binding that was not prepared
 * this frame asserts at draw; 0 is reserved and never a live epoch.
 */
typedef struct {
    nt_texture_t texture;    /* borrowed bank or dynamic texture */
    uint16_t x0, y0, x1, y1; /* origins of the two frames (CPU: x1==x0, y1==y0) */
    float alpha;             /* CPU: 0 */
    uint32_t frame_epoch;    /* 0 reserved */
} nt_deformation_binding_t;

_Static_assert(sizeof(nt_deformation_binding_t) == 20, "deformation binding ABI: 20 bytes");

#endif /* NT_ANIM_GPU_H */
