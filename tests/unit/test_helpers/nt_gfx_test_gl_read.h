#ifndef NT_GFX_TEST_GL_READ_H
#define NT_GFX_TEST_GL_READ_H

#include "graphics/nt_gfx_frame.h"

#include <stdint.h>

#include <glad/gl.h>

/* Mid-pass read for native GL tests: replays the recorded calls and reads RGBA8 from the bound
 * framebuffer (bottom-left origin), keeping the pass and its bound state open. */
static inline void nt_test_gl_read_in_pass(int x, int y, int width, int height, uint8_t *out) {
    nt_gfx_frame_execute();
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

#endif /* NT_GFX_TEST_GL_READ_H */
