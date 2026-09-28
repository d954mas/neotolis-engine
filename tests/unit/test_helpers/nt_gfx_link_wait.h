#ifndef NT_GFX_LINK_WAIT_H
#define NT_GFX_LINK_WAIT_H

#include "graphics/nt_gfx.h"

/* Polls a started link to its end across frames, as a game loop does; a
 * link-time assert fires inside the caller. False when the link failed. */
static inline bool nt_test_wait_program(nt_program_t prog) {
    for (uint32_t frame = 0; frame < 1000000U; frame++) {
        const nt_gfx_program_state_t state = nt_gfx_program_poll(prog);
        if (state != NT_GFX_PROGRAM_LINKING) {
            return state == NT_GFX_PROGRAM_READY;
        }
        nt_gfx_begin_frame();
    }
    return false;
}

#endif /* NT_GFX_LINK_WAIT_H */
