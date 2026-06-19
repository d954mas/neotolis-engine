#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "devapi/nt_devapi_internal.h"
#include "window/nt_window.h"

/* Engine `core` command group: ping / engine.info / view. Compiles out entirely
   when NT_DEVAPI_GROUP_CORE is absent. */

#ifdef NT_DEVAPI_GROUP_CORE

static bool cmd_ping(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    devapi_add_bool(result, "pong", true);
    return true; /* success result is always an object */
}

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

/* version/build/preset are engine-identity facts owned by nt_core. The active
   command-group list lives in `features`, so engine.info doesn't duplicate it. */
static bool cmd_engine_info(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    devapi_add_string(result, "version", nt_engine_version_string());
    devapi_add_string(result, "build", nt_engine_build_string());
    devapi_add_string(result, "preset", nt_engine_preset_string());
    return true;
}

static const nt_devapi_command_desc k_core_cmds[] = {
    {
        .method = "ping",
        .group = "core",
        .summary = "liveness check",
        .params_shape = "{}",
        .result_shape = "{pong:bool}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "engine.info",
        .group = "core",
        .summary = "engine version / build / preset",
        .params_shape = "{}",
        .result_shape = "{version:string,build:string,preset:string}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "view",
        .group = "core",
        .summary = "framebuffer + logical size + device pixel ratio",
        .params_shape = "{}",
        .result_shape = "{fb_width:number,fb_height:number,width:number,height:number,dpr:number}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
};

static const nt_devapi_handler_fn k_core_handlers[] = {cmd_ping, cmd_engine_info, cmd_view};
_Static_assert(sizeof(k_core_cmds) / sizeof(k_core_cmds[0]) == sizeof(k_core_handlers) / sizeof(k_core_handlers[0]), "core: descriptor/handler arrays must have equal length");

void nt_devapi_register_core(void) {
    /* Engine-internal dup is a build-time bug → assert NT_OK. Capture first: NT_ASSERT
       compiles out under NT_ASSERT_MODE=0, so the call must not live inside the macro. */
    int n = (int)(sizeof(k_core_cmds) / sizeof(k_core_cmds[0]));
    for (int i = 0; i < n; i++) {
        nt_result_t rr = nt_devapi_register(&k_core_cmds[i], k_core_handlers[i], NULL);
        NT_ASSERT(rr == NT_OK);
        (void)rr;
    }
}

#endif /* NT_DEVAPI_GROUP_CORE */
