#include "window/nt_window.h"

#include <math.h>

#include "core/nt_assert.h"

/* Global window state, shared by all platform backends. */
nt_window_t g_nt_window = {.max_dpr = 2.0F, .resizable = true};

/* Pre-swap hook registry (shared across backends; each backend's swap_buffers runs it). Small fixed
   array — heap-free, "use only what you need". Process-lifetime: a system registers once at startup. */
#ifndef NT_WINDOW_MAX_PRE_SWAP_HOOKS
#define NT_WINDOW_MAX_PRE_SWAP_HOOKS 4
#endif
static nt_window_pre_swap_hook_fn s_pre_swap_hooks[NT_WINDOW_MAX_PRE_SWAP_HOOKS];
static int s_pre_swap_hook_count;

void nt_window_add_pre_swap_hook(nt_window_pre_swap_hook_fn fn) {
    if (fn == NULL) {
        return;
    }
    for (int i = 0; i < s_pre_swap_hook_count; i++) {
        if (s_pre_swap_hooks[i] == fn) {
            return; /* idempotent: registering the same hook twice is a no-op. */
        }
    }
    NT_ASSERT(s_pre_swap_hook_count < NT_WINDOW_MAX_PRE_SWAP_HOOKS);
    s_pre_swap_hooks[s_pre_swap_hook_count++] = fn;
}

void nt_window_run_pre_swap_hooks(void) {
    for (int i = 0; i < s_pre_swap_hook_count; i++) {
        s_pre_swap_hooks[i]();
    }
}

void nt_window_apply_sizes(float canvas_w, float canvas_h, float device_dpr) {
    float effective_dpr = fminf(device_dpr, g_nt_window.max_dpr);
    if (effective_dpr < 1.0F) {
        effective_dpr = 1.0F; /* Safety: never go below 1x */
    }

    g_nt_window.width = (uint32_t)canvas_w;
    g_nt_window.height = (uint32_t)canvas_h;
    g_nt_window.dpr = effective_dpr;
    g_nt_window.fb_width = (uint32_t)roundf(canvas_w * effective_dpr);
    g_nt_window.fb_height = (uint32_t)roundf(canvas_h * effective_dpr);
}
