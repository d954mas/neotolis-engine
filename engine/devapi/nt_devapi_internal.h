#ifndef NT_DEVAPI_INTERNAL_H
#define NT_DEVAPI_INTERNAL_H

/* Internal registry layout, shared by the registry store and the
   dispatch/discovery code. Not part of the public API. */

#include "devapi/nt_devapi.h"

/* Stable machine error tokens emitted by the generic dispatch core. Group-specific codes live in the
   group itself (passed in as err->code) — the core never names a group. */
#define NT_DEVAPI_ERR_BAD_PARAMS "bad_params"
#define NT_DEVAPI_ERR_UNKNOWN_METHOD "unknown_method"
#define NT_DEVAPI_ERR_BUSY "busy"       /* a deferred reply is pending */
#define NT_DEVAPI_ERR_STALLED "stalled" /* a deferred reply's gate can no longer open */

/* cJSON_Add{String,Number,Bool,Null}ToObject wrappers that assert success — OOM traps
   (fail-early) instead of silently producing an incomplete response. */
void devapi_add_string(cJSON *obj, const char *key, const char *value);
void devapi_add_number(cJSON *obj, const char *key, double value);
void devapi_add_bool(cJSON *obj, const char *key, bool value);
void devapi_add_null(cJSON *obj, const char *key);

/* Strict unsigned-integer param parsers: require a finite, integer-valued number in [0, max]
   (a blind cast of an out-of-range/NaN/Inf double to unsigned is UB). On violation they call
   set_bad(err, message) and return false, else write the parsed value to *out. */
typedef void (*nt_devapi_bad_params_fn)(nt_devapi_error *err, const char *message);
bool nt_devapi_parse_u32_param_exact(const cJSON *node, uint32_t max, nt_devapi_error *err, nt_devapi_bad_params_fn set_bad, const char *message, uint32_t *out);
bool nt_devapi_parse_u16_param_exact(const cJSON *node, uint16_t max, nt_devapi_error *err, nt_devapi_bad_params_fn set_bad, const char *message, uint16_t *out);

/* One registered command. The 7 descriptor strings are strdup-owned copies
   freed at shutdown; handler + user_data are stored verbatim. */
typedef struct nt_devapi_slot {
    char *method;
    char *group;
    char *summary;
    char *params_shape;
    char *result_shape;
    char *frame_behavior;
    char *side_effects;
    nt_devapi_handler_fn handler;
    void *user_data;
} nt_devapi_slot;

/* Release the response buffer (statics are file-local to nt_devapi.c). Called from
   shutdown so init->shutdown->init is leak-free. */
void nt_devapi_resp_reset(void);

// #region deferred reply
/* One command can defer its reply: submit returns NULL and poll_response yields the reply once its gate
   opens. Only one deferred reply is in flight: a command submitted while it is pending answers busy. */
typedef enum nt_devapi_gate {
    NT_DEVAPI_GATE_FRAMES,  /* g_nt_app.frame advanced by `amount` */
    NT_DEVAPI_GATE_SECONDS, /* g_nt_app.time advanced by `amount` */
    NT_DEVAPI_GATE_PRESENT, /* the next pre-swap seam (a frame is presented) */
} nt_devapi_gate;

#define NT_DEVAPI_DEFER_CTX_SIZE 24

/* Defer the in-flight command's reply. When the gate opens, `reply` runs with the handler ABI (params NULL,
   user_data = the slot's copy of ctx) and its result or error is the response; a gate that can no longer open
   (sim frozen, time mode changed, rendering off) answers stalled instead. ctx (ctx_size bytes, at most
   NT_DEVAPI_DEFER_CTX_SIZE; NULL when 0) is copied. Returns false with err set inside a batch; the handler
   returns that. Call only from a handler, before any side effect that a failed defer must not leave. */
bool nt_devapi_defer(nt_devapi_gate gate, double amount, nt_devapi_handler_fn reply, const void *ctx, size_t ctx_size, nt_devapi_error *err);

/* Build a pending PRESENT reply at the pre-swap seam (GL-valid, before swap). A group that defers on
   PRESENT wraps it with its own host-facing seam. */
void nt_devapi_run_pre_swap_reply(void);

/* Drop the pending deferred reply (client drop, web reset, shutdown). */
void nt_devapi_deferred_reset(void);
// #endregion

/* True between init and shutdown — lets submit enforce init-before-use. */
bool nt_devapi_initialized(void);

// #region lifecycle hooks
/* Lifecycle hooks let an optional group plug into the core/transport without either naming the
   group: a compiled-out group registers nothing, so the core/net TUs carry zero of its symbols. */
typedef void (*nt_devapi_hook_fn)(void);

/* Register a per-tick / client-reset hook. Cleared on shutdown, re-registered each init;
   overflow asserts (build-time bug). */
/* Two ordered phases: transport-poll hooks (recv + enqueue) run before tick hooks (schedule release).
   transport_is_registered lets a transport register its poll at most once per init (the table is
   append-only, so a stop->start restart must not duplicate it). */
void nt_devapi_register_transport_poll(nt_devapi_hook_fn fn);
void nt_devapi_register_tick(nt_devapi_hook_fn fn);
void nt_devapi_register_reset(nt_devapi_hook_fn fn);
bool nt_devapi_transport_is_registered(nt_devapi_hook_fn fn);

/* Run the transport-poll phase, then the tick phase; reset hooks fire on a client drop. */
void nt_devapi_run_transport_hooks(void);
void nt_devapi_run_tick_hooks(void);
void nt_devapi_run_reset_hooks(void);
// #endregion

/* Registry-table accessors (used by the dispatch core + discovery handlers). */
int nt_devapi_registry_count(void);
const nt_devapi_slot *nt_devapi_registry_slot(int index);
const nt_devapi_slot *nt_devapi_registry_find(const char *method);

/* True if slot[index]'s group first appears at index — lets `features` / engine.info emit
   each distinct group once, derived from the registered commands. */
bool nt_devapi_group_is_first(int index);

/* The single inject scheduler lives in the input group, but the ui group reuses it (one scheduler,
   one cap <= the immediate buffer — so two sibling groups can never over-subscribe the buffer). Its
   cap + the cross-group reuse wrappers are therefore visible when EITHER group is built. */
#endif /* NT_DEVAPI_INTERNAL_H */
