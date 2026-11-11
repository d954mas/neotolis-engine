#ifndef NT_BLOB_H
#define NT_BLOB_H

#include "core/nt_types.h"

typedef struct {
    uint32_t id;
} nt_blob_t;
typedef struct {
    const uint8_t *data;
    uint32_t size;
} nt_blob_view_t;

/* Independent fixed-capacity pool. Zero capacity, double init and OOM assert. */
void nt_blob_init(uint16_t max_blobs);
/* After nt_resource_shutdown when BLOB packs were registered: unmount deactivates blobs. */
void nt_blob_shutdown(void);
/* Copies caller bytes; NULL is allowed only for size 0. Capacity/OOM assert. */
nt_blob_t nt_blob_create(const void *data, uint32_t size);
/* Borrowed immutable view until destroy/shutdown. Empty objects have {NULL, 0}.
 * Invalid or stale handles assert. */
nt_blob_view_t nt_blob_read(nt_blob_t blob);
/* Frees the copy. Zero is a no-op; other invalid or stale handles assert. */
void nt_blob_destroy(nt_blob_t blob);

/* Resource activation adapters. Common header validation belongs to the caller. */
uint32_t nt_blob_activate(const uint8_t *data, uint32_t size);
void nt_blob_deactivate(uint32_t handle);

#endif
