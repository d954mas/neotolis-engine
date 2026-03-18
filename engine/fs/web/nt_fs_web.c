#include "fs/nt_fs_internal.h"

/* No file system on web in Phase 25 — immediately fail */
void nt_fs_backend_read(uint16_t slot_index, const char *path) {
    (void)path;
    NtFsSlot *slot = nt_fs_get_slot(slot_index);
    if (slot) {
        slot->state = (uint8_t)NT_FS_STATE_FAILED;
    }
}
