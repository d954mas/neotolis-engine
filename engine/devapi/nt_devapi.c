#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "app/nt_app.h"
#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"

/* Transport-agnostic core: registry, dispatch, deferred reply, lifecycle hooks and the discovery
   commands. No platform/socket code, so it runs from CTest with literal JSON lines. */

// #region registry
/* Dev-only registry: fixed table of borrowed descriptors, linear scan. Not a frame-hot path. */
typedef struct registry_slot {
    const nt_devapi_command *cmd;
    void *user_data;
} registry_slot;

static registry_slot s_slots[NT_DEVAPI_MAX_COMMANDS];
static int s_count;
static bool s_initialized;

static const registry_slot *registry_find(const char *method) {
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_slots[i].cmd->method, method) == 0) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static void register_one(const nt_devapi_command *cmd, void *user_data) {
    NT_ASSERT(cmd->method != NULL && cmd->summary != NULL && cmd->params_shape != NULL && cmd->result_shape != NULL && cmd->fn != NULL);
    NT_ASSERT(registry_find(cmd->method) == NULL); /* a duplicate method is a registration bug */
    s_slots[s_count++] = (registry_slot){.cmd = cmd, .user_data = user_data};
}

void nt_devapi_register(const nt_devapi_command *cmds, size_t count, void *user_data) {
    NT_ASSERT(s_initialized && cmds != NULL && count <= (size_t)(NT_DEVAPI_MAX_COMMANDS - s_count));
    for (size_t i = 0; i < count; i++) {
        register_one(&cmds[i], user_data);
    }
}

// #endregion

// #region lifecycle hooks
/* Three ordered phases run every update: the frame poll (the gfx group stores the last finished frame)
   before the transport poll (recv + enqueue), so this update's commands see that frame, and the
   transport poll before the tick (the input schedule release), so a line received this tick is
   scheduled before the tick reads it, whatever the registration order. One slot each: the gfx group is
   the only frame poller, native net the only polling transport, input the only ticker. Reset hooks
   fire on a client drop. */
#ifndef NT_DEVAPI_MAX_RESET_HOOKS
#define NT_DEVAPI_MAX_RESET_HOOKS 8
#endif

static nt_devapi_hook_fn s_frame_hook;
static nt_devapi_hook_fn s_transport_hook;
static nt_devapi_hook_fn s_tick_hook;
static nt_devapi_hook_fn s_reset_hooks[NT_DEVAPI_MAX_RESET_HOOKS];
static int s_reset_count;

void nt_devapi_set_frame_poll(nt_devapi_hook_fn fn) {
    NT_ASSERT(fn != NULL && s_frame_hook == NULL);
    s_frame_hook = fn;
}

/* Idempotent, so a transport stop->start re-sets the same poll. */
void nt_devapi_set_transport_poll(nt_devapi_hook_fn fn) {
    NT_ASSERT(fn != NULL && (s_transport_hook == NULL || s_transport_hook == fn));
    s_transport_hook = fn;
}

void nt_devapi_set_tick(nt_devapi_hook_fn fn) {
    NT_ASSERT(fn != NULL && s_tick_hook == NULL);
    s_tick_hook = fn;
}

void nt_devapi_register_reset(nt_devapi_hook_fn fn) {
    NT_ASSERT(fn != NULL && s_reset_count < NT_DEVAPI_MAX_RESET_HOOKS);
    s_reset_hooks[s_reset_count++] = fn;
}

void nt_devapi_run_reset_hooks(void) {
    for (int i = 0; i < s_reset_count; i++) {
        s_reset_hooks[i]();
    }
}
// #endregion

/* The last response line handed out; valid until the next submit/poll_response frees it. */
static char *s_resp;

// #region deferred reply
/* The one pending deferred reply. It owns the request_id dup, never a pointer into s_resp; the
   envelope is serialized at yield time. */
