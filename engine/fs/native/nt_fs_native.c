#include "fs/nt_fs_internal.h"

#include <stdio.h>
#include <stdlib.h>

/* Native backend — read file via fopen/fread */
void nt_fs_backend_read(uint16_t slot_index, const char *path) {
    NtFsSlot *slot = nt_fs_get_slot(slot_index);
    if (!slot) {
        return;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        slot->state = (uint8_t)NT_FS_STATE_FAILED;
        return;
    }

    /* Get file size */
    if (fseek(f, 0, SEEK_END) != 0) {
        (void)fclose(f);
        slot->state = (uint8_t)NT_FS_STATE_FAILED;
        return;
    }

    long file_size = ftell(f);
    if (file_size < 0) {
        (void)fclose(f);
        slot->state = (uint8_t)NT_FS_STATE_FAILED;
        return;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        (void)fclose(f);
        slot->state = (uint8_t)NT_FS_STATE_FAILED;
        return;
    }

    /* Allocate buffer and read */
    uint8_t *data = (uint8_t *)malloc((size_t)file_size);
    if (!data) {
        (void)fclose(f);
        slot->state = (uint8_t)NT_FS_STATE_FAILED;
        return;
    }

    size_t read_count = fread(data, 1, (size_t)file_size, f);
    (void)fclose(f);

    if (read_count != (size_t)file_size) {
        free(data);
        slot->state = (uint8_t)NT_FS_STATE_FAILED;
        return;
    }

    slot->data = data;
    slot->size = (uint32_t)file_size;
    slot->state = (uint8_t)NT_FS_STATE_DONE;
}
