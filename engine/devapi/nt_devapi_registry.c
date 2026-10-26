#include <string.h>

#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"

/* Dev-only registry: fixed table, linear scan. Not a frame-hot path. */

static nt_devapi_slot s_slots[NT_DEVAPI_MAX_COMMANDS];
static int s_count;

static bool s_initialized;

// #region lifecycle hooks
/* Two ordered phases run every update: the transport poll (recv + enqueue) before the tick (the input
   schedule release), so a line received this tick is scheduled before the tick reads it, whatever the
   registration order. One slot each: native net is the only polling transport, input the only ticker.
   Reset hooks fire on a client drop. */
#ifndef NT_DEVAPI_MAX_HOOKS
#define NT_DEVAPI_MAX_HOOKS 8
#endif

static nt_devapi_hook_fn s_transport_hook;
static nt_devapi_hook_fn s_tick_hook;
static nt_devapi_hook_fn s_reset_hooks[NT_DEVAPI_MAX_HOOKS];
static int s_reset_count;

/* Idempotent, so a transport stop->start re-sets the same poll. */
void nt_devapi_set_transport_poll(nt_devapi_hook_fn fn) {
    NT_ASSERT(fn != NULL && (s_transport_hook == NULL || s_transport_hook == fn));
    s_transport_hook = fn;
}

void nt_devapi_set_tick(nt_devapi_hook_fn fn) {
    NT_ASSERT(fn != NULL && s_tick_hook == NULL);
    s_tick_hook = fn;
}

void nt_devapi_register_reset(nt_devapi_hook_fn fn) {
    NT_ASSERT(fn != NULL && s_reset_count < NT_DEVAPI_MAX_HOOKS);
    s_reset_hooks[s_reset_count++] = fn;
}

void nt_devapi_run_transport_and_tick(void) {
    if (s_transport_hook != NULL) {
        s_transport_hook();
    }
    if (s_tick_hook != NULL) {
        s_tick_hook();
    }
}

void nt_devapi_run_reset_hooks(void) {
    for (int i = 0; i < s_reset_count; i++) {
        s_reset_hooks[i]();
    }
}

// #endregion

nt_result_t nt_devapi_init(void) {
    if (s_initialized) {
        return NT_ERR_INIT_FAILED;
    }
    s_count = 0;
    s_transport_hook = NULL; /* groups re-register their lifecycle hooks when a host registers them. */
    s_tick_hook = NULL;
    s_reset_count = 0;
    s_initialized = true;

    /* No group wiring here: init builds only the framework. A host registers the groups it wants
       AFTER init — nt_devapi_register_default() for all compiled-in, or specific
       nt_devapi_register_<group>() calls for a minimal surface (see nt_devapi_groups.h). */
    return NT_OK;
}

void nt_devapi_shutdown(void) {
    s_count = 0;
    s_transport_hook = NULL; /* drop group hooks so init->shutdown->init re-registers cleanly. */
    s_tick_hook = NULL;
    s_reset_count = 0;
    nt_devapi_resp_reset(); /* symmetric teardown of the dispatch-core response buffer. */
    s_initialized = false;
}

static void register_one(const nt_devapi_command *cmd, void *user_data) {
    NT_ASSERT(cmd->method != NULL && cmd->summary != NULL && cmd->params_shape != NULL && cmd->result_shape != NULL && cmd->fn != NULL);
    NT_ASSERT(nt_devapi_registry_find(cmd->method) == NULL); /* a duplicate method is a registration bug */
    s_slots[s_count++] = (nt_devapi_slot){.cmd = cmd, .user_data = user_data};
}

void nt_devapi_register(const nt_devapi_command *cmds, size_t count, void *user_data) {
    NT_ASSERT(s_initialized && cmds != NULL && count <= (size_t)(NT_DEVAPI_MAX_COMMANDS - s_count));
    for (size_t i = 0; i < count; i++) {
        register_one(&cmds[i], user_data);
    }
}

bool nt_devapi_initialized(void) { return s_initialized; }

int nt_devapi_registry_count(void) { return s_count; }

const nt_devapi_slot *nt_devapi_registry_slot(int index) {
    NT_ASSERT(index >= 0 && index < s_count);
    return &s_slots[index];
}

const nt_devapi_slot *nt_devapi_registry_find(const char *method) {
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_slots[i].cmd->method, method) == 0) {
            return &s_slots[i];
        }
    }
    return NULL;
}
