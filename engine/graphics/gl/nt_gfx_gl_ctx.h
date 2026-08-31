#ifndef NT_GFX_GL_CTX_H
#define NT_GFX_GL_CTX_H

#include <stdbool.h>
#include <stdint.h>

#include "graphics/nt_gfx.h"

/* GL context platform abstraction.
   Implemented per-platform in nt_gfx_gl_ctx_web.c / nt_gfx_gl_ctx_native.c.
   Keeps all Emscripten / GLFW / OS calls out of the shared GL backend. */

bool nt_gfx_gl_ctx_create(const nt_gfx_desc_t *desc);
void nt_gfx_gl_ctx_destroy(void);
bool nt_gfx_gl_ctx_is_lost(void);

/* Cold reflection; the required output may remain unchanged on context loss. */
void nt_gfx_gl_ctx_get_programiv(uint32_t program, uint32_t query, int32_t *out_value);
/* Missing locations and interrupted context-loss queries return -1. */
int32_t nt_gfx_gl_ctx_get_uniform_location(uint32_t program, const char *name);

/* nt_gfx_gl_ctx_detect_gpu_caps is declared in graphics/nt_gfx_internal.h —
 * the stub backend also implements it, so the declaration lives at the
 * shared internal layer, not the GL-only header. */

/* Enable EXT_disjoint_timer_query_webgl2 (web) or check ARB_timer_query
 * support (native). Returns true if GL_TIME_ELAPSED queries are usable. */
bool nt_gfx_gl_ctx_enable_timer_query(void);

/* KHR_debug check (native: GL 4.3+ or KHR_debug extension; web: usually
 * absent). Returns true if glPushDebugGroup / glPopDebugGroup are safe to
 * call. The GL backend uses these to label GPU timer segments so RenderDoc
 * / Apitrace / gDEBugger show segment names as debug groups. */
bool nt_gfx_gl_ctx_enable_debug_groups(void);

/* Install a KHR_debug message callback that routes GL errors to NT_LOG_ERROR + assert, synchronously
 * so a breakpoint lands on the offending call. Native + NT_DEBUG only; no-op (returns false) in
 * release, on web, or when the driver lacks KHR_debug. */
bool nt_gfx_gl_ctx_enable_debug_callback(void);

#endif /* NT_GFX_GL_CTX_H */