typedef struct deferred_slot {
    cJSON *id;                  /* owned request_id dup; NULL if absent. */
    cJSON *entry;               /* the reply or stalled envelope once resolved; NULL until then. */
    nt_devapi_handler_fn reply; /* NULL: nothing pending. */
    double target_time;         /* SECONDS: g_nt_app.time deadline. */
    uint32_t target_frame;      /* FRAMES: g_nt_app.frame deadline (wrap-safe compare). */
    nt_devapi_gate gate;
    nt_app_mode_t mode; /* the time mode at submit; a switch strands a clock gate. */
    bool checked;       /* RENDERED: an update has seen it, so the next one means a frame went unrendered. */
} deferred_slot;

static deferred_slot s_pending;

/* A command is being dispatched (nt_devapi_defer requires it), and whether it sits in a batch. One at a
   time: a handler never re-enters submit. */
static bool s_in_dispatch;
static bool s_in_batch;

void nt_devapi_deferred_reset(void) {
    cJSON_Delete(s_pending.id);
    cJSON_Delete(s_pending.entry);
    s_pending = (deferred_slot){0};
}
// #endregion

static const char *resp_serialize(cJSON *tree) {
    cJSON_free(s_resp);
    s_resp = cJSON_PrintUnformatted(tree);
    NT_ASSERT(s_resp != NULL);
    return s_resp;
}

/* cJSON_Add{String,Number,Bool,Null}ToObject wrappers that assert success (OOM traps rather
   than silently dropping a field). Result captured first — assert expressions must stay side-effect-free. */
void devapi_add_string(cJSON *obj, const char *key, const char *value) {
    cJSON *item = cJSON_AddStringToObject(obj, key, value);
    NT_ASSERT(item != NULL);
    (void)item;
}

void devapi_add_number(cJSON *obj, const char *key, double value) {
    cJSON *item = cJSON_AddNumberToObject(obj, key, value);
    NT_ASSERT(item != NULL);
    (void)item;
}

void devapi_add_bool(cJSON *obj, const char *key, bool value) {
    cJSON *item = cJSON_AddBoolToObject(obj, key, value);
    NT_ASSERT(item != NULL);
    (void)item;
}

void devapi_add_null(cJSON *obj, const char *key) {
    cJSON *item = cJSON_AddNullToObject(obj, key);
    NT_ASSERT(item != NULL);
    (void)item;
}

/* Require a finite, integer-valued number in [0, max_d], writing it to *out_d. Casting an
   out-of-range/NaN/Inf double to unsigned is UB, so all checks run on the double before any cast. */
static bool parse_number_exact(const cJSON *node, double max_d, nt_devapi_error *err, const char *message, double *out_d) {
    if (!cJSON_IsNumber(node)) {
        return nt_devapi_fail(err, message);
    }
    double d = node->valuedouble;
    if (!isfinite(d) || d != floor(d) || d < 0.0 || d > max_d) {
        return nt_devapi_fail(err, message);
    }
    *out_d = d;
    return true;
}

bool nt_devapi_parse_u32_param_exact(const cJSON *node, uint32_t max, nt_devapi_error *err, const char *message, uint32_t *out) {
    double d = 0.0;
    if (!parse_number_exact(node, (double)max, err, message, &d)) {
        return false;
    }
    *out = (uint32_t)d; /* d is integer-valued in [0, max] — the cast is exact and in range. */
    return true;
}

bool nt_devapi_parse_u53_param_exact(const cJSON *node, nt_devapi_error *err, const char *message, uint64_t *out) {
    double d = 0.0;
    if (!parse_number_exact(node, 9007199254740991.0, err, message, &d)) { /* 2^53 - 1: every integer below is exact in a JSON double */
        return false;
    }
    *out = (uint64_t)d;
    return true;
}

