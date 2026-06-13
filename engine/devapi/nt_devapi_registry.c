#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"

/* Dev-only registry: fixed table, linear scan. Not a frame-hot path (Pitfall 4). */

#ifndef NT_DEVAPI_MAX_GROUPS
#define NT_DEVAPI_MAX_GROUPS 16
#endif

static nt_devapi_slot s_slots[NT_DEVAPI_MAX_COMMANDS];
static int s_count;

static char *s_groups[NT_DEVAPI_MAX_GROUPS];
static int s_group_count;

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
    free(slot->layer);
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
    s_group_count = 0;
    s_initialized = true;
    return NT_OK;
}

void nt_devapi_shutdown(void) {
    for (int i = 0; i < s_count; i++) {
        slot_free(&s_slots[i]);
    }
    s_count = 0;
    for (int i = 0; i < s_group_count; i++) {
        free(s_groups[i]);
        s_groups[i] = NULL;
    }
    s_group_count = 0;
    s_initialized = false;
}

nt_result_t nt_devapi_register(const nt_devapi_command_desc *desc, nt_devapi_handler_fn handler, void *user_data) {
    NT_ASSERT(desc != NULL);
    NT_ASSERT(desc->method != NULL);
    NT_ASSERT(handler != NULL);

    /* dup_method is a legitimate game-author error, not a bug: reject, don't overwrite (D-06). */
    if (nt_devapi_registry_find(desc->method) != NULL) {
        return NT_ERR_INIT_FAILED;
    }

    /* Table overflow is an invariant bug at this scale — assert (T-63-02). */
    NT_ASSERT(s_count < NT_DEVAPI_MAX_COMMANDS);

    nt_devapi_slot *slot = &s_slots[s_count];
    slot->method = devapi_strdup(desc->method);
    slot->layer = devapi_strdup(desc->layer);
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

nt_result_t nt_devapi_register_group(const char *group_name) {
    NT_ASSERT(group_name != NULL);
    NT_ASSERT(s_group_count < NT_DEVAPI_MAX_GROUPS);
    s_groups[s_group_count] = devapi_strdup(group_name);
    s_group_count++;
    return NT_OK;
}

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

int nt_devapi_group_count(void) { return s_group_count; }

const char *nt_devapi_group_name(int index) {
    NT_ASSERT(index >= 0 && index < s_group_count);
    return s_groups[index];
}
