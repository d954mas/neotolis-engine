#ifndef NT_FS_INTERNAL_H
#define NT_FS_INTERNAL_H

#include "fs/nt_fs.h"

/* ---- Slot data (single definition, shared by core + backends) ---- */

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint16_t generation;
    uint8_t state; /* nt_fs_state_t */
    uint8_t _pad;
} NtFsSlot;

/* ---- Backend access ---- */

NtFsSlot *nt_fs_get_slot(uint16_t slot_index);

/* ---- Backend functions (implemented per platform) ---- */

void nt_fs_backend_read(uint16_t slot_index, const char *path);

#endif /* NT_FS_INTERNAL_H */
