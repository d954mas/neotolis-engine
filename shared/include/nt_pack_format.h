#ifndef NT_PACK_FORMAT_H
#define NT_PACK_FORMAT_H

#include <stdint.h>

/*
 * NEOPAK binary pack format -- shared between builder (native) and runtime (WASM).
 * All multi-byte fields are little-endian.
 * See docs/neotolis_engine_spec_1.md S19.2 for the canonical layout.
 */

/* Magic: ASCII "NPAK" read as uint32_t little-endian = 0x4B41504E */
#define NT_PACK_MAGIC 0x4B41504E
#define NT_PACK_VERSION 1
#define NT_PACK_VERSION_MAX 1
#define NT_PACK_ALIGN 4

/* Round size up to NT_PACK_ALIGN boundary */
#define NT_PACK_ALIGN_UP(size) (((size) + (NT_PACK_ALIGN - 1)) & ~(NT_PACK_ALIGN - 1))

/* Asset type enum (uint8_t compatible) */
typedef enum {
    NT_ASSET_MESH = 1,
    NT_ASSET_TEXTURE = 2,
    NT_ASSET_SHADER = 3,
} nt_asset_type_t;

/*
 * PackHeader: 24 bytes total (packed).
 * magic(4) + version(2) + pack_id(4) + asset_count(2) +
 * header_size(4) + total_size(4) + checksum(4) = 24
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;       /* NT_PACK_MAGIC ("NPAK") */
    uint16_t version;     /* NT_PACK_VERSION */
    uint32_t pack_id;     /* unique pack identifier */
    uint16_t asset_count; /* number of assets */
    uint32_t header_size; /* offset where data region starts */
    uint32_t total_size;  /* total file size in bytes */
    uint32_t checksum;    /* CRC32 of data after header */
} NtPackHeader;
#pragma pack(pop)

/*
 * AssetEntry: 16 bytes total (packed).
 * resource_id(4) + asset_type(1) + format_version(2) +
 * _pad(1) + offset(4) + size(4) = 16
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t resource_id;    /* FNV-1a hash of asset path */
    uint8_t asset_type;      /* nt_asset_type_t */
    uint16_t format_version; /* per-asset-type format version */
    uint8_t _pad;            /* explicit padding for alignment */
    uint32_t offset;         /* byte offset from file start */
    uint32_t size;           /* asset data size in bytes */
} NtAssetEntry;
#pragma pack(pop)

_Static_assert(sizeof(NtPackHeader) == 24, "PackHeader must be 24 bytes");
_Static_assert(sizeof(NtAssetEntry) == 16, "AssetEntry must be 16 bytes");

#endif /* NT_PACK_FORMAT_H */
