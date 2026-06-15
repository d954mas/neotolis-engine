#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"

/* The `window` command group: view. It is the only group that reads g_nt_window, so it is
   gated by NT_DEVAPI_REGISTER_window (CMake option NT_DEVAPI_WINDOW) — turning it off lets
   devapi build without linking nt_window. */

#ifdef NT_DEVAPI_REGISTER_window
#include "window/nt_window.h"

/* Read-only veneer over g_nt_window — never mutates it. */
static bool cmd_view(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    devapi_add_number(result, "fb_width", g_nt_window.fb_width);
    devapi_add_number(result, "fb_height", g_nt_window.fb_height);
    devapi_add_number(result, "width", g_nt_window.width); /* logical */
    devapi_add_number(result, "height", g_nt_window.height);
    devapi_add_number(result, "dpr", (double)g_nt_window.dpr);
    return true;
}

static const nt_devapi_command_desc k_window_cmds[] = {
    {
        .method = "view",
        .group = "window",
        .summary = "framebuffer + logical size + device pixel ratio",
        .params_shape = "{}",
        .result_shape = "{fb_width:number,fb_height:number,width:number,height:number,dpr:number}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
};
static const nt_devapi_handler_fn k_window_handlers[] = {cmd_view};
_Static_assert(sizeof(k_window_cmds) / sizeof(k_window_cmds[0]) == sizeof(k_window_handlers) / sizeof(k_window_handlers[0]), "window: descriptor/handler arrays must have equal length");

void nt_devapi_register_window(void) {
    int n = (int)(sizeof(k_window_cmds) / sizeof(k_window_cmds[0]));
    for (int i = 0; i < n; i++) {
        nt_result_t rr = nt_devapi_register(&k_window_cmds[i], k_window_handlers[i], NULL);
        NT_ASSERT(rr == NT_OK);
        (void)rr;
    }
}

#endif /* NT_DEVAPI_REGISTER_window */
