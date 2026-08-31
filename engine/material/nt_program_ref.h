#ifndef NT_PROGRAM_REF_H
#define NT_PROGRAM_REF_H

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "resource/nt_resource.h"

/* Game-side owner of a program linked from two shader resources; release with drop().
 * Stored resource handles let update() fetch replacement stages after context loss.
 * The material module does
 * not call this helper; recovery contract: docs/spec/render/shader.md. */
typedef struct {
    nt_resource_t vs; /* set once by the game */
    nt_resource_t fs;
    nt_program_t program; /* filled by update(), cleared by drop() */
} nt_program_ref_t;

/* ref is required, with both stage resources assigned. Returns true only when a program is linked.
 * Reclaims an unready owned program before retrying; linking waits for both stages to be ready.
 *
 * May be called each frame; the caller assigns ref->program to materials when true. */
static inline bool nt_program_ref_update(nt_program_ref_t *ref) {
    NT_ASSERT(ref != NULL && "nt_program_ref_update: ref is required");
    NT_ASSERT(ref->vs.id != 0 && ref->fs.id != 0 && "nt_program_ref: request both stage resources before update()");
    if (ref->program.id != 0) {
        if (nt_gfx_program_ready(ref->program)) {
            return false;
        }
        /* Lost readiness is terminal; reclaim before linking a replacement. */
        nt_gfx_destroy_program(ref->program);
        ref->program = NT_PROGRAM_INVALID;
    }
    uint32_t vs = nt_resource_get(ref->vs);
    uint32_t fs = nt_resource_get(ref->fs);
    if (vs == 0 || fs == 0) {
        return false;
    }
    /* A stage handle outlives its GPU object, so a resolved id is not enough:
     * after a context loss these stay non-zero until the asset re-activates. */
    if (!nt_gfx_shader_ready((nt_shader_t){vs}) || !nt_gfx_shader_ready((nt_shader_t){fs})) {
        return false;
    }
    /* Invalid means a lost context; the next frame retries. */
    ref->program = nt_gfx_make_program((nt_shader_t){vs}, (nt_shader_t){fs});
    return ref->program.id != 0;
}

/* ref is required. Destroys and clears its owned program; update() may link again.
 * Call before nt_gfx_shutdown or discarding the ref; never destroy ref->program directly.
 * Renderer resets and
 * shader-resource invalidation remain the game's responsibility. */
static inline void nt_program_ref_drop(nt_program_ref_t *ref) {
    NT_ASSERT(ref != NULL && "nt_program_ref_drop: ref is required");
    nt_gfx_destroy_program(ref->program);
    ref->program = NT_PROGRAM_INVALID;
}

#endif /* NT_PROGRAM_REF_H */
