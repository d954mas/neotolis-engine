#ifndef NT_DEVAPI_H
#define NT_DEVAPI_H

/* Self-describing external command surface. Dev-only: the whole layer is gated
   by NT_DEVAPI_ENABLED and excluded from release builds at the CMake level. */

/* Must come from the nt_devapi target compile-defines; consumers without the link see stubs that mismatch ABI. */
#ifndef NT_DEVAPI_ENABLED
#error "NT_DEVAPI_ENABLED not defined — link against nt_devapi via target_link_libraries(<target> PUBLIC|PRIVATE nt_devapi)"
#endif

#include <stdbool.h>

#include "cJSON.h"
#include "core/nt_types.h"

/* Registry table cap. Dev-only, linear-scanned — 64 covers engine + game groups. */
#ifndef NT_DEVAPI_MAX_COMMANDS
#define NT_DEVAPI_MAX_COMMANDS 64
#endif

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

/* Lifecycle. init returns NT_OK once; a second init without shutdown returns NT_ERR_INIT_FAILED. */
nt_result_t nt_devapi_init(void);
void nt_devapi_shutdown(void);

/* Register a command. Copies all 7 descriptor strings (caller buffers need not
   outlive the call). Returns NT_OK on success, NT_ERR_INIT_FAILED if a command
   with the same method is already registered (dup_method — not overwritten). */
nt_result_t nt_devapi_register(const nt_devapi_command_desc *desc, nt_devapi_handler_fn handler, void *user_data);

/* Record one group-name string (e.g. "core") for the `features` discovery list. */
nt_result_t nt_devapi_register_group(const char *group_name);

/* Submit one JSON request line, returns the JSON response line.
   LIFETIME: the returned pointer is valid only until the next nt_devapi_submit
   call — the caller MUST consume or copy it before calling submit again.
   (Defined in Plan 02; declared here so the contract is single-sourced.) */
const char *nt_devapi_submit(const char *line);

#endif /* NT_DEVAPI_H */
