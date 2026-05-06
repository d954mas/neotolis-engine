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

/* Detect compressed-format support — implementation lives per-platform. */
nt_gfx_gpu_caps_t nt_gfx_gl_ctx_detect_gpu_caps(void);

/* Enable EXT_disjoint_timer_query_webgl2 (web) or check ARB_timer_query
 * support (native). Returns true if GL_TIME_ELAPSED queries are usable. */
bool nt_gfx_gl_ctx_enable_timer_query(void);

#endif /* NT_GFX_GL_CTX_H */
