#include "graphics/gl/nt_gfx_gl_ctx.h"

/* On desktop the window library (GLFW, SDL) creates the GL context.
   The GL backend assumes a current context already exists.
   When glad is vendored, gl_ctx_create will call gladLoadGL(). */

bool nt_gfx_gl_ctx_create(const nt_gfx_desc_t *desc) {
    (void)desc;
    return true;
}

void nt_gfx_gl_ctx_destroy(void) {}

bool nt_gfx_gl_ctx_is_lost(void) { return false; }
