#ifndef NT_BLOB_FORMAT_H
#define NT_BLOB_FORMAT_H

#include <stdint.h>

/* Magic: ASCII "BLOB" as uint32_t little-endian = 0x424F4C42 */
#define NT_BLOB_MAGIC 0x424F4C42
#define NT_BLOB_VERSION 1

/*
 * BlobAssetHeader -- binary header prepended to blob data in ntpack.
 *
 * Layout (8 bytes):
 *   magic(4) + version(2) + _pad(2)
 *
 * After header: raw blob data (size determined by NtAssetEntry.size - 8).
 * Interpretation of blob data is entirely game-defined.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;   /* NT_BLOB_MAGIC */
    uint16_t version; /* NT_BLOB_VERSION */
    uint16_t _pad;    /* alignment padding */
} NtBlobAssetHeader;
#pragma pack(pop)

_Static_assert(sizeof(NtBlobAssetHeader) == 8, "NtBlobAssetHeader must be 8 bytes");

#endif /* NT_BLOB_FORMAT_H */
