#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"
#include "window/nt_window.h"

/* The engine `core` command group: ping / engine.info / view. First instance of
   the per-group #ifdef NT_DEVAPI_REGISTER_core pattern that later groups clone.
   The whole group compiles out when the define is absent. */

#ifdef NT_DEVAPI_REGISTER_core

/* Compile-time facts for engine.info — no runtime module registry. */
#ifndef NT_DEVAPI_ENGINE_VERSION
#define NT_DEVAPI_ENGINE_VERSION "unknown"
#endif
#ifndef NT_DEVAPI_BUILD_TYPE
#define NT_DEVAPI_BUILD_TYPE "unknown"
#endif
#ifndef NT_PRESET_NAME
#define NT_PRESET_NAME "unknown"
#endif

static bool cmd_ping(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    cJSON_AddBoolToObject(result, "pong", true);
    return true; /* success result is always an object */
}

/* Read-only veneer over g_nt_window (L2 rule: never mutates the window). */
static bool cmd_view(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    cJSON_AddNumberToObject(result, "fb_width", g_nt_window.fb_width);
    cJSON_AddNumberToObject(result, "fb_height", g_nt_window.fb_height);
    cJSON_AddNumberToObject(result, "width", g_nt_window.width); /* logical */
    cJSON_AddNumberToObject(result, "height", g_nt_window.height);
    cJSON_AddNumberToObject(result, "dpr", (double)g_nt_window.dpr);
    return true;
}

/* version/build/preset from compile-defs; "modules" = the active compiled-group
   list — the meaningful "linked modules" answer with no runtime registry. */
static bool cmd_engine_info(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    cJSON_AddStringToObject(result, "version", NT_DEVAPI_ENGINE_VERSION);
    cJSON_AddStringToObject(result, "build", NT_DEVAPI_BUILD_TYPE);
    cJSON_AddStringToObject(result, "preset", NT_PRESET_NAME);

    cJSON *modules = cJSON_AddArrayToObject(result, "modules");
    NT_ASSERT(modules != NULL);
    int n = nt_devapi_group_count();
    for (int i = 0; i < n; i++) {
        cJSON *name = cJSON_CreateString(nt_devapi_group_name(i));
        NT_ASSERT(name != NULL); /* OOM: trap rather than silently drop a module name. */
        cJSON_bool added = cJSON_AddItemToArray(modules, name);
        NT_ASSERT(added);
        (void)added;
    }
    return true;
}

static const nt_devapi_command_desc k_core_cmds[] = {
    {
        .method = "ping",
        .layer = "core",
        .summary = "liveness check",
        .params_shape = "{}",
        .result_shape = "{pong:bool}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "engine.info",
        .layer = "core",
        .summary = "engine version/build/preset + active command groups",
        .params_shape = "{}",
        .result_shape = "{version:string,build:string,preset:string,modules:string[]}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "view",
        .layer = "core",
        .summary = "framebuffer + logical size + device pixel ratio",
        .params_shape = "{}",
        .result_shape = "{fb_width:number,fb_height:number,width:number,height:number,dpr:number}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
};

static const nt_devapi_handler_fn k_core_handlers[] = {cmd_ping, cmd_engine_info, cmd_view};

void nt_devapi_register_core(void) {
    /* Engine-internal registration: a dup group/method here is a build-time collision
       (programming bug), so assert NT_OK. Capture first — NT_ASSERT compiles out under
       NT_ASSERT_MODE=0, so the call must NOT live inside the macro. */
    nt_result_t gr = nt_devapi_register_group("core"); /* group-name registered once */
    NT_ASSERT(gr == NT_OK);
    (void)gr;
    int n = (int)(sizeof(k_core_cmds) / sizeof(k_core_cmds[0]));
    for (int i = 0; i < n; i++) {
        nt_result_t rr = nt_devapi_register(&k_core_cmds[i], k_core_handlers[i], NULL);
        NT_ASSERT(rr == NT_OK);
        (void)rr;
    }
}

#endif /* NT_DEVAPI_REGISTER_core */
