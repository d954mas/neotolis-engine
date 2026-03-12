#ifndef NT_GFX_GL_CTX_H
#define NT_GFX_GL_CTX_H

#include <stdbool.h>

#include "graphics/nt_gfx.h"

/* GL context platform abstraction.
   Implemented per-platform in nt_gfx_gl_ctx_web.c / nt_gfx_gl_ctx_native.c.
   Keeps all Emscripten / GLFW / OS calls out of the shared GL backend. */

bool nt_gfx_gl_ctx_create(const nt_gfx_desc_t *desc);
void nt_gfx_gl_ctx_destroy(void);
bool nt_gfx_gl_ctx_is_lost(void);

#endif /* NT_GFX_GL_CTX_H */