bool nt_devapi_parse_u16_param_exact(const cJSON *node, uint16_t max, nt_devapi_error *err, const char *message, uint16_t *out) {
    double d = 0.0;
    if (!parse_number_exact(node, (double)max, err, message, &d)) {
        return false;
    }
    *out = (uint16_t)d; /* d is integer-valued in [0, max] — the cast is exact and in range. */
    return true;
}

bool nt_devapi_parse_f32_param(const cJSON *node, nt_devapi_error *err, const char *message, float *out) {
    /* On the double: an out-of-range narrowing is undefined in C17. !(<=) also rejects NaN. */
    if (!cJSON_IsNumber(node) || !(fabs(node->valuedouble) <= (double)FLT_MAX)) {
        return nt_devapi_fail(err, message);
    }
    *out = (float)node->valuedouble;
    return true;
}

// #region discovery
static void emit_command(cJSON *obj, const nt_devapi_command *cmd) {
    devapi_add_string(obj, "method", cmd->method);
    devapi_add_string(obj, "summary", cmd->summary);
    devapi_add_string(obj, "params_shape", cmd->params_shape);
    devapi_add_string(obj, "result_shape", cmd->result_shape);
}

/* result wraps a `commands` array — never a bare top-level array. */
static bool cmd_endpoints(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    cJSON *commands = cJSON_AddArrayToObject(result, "commands");
    NT_ASSERT(commands != NULL);
    for (int i = 0; i < s_count; i++) {
        cJSON *obj = cJSON_CreateObject();
        NT_ASSERT(obj != NULL);
        cJSON_bool added = cJSON_AddItemToArray(commands, obj);
        NT_ASSERT(added);
        (void)added;
        emit_command(obj, s_slots[i].cmd);
    }
    return true;
}

/* Full contract for params.method. Missing/non-string -> bad_params; unknown -> unknown_method. */
static bool cmd_command_describe(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    const cJSON *method_item = cJSON_GetObjectItemCaseSensitive(params, "method");
    if (!cJSON_IsString(method_item) || method_item->valuestring == NULL) {
        return nt_devapi_fail(err, (method_item == NULL) ? "missing params.method" : "non-string params.method");
    }
    const registry_slot *slot = registry_find(method_item->valuestring);
    if (slot == NULL) {
        return nt_devapi_fail_code(err, NT_DEVAPI_ERR_UNKNOWN_METHOD, "no command registered for this method");
    }
    emit_command(result, slot->cmd);
    return true;
}

