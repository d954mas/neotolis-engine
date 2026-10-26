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

/* Fill err and return false, so a failing handler is one statement: return nt_devapi_fail(err, "bad_params", "...");
   Both strings must outlive the response (static literals). */
static inline bool nt_devapi_fail(nt_devapi_error *err, const char *code, const char *message) {
    err->code = code;
    err->message = message;
    return false;
}

/* Self-describing command metadata: documentation strings the registry copies (strdup) at
   registration. `method`, `summary` and `result_shape` are required. A NULL field takes its
   default: `group` = the method's first dot segment (the whole method when it has no dot),
   `params_shape` = "none", `frame_behavior` = "immediate", `side_effects` = "none". `group`
   names the command's capability bundle; the `features` command lists the distinct groups. */
typedef struct nt_devapi_command_desc {
    const char *method;
    const char *group;
    const char *summary;
    const char *params_shape;
    const char *result_shape;
    const char *frame_behavior;
    const char *side_effects;
} nt_devapi_command_desc;

/* Command handler. Fills result_obj (a pre-created cJSON object) on success and
   returns true; on failure fills err and returns false. params may be NULL. A handler never
   calls nt_devapi_submit (dispatch asserts no re-entry). */
typedef bool (*nt_devapi_handler_fn)(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data);

typedef void (*nt_devapi_hook_fn)(void);

#endif /* NT_DEVAPI_TYPES_H */
