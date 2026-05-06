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

/* nt_gfx_gl_ctx_detect_gpu_caps is declared in graphics/nt_gfx_internal.h —
 * the stub backend also implements it, so the declaration lives at the
 * shared internal layer, not the GL-only header. */

/* Enable EXT_disjoint_timer_query_webgl2 (web) or check ARB_timer_query
 * support (native). Returns true if GL_TIME_ELAPSED queries are usable. */
bool nt_gfx_gl_ctx_enable_timer_query(void);

#endif /* NT_GFX_GL_CTX_H */
