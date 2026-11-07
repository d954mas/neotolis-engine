#include "blob/nt_blob.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "nt_blob_format.h"
#include "pool/nt_pool.h"

static struct {
    nt_pool_t pool;
    nt_blob_view_t *slots;
} s_blob;

void nt_blob_init(uint16_t max_blobs) {
    NT_ASSERT(s_blob.slots == NULL && max_blobs > 0);
    nt_pool_init(&s_blob.pool, max_blobs);
    s_blob.slots = calloc((size_t)max_blobs + 1U, sizeof(*s_blob.slots));
    NT_ASSERT(s_blob.slots != NULL);
}

void nt_blob_shutdown(void) {
    if (s_blob.slots == NULL) {
        return;
    }
    for (uint32_t i = 1; i <= s_blob.pool.capacity; i++) {
        free((void *)s_blob.slots[i].data);
    }
    free(s_blob.slots);
    nt_pool_shutdown(&s_blob.pool);
    memset(&s_blob, 0, sizeof s_blob);
}

nt_blob_t nt_blob_create(const void *data, uint32_t size) {
    NT_ASSERT(s_blob.slots != NULL && (data != NULL || size == 0));
    uint32_t id = nt_pool_alloc(&s_blob.pool);
    NT_ASSERT(id != 0 && "blob pool exhausted");
    uint8_t *copy = NULL;
    if (size != 0) {
        copy = malloc(size);
        NT_ASSERT(copy != NULL);
        memcpy(copy, data, size);
    }
    s_blob.slots[nt_pool_slot_index(id)] = (nt_blob_view_t){copy, size};
    return (nt_blob_t){id};
}

nt_blob_view_t nt_blob_read(nt_blob_t blob) {
    NT_ASSERT(s_blob.slots != NULL && nt_pool_valid(&s_blob.pool, blob.id));
    return s_blob.slots[nt_pool_slot_index(blob.id)];
}

void nt_blob_destroy(nt_blob_t blob) {
    if (blob.id == 0) {
        return;
    }
    NT_ASSERT(s_blob.slots != NULL && nt_pool_valid(&s_blob.pool, blob.id));
    nt_blob_view_t *slot = &s_blob.slots[nt_pool_slot_index(blob.id)];
    free((void *)slot->data);
    *slot = (nt_blob_view_t){0};
    nt_pool_free(&s_blob.pool, blob.id);
}

uint32_t nt_blob_activate(const uint8_t *data, uint32_t size) {
    NT_ASSERT(data != NULL && size >= sizeof(NtBlobAssetHeader));
    return nt_blob_create(data + sizeof(NtBlobAssetHeader), size - (uint32_t)sizeof(NtBlobAssetHeader)).id;
}

void nt_blob_deactivate(uint32_t handle) { nt_blob_destroy((nt_blob_t){handle}); }
