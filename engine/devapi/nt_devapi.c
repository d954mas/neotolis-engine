#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "app/nt_app.h"
#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"

/* Transport-agnostic dispatch core: no platform/socket code, so it runs from
   CTest with literal JSON lines. */

/* Reusable response buffer. The pointer nt_devapi_submit returns is valid only
   until the next submit (a grow may move it). Dev-only: unbounded growth accepted. */
static char *s_resp_buf;
static size_t s_resp_cap;

/* Grow to hold >= need bytes (geometric, min 256). */
static void resp_reserve(size_t need) {
    if (need <= s_resp_cap) {
        return;
    }
    size_t cap = s_resp_cap ? s_resp_cap : 256U;
    while (cap < need) {
        if (cap > SIZE_MAX / 2U) {
            cap = need; /* geometric growth would wrap size_t — allocate exactly. */
            break;
        }
        cap *= 2U;
    }
    char *grown = (char *)realloc(s_resp_buf, cap);
    NT_ASSERT(grown != NULL);
    s_resp_buf = grown;
    s_resp_cap = cap;
}

// #region deferred reply
/* The one pending deferred reply. It owns the request_id dup, never a pointer into s_resp_buf; the
   envelope is serialized at yield time. */
typedef struct deferred_slot {
    cJSON *id;                  /* owned request_id dup; NULL if absent. */
    cJSON *entry;               /* the reply or stalled envelope once resolved; NULL until then. */
    nt_devapi_handler_fn reply; /* NULL: nothing pending. */
    double target_time;         /* SECONDS: g_nt_app.time deadline. */
    uint32_t target_frame;      /* FRAMES: g_nt_app.frame deadline (wrap-safe compare). */
    nt_devapi_gate gate;
    nt_app_mode_t mode; /* the time mode at submit; a switch strands a clock gate. */
} deferred_slot;

static deferred_slot s_pending;

/* The command being dispatched; nt_devapi_defer reads it. One at a time: a handler never re-enters submit. */
typedef struct dispatch_state {
    bool batch;
} dispatch_state;

static dispatch_state *s_current;

void nt_devapi_deferred_reset(void) {
    cJSON_Delete(s_pending.id);
    cJSON_Delete(s_pending.entry);
    s_pending = (deferred_slot){0};
}
// #endregion

/* Release the response buffer on shutdown so init->shutdown->init stays leak-free. */
void nt_devapi_resp_reset(void) {
    free(s_resp_buf);
    s_resp_buf = NULL;
    s_resp_cap = 0U;
    nt_devapi_deferred_reset();
}

/* Serialize `tree` into the growing buffer and return it (valid until the next submit). */
static const char *resp_serialize(cJSON *tree) {
    char *unformatted = cJSON_PrintUnformatted(tree);
    NT_ASSERT(unformatted != NULL);
    size_t len = strlen(unformatted) + 1U;
    resp_reserve(len);
    memcpy(s_resp_buf, unformatted, len);
    cJSON_free(unformatted);
    return s_resp_buf;
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
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, message);
    }
    double d = node->valuedouble;
    if (!isfinite(d) || d != floor(d) || d < 0.0 || d > max_d) {
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, message);
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

