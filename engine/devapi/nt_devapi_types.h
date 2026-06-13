#ifndef NT_DEVAPI_TYPES_H
#define NT_DEVAPI_TYPES_H

/* Shared devapi value types — available WITH or WITHOUT NT_DEVAPI_ENABLED, so the
   no-op stub surface (nt_devapi_stub.h) can mirror the real API signatures exactly
   and opt-in registration code compiles unchanged at NT_DEVAPI_ENABLED=0.
   cJSON is forward-declared (signatures use cJSON* only) to keep cJSON.h out of
   zero-delta release builds. */

#include <stdbool.h>

typedef struct cJSON cJSON;

/* Error returned by a handler: a stable machine code token + a human message.
   Both point at static string literals owned by the handler/engine. */
typedef struct nt_devapi_error {
    const char *code;
    const char *message;
} nt_devapi_error;

/* Self-describing command metadata. All 7 fields are documentation strings the
   registry copies (strdup) at registration — the caller may free its buffers after.
   D-08: NO `group` field; group membership is tracked separately via register_group. */
typedef struct nt_devapi_command_desc {
    const char *method;
    const char *layer;
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
