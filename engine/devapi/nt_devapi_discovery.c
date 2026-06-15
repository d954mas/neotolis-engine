#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"

/* Discovery group: endpoints / command.describe / features — the self-describing
   surface a client reads without source. Always-on when devapi is built. */

/* Emit one descriptor: detail=false → {method,group,summary}; true → all 7 fields. */
static void emit_command(cJSON *arr, const nt_devapi_slot *slot, bool detail) {
    cJSON *obj = cJSON_CreateObject();
    NT_ASSERT(obj != NULL);
    cJSON_AddStringToObject(obj, "method", slot->method);
    cJSON_AddStringToObject(obj, "group", slot->group);
    cJSON_AddStringToObject(obj, "summary", slot->summary);
    if (detail) {
        cJSON_AddStringToObject(obj, "params_shape", slot->params_shape);
        cJSON_AddStringToObject(obj, "result_shape", slot->result_shape);
        cJSON_AddStringToObject(obj, "frame_behavior", slot->frame_behavior);
        cJSON_AddStringToObject(obj, "side_effects", slot->side_effects);
    }
    cJSON_bool added = cJSON_AddItemToArray(arr, obj);
    NT_ASSERT(added);
    (void)added;
}

/* result wraps a `commands` array — never a bare top-level array. */
static bool cmd_endpoints(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    /* detail is optional; if present it must be a bool (explicit over implicit). */
    bool detail = false;
    const cJSON *detail_item = cJSON_GetObjectItemCaseSensitive(params, "detail");
    if (detail_item != NULL) {
        if (!cJSON_IsBool(detail_item)) {
            err->code = NT_DEVAPI_ERR_BAD_PARAMS;
            err->message = "params.detail must be a bool";
            return false;
        }
        detail = cJSON_IsTrue(detail_item);
    }

    cJSON *commands = cJSON_AddArrayToObject(result, "commands");
    NT_ASSERT(commands != NULL);
    int n = nt_devapi_registry_count();
    for (int i = 0; i < n; i++) {
        emit_command(commands, nt_devapi_registry_slot(i), detail);
    }
    return true;
}

/* Full 7-field contract for params.method. Missing/non-string → bad_params; unknown → unknown_method. */
static bool cmd_command_describe(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *method_item = cJSON_GetObjectItemCaseSensitive(params, "method");
    if (!cJSON_IsString(method_item) || method_item->valuestring == NULL) {
        err->code = NT_DEVAPI_ERR_BAD_PARAMS;
        err->message = (method_item == NULL) ? "missing params.method" : "non-string params.method";
        return false;
    }

    const nt_devapi_slot *slot = nt_devapi_registry_find(method_item->valuestring);
    if (slot == NULL) {
        err->code = NT_DEVAPI_ERR_UNKNOWN_METHOD;
        err->message = "no command registered for this method";
        return false;
    }

    cJSON_AddStringToObject(result, "method", slot->method);
    cJSON_AddStringToObject(result, "group", slot->group);
    cJSON_AddStringToObject(result, "summary", slot->summary);
    cJSON_AddStringToObject(result, "params_shape", slot->params_shape);
    cJSON_AddStringToObject(result, "result_shape", slot->result_shape);
    cJSON_AddStringToObject(result, "frame_behavior", slot->frame_behavior);
    cJSON_AddStringToObject(result, "side_effects", slot->side_effects);
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

/* The distinct groups across all registered commands (engine + game). */
static bool cmd_features(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    cJSON *groups = cJSON_AddArrayToObject(result, "groups");
    NT_ASSERT(groups != NULL);
    int n = nt_devapi_registry_count();
    for (int i = 0; i < n; i++) {
        if (nt_devapi_group_is_first(i)) {
            emit_group(groups, nt_devapi_registry_slot(i)->group);
        }
    }
    return true;
}

/* Engine introspection commands; group "discovery". */
static const nt_devapi_command_desc k_discovery_cmds[] = {
    {
        .method = "endpoints",
        .group = "discovery",
        .summary = "list all registered commands (cheap; detail=true for full descriptors)",
        .params_shape = "{detail?:bool}",
        .result_shape = "{commands:[{method,group,summary}]|[{...7 fields}]}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "command.describe",
        .group = "discovery",
        .summary = "full self-describing contract for one command",
        .params_shape = "{method:string}",
        .result_shape = "{method,group,summary,params_shape,result_shape,frame_behavior,side_effects}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
    {
        .method = "features",
        .group = "discovery",
        .summary = "active command groups",
        .params_shape = "{}",
        .result_shape = "{groups:string[]}",
        .frame_behavior = "any",
        .side_effects = "none",
    },
};

static const nt_devapi_handler_fn k_discovery_handlers[] = {cmd_endpoints, cmd_command_describe, cmd_features};

void nt_devapi_register_discovery(void) {
    /* Engine-internal dup is a build-time bug → assert NT_OK. Capture first: NT_ASSERT
       compiles out under NT_ASSERT_MODE=0, so the call must not live inside the macro. */
    int n = (int)(sizeof(k_discovery_cmds) / sizeof(k_discovery_cmds[0]));
    for (int i = 0; i < n; i++) {
        nt_result_t rr = nt_devapi_register(&k_discovery_cmds[i], k_discovery_handlers[i], NULL);
        NT_ASSERT(rr == NT_OK);
        (void)rr;
    }
}
