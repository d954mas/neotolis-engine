#include "fs/nt_fs.h"

#include <stdlib.h>
#include <string.h>

/* ---- Backend function (implemented in web/native/stub .c) ---- */

extern void nt_fs_backend_read(uint16_t slot_index, const char *path);

/* ---- Slot data ---- */

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint16_t generation;
    uint8_t state; /* nt_fs_state_t */
    uint8_t _pad;
} NtFsSlot;

/* ---- Module state ---- */

static struct {
    NtFsSlot slots[NT_FS_MAX_REQUESTS + 1]; /* index 0 reserved */
    uint16_t free_queue[NT_FS_MAX_REQUESTS];
    uint16_t queue_top;
    bool initialized;
} s_fs;

/* ---- Handle encoding: lower 16 bits = slot index, upper 16 bits = generation ---- */

static inline nt_fs_request_t fs_make(uint16_t index, uint16_t gen) { return (nt_fs_request_t){.id = ((uint32_t)gen << 16) | index}; }

static inline uint16_t fs_slot_index(nt_fs_request_t r) { return (uint16_t)(r.id & 0xFFFF); }

static inline uint16_t fs_generation(nt_fs_request_t r) { return (uint16_t)(r.id >> 16); }

/* ---- Handle validation ---- */

static NtFsSlot *fs_validate(nt_fs_request_t req) {
    uint16_t index = fs_slot_index(req);
    uint16_t gen = fs_generation(req);
    if (index == 0 || index > NT_FS_MAX_REQUESTS) {
        return NULL;
    }
    if (s_fs.slots[index].generation != gen) {
        return NULL; /* stale handle */
    }
    return &s_fs.slots[index];
}

/* ---- Lifecycle ---- */

nt_result_t nt_fs_init(void) {
    if (s_fs.initialized) {
        return NT_ERR_INIT_FAILED;
    }

    memset(&s_fs, 0, sizeof(s_fs));

    /* Fill free queue: stack with lowest index on top (first alloc gets 1) */
    s_fs.queue_top = NT_FS_MAX_REQUESTS;
    for (uint16_t i = 0; i < NT_FS_MAX_REQUESTS; i++) {
        s_fs.free_queue[i] = (uint16_t)(NT_FS_MAX_REQUESTS - i);
    }

    s_fs.initialized = true;
    return NT_OK;
}

void nt_fs_shutdown(void) {
    /* Free any remaining slot data */
    for (uint16_t i = 1; i <= NT_FS_MAX_REQUESTS; i++) {
        if (s_fs.slots[i].data != NULL) {
            free(s_fs.slots[i].data);
        }
    }
    memset(&s_fs, 0, sizeof(s_fs));
}

/* ---- Request management ---- */

nt_fs_request_t nt_fs_read_file(const char *path) {
    if (!s_fs.initialized || path == NULL || s_fs.queue_top == 0) {
        return NT_FS_REQUEST_INVALID;
    }

    /* Allocate slot from free queue */
    s_fs.queue_top--;
    uint16_t index = s_fs.free_queue[s_fs.queue_top];

    NtFsSlot *slot = &s_fs.slots[index];

    /* Increment generation (skip 0: reserved for invalid handles) */
    slot->generation++;
    if (slot->generation == 0) {
        slot->generation = 1;
    }

    slot->data = NULL;
    slot->size = 0;
    slot->state = (uint8_t)NT_FS_STATE_PENDING;
    slot->_pad = 0;

    nt_fs_backend_read(index, path);

    return fs_make(index, slot->generation);
}

nt_fs_state_t nt_fs_state(nt_fs_request_t req) {
    NtFsSlot *slot = fs_validate(req);
    if (!slot) {
        return NT_FS_STATE_NONE;
    }
    return (nt_fs_state_t)slot->state;
}

uint8_t *nt_fs_take_data(nt_fs_request_t req, uint32_t *out_size) {
    NtFsSlot *slot = fs_validate(req);
    if (!slot || slot->state != (uint8_t)NT_FS_STATE_DONE) {
        if (out_size) {
            *out_size = 0;
        }
        return NULL;
    }

    uint8_t *ptr = slot->data;
    uint32_t sz = slot->size;
    slot->data = NULL;
    slot->size = 0;

    if (out_size) {
        *out_size = sz;
    }
    return ptr;
}

void nt_fs_free(nt_fs_request_t req) {
    uint16_t index = fs_slot_index(req);
    NtFsSlot *slot = fs_validate(req);
    if (!slot) {
        return;
    }

    /* Free any remaining data */
    if (slot->data != NULL) {
        free(slot->data);
        slot->data = NULL;
    }

    /* Clear slot state */
    slot->size = 0;
    slot->state = (uint8_t)NT_FS_STATE_NONE;
    slot->_pad = 0;

    /* Increment generation so stale handles are rejected */
    slot->generation++;
    if (slot->generation == 0) {
        slot->generation = 1;
    }

    /* Return slot to free queue */
    s_fs.free_queue[s_fs.queue_top] = index;
    s_fs.queue_top++;
}

/* ---- Backend access (for native/web callbacks) ---- */

NtFsSlot *nt_fs_get_slot(uint16_t slot_index) {
    if (slot_index == 0 || slot_index > NT_FS_MAX_REQUESTS) {
        return NULL;
    }
    return &s_fs.slots[slot_index];
}
