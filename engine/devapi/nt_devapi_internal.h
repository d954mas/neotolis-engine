#ifndef NT_DEVAPI_INTERNAL_H
#define NT_DEVAPI_INTERNAL_H

/* Internal registry layout, shared by the registry store and the
   dispatch/discovery code. Not part of the public API. */

#include "devapi/nt_devapi.h"

/* Stable machine error tokens emitted by the generic dispatch core. Group-specific codes live in the
   group itself (passed in as err->code / a slot fail_code) — the core never names a group. */
#define NT_DEVAPI_ERR_BAD_PARAMS "bad_params"
#define NT_DEVAPI_ERR_UNKNOWN_METHOD "unknown_method"

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

// #region deferred queue
/* Preallocated deferred-response queue cap (static, heap-free; overflow rejected fail-early).
   Override per build with -DNT_DEVAPI_MAX_DEFERRED=N. */
#ifndef NT_DEVAPI_MAX_DEFERRED
#define NT_DEVAPI_MAX_DEFERRED 128
#endif

/* A pre-swap payload producer: invoked at the pre-swap seam to build the slot's owned result object.
   Returns an owned cJSON* (the result payload) or NULL on failure (a DATA slot then yields its
   command-supplied fail_code). `ctx` is the producer-owned param block; the core never inspects it. */
typedef cJSON *(*nt_devapi_payload_producer_fn)(void *ctx);

/* Frees a producer's owned ctx block. Called exactly once per slot (on yield or reset); NULL when
   the ctx is not heap-owned. Distinct from nt_devapi_hook_fn — it takes the ctx pointer. */
typedef void (*nt_devapi_ctx_free_fn)(void *ctx);

/* One pending deferred response. The slot owns the duplicated request_id and the
   continuation state ONLY — never a pointer into the shared s_resp_buf. */
typedef struct nt_devapi_deferred_slot {
    /* 8-byte members first (pointers + double) so the trailing small fields pack with minimal padding. */
    cJSON *id;          /* owned duplicate of request_id (number or string); NULL if absent. */
    double target_time; /* by_time == true: yields once g_nt_app.time reaches this (game seconds). */
    /* Result-payload continuation (producer-bearing commands). producer fills `payload` at the pre-swap
       seam; the core names no group — the producer + fail strings are supplied by the handler. */
    nt_devapi_payload_producer_fn producer;  /* NULL for legacy frame.wait/time.step slots. */
    void *producer_ctx;                      /* owned param block; freed via producer_ctx_free. */
    nt_devapi_ctx_free_fn producer_ctx_free; /* frees producer_ctx on yield/reset; NULL if ctx is unowned. */
    cJSON *payload;                          /* owned serialized producer result; NULL until the seam fills it. */
    const char *fail_code;                   /* DATA slot: wire error code + message the core yields if the */
    const char *fail_msg;                    /* producer returns NULL — command-supplied, so the core names no group. */
    uint32_t target_frame;                   /* by_time == false: yields once g_nt_app.frame reaches this (wrap-safe). */
    bool by_time;                            /* selects which deadline field slot_ready compares. */
    bool in_use;
    bool is_data; /* a DATA (producer-bearing) slot: outlives `producer` (cleared at the seam) so a
                     producer-ran-but-failed slot yields its fail_code, not the legacy {deferred:true}. */
} nt_devapi_deferred_slot;

/* Mark the in-flight command as deferred: submit() returns NULL and the response is yielded once
   g_nt_app.frame has advanced `frames` game frames past submit. Returns true so the handler returns
   normally (the bool ABI is unchanged). Must be called from inside a handler dispatch. */
bool nt_devapi_defer_current(int frames);

/* Preflight for handlers that mutate state before deferring. */
bool nt_devapi_can_defer_current(void);

/* Like nt_devapi_defer_current but on a GAME-TIME deadline: yields once g_nt_app.time has advanced
   `seconds` past submit. For time.wait (RUN, where dt is variable so a frame count can't be
   precomputed). Must be called from inside a handler dispatch. */
bool nt_devapi_defer_current_time(double seconds);

/* Like nt_devapi_defer_current but ASSOCIATES a result producer with the slot: at the pre-swap seam the
   producer fills the slot's owned payload, which poll_response then yields as the ok-entry result (instead
   of the legacy {deferred:true}). `ctx`/`ctx_free` are the producer's owned param block + its destructor
   (NULL if unowned). `fail_code`/`fail_msg` are the wire error the core yields if the producer returns NULL
   — static literals supplied by the handler, so the core names no group (zero release delta when absent).
   Must be called from inside a handler dispatch.

   CONTRACT: a producer-bearing slot is withheld until the pre-swap seam fills it — poll_response never
   yields {deferred:true} for it (that would re-introduce the drain race). So the HOST MUST drive the
   pre-swap seam on every rendered frame; while rendering is suppressed the slot legitimately stalls until
   render resumes (bounded per-session by close_client -> nt_devapi_deferred_reset). */
bool nt_devapi_defer_current_with_result(int frames, nt_devapi_payload_producer_fn producer, void *ctx, nt_devapi_ctx_free_fn ctx_free, const char *fail_code, const char *fail_msg);

/* Run every ready producer-slot's producer at the pre-swap seam (fills the slot's payload). The generic
   core entry; a producer-bearing group wraps it with its own host-facing seam. */
void nt_devapi_run_pre_swap_producers(void);

/* Free any owned deferred-slot ids + clear the queue. Called from shutdown alongside
   nt_devapi_resp_reset so init->shutdown->init stays leak-free. */
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
