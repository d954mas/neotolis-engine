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

/* Release the response buffer on shutdown so init->shutdown->init stays leak-free. */
void nt_devapi_resp_reset(void) {
    free(s_resp_buf);
    s_resp_buf = NULL;
    s_resp_cap = 0U;
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

/* Build an owned {ok:false,error} entry. code/message are copied by cJSON, not owned. */
static cJSON *make_error_entry(const char *code, const char *message) {
    cJSON *entry = cJSON_CreateObject();
    NT_ASSERT(entry != NULL);
    cJSON_AddBoolToObject(entry, "ok", false);
    cJSON *err = cJSON_AddObjectToObject(entry, "error");
    NT_ASSERT(err != NULL);
    cJSON_AddStringToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message);
    return entry;
}

/* Echo request_id if present and a scalar (number/string); absent or non-scalar is not
   echoed. Duplicate preserves number-vs-string type. */
static void echo_request_id(cJSON *entry, const cJSON *req) {
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(req, "request_id");
    if (!cJSON_IsNumber(id) && !cJSON_IsString(id)) {
        return;
    }
    cJSON *dup = cJSON_Duplicate(id, true);
    NT_ASSERT(dup != NULL);
    cJSON_bool added = cJSON_AddItemToObject(entry, "request_id", dup);
    NT_ASSERT(added);
    (void)added;
}

/* Build {ok:true,result}, taking ownership of result_obj. */
static cJSON *make_ok_entry(cJSON *result_obj) {
    cJSON *entry = cJSON_CreateObject();
    NT_ASSERT(entry != NULL);
    cJSON_AddBoolToObject(entry, "ok", true);
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

/* Dispatch one request object → an owned response entry. The dispatcher pre-creates
   and always frees result_obj, so a handler can neither leak it nor free it itself. */
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

    cJSON *entry;
    if (slot->handler(params, result_obj, &err, slot->user_data)) {
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
        cJSON_bool added = cJSON_AddItemToArray(response, dispatch_one(req));
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

    cJSON *response = cJSON_IsArray(root) ? dispatch_batch(root) : dispatch_one(root);

    const char *out = resp_serialize(response);
    cJSON_Delete(response);
    cJSON_Delete(root);
    return out;
}