static const nt_devapi_command k_discovery_cmds[] = {
    {
        .method = "endpoints",
        .summary = "list all registered commands",
        .params_shape = "{}",
        .result_shape = "{commands:[{method,summary,params_shape,result_shape}]}",
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
// #endregion

nt_result_t nt_devapi_init(void) {
    if (s_initialized) {
        return NT_ERR_INIT_FAILED;
    }
    s_count = 0;
    s_frame_hook = NULL; /* groups re-register their lifecycle hooks when a host registers them. */
    s_transport_hook = NULL;
    s_tick_hook = NULL;
    s_reset_count = 0;
    s_initialized = true;
    /* Discovery is the registry's own surface. Groups are the host's choice, registered after init:
       nt_devapi_register_default() or specific nt_devapi_register_<group>() calls (nt_devapi_groups.h). */
    nt_devapi_register(k_discovery_cmds, NT_COUNTOF(k_discovery_cmds), NULL);
    return NT_OK;
}

void nt_devapi_shutdown(void) {
    s_count = 0;
    s_frame_hook = NULL; /* drop group hooks so init->shutdown->init re-registers cleanly. */
    s_transport_hook = NULL;
    s_tick_hook = NULL;
    s_reset_count = 0;
    cJSON_free(s_resp);
    s_resp = NULL;
    nt_devapi_deferred_reset();
    s_initialized = false;
}

/* Build an owned {ok:false,error} entry. code/message are copied by cJSON, not owned. */
static cJSON *make_error_entry(const char *code, const char *message) {
    cJSON *entry = cJSON_CreateObject();
    NT_ASSERT(entry != NULL);
    devapi_add_bool(entry, "ok", false);
    cJSON *err = cJSON_AddObjectToObject(entry, "error");
    NT_ASSERT(err != NULL);
    devapi_add_string(err, "code", code);
    devapi_add_string(err, "message", message);
    return entry;
}

/* Duplicate request_id if present and a scalar (number/string); NULL if absent or non-scalar.
   Duplicate preserves number-vs-string type. Caller owns the returned node. */
static cJSON *dup_request_id(const cJSON *req) {
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(req, "request_id");
    if (!cJSON_IsNumber(id) && !cJSON_IsString(id)) {
        return NULL;
    }
    cJSON *dup = cJSON_Duplicate(id, true);
    NT_ASSERT(dup != NULL);
    return dup;
}

/* Attach an owned request_id node into entry (ownership transfers into the cJSON tree). */
static void attach_request_id(cJSON *entry, cJSON *id) {
    cJSON_bool added = cJSON_AddItemToObject(entry, "request_id", id);
    NT_ASSERT(added);
    (void)added;
}

/* Echo request_id if present and a scalar; absent or non-scalar is not echoed. */
static void echo_request_id(cJSON *entry, const cJSON *req) {
    cJSON *dup = dup_request_id(req);
    if (dup == NULL) {
        return;
    }
    attach_request_id(entry, dup);
}

/* Build {ok:true,result}, taking ownership of result_obj. */
static cJSON *make_ok_entry(cJSON *result_obj) {
    cJSON *entry = cJSON_CreateObject();
    NT_ASSERT(entry != NULL);
    devapi_add_bool(entry, "ok", true);
    cJSON_bool added = cJSON_AddItemToObject(entry, "result", result_obj);
    NT_ASSERT(added);
    (void)added;
    return entry;
}

/* Why the request object cannot run now (sets *code; bad_params unless named), or NULL with *slot set. */
static const char *request_reject(const cJSON *req, const char **code, const registry_slot **slot) {
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(req, "request_id");
    if (id != NULL && !cJSON_IsNumber(id) && !cJSON_IsString(id)) {
        return "request_id must be a number or string";
    }
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(req, "params");
    if (params != NULL && !cJSON_IsObject(params)) {
        return "params must be a JSON object";
    }
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(req, "method");
    if (!cJSON_IsString(method) || method->valuestring == NULL) {
        return "missing or non-string method";
    }
    *slot = registry_find(method->valuestring);
    if (*slot == NULL) {
        *code = NT_DEVAPI_ERR_UNKNOWN_METHOD;
        return "no command registered for this method";
    }
    if (s_pending.reply != NULL) {
        *code = NT_DEVAPI_ERR_BUSY;
        return "a deferred reply is pending: wait for it (a reconnect or reset drops it)";
    }
    return NULL;
}

bool nt_devapi_defer(nt_devapi_gate gate, double amount, nt_devapi_handler_fn reply, nt_devapi_error *err) {
    NT_ASSERT(s_in_dispatch && reply != NULL);                                                  /* only from a handler; a reply never defers. */
    NT_ASSERT(s_pending.reply == NULL);                                                         /* dispatch answers busy first; one defer per command. */
    NT_ASSERT(gate != NT_DEVAPI_GATE_FRAMES || (amount >= 0.0 && amount <= (double)INT32_MAX)); /* wrap-safe compare range */
    if (s_in_batch) {
        /* A batch is one ordered response array; a deferred reply cannot go in it. */
        return nt_devapi_fail(err, "deferred commands are not supported inside a batch");
    }
    s_pending = (deferred_slot){.reply = reply, .gate = gate, .mode = g_nt_app.mode};
    if (gate == NT_DEVAPI_GATE_FRAMES) {
        s_pending.target_frame = g_nt_app.frame + (uint32_t)amount;
    } else if (gate == NT_DEVAPI_GATE_SECONDS) {
        s_pending.target_time = g_nt_app.time + amount;
    }
    return true;
}

/* Run a handler into an owned {ok,result} or {ok:false,error} entry without request_id, or NULL when a
   dispatched command deferred. The handler gets a fresh result object it can neither leak nor free. */
static cJSON *handler_entry(nt_devapi_handler_fn fn, const cJSON *params, void *user_data) {
    cJSON *result_obj = cJSON_CreateObject();
    NT_ASSERT(result_obj != NULL);
    nt_devapi_error err = {0};
    bool ok = fn(params, result_obj, &err, user_data);
    bool deferred = s_in_dispatch && s_pending.reply != NULL; /* a reply runs with no dispatch in flight. */
    NT_ASSERT(ok || !deferred);                               /* a handler that deferred returns true. */
    if (ok && !deferred) {
        return make_ok_entry(result_obj);
    }
    cJSON_Delete(result_obj);
    if (ok) {
        return NULL;
    }
    NT_ASSERT(err.code != NULL && err.message != NULL); /* a failing handler fills err (nt_devapi_fail) */
    return make_error_entry(err.code, err.message);
}

/* Dispatch one request object → an owned response entry, or NULL if the command deferred. */
static cJSON *dispatch_one(const cJSON *req, bool batch) {
    if (!cJSON_IsObject(req)) {
        return make_error_entry(NT_DEVAPI_ERR_BAD_PARAMS, "request must be a JSON object");
    }

    const char *code = NT_DEVAPI_ERR_BAD_PARAMS;
    const registry_slot *slot = NULL;
    const char *reject = request_reject(req, &code, &slot);
    if (reject != NULL) {
        /* echo_request_id is type-safe: it skips an invalid (non-scalar) id automatically. */
        cJSON *entry = make_error_entry(code, reject);
        echo_request_id(entry, req);
        return entry;
    }

    const cJSON *params = cJSON_GetObjectItemCaseSensitive(req, "params"); /* validated above */
    NT_ASSERT(!s_in_dispatch);                                             /* a handler never re-enters submit. */
    s_in_dispatch = true;
    s_in_batch = batch;
    cJSON *entry = handler_entry(slot->cmd->fn, params, slot->user_data);
    s_in_dispatch = false;
    if (entry == NULL) {
        s_pending.id = dup_request_id(req);
    } else {
        echo_request_id(entry, req);
    }
    return entry;
}

/* Ordered batch: each request gets its own entry; one failure doesn't abort the rest. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static cJSON *dispatch_batch(const cJSON *root) {
    cJSON *response = cJSON_CreateArray();
    NT_ASSERT(response != NULL);
    const cJSON *req = NULL;
    cJSON_ArrayForEach(req, root) {
        cJSON *entry = dispatch_one(req, true);
        NT_ASSERT(entry != NULL); /* nt_devapi_defer refuses inside a batch. */
        cJSON_bool added = cJSON_AddItemToArray(response, entry);
        NT_ASSERT(added);
        (void)added;
    }
    return response;
}

