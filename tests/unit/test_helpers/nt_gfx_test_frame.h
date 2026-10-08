#ifndef NT_GFX_TEST_FRAME_H
#define NT_GFX_TEST_FRAME_H

#include "graphics/nt_gfx.h"

#include <stdbool.h>

/* Frame helpers for fake-based renderer tests: the fake observes a frame once nt_gfx_end_frame
 * has executed it. Each test file is its own executable, so one flag per header include is enough. */

static bool s_nt_test_frame_ended; /* the frame was ended and not reopened: tearDown must not close again */

/* Opens the pass every renderer test draws in; setUp calls it to start each test with an open pass. */
static inline void nt_test_frame_begin_pass(void) {
    nt_gfx_begin_pass(&(nt_pass_desc_t){.clear_depth = 1.0F});
    s_nt_test_frame_ended = false;
}

static inline void nt_test_frame_open(void) {
    nt_gfx_begin_frame();
    nt_test_frame_begin_pass();
}

static inline void nt_test_frame_end(void) {
    nt_gfx_end_frame();
    s_nt_test_frame_ended = true;
}

static inline void nt_test_frame_close(void) {
    nt_gfx_end_pass();
    nt_test_frame_end();
}

static inline void nt_test_frame_next(void) {
    nt_test_frame_close();
    nt_test_frame_open();
}

/* Survives a failed assert after nt_test_frame_close: closes only a frame that is still open. */
static inline void nt_test_frame_teardown(void) {
    if (!s_nt_test_frame_ended) {
        nt_test_frame_close();
    }
}

#endif /* NT_GFX_TEST_FRAME_H */
