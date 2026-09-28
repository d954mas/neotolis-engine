#ifndef NT_GFX_LINK_WAIT_H
#define NT_GFX_LINK_WAIT_H

#include "graphics/nt_gfx.h"

/* Polls a started link to its end across frames, as a game loop does; a
 * link-time assert fires inside the caller. False when the link failed. */
static inline bool nt_test_wait_program(nt_program_t prog) {
    for (uint32_t frame = 0; frame < 1000000U && nt_gfx_program_linking(prog); frame++) {
        nt_gfx_begin_frame();
    }
    return nt_gfx_program_ready(prog);
}

#endif /* NT_GFX_LINK_WAIT_H */
