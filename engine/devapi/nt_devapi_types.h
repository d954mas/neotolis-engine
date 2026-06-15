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

/* Self-describing command metadata. All 7 fields are documentation strings the registry
   copies (strdup) at registration. `group` names the command's capability bundle; the
   `features` command lists the distinct groups across all registered commands. */
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
   returns true; on failure fills err and returns false. params may be NULL. */
typedef bool (*nt_devapi_handler_fn)(const cJSON *params, cJSON *result_obj, nt_devapi_error *err, void *user_data);

#endif /* NT_DEVAPI_TYPES_H */
