#include "core/nt_assert.h"
#include "core/nt_core.h"
#include "devapi/nt_devapi_internal.h"
#include "window/nt_window.h"

/* Engine `core` command group: ping / engine.info / view. Compiles out entirely
   when NT_DEVAPI_REGISTER_core is absent. */

#ifdef NT_DEVAPI_REGISTER_core

/* Compile-time facts for engine.info — no runtime module registry. */
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

/* Append a group-name string to `arr` (OOM traps, never silently dropped). */
static void emit_group(cJSON *arr, const char *name) {
    cJSON *item = cJSON_CreateString(name);
    NT_ASSERT(item != NULL);
    cJSON_bool added = cJSON_AddItemToArray(arr, item);
    NT_ASSERT(added);
    (void)added;
}

/* version/build/preset from compile-defs; "modules" = the distinct active groups. */
static bool cmd_engine_info(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    devapi_add_string(result, "version", nt_engine_version_string());
    devapi_add_string(result, "build", NT_DEVAPI_BUILD_TYPE);
    devapi_add_string(result, "preset", NT_PRESET_NAME);

    cJSON *modules = cJSON_AddArrayToObject(result, "modules");
    NT_ASSERT(modules != NULL);
    int n = nt_devapi_registry_count();
    for (int i = 0; i < n; i++) {
        if (nt_devapi_group_is_first(i)) {
            emit_group(modules, nt_devapi_registry_slot(i)->group);
        }
    }
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
        .summary = "engine version/build/preset + active command groups",
        .params_shape = "{}",
        .result_shape = "{version:string,build:string,preset:string,modules:string[]}",
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

#endif /* NT_DEVAPI_REGISTER_core */
