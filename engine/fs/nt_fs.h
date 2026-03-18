#ifndef NT_FS_H
#define NT_FS_H

#include "core/nt_types.h"

#ifndef NT_FS_MAX_REQUESTS
#define NT_FS_MAX_REQUESTS 8
#endif

typedef struct {
    uint32_t id;
} nt_fs_request_t;

#define NT_FS_REQUEST_INVALID ((nt_fs_request_t){0})

typedef enum {
    NT_FS_STATE_NONE = 0,
    NT_FS_STATE_PENDING,
    NT_FS_STATE_DONE,
    NT_FS_STATE_FAILED,
} nt_fs_state_t;

nt_result_t nt_fs_init(void);
void nt_fs_shutdown(void);
nt_fs_request_t nt_fs_read_file(const char *path);
nt_fs_state_t nt_fs_state(nt_fs_request_t req);
uint8_t *nt_fs_take_data(nt_fs_request_t req, uint32_t *out_size);
void nt_fs_free(nt_fs_request_t req);

#endif /* NT_FS_H */
