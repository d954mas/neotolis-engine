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

// #region deferred queue (D-09)
/* Preallocated deferred-response queue cap. Small + static: devapi is low-frequency,
   so a steady-state heap-free queue is enough. Overflow is rejected (fail-early). */
#ifndef NT_DEVAPI_MAX_DEFERRED
#define NT_DEVAPI_MAX_DEFERRED 16
#endif

/* One pending deferred response. The slot owns the duplicated request_id and the
   continuation state ONLY — never a pointer into the shared s_resp_buf (Pitfall 4 / D-04). */
typedef struct nt_devapi_deferred_slot {
    cJSON *id;       /* owned duplicate of request_id (number or string); NULL if absent. */
    int frames_left; /* continuation: yields when it reaches 0. */
    bool in_use;
} nt_devapi_deferred_slot;

/* Mark the in-flight command as deferred: submit() returns NULL and the response is
   yielded after `frames_left` poll_response() calls. Returns true so the handler returns
   normally (the bool ABI is unchanged). Must be called from inside a handler dispatch. */
bool nt_devapi_defer_current(int frames_left);

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

/* Discovery group registrar — always-on (not behind an #ifdef). Defined in
   nt_devapi_discovery.c, invoked from nt_devapi_init. */
void nt_devapi_register_discovery(void);

#endif /* NT_DEVAPI_INTERNAL_H */
