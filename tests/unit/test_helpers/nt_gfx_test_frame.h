#ifndef NT_GFX_TEST_FRAME_H
#define NT_GFX_TEST_FRAME_H

#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"

#include <stdbool.h>
#include <time.h>

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

/* With the test pass open and nothing drawn yet: the next begin_frame finishes the fake's pending
 * program links, so a test calls this after making its programs and before it draws. */
static inline void nt_test_frame_finish_links(void) { nt_test_frame_next(); }

/* A driver with parallel compile may need several polls to finish a link, so a link wait is
 * bounded by time. Tests that expect an assert from a link check its message, not just that one fired. */
static inline struct timespec nt_test_link_wait_start(void) {
    struct timespec start;
    (void)timespec_get(&start, TIME_UTC);
    return start;
}

static inline void nt_test_link_wait_check(const struct timespec *start) {
    struct timespec now;
    (void)timespec_get(&now, TIME_UTC);
    NT_ASSERT(now.tv_sec - start->tv_sec < 10 && "program link did not finish within 10 s");
}

/* In an open frame with no pass: starts frames until the program's link finished, leaving a frame open. */
static inline void nt_test_gfx_link_wait(nt_program_t program) {
    const struct timespec start = nt_test_link_wait_start();
    for (;;) {
        nt_gfx_end_frame();
        nt_gfx_begin_frame();
        if (nt_gfx_program_ready(program)) {
            return;
        }
        nt_test_link_wait_check(&start);
    }
}

/* Survives a failed assert after nt_test_frame_close: closes only a frame that is still open. */
static inline void nt_test_frame_teardown(void) {
    if (!s_nt_test_frame_ended) {
        nt_test_frame_close();
    }
}

#endif /* NT_GFX_TEST_FRAME_H */