bool nt_devapi_parse_u16_param_exact(const cJSON *node, uint16_t max, nt_devapi_error *err, const char *message, uint16_t *out) {
    double d = 0.0;
    if (!parse_number_exact(node, (double)max, err, message, &d)) {
        return false;
    }
    *out = (uint16_t)d; /* d is integer-valued in [0, max] — the cast is exact and in range. */
    return true;
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

/* Request-level preconditions: request_id (if present) must be a scalar; params (if present)
   must be an object. Returns the rejection reason, or NULL if the request is well-formed. */
static const char *request_reject_reason(const cJSON *req) {
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(req, "request_id");
    if (id != NULL && !cJSON_IsNumber(id) && !cJSON_IsString(id)) {
        return "request_id must be a number or string";
    }
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(req, "params");
    if (params != NULL && !cJSON_IsObject(params)) {
        return "params must be a JSON object";
    }
    return NULL;
}

bool nt_devapi_defer(nt_devapi_gate gate, double amount, nt_devapi_handler_fn reply, nt_devapi_error *err) {
    NT_ASSERT(s_current != NULL && reply != NULL);                                              /* only from a handler; a reply never defers. */
    NT_ASSERT(s_pending.reply == NULL);                                                         /* dispatch answers busy first; one defer per command. */
    NT_ASSERT(gate != NT_DEVAPI_GATE_FRAMES || (amount >= 0.0 && amount <= (double)INT32_MAX)); /* wrap-safe compare range */
    if (s_current->batch) {
        /* A batch is one ordered response array; a deferred reply cannot go in it. */
        return nt_devapi_fail(err, NT_DEVAPI_ERR_BAD_PARAMS, "deferred commands are not supported inside a batch");
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
    bool deferred = s_current != NULL && s_pending.reply != NULL; /* a reply runs with no dispatch in flight. */
    NT_ASSERT(ok || !deferred);                                   /* a handler that deferred returns true. */
    if (ok && !deferred) {
        return make_ok_entry(result_obj);
    }
    cJSON_Delete(result_obj);
    if (ok) {
        return NULL;
    }
    return make_error_entry(err.code ? err.code : NT_DEVAPI_ERR_BAD_PARAMS, err.message ? err.message : "handler reported failure");
}

/* Dispatch one request object → an owned response entry, or NULL if the command deferred. */
static cJSON *dispatch_one(const cJSON *req, bool batch) {
    if (!cJSON_IsObject(req)) {
        return make_error_entry(NT_DEVAPI_ERR_BAD_PARAMS, "request must be a JSON object");
    }

    const char *reject = request_reject_reason(req);
    if (reject != NULL) {
        /* echo_request_id is type-safe: it skips an invalid (non-scalar) id automatically. */
        cJSON *entry = make_error_entry(NT_DEVAPI_ERR_BAD_PARAMS, reject);
        echo_request_id(entry, req);
        return entry;
    }

    const cJSON *method_item = cJSON_GetObjectItemCaseSensitive(req, "method");
    if (method_item == NULL || !cJSON_IsString(method_item) || method_item->valuestring == NULL) {
        cJSON *entry = make_error_entry(NT_DEVAPI_ERR_BAD_PARAMS, "missing or non-string method");
        echo_request_id(entry, req);
        return entry;
    }

    const nt_devapi_slot *slot = nt_devapi_registry_find(method_item->valuestring);
    if (slot == NULL) {
        cJSON *entry = make_error_entry(NT_DEVAPI_ERR_UNKNOWN_METHOD, "no command registered for this method");
        echo_request_id(entry, req);
        return entry;
    }

    if (s_pending.reply != NULL) {
        cJSON *entry = make_error_entry(NT_DEVAPI_ERR_BUSY, "a deferred reply is pending: wait for it (a reconnect or reset drops it)");
        echo_request_id(entry, req);
        return entry;
    }

    const cJSON *params = cJSON_GetObjectItemCaseSensitive(req, "params"); /* validated above */
    NT_ASSERT(s_current == NULL);                                          /* a handler never re-enters submit. */
    dispatch_state state = {.batch = batch};
    s_current = &state;
    cJSON *entry = handler_entry(slot->handler, params, slot->user_data);
    s_current = NULL;
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
    NT_ASSERT(nt_devapi_initialized()); /* dispatch before init is a caller bug (empty registry). */
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
   so a pause, mode switch or render-off from game code or a host is seen like a bot's. */
static const char *stall_reason(void) {
    if (s_pending.gate == NT_DEVAPI_GATE_PRESENT) {
        return g_nt_app.render_enabled ? NULL : "rendering is off, so no frame is presented";
    }
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

/* Resolve the pending clock or PRESENT gate once per tick, before the transports drain, so native and
   web see the same state and a clock reply is read the tick its gate opens. Ready wins over a frozen sim
   (the last step of a crunch both reaches the target and empties the queue), not over a mode switch: RUN
   frames past a manual target are not the steps asked for. */
static void resolve_pending(void) {
    if (s_pending.reply == NULL || s_pending.entry != NULL) {
        return;
    }
    if (s_pending.gate != NT_DEVAPI_GATE_PRESENT && g_nt_app.mode == s_pending.mode && clock_ready()) {
        s_pending.entry = handler_entry(s_pending.reply, NULL, NULL);
        return;
    }
    const char *why = stall_reason();
    if (why != NULL) {
        s_pending.entry = make_error_entry(NT_DEVAPI_ERR_STALLED, why);
    }
}

void nt_devapi_run_pre_swap_reply(void) {
    /* May fire after nt_devapi_shutdown (a host that swaps once more during teardown) — no-op then. */
    if (!nt_devapi_initialized() || s_pending.reply == NULL || s_pending.gate != NT_DEVAPI_GATE_PRESENT || s_pending.entry != NULL) {
        return;
    }
    /* The read must happen here, before swap: poll_response runs where the back buffer is undefined. */
    s_pending.entry = handler_entry(s_pending.reply, NULL, NULL);
}

const char *nt_devapi_poll_response(void) {
    NT_ASSERT(nt_devapi_initialized());
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

/* Core per-tick entry: resolves the pending gate, runs the transport-poll phase (each transport's recv +
   drain), then the tick phase (e.g. the input schedule release), naming neither transport. The phase order is what guarantees
   a received line is scheduled before the tick reads it, so a host may register the two in any order. */
void nt_devapi_update(void) {
    resolve_pending();
    nt_devapi_run_transport_hooks();
    nt_devapi_run_tick_hooks();
}
