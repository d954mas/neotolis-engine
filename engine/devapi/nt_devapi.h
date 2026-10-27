#ifndef NT_DEVAPI_H
#define NT_DEVAPI_H

/* Self-describing external command surface. Dev-only: the whole layer is gated
   by NT_DEVAPI_ENABLED and excluded from release builds at the CMake level. */

/* Must come from the nt_devapi target compile-defines: the header is meaningless without the link. */
#ifndef NT_DEVAPI_ENABLED
#error "NT_DEVAPI_ENABLED not defined — link against nt_devapi via target_link_libraries(<target> PUBLIC|PRIVATE nt_devapi)"
#endif

#include "cJSON.h"
#include "core/nt_types.h"
#include "devapi/nt_devapi_groups.h" /* host-facing register_<group>() / register_default surface. */

#define NT_DEVAPI_ERR_BAD_PARAMS "bad_params"

/* Error returned by a handler: a stable machine code token + a human message. */
typedef struct nt_devapi_error {
    const char *code;
    const char *message;
} nt_devapi_error;

/* Fill err and return false, so a failing handler is one statement: return nt_devapi_fail(err, "...");
   nt_devapi_fail answers bad_params; nt_devapi_fail_code names another code. The dispatcher copies both
   strings into the response as soon as the handler returns. */
static inline bool nt_devapi_fail_code(nt_devapi_error *err, const char *code, const char *message) {
    err->code = code;
    err->message = message;
    return false;
}

static inline bool nt_devapi_fail(nt_devapi_error *err, const char *message) { return nt_devapi_fail_code(err, NT_DEVAPI_ERR_BAD_PARAMS, message); }

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

/* Registry table cap. Dev-only static slots; lookup scans s_count, not the cap.
   512 leaves wide headroom for engine + game groups (~4KB BSS wasm32, dev-only). */
#ifndef NT_DEVAPI_MAX_COMMANDS
#define NT_DEVAPI_MAX_COMMANDS 512
#endif

/* Lifecycle. init returns NT_OK once; a second init without shutdown returns NT_ERR_INIT_FAILED. */
nt_result_t nt_devapi_init(void);
void nt_devapi_shutdown(void);

/* Register a table of commands; every handler gets user_data (may be NULL). The table, its strings and
   user_data are stored by reference until nt_devapi_shutdown(): keep them static (a table on the stack
   dangles once its function returns); the engine never frees them. Every field is required; a method
   already registered asserts. */
void nt_devapi_register(const nt_devapi_command *cmds, size_t count, void *user_data);

/* Submit one JSON request line → the JSON response line. The returned pointer is valid
   only until the next submit/poll_response — copy it before the next core call. Returns
   NULL when the command deferred its response (drain it later via nt_devapi_poll_response).
   While a deferred response is pending, every well-formed command for a registered method answers busy. */
const char *nt_devapi_submit(const char *line);

/* Yield the next ready deferred response, or NULL if none is ready this call. The returned
   pointer is valid only until the next submit/poll_response, which frees it — copy it before the
   next core call. Drain in a loop until it returns NULL. */
const char *nt_devapi_poll_response(void);

/* Per-tick devapi update — the game calls this once per frame. It checks the pending deferred reply's
   gate, then runs the transport poll (native net's recv + deferred drain), then the tick hook (the
   input schedule release). */
void nt_devapi_update(void);

/* The rendered frame is readable: call it on each frame the host drew, after nt_gfx_end_frame and before
   nt_window_swap_buffers (the back buffer is undefined after the swap). It builds a pending capture reply;
   with nothing pending it returns at once. Two nt_devapi_update calls without it in between fail a pending
   capture as stalled, so a host calls update once per frame. */
void nt_devapi_frame_rendered(void);

/* Run fn whenever a client leaves: a native disconnect, a newer connection taking over, or a web
   reset(). The engine resets only devapi-owned state there; recovering game state (time mode, pause,
   queued steps) is the host's policy. Call after nt_devapi_init (shutdown clears the table); overflow asserts.
   fn runs inside a transport's close or reset: it must not call back into devapi (submit, update,
   net start/stop, web reset). */
void nt_devapi_register_reset(nt_devapi_hook_fn fn);

#ifdef NT_DEVAPI_GROUP_UI
/* Host-facing: name the UI contexts the `ui` group resolves (`ctx` param; absent = the first one).
   The table is stored BY REFERENCE and read only when a ui.* command runs, so call it at any point;
   a later call replaces it. Keep the table static. A NULL name/ctx or a duplicate name asserts. */
typedef struct nt_ui_context nt_ui_context_t;
typedef struct nt_devapi_ui_context {
    const char *name;
    nt_ui_context_t *ctx;
} nt_devapi_ui_context;
void nt_devapi_ui_set_contexts(const nt_devapi_ui_context *list, size_t count);
#endif

#endif /* NT_DEVAPI_H */
