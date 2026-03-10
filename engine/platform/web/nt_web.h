#ifndef NT_WEB_H
#define NT_WEB_H

/**
 * @file nt_web.h
 * @brief Web platform utilities for WASM builds.
 *
 * Provides C-callable functions for web-specific shell interactions.
 * On non-web platforms, functions are no-ops.
 */

#include "core/nt_platform.h"

#ifdef NT_PLATFORM_WEB

#include <emscripten.h>

/**
 * Hide the loading spinner in the HTML shell.
 * Call this once the game has finished initialization and is ready to render.
 * Safe to call multiple times (subsequent calls are no-ops).
 */
/* clang-format off */
EM_JS(void, nt_web_loading_complete, (void), {
    var el = document.getElementById('spinner');
    if (el) el.style.display = 'none';
})
/* clang-format on */

#else

static inline void nt_web_loading_complete(void) { /* No-op on non-web platforms */ }

#endif /* NT_PLATFORM_WEB */

#endif /* NT_WEB_H */
