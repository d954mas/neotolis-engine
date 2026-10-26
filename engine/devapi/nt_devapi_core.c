#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "devapi/nt_devapi_internal.h"
#include "version/nt_version.h"
#include "window/nt_window.h"

/* Engine `core` command group: ping / engine.info / view. */

static bool cmd_ping(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    devapi_add_bool(result, "pong", true);
    return true; /* success result is always an object */
}

/* Read-only veneer over g_nt_window — never mutates it. */
static bool cmd_view(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    devapi_add_number(result, "fb_width", g_nt_window.fb_width);
    devapi_add_number(result, "fb_height", g_nt_window.fb_height);
    devapi_add_number(result, "width", g_nt_window.width); /* logical */
    devapi_add_number(result, "height", g_nt_window.height);
    devapi_add_number(result, "dpr", (double)g_nt_window.dpr);
    return true;
}

/* Engine-identity facts: the commit-qualified version (nt_version) and build/preset (nt_core). */
static bool cmd_engine_info(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    devapi_add_string(result, "version", nt_version_engine());
    devapi_add_string(result, "build", nt_engine_build_string());
    devapi_add_string(result, "preset", nt_engine_preset_string());
    return true;
}

static const nt_devapi_command k_core_cmds[] = {
    {
        .method = "ping",
        .summary = "liveness check",
        .params_shape = "{}",
        .result_shape = "{pong:bool}",
        .fn = cmd_ping,
    },
    {
        .method = "engine.info",
        .summary = "engine version / build / preset",
        .params_shape = "{}",
        .result_shape = "{version:string,build:string,preset:string}",
        .fn = cmd_engine_info,
    },
    {
        .method = "view",
        .summary = "framebuffer + logical size + device pixel ratio",
        .params_shape = "{}",
        .result_shape = "{fb_width:number,fb_height:number,width:number,height:number,dpr:number}",
        .fn = cmd_view,
    },
};

void nt_devapi_register_core(void) { nt_devapi_register(k_core_cmds, NT_COUNTOF(k_core_cmds), NULL); }
