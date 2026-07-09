#ifndef NT_DEVAPI_H
#define NT_DEVAPI_H

/* Self-describing external command surface. Dev-only: the whole layer is gated
   by NT_DEVAPI_ENABLED and excluded from release builds at the CMake level. */

/* Must come from the nt_devapi target compile-defines; consumers without the link see stubs that mismatch ABI. */
#ifndef NT_DEVAPI_ENABLED
#error "NT_DEVAPI_ENABLED not defined — link against nt_devapi via target_link_libraries(<target> PUBLIC|PRIVATE nt_devapi)"
#endif

#include "cJSON.h"
#include "core/nt_types.h"
#include "devapi/nt_devapi_groups.h" /* host-facing register_<group>() / register_default surface. */
#include "devapi/nt_devapi_types.h"  /* nt_devapi_error / _command_desc / _handler_fn (shared with the stub) */

/* Registry table cap. Dev-only static slots; lookup scans s_count, not the cap.
   512 leaves wide headroom for engine + game groups (~18KB BSS wasm32, dev-only). */
#ifndef NT_DEVAPI_MAX_COMMANDS
#define NT_DEVAPI_MAX_COMMANDS 512
#endif

/* Lifecycle. init returns NT_OK once; a second init without shutdown returns NT_ERR_INIT_FAILED. */
nt_result_t nt_devapi_init(void);
void nt_devapi_shutdown(void);

/* Register a command. desc, handler, and all 7 descriptor strings must be non-NULL.
   Copies the strings; user_data may be NULL. Non-NULL user_data is stored by
   reference and passed to the handler until nt_devapi_shutdown(); caller owns it
   and the engine never frees it. NT_ERR_INVALID_ARG if `method` is already registered —
   the dup is rejected, not overwritten. */
nt_result_t nt_devapi_register(const nt_devapi_command_desc *desc, nt_devapi_handler_fn handler, void *user_data);

/* Submit one JSON request line → the JSON response line. The returned pointer is valid
   only until the next submit/poll_response — copy it before the next core call. Returns
   NULL when the command deferred its response (drain it later via nt_devapi_poll_response). */
const char *nt_devapi_submit(const char *line);

/* Yield the next ready deferred response, or NULL if none is ready this call. The returned
   pointer is valid only until the next submit/poll_response — copy it before the next core
   call (it shares the same growing buffer as submit). Drain in a loop until it returns NULL. */
const char *nt_devapi_poll_response(void);

/* Per-tick devapi update — the game calls this once per frame. Core owns the entry; it runs only
   the registered tick hooks (each transport registers its own poll + frame-keyed deferred drain via
   the hook registry, so native net and web share this one seam). */
void nt_devapi_update(void);

#ifdef NT_DEVAPI_GROUP_UI
/* Host-facing: register a UI context under a name so the `ui` group can resolve it.
   Call AFTER nt_devapi_init(): init clears the host ctx table, so a pre-init registration is
   silently wiped (asserts when NT_ASSERT is on). `name` and `ctx` are stored BY REFERENCE (no copy):
   both must stay valid and pointer-stable for the devapi's lifetime. Trusted in-process call:
   pre-init / overflow / duplicate name asserts (not bot input). */
typedef struct nt_ui_context nt_ui_context_t;
void nt_devapi_ui_register_context(const char *name, nt_ui_context_t *ctx);
#endif

#endif /* NT_DEVAPI_H */
