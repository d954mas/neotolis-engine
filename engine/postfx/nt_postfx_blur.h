#ifndef NT_POSTFX_BLUR_H
#define NT_POSTFX_BLUR_H

#include "core/nt_types.h"
#include "graphics/nt_gfx.h"

#include <stdbool.h>
#include <stdint.h>

#define NT_POSTFX_BLUR_MAX_RADIUS 16
#define NT_POSTFX_BLUR_MAX_KERNEL ((NT_POSTFX_BLUR_MAX_RADIUS * 2) + 1)

typedef struct {
    nt_texture_t source;
    nt_render_target_t temp;
    nt_render_target_t dest;
    float radius;
    float sigma; /* 0 derives from radius. */
} nt_postfx_blur_pass_t;

/* The module owns only shader, program, pipeline, and fullscreen primitive state. */
void nt_postfx_blur_init(void);
void nt_postfx_blur_shutdown(void);
/* Rebuilds GPU resources after context restore; a loss during it is retried by the next restore. */
void nt_postfx_blur_restore_gpu(void);
/* Borrows a ready source texture and valid temp/dest targets of matching dimensions; source must be
 * R8/RG8/RGB8/RGBA8/RGBA16F/RGBA32F (sampler2D).
 * The source is sampled NEAREST with clamped edges; taps land on texel centres.
 * Runs outside a pass; its own passes start with scissor disabled. Pass and binding state are not restored.
 * The helper never creates, destroys, or stores caller handles.
 * True when both passes were recorded into this frame. False on a lost context or while the
 * helper's program links (after init and after a restore): dest is not written this frame. */
bool nt_postfx_blur_gaussian(const nt_postfx_blur_pass_t *pass);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_postfx_blur_test_build_kernel(float radius, float sigma, float out_weights[NT_POSTFX_BLUR_MAX_KERNEL]);
/* Borrowed static fragment source; never freed or modified by the caller. */
const char *nt_postfx_blur_test_fs_source(void);
#endif
// #endregion

#endif /* NT_POSTFX_BLUR_H */
