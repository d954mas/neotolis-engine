#ifndef NT_GFX_TEST_TICK_H
#define NT_GFX_TEST_TICK_H

#include "graphics/nt_gfx.h"

/* Rendering tests run inside one open tick. Shutdown discards an open tick,
 * so tearDown needs no matching end. */
static inline void nt_gfx_test_init(const nt_gfx_desc_t *desc) {
    nt_gfx_init(desc);
    nt_gfx_begin_tick();
}

/* Closes the open tick and opens the next one; live counters restart at zero. */
static inline void nt_gfx_test_next_tick(void) {
    nt_gfx_end_tick();
    nt_gfx_begin_tick();
}

#endif
