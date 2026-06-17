#ifndef NT_DEVAPI_INTERNAL_H
#define NT_DEVAPI_INTERNAL_H

/* Internal registry layout, shared by the registry store and the
   dispatch/discovery code. Not part of the public API. */

#include "devapi/nt_devapi.h"

/* Stable machine error tokens, shared by the dispatch core and discovery handlers. */
#define NT_DEVAPI_ERR_BAD_PARAMS "bad_params"
#define NT_DEVAPI_ERR_UNKNOWN_METHOD "unknown_method"

/* cJSON_Add{String,Number,Bool}ToObject wrappers that assert success — OOM traps
   (fail-early) instead of silently producing an incomplete response. */
void devapi_add_string(cJSON *obj, const char *key, const char *value);
void devapi_add_number(cJSON *obj, const char *key, double value);
void devapi_add_bool(cJSON *obj, const char *key, bool value);

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

/* Upper bound for frame.wait{frames}. Independent of the slot-count cap: a single long wait
   holds ONE slot for its whole duration, so a ceiling of a few thousand frames is correct, not
   the concurrent-slot limit. Override per build with -D. */
#ifndef NT_DEVAPI_FRAME_WAIT_MAX
#define NT_DEVAPI_FRAME_WAIT_MAX 4096
#endif

/* Upper bound for time.step{count} (lockstep crunch). Bot input must fail fast over this, never
   queue an unbounded backlog — cJSON clamps huge JSON numbers to INT_MAX, so without a cap one
   line could wedge the host for billions of frames. Generous for long deterministic crunches yet
   far below INT_MAX, so nt_app_step's saturate guard is only a backstop. Override per build with -D. */
#ifndef NT_DEVAPI_STEP_MAX
#define NT_DEVAPI_STEP_MAX 1048576
#endif

/* One pending deferred response. The slot owns the duplicated request_id and the
   continuation state ONLY — never a pointer into the shared s_resp_buf. */
typedef struct nt_devapi_deferred_slot {
    cJSON *id;             /* owned duplicate of request_id (number or string); NULL if absent. */
    uint32_t target_frame; /* yields once g_nt_app.frame reaches this (wrap-safe compare). */
    bool in_use;
} nt_devapi_deferred_slot;

/* Mark the in-flight command as deferred: submit() returns NULL and the response is yielded once
   g_nt_app.frame has advanced `frames` game frames past submit. Returns true so the handler returns
   normally (the bool ABI is unchanged). Must be called from inside a handler dispatch. */
bool nt_devapi_defer_current(int frames);

/* Free any owned deferred-slot ids + clear the queue. Called from shutdown alongside
   nt_devapi_resp_reset so init->shutdown->init stays leak-free. */
void nt_devapi_deferred_reset(void);

// #endregion

/* True between init and shutdown — lets submit enforce init-before-use. */
bool nt_devapi_initialized(void);

/* Registry-table accessors (used by the dispatch core + discovery handlers). */
int nt_devapi_registry_count(void);
const nt_devapi_slot *nt_devapi_registry_slot(int index);
const nt_devapi_slot *nt_devapi_registry_find(const char *method);

/* True if slot[index]'s group first appears at index — lets `features` / engine.info emit
   each distinct group once, derived from the registered commands. */
bool nt_devapi_group_is_first(int index);

/* Engine `core` group registrar (per-group #ifdef). Defined in
   nt_devapi_core.c, invoked from nt_devapi_init under the same compile gate. */
#ifdef NT_DEVAPI_REGISTER_core
void nt_devapi_register_core(void);
#endif

/* Engine `time`/`render`/`frame` group registrar (per-group #ifdef). Defined in
   nt_devapi_time.c, invoked from nt_devapi_init under the same compile gate. */
#ifdef NT_DEVAPI_REGISTER_time
void nt_devapi_register_time(void);
#endif

/* Discovery group registrar — always-on (not behind an #ifdef). Defined in
   nt_devapi_discovery.c, invoked from nt_devapi_init. */
void nt_devapi_register_discovery(void);

#endif /* NT_DEVAPI_INTERNAL_H */