const char *nt_devapi_submit(const char *line) {
    NT_ASSERT(s_initialized); /* dispatch before init is a caller bug (empty registry). */
    /* Strict: a line protocol rejects trailing garbage ('{...} junk'). */
    cJSON *root = cJSON_ParseWithOpts(line, NULL, true);
    if (root == NULL) {
        /* Contract path, not an assert: malformed JSON → bad_params. */
        cJSON *entry = make_error_entry(NT_DEVAPI_ERR_BAD_PARAMS, "malformed JSON");
        const char *out = resp_serialize(entry);
        cJSON_Delete(entry);
        return out;
    }

    if (!cJSON_IsArray(root)) {
        cJSON *entry = dispatch_one(root, false);
        if (entry == NULL) {
            /* The lone command deferred: no sync response — drain it via poll_response. */
            cJSON_Delete(root);
            return NULL;
        }
        const char *out = resp_serialize(entry);
        cJSON_Delete(entry);
        cJSON_Delete(root);
        return out;
    }

    cJSON *response = dispatch_batch(root);
    const char *out = resp_serialize(response);
    cJSON_Delete(response);
    cJSON_Delete(root);
    return out;
}

/* Why the pending gate can no longer open, or NULL while it still can. It reads state, not progress,
   so a pause or mode switch from game code or a host is seen like a bot's. */
