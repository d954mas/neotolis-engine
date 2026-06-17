#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

// #region deferred queue
/* Pending deferred responses. Small preallocated queue: devapi is low-frequency, so no
   steady-state heap. A slot stores the owned request_id + continuation only — NEVER a
   pointer into s_resp_buf; the envelope is serialized at yield time. */
static nt_devapi_deferred_slot s_deferred[NT_DEVAPI_MAX_DEFERRED];

/* Set around the handler call in dispatch_one so nt_devapi_defer_current can signal the
   third "deferred" outcome without changing the bool handler ABI (out-param route). */
static bool *s_out_deferred;
static int s_defer_frames;

bool nt_devapi_defer_current(int frames_left) {
    NT_ASSERT(s_out_deferred != NULL); /* only valid inside a handler dispatch. */
    *s_out_deferred = true;
    s_defer_frames = frames_left;
    return true; /* handler returns normally; the bool path is unchanged. */
}

/* Free owned ids + clear the queue. Called from shutdown so init->shutdown->init is leak-free. */
void nt_devapi_deferred_reset(void) {
    for (int i = 0; i < NT_DEVAPI_MAX_DEFERRED; i++) {
        if (s_deferred[i].id != NULL) {
            cJSON_Delete(s_deferred[i].id);
        }
        s_deferred[i].id = NULL;
        s_deferred[i].in_use = false;
        s_deferred[i].frames_left = 0;
    }
}
// #endregion

/* Release the response buffer on shutdown so init->shutdown->init stays leak-free. */
void nt_devapi_resp_reset(void) {
    free(s_resp_buf);
    s_resp_buf = NULL;
    s_resp_cap = 0U;
    nt_devapi_deferred_reset(); /* shutdown path: drop any pending deferred slots. */
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

/* cJSON_Add{String,Number,Bool}ToObject wrappers that assert success (OOM traps rather
   than silently dropping a field). Result captured first — NT_ASSERT compiles out at
   NT_ASSERT_MODE=0, so the call must not live inside the macro. */
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

/* dispatch_one sentinel: the command deferred, so the caller must emit NOTHING for it
   (submit returns NULL; batch skips the entry). Distinct from NULL (no response built).
   Address of a file-local marker — a unique non-NULL pointer, never dereferenced. */
static cJSON s_deferred_marker;
#define DEFERRED_SENTINEL (&s_deferred_marker)

/* Enqueue a deferred command: store the owned duplicated id + continuation into a free
   slot. Returns true on success; false on overflow (caller emits a structured error). */
static bool deferred_enqueue(const cJSON *req, int frames_left) {
    for (int i = 0; i < NT_DEVAPI_MAX_DEFERRED; i++) {
        if (s_deferred[i].in_use) {
            continue;
        }
        s_deferred[i].id = dup_request_id(req); /* owned; NULL if absent — fine. */
        s_deferred[i].frames_left = frames_left;
        s_deferred[i].in_use = true;
        return true;
    }
    return false; /* no free slot — fail-early, leave no partial slot. */
}

/* Dispatch one request object → an owned response entry, DEFERRED_SENTINEL if the command
   deferred, or NULL never (a deferred command yields the sentinel). The dispatcher
   pre-creates and always frees result_obj, so a handler can neither leak it nor free it. */
static cJSON *dispatch_one(const cJSON *req) {
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

    const cJSON *params = cJSON_GetObjectItemCaseSensitive(req, "params"); /* validated above */

    cJSON *result_obj = cJSON_CreateObject();
    NT_ASSERT(result_obj != NULL);
    nt_devapi_error err = {0};

    /* Wire the deferral out-param around the handler call: a handler that never calls
       nt_devapi_defer_current behaves exactly as before (deferred stays false). */
    bool deferred = false;
    s_out_deferred = &deferred;
    s_defer_frames = 0;
    bool ok = slot->handler(params, result_obj, &err, slot->user_data);
    s_out_deferred = NULL;

    if (deferred) {
        /* No ok/error entry: enqueue the continuation (with the owned request_id) and emit
           nothing. Overflow rejects the command whole — fail-early, no partial slot. */
        cJSON_Delete(result_obj);
        if (!deferred_enqueue(req, s_defer_frames)) {
            cJSON *entry = make_error_entry(NT_DEVAPI_ERR_BAD_PARAMS, "deferred queue full");
            echo_request_id(entry, req);
            return entry;
        }
        return DEFERRED_SENTINEL;
    }

    cJSON *entry;
    if (ok) {
        entry = make_ok_entry(result_obj); /* detaches result_obj into the envelope */
        result_obj = NULL;
    } else {
        const char *code = err.code ? err.code : NT_DEVAPI_ERR_BAD_PARAMS;
        const char *message = err.message ? err.message : "handler reported failure";
        entry = make_error_entry(code, message);
    }

    /* Always free result_obj: unused on the error path, NULL (detached) on the ok path. */
    cJSON_Delete(result_obj);

    echo_request_id(entry, req);
    return entry;
}

/* Ordered batch: each request gets its own entry; one failure doesn't abort the rest. */
static cJSON *dispatch_batch(const cJSON *root) {
    cJSON *response = cJSON_CreateArray();
    NT_ASSERT(response != NULL);
    const cJSON *req = NULL;
    cJSON_ArrayForEach(req, root) {
        cJSON *entry = dispatch_one(req);
        if (entry == DEFERRED_SENTINEL) {
            continue; /* deferred: emit nothing for this entry, its result arrives via poll. */
        }
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
        cJSON *entry = dispatch_one(root);
        if (entry == DEFERRED_SENTINEL) {
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

const char *nt_devapi_poll_response(void) {
    NT_ASSERT(nt_devapi_initialized());
    /* Advance every in-flight continuation; yield the FIRST that became ready this call.
       One ready result per call — the transport drains in a loop. */
    for (int i = 0; i < NT_DEVAPI_MAX_DEFERRED; i++) {
        if (!s_deferred[i].in_use) {
            continue;
        }
        s_deferred[i].frames_left--;
        if (s_deferred[i].frames_left > 0) {
            continue;
        }

        /* Minimal continuation result — the slot only proves the yield path; real per-command
           shapes land with their respective deferred commands. */
        cJSON *result_obj = cJSON_CreateObject();
        NT_ASSERT(result_obj != NULL);
        devapi_add_bool(result_obj, "deferred", true);
        cJSON *entry = make_ok_entry(result_obj); /* takes ownership of result_obj. */
        if (s_deferred[i].id != NULL) {
            attach_request_id(entry, s_deferred[i].id); /* transfer ownership into the tree. */
            s_deferred[i].id = NULL;                    /* so reset doesn't double-free. */
        }

        const char *out = resp_serialize(entry);
        cJSON_Delete(entry);
        s_deferred[i].in_use = false;
        s_deferred[i].frames_left = 0;
        return out;
    }
    return NULL; /* nothing ready this call. */
}
