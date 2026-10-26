#ifndef NT_DEVAPI_TYPES_H
#define NT_DEVAPI_TYPES_H

/* Value types shared by the public header and the no-op stub, so both match at
   NT_DEVAPI_ENABLED on/off. cJSON is forward-declared (cJSON* only) to keep cJSON.h
   out of devapi-OFF builds. */

#include <stdbool.h>

typedef struct cJSON cJSON;

/* Error returned by a handler: a stable machine code token + a human message.
   Both point at static string literals owned by the handler/engine. */
typedef struct nt_devapi_error {
    const char *code;
    const char *message;
} nt_devapi_error;

/* Fill err and return false, so a failing handler is one statement: return nt_devapi_fail(err, "...");
   nt_devapi_fail answers bad_params; nt_devapi_fail_code names another code. Both strings must outlive
   the response (static literals or a static buffer). */
static inline bool nt_devapi_fail_code(nt_devapi_error *err, const char *code, const char *message) {
    err->code = code;
    err->message = message;
    return false;
}

static inline bool nt_devapi_fail(nt_devapi_error *err, const char *message) { return nt_devapi_fail_code(err, "bad_params", message); }

/* Command handler. Fills result_obj (a pre-created cJSON object) on success and returns true; on
   failure fills err and returns false. params may be NULL. A handler never calls nt_devapi_submit
   (dispatch asserts no re-entry). */
typedef bool (*nt_devapi_handler_fn)(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data);

/* One command: its handler plus the self-description `endpoints` / `command.describe` report.
   summary says what the command does, including any side effect and whether it replies later. */
typedef struct nt_devapi_command {
    const char *method;
    const char *summary;
    const char *params_shape;
    const char *result_shape;
    nt_devapi_handler_fn fn;
} nt_devapi_command;

typedef void (*nt_devapi_hook_fn)(void);

#endif /* NT_DEVAPI_TYPES_H */
