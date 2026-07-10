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

/* The module owns only shader, pipeline, and fullscreen primitive state. */
nt_result_t nt_postfx_blur_init(void);
void nt_postfx_blur_shutdown(void);
/* Rebuilds GPU resources after context restore. */
nt_result_t nt_postfx_blur_restore_gpu(void);
/* Borrows ready handles with matching dimensions. Source must be an R8/RG8/
 * RGB8/RGBA8/RGBA16F/RGBA32F sampler2D color texture; integer and depth
 * formats are invalid. Scissor must be disabled. The helper never changes
 * caller state or creates, resizes, destroys, or stores the handles. */
void nt_postfx_blur_gaussian(const nt_postfx_blur_pass_t *pass);

// #region test_access
#ifdef NT_TEST_ACCESS
uint32_t nt_postfx_blur_test_build_kernel(float radius, float sigma, float out_weights[NT_POSTFX_BLUR_MAX_KERNEL]);
uint32_t nt_postfx_blur_test_draw_count(void);
void nt_postfx_blur_test_reset_counters(void);
#endif
// #endregion

#endif /* NT_POSTFX_BLUR_H */
