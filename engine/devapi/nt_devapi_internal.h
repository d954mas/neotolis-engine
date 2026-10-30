#ifndef NT_DEVAPI_INTERNAL_H
#define NT_DEVAPI_INTERNAL_H

/* Core surface shared with the group and transport TUs. Not part of the public API. */

#include "devapi/nt_devapi.h"

/* Stable machine error tokens emitted by the generic dispatch core. Group-specific codes live in the
   group itself (passed in as err->code) — the core never names a group. */
#define NT_DEVAPI_ERR_UNKNOWN_METHOD "unknown_method"
#define NT_DEVAPI_ERR_BUSY "busy"       /* a deferred reply is pending */
#define NT_DEVAPI_ERR_STALLED "stalled" /* a deferred reply's gate can no longer open */

/* Stringize a -D cap so a message tracks it (no macro expansion inside a string literal). */
#define NT_DEVAPI_STR2(x) #x
#define NT_DEVAPI_STR(x) NT_DEVAPI_STR2(x)

/* cJSON_Add{String,Number,Bool,Null}ToObject wrappers that assert success — OOM traps
   (fail-early) instead of silently producing an incomplete response. */
void devapi_add_string(cJSON *obj, const char *key, const char *value);
void devapi_add_number(cJSON *obj, const char *key, double value);
void devapi_add_bool(cJSON *obj, const char *key, bool value);
void devapi_add_null(cJSON *obj, const char *key);

/* Strict unsigned-integer param parsers: require a finite, integer-valued number in [0, max]
   (a blind cast of an out-of-range/NaN/Inf double to unsigned is UB). On violation they fail
   with bad_params and the given message, else write the parsed value to *out. */
bool nt_devapi_parse_u32_param_exact(const cJSON *node, uint32_t max, nt_devapi_error *err, const char *message, uint32_t *out);
bool nt_devapi_parse_u53_param_exact(const cJSON *node, nt_devapi_error *err, const char *message, uint64_t *out);
bool nt_devapi_parse_u16_param_exact(const cJSON *node, uint16_t max, nt_devapi_error *err, const char *message, uint16_t *out);
/* A bot float: a number whose float form is finite (a finite double past FLT_MAX would narrow to inf
   and poison hit-tests / deltas). Fails with bad_params and message. */
bool nt_devapi_parse_f32_param(const cJSON *node, nt_devapi_error *err, const char *message, float *out);

// #region deferred reply
/* One command can defer its reply: submit returns NULL and poll_response yields the reply once its gate
   opens. Only one deferred reply is in flight: a command submitted while it is pending answers busy. */
typedef enum nt_devapi_gate {
    NT_DEVAPI_GATE_FRAMES,   /* g_nt_app.frame advanced by `amount` */
    NT_DEVAPI_GATE_SECONDS,  /* g_nt_app.time advanced by `amount` */
    NT_DEVAPI_GATE_RENDERED, /* the next nt_devapi_frame_rendered (a frame is rendered, not yet swapped) */
} nt_devapi_gate;

/* Defer the in-flight command's reply. nt_devapi_update checks the gate each tick: once it opens, `reply`
   runs with the handler ABI (params and user_data NULL) and its result or error is the response; a gate that
   can no longer open (sim frozen, time mode changed, a frame passed unrendered) answers stalled instead. A reply that
   needs input keeps it in its group: only one reply is ever pending. Returns false with err set inside a
   batch; call it before any side effect a failed defer must not leave. */
bool nt_devapi_defer(nt_devapi_gate gate, double amount, nt_devapi_handler_fn reply, nt_devapi_error *err);

/* Drop the pending deferred reply (client drop, web reset, shutdown). */
void nt_devapi_deferred_reset(void);
// #endregion

// #region lifecycle hooks
/* Lifecycle hooks let an optional group plug into the core/transport without either naming the
   group: a compiled-out group registers nothing, so the core/net TUs carry zero of its symbols. */
/* One frame poll, one transport poll and one tick, cleared on shutdown and set again after each init,
   run in that order. Reset hooks are registered through the public nt_devapi_register_reset. */
void nt_devapi_set_frame_poll(nt_devapi_hook_fn fn);
void nt_devapi_set_transport_poll(nt_devapi_hook_fn fn);
void nt_devapi_set_tick(nt_devapi_hook_fn fn);
void nt_devapi_run_reset_hooks(void);
// #endregion

#endif /* NT_DEVAPI_INTERNAL_H */
