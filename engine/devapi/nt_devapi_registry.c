#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"

/* Dev-only registry: fixed table, linear scan. Not a frame-hot path. */

static nt_devapi_slot s_slots[NT_DEVAPI_MAX_COMMANDS];
static int s_count;

static bool s_initialized;

/* Local strdup: portable under strict C17 (POSIX strdup not guaranteed declared). */
static char *devapi_strdup(const char *src) {
    if (src == NULL) {
        return NULL;
    }
    size_t len = strlen(src) + 1U;
    char *dst = (char *)malloc(len);
    NT_ASSERT(dst != NULL);
    memcpy(dst, src, len);
    return dst;
}

static void slot_free(nt_devapi_slot *slot) {
    free(slot->method);
    free(slot->group);
    free(slot->summary);
    free(slot->params_shape);
    free(slot->result_shape);
    free(slot->frame_behavior);
    free(slot->side_effects);
    *slot = (nt_devapi_slot){0};
}

nt_result_t nt_devapi_init(void) {
    if (s_initialized) {
        return NT_ERR_INIT_FAILED;
    }
    s_count = 0;
    s_initialized = true;

    /* Wire compiled-in engine groups; each is opt-in via its NT_DEVAPI_REGISTER_<group> define. */
#ifdef NT_DEVAPI_REGISTER_core
    nt_devapi_register_core();
#endif
#ifdef NT_DEVAPI_REGISTER_time
    nt_devapi_register_time();
#endif
#ifdef NT_DEVAPI_REGISTER_input
    nt_devapi_register_input();
#endif

    /* Discovery is always-on when devapi is built. */
    nt_devapi_register_discovery();

    return NT_OK;
}

void nt_devapi_shutdown(void) {
    for (int i = 0; i < s_count; i++) {
        slot_free(&s_slots[i]);
    }
    s_count = 0;
    nt_devapi_resp_reset(); /* symmetric teardown of the dispatch-core response buffer. */
    s_initialized = false;
}

nt_result_t nt_devapi_register(const nt_devapi_command_desc *desc, nt_devapi_handler_fn handler, void *user_data) {
    /* Preconditions (caller bugs): initialized + desc/handler/all-7-fields non-NULL — a NULL
       field would strdup to NULL and silently vanish from the self-describing surface. */
    NT_ASSERT(s_initialized && desc != NULL && handler != NULL && desc->method != NULL && desc->group != NULL && desc->summary != NULL && desc->params_shape != NULL && desc->result_shape != NULL &&
              desc->frame_behavior != NULL && desc->side_effects != NULL);

    /* dup_method is a legitimate game-author error, not a bug: reject, don't overwrite. INVALID_ARG (not
       INIT_FAILED) so a caller can tell "bad method arg" apart from "devapi double-init". */
    if (nt_devapi_registry_find(desc->method) != NULL) {
        return NT_ERR_INVALID_ARG;
    }

    /* Table overflow is an invariant bug at this scale — assert. */
    NT_ASSERT(s_count < NT_DEVAPI_MAX_COMMANDS);

    nt_devapi_slot *slot = &s_slots[s_count];
    slot->method = devapi_strdup(desc->method);
    slot->group = devapi_strdup(desc->group);
    slot->summary = devapi_strdup(desc->summary);
    slot->params_shape = devapi_strdup(desc->params_shape);
    slot->result_shape = devapi_strdup(desc->result_shape);
    slot->frame_behavior = devapi_strdup(desc->frame_behavior);
    slot->side_effects = devapi_strdup(desc->side_effects);
    slot->handler = handler;
    slot->user_data = user_data;
    s_count++;
    return NT_OK;
}

bool nt_devapi_initialized(void) { return s_initialized; }

int nt_devapi_registry_count(void) { return s_count; }

const nt_devapi_slot *nt_devapi_registry_slot(int index) {
    NT_ASSERT(index >= 0 && index < s_count);
    return &s_slots[index];
}

const nt_devapi_slot *nt_devapi_registry_find(const char *method) {
    if (method == NULL) {
        return NULL;
    }
    for (int i = 0; i < s_count; i++) {
        if (s_slots[i].method != NULL && strcmp(s_slots[i].method, method) == 0) {
            return &s_slots[i];
        }
    }
    return NULL;
}

/* True if slot[index]'s group has not appeared in any earlier slot — lets `features` and
   engine.info emit each distinct group once, derived from the commands (no separate list). */
bool nt_devapi_group_is_first(int index) {
    NT_ASSERT(index >= 0 && index < s_count);
    /* Every slot's group is non-NULL (nt_devapi_register asserts all fields), so no NULL guards. */
    const char *group = s_slots[index].group;
    for (int i = 0; i < index; i++) {
        if (strcmp(s_slots[i].group, group) == 0) {
            return false;
        }
    }
    return true;
}