static const char *stall_reason(void) {
    if (g_nt_app.mode != s_pending.mode) {
        return "the time mode changed";
    }
    if (g_nt_app.mode == NT_APP_MODE_RUN ? g_nt_app.paused : g_nt_app.pending_steps == 0) {
        return "the sim stopped advancing (paused, or no manual step queued)";
    }
    if (s_pending.gate == NT_DEVAPI_GATE_SECONDS && g_nt_app.scale <= 0.0F) {
        return "game time is frozen (scale 0)";
    }
    return NULL;
}

static bool clock_ready(void) {
    if (s_pending.gate == NT_DEVAPI_GATE_SECONDS) {
        return g_nt_app.time >= s_pending.target_time;
    }
    return (int32_t)(g_nt_app.frame - s_pending.target_frame) >= 0; /* wraps together with g_nt_app.frame. */
}

/* Resolve the pending clock gate, or stall a RENDERED one, once per tick, before the transports drain, so native and
   web see the same state and a clock reply is read the tick its gate opens. Ready wins over a frozen sim
   (the last step of a crunch both reaches the target and empties the queue), not over a mode switch: RUN
   frames past a manual target are not the steps asked for. */
static void resolve_pending(void) {
    if (s_pending.reply == NULL || s_pending.entry != NULL) {
        return;
    }
    if (s_pending.gate == NT_DEVAPI_GATE_RENDERED) {
        /* A whole frame between two checks rendered nothing: rendering is off or the host skips the call. */
        if (s_pending.checked) {
            s_pending.entry = make_error_entry(NT_DEVAPI_ERR_STALLED, "no nt_devapi_frame_rendered since the request (rendering off, or the host does not call it)");
        }
        s_pending.checked = true;
        return;
    }
    if (g_nt_app.mode == s_pending.mode && clock_ready()) {
        s_pending.entry = handler_entry(s_pending.reply, NULL, NULL);
        return;
    }
    const char *why = stall_reason();
    if (why != NULL) {
        s_pending.entry = make_error_entry(NT_DEVAPI_ERR_STALLED, why);
    }
}

void nt_devapi_frame_rendered(void) {
    NT_ASSERT(s_initialized);
    if (s_pending.reply == NULL || s_pending.gate != NT_DEVAPI_GATE_RENDERED || s_pending.entry != NULL) {
        return;
    }
    /* The read must happen here, before swap: poll_response runs where the back buffer is undefined. */
    s_pending.entry = handler_entry(s_pending.reply, NULL, NULL);
}

const char *nt_devapi_poll_response(void) {
    NT_ASSERT(s_initialized);
    cJSON *entry = s_pending.entry;
    if (entry == NULL) {
        return NULL;
    }
    s_pending.entry = NULL;
    if (s_pending.id != NULL) {
        attach_request_id(entry, s_pending.id);
        s_pending.id = NULL;
    }
    const char *out = resp_serialize(entry);
    cJSON_Delete(entry);
    nt_devapi_deferred_reset();
    return out;
}

/* Core per-tick entry: resolves the pending gate, then the frame poll, the transport poll (recv + drain)
   and the tick (e.g. the input schedule release), naming neither transport nor group. */
void nt_devapi_update(void) {
    resolve_pending();
    if (s_frame_hook != NULL) {
        s_frame_hook();
    }
    if (s_transport_hook != NULL) {
        s_transport_hook();
    }
    if (s_tick_hook != NULL) {
        s_tick_hook();
    }
}
