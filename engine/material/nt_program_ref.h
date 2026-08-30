#ifndef NT_PROGRAM_REF_H
#define NT_PROGRAM_REF_H

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "resource/nt_resource.h"

/* Links a program from two pack-loaded shader stages once both resolve, and owns
 * it on the game's behalf. Stores the resource handles, not the compiled stages:
 * only the handles survive a context loss. Rationale and the recovery contract
 * live in docs/spec/render/shader.md.
 *
 * Not a source of the nt_material target -- game-side glue over two public APIs;
 * the material module still never links, destroys or inspects a program. */
typedef struct {
    nt_resource_t vs; /* set once by the game */
    nt_resource_t fs;
    nt_program_t program; /* filled by update(), cleared by drop() */
} nt_program_ref_t;

/* Links once both stages resolve. Returns true only on the frame it links, so a
 * caller can assign the program to its materials exactly once and needs no latch
 * of its own. Safe and free to call every frame.
 *
 * A ref never holds a dead program: one whose GPU object died with the context
 * is reclaimed here. Calling drop() in the restore branch is still the clear way
 * to say so, but forgetting it costs a frame rather than the session. */
static inline bool nt_program_ref_update(nt_program_ref_t *ref) {
    NT_ASSERT(ref != NULL && "nt_program_ref_update: ref is required");
    NT_ASSERT(ref->vs.id != 0 && ref->fs.id != 0 && "nt_program_ref: request both stage resources before update()");
    if (ref->program.id != 0) {
        if (nt_gfx_program_ready(ref->program)) {
            return false;
        }
        /* Readiness is terminal, so this handle is a corpse -- reclaim it and
         * relink below once the stages come back. */
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

/* Destroys the program and clears the handle, which re-arms update().
 *
 * This is the ref's whole part of a context restore. The rest is not its
 * business and still belongs to the game: reset every renderer that drew these
 * materials (*_restore_gpu), and nt_resource_invalidate() the shader-code asset
 * type so the stages recompile. */
static inline void nt_program_ref_drop(nt_program_ref_t *ref) {
    NT_ASSERT(ref != NULL && "nt_program_ref_drop: ref is required");
    nt_gfx_destroy_program(ref->program);
    ref->program = NT_PROGRAM_INVALID;
}

#endif /* NT_PROGRAM_REF_H */
