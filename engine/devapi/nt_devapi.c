#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"

/* Transport-agnostic dispatch core (D-01: zero platform/socket code). submit()
   parses one JSON line, routes object-vs-array, wraps each request in the
   {ok,result}/{ok,error} envelope, echoes request_id unchanged, and serializes
   into one growing reusable buffer. The whole core is reachable from CTest via
   literal JSON strings — no transport. */

/* D-04: single growing reusable response buffer. The pointer returned by
   nt_devapi_submit is valid ONLY until the next submit — the next call memcpys a
   new payload here and a grow reallocs (the pointer may MOVE). Dev-only, single
   client: T-63-06 accepts unbounded growth (one buffer, reused across calls). */
static char *s_resp_buf;
static size_t s_resp_cap;

/* Grow s_resp_buf to hold at least `need` bytes (geometric, min 256). */
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

/* Release the reusable response buffer. Called from nt_devapi_shutdown so an
   init -> shutdown -> init cycle returns to a pristine state (no buffer leak). */
void nt_devapi_resp_reset(void) {
    free(s_resp_buf);
    s_resp_buf = NULL;
    s_resp_cap = 0U;
}

/* Serialize `tree` into the growing buffer and return it (D-04 lifetime). */
static const char *resp_serialize(cJSON *tree) {
    char *unformatted = cJSON_PrintUnformatted(tree);
    NT_ASSERT(unformatted != NULL);
    size_t len = strlen(unformatted) + 1U;
    resp_reserve(len);
    memcpy(s_resp_buf, unformatted, len);
    cJSON_free(unformatted);
    return s_resp_buf;
}

/* Build {ok:false,error:{code,message}} and return it (caller owns the returned
   object). code/message are borrowed — cJSON copies them, this takes no ownership. */
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

/* Echo request_id unchanged into `entry` if present (number OR string; absent
   → omitted). cJSON_Duplicate preserves the exact item type (Pitfall 5). */
static void echo_request_id(cJSON *entry, const cJSON *req) {
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(req, "request_id");
    if (id == NULL) {
        return;
    }
    cJSON *dup = cJSON_Duplicate(id, true);
    NT_ASSERT(dup != NULL);
    cJSON_AddItemToObject(entry, "request_id", dup);
}

/* Dispatch ONE request object → a fresh response entry object (caller owns it).
   D-02: kept separate from in-call assembly so the Phase 64/65 deferred path can
   reuse it. D-05: the dispatcher pre-creates AND always frees result_obj, so a
   handler cannot leak and cannot free the tree itself. */
static cJSON *dispatch_one(const cJSON *req) {
    if (!cJSON_IsObject(req)) {
        return make_error_entry(NT_DEVAPI_ERR_BAD_PARAMS, "request must be a JSON object");
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

    const cJSON *params = cJSON_GetObjectItemCaseSensitive(req, "params");

    /* D-05: dispatcher owns result_obj; the handler only fills it. */
    cJSON *result_obj = cJSON_CreateObject();
    NT_ASSERT(result_obj != NULL);
    nt_devapi_error err = {0};

    cJSON *entry;
    if (slot->handler(params, result_obj, &err, slot->user_data)) {
        entry = cJSON_CreateObject();
        NT_ASSERT(entry != NULL);
        cJSON_AddBoolToObject(entry, "ok", true);
        /* result_obj is detached into the envelope; it is no longer free'd below. */
        cJSON_AddItemToObject(entry, "result", result_obj);
        result_obj = NULL;
    } else {
        const char *code = err.code ? err.code : NT_DEVAPI_ERR_BAD_PARAMS;
        const char *message = err.message ? err.message : "handler reported failure";
        entry = make_error_entry(code, message);
    }

    /* D-05 never-leak: on the error path result_obj was unused → always free it.
       On the ok path it was detached (set NULL) → this is a no-op. */
    cJSON_Delete(result_obj);

    echo_request_id(entry, req);
    return entry;
}

/* D-07: ordered batch, continue-on-error — each request yields its own envelope
   entry in order; one failure does not abort the loop. */
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
    cJSON *root = cJSON_Parse(line);
    if (root == NULL) {
        /* API contract path, NOT an assert (T-63-05): malformed JSON → bad_params. */
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
