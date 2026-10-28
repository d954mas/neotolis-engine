#ifndef NT_GFX_TEST_CAPTURE_H
#define NT_GFX_TEST_CAPTURE_H

#include "graphics/nt_gfx.h"
#include "graphics/nt_gfx_internal.h"

#if NT_GFX_CAPTURE_ENABLED
/* The frame recording now, or the last finalized one when none records: tests check records while
 * the frame that makes them is still open. */
static inline nt_gfx_capture_view_t nt_test_capture_latest(void) {
    if (!g_nt_gfx.capture_recording) {
        return nt_gfx_capture_read();
    }
    nt_gfx_capture_view_t view = g_nt_gfx_capture.view;
    view.events = g_nt_gfx_capture.events; /* a recording frame always has its array */
    return view;
}
#endif

#endif /* NT_GFX_TEST_CAPTURE_H */
