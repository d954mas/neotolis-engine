#include "graphics/nt_gfx.h"

#include "core/nt_assert.h"

#include <string.h>

/* Pure identity packer, compiled into the real gfx and the stub alike. */

// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- NT_ASSERT expansion inflates the metric
nt_gfx_pipeline_key_t nt_gfx_pipeline_key(const nt_pipeline_desc_t *desc) {
    NT_ASSERT(desc != NULL);
    NT_ASSERT(desc->cull_mode <= 2 && "cull_mode out of range");
    NT_ASSERT((uint32_t)desc->depth_func <= NT_DEPTH_ALWAYS && "depth_func out of range");
    NT_ASSERT(desc->blend.src_rgb <= NT_BLEND_SRC_ALPHA_SATURATE && desc->blend.dst_rgb <= NT_BLEND_SRC_ALPHA_SATURATE && desc->blend.src_alpha <= NT_BLEND_SRC_ALPHA_SATURATE &&
              desc->blend.dst_alpha <= NT_BLEND_SRC_ALPHA_SATURATE && "blend factor out of range");
    NT_ASSERT(desc->blend.op_rgb <= NT_BLEND_OP_MAX && desc->blend.op_alpha <= NT_BLEND_OP_MAX && "blend op out of range");
    /* Identity only: a disabled blend is opaque whatever its (valid) factors say. */
    const nt_blend_state_t blend = desc->blend.enabled ? desc->blend : nt_blend_opaque();
    nt_gfx_pipeline_key_t key;
    key.bits = (uint64_t)desc->program.id | (uint64_t)(desc->depth_test ? 1U : 0U) << 32 | (uint64_t)(desc->depth_write ? 1U : 0U) << 33 | (uint64_t)(desc->polygon_offset ? 1U : 0U) << 34 |
               (uint64_t)(blend.enabled ? 1U : 0U) << 35 | (uint64_t)desc->depth_func << 36 | (uint64_t)desc->cull_mode << 38 | (uint64_t)blend.src_rgb << 40 | (uint64_t)blend.dst_rgb << 44 |
               (uint64_t)blend.src_alpha << 48 | (uint64_t)blend.dst_alpha << 52 | (uint64_t)blend.op_rgb << 56 | (uint64_t)blend.op_alpha << 59;
    /* Bit patterns: exact, so -0.0 vs 0.0 can only over-split, never alias. Disabled offset packs as zero. */
    const float float_bits[6] = {blend.constant_color[0],
                                 blend.constant_color[1],
                                 blend.constant_color[2],
                                 blend.constant_color[3],
                                 desc->polygon_offset ? desc->polygon_offset_factor : 0.0F,
                                 desc->polygon_offset ? desc->polygon_offset_units : 0.0F};
    memcpy(key.float_bits, float_bits, sizeof(key.float_bits));
    return key;
}
