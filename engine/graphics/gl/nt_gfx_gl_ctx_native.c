#include "core/nt_platform.h"

#ifndef NT_PLATFORM_WEB

#include "graphics/gl/nt_gfx_gl_ctx.h"

/* On desktop the window library (GLFW, SDL) creates the GL context.
   The GL backend assumes a current context already exists.
   When glad is vendored, gl_ctx_create will call gladLoadGL(). */

bool nt_gfx_gl_ctx_create(void) { return true; }

void nt_gfx_gl_ctx_destroy(void) {}

bool nt_gfx_gl_ctx_is_lost(void) { return false; }

#else
typedef int nt_gfx_gl_ctx_native_unused;
#endif
