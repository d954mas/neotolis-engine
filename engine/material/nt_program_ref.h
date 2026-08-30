#ifndef NT_PROGRAM_REF_H
#define NT_PROGRAM_REF_H

#include "graphics/nt_gfx.h"
#include "resource/nt_resource.h"

/*
 * A program linked from two pack-loaded shader stages.
 *
 * The stages arrive asynchronously, so the game cannot link at startup; it holds
 * this and calls update() every frame until the link happens. The ref stores the
 * resource handles rather than the compiled stages or the source text, because
 * only the handles survive a context loss: nt_resource_invalidate() puts the
 * assets back to REGISTERED, the next resource step recompiles them from the
 * resident blob, and update() links again with nothing else to remember.
 *
 * The program is the game's, as always -- it sits in the game's own struct and
 * the game destroys it with drop(). Materials only borrow the handle.
 *
 * A shader embedded as a source string needs none of this: nt_gfx_make_shader
 * plus nt_gfx_make_program at init, with nothing to wait for.
 */
typedef struct {
    nt_resource_t vs; /* set once by the game */
    nt_resource_t fs;
    nt_program_t program; /* filled by update(), cleared by drop() */
} nt_program_ref_t;

/* Links once both stages resolve. Returns true only on the frame it links, so a
 * caller can assign the program to its materials exactly once and needs no latch
 * of its own. Safe and free to call every frame. */
static inline bool nt_program_ref_update(nt_program_ref_t *ref) {
    if (ref->program.id != 0) {
        return false;
    }
    uint32_t vs = nt_resource_get(ref->vs);
    uint32_t fs = nt_resource_get(ref->fs);
    if (vs == 0 || fs == 0) {
        return false;
    }
    /* Invalid means a lost context; the next frame retries. */
    ref->program = nt_gfx_make_program((nt_shader_t){vs}, (nt_shader_t){fs});
    return ref->program.id != 0;
}

/* Destroys the program and clears the handle, which re-arms update(). This is
 * the whole of what a context restore needs from the ref. */
static inline void nt_program_ref_drop(nt_program_ref_t *ref) {
    nt_gfx_destroy_program(ref->program);
    ref->program = NT_PROGRAM_INVALID;
}

#endif /* NT_PROGRAM_REF_H */
