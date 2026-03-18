#include "fs/nt_fs.h"

/* ---- Slot data (defined in nt_fs.c) ---- */

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint16_t generation;
    uint8_t state; /* nt_fs_state_t */
    uint8_t _pad;
} NtFsSlot;

extern NtFsSlot *nt_fs_get_slot(uint16_t slot_index);

/* Stub backend — immediately fail all requests */
void nt_fs_backend_read(uint16_t slot_index, const char *path) {
    (void)path;
    NtFsSlot *slot = nt_fs_get_slot(slot_index);
    if (slot) {
        slot->state = (uint8_t)NT_FS_STATE_FAILED;
    }
}
