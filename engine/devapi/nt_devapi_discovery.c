#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"

/* Discovery group: endpoints / command.describe — the self-describing
   surface a client reads without source. Compiles out entirely when
   NT_DEVAPI_GROUP_DISCOVERY is absent. */

#ifdef NT_DEVAPI_GROUP_DISCOVERY

/* Emit one command: detail=false -> {method,summary}; true -> all 4 fields. */
static void emit_command(cJSON *obj, const nt_devapi_command *cmd, bool detail) {
    devapi_add_string(obj, "method", cmd->method);
    devapi_add_string(obj, "summary", cmd->summary);
    if (detail) {
        devapi_add_string(obj, "params_shape", cmd->params_shape);
        devapi_add_string(obj, "result_shape", cmd->result_shape);
    }
}

/* Append an empty object to arr and return it (OOM traps, never silently dropped). */
static cJSON *add_object(cJSON *arr) {
    cJSON *obj = cJSON_CreateObject();
    NT_ASSERT(obj != NULL);
    cJSON_bool added = cJSON_AddItemToArray(arr, obj);
    NT_ASSERT(added);
    (void)added;
    return obj;
}

/* result wraps a `commands` array — never a bare top-level array. */
static bool cmd_endpoints(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    /* detail is optional; if present it must be a bool (explicit over implicit). */
    bool detail = false;
    const cJSON *detail_item = cJSON_GetObjectItemCaseSensitive(params, "detail");
    if (detail_item != NULL) {
        if (!cJSON_IsBool(detail_item)) {
            return nt_devapi_fail(err, "params.detail must be a bool");
        }
        detail = cJSON_IsTrue(detail_item);
    }

    cJSON *commands = cJSON_AddArrayToObject(result, "commands");
    NT_ASSERT(commands != NULL);
    int n = nt_devapi_registry_count();
    for (int i = 0; i < n; i++) {
        emit_command(add_object(commands), nt_devapi_registry_slot(i)->cmd, detail);
    }
    return true;
}

/* Full contract for params.method. Missing/non-string -> bad_params; unknown -> unknown_method. */
static bool cmd_command_describe(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *method_item = cJSON_GetObjectItemCaseSensitive(params, "method");
    if (!cJSON_IsString(method_item) || method_item->valuestring == NULL) {
        return nt_devapi_fail(err, (method_item == NULL) ? "missing params.method" : "non-string params.method");
    }

    const nt_devapi_slot *slot = nt_devapi_registry_find(method_item->valuestring);
    if (slot == NULL) {
        return nt_devapi_fail_code(err, NT_DEVAPI_ERR_UNKNOWN_METHOD, "no command registered for this method");
    }
    emit_command(result, slot->cmd, true);
    return true;
}

static const nt_devapi_command k_discovery_cmds[] = {
    {
        .method = "endpoints",
        .summary = "list all registered commands (cheap; detail=true for full descriptors)",
        .params_shape = "{detail?:bool}",
        .result_shape = "{commands:[{method,summary}]|[{method,summary,params_shape,result_shape}]}",
        .fn = cmd_endpoints,
    },
    {
        .method = "command.describe",
        .summary = "full self-describing contract for one command",
        .params_shape = "{method:string}",
        .result_shape = "{method,summary,params_shape,result_shape}",
        .fn = cmd_command_describe,
    },
};

void nt_devapi_register_discovery(void) { nt_devapi_register(k_discovery_cmds, sizeof(k_discovery_cmds) / sizeof(k_discovery_cmds[0]), NULL); }

#endif /* NT_DEVAPI_GROUP_DISCOVERY */
