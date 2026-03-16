#ifndef NT_PACK_FORMAT_H
#define NT_PACK_FORMAT_H

#include <stdint.h>

/*
 * ntpack binary pack format -- shared between builder (native) and runtime (WASM).
 * All multi-byte fields are little-endian.
 * See docs/neotolis_engine_spec_1.md S19.2 for the canonical layout.
 */

/* Magic: ASCII "NPAK" read as uint32_t little-endian = 0x4B41504E */
#define NT_PACK_MAGIC 0x4B41504E
#define NT_PACK_VERSION 1
#define NT_PACK_VERSION_MAX 1
/*
 * Alignment constants for zero-copy access.
 *
 * When runtime loads a .ntpack blob it casts pointers directly into the data:
 *   const NtMeshAssetHeader* mesh = (NtMeshAssetHeader*)(blob + entry->offset);
 *
 * Unaligned offset would be UB on strict-alignment platforms and slower
 * everywhere else. WASM is tolerant to misalignment today, but we align
 * anyway for correctness, portability, and to match the spec (S19.2).
 *
 * NT_PACK_DATA_ALIGN (8) — padding between header/entries region and first
 *   asset data. Ensures data region starts at 8-byte boundary.
 * NT_PACK_ASSET_ALIGN (4) — padding between individual assets inside the
 *   data region. 4 bytes covers our largest primitive (uint32_t).
 *
 * align must be a power of two.
 */
#define NT_PACK_ASSET_ALIGN 4
#define NT_PACK_DATA_ALIGN 8

#define NT_PACK_ALIGN_UP(size, align) (((size) + ((align) - 1)) & ~((align) - 1))

/* Asset type enum (uint8_t compatible) */
typedef enum {
    NT_ASSET_MESH = 1,
    NT_ASSET_TEXTURE = 2,
    NT_ASSET_SHADER_CODE = 3, /* individual VS or FS source */
} nt_asset_type_t;

/*
 * PackHeader: 24 bytes total (packed, naturally aligned).
 * magic(4) + pack_id(4) + version(2) + asset_count(2) +
 * header_size(4) + total_size(4) + checksum(4) = 24
 *
 * Fields ordered so every field sits on its natural alignment boundary
 * (uint32 at 4-byte, uint16 at 2-byte). This avoids slow unaligned
 * access when casting directly from a loaded blob pointer.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;       /* 0:  NT_PACK_MAGIC ("NPAK") */
    uint32_t pack_id;     /* 4:  unique pack identifier */
    uint16_t version;     /* 8:  NT_PACK_VERSION */
    uint16_t asset_count; /* 10: number of assets */
    uint32_t header_size; /* 12: offset where data region starts */
    uint32_t total_size;  /* 16: total file size in bytes */
    uint32_t checksum;    /* 20: CRC32 of data after header */
} NtPackHeader;
#pragma pack(pop)

/*
 * AssetEntry: 16 bytes total (packed, naturally aligned).
 * resource_id(4) + format_version(2) + asset_type(1) +
 * _pad(1) + offset(4) + size(4) = 16
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t resource_id;    /* 0:  hash of asset path (algorithm TBD in nt_hash) */
    uint16_t format_version; /* 4:  per-asset-type format version */
    uint8_t asset_type;      /* 6:  nt_asset_type_t */
    uint8_t _pad;            /* 7:  explicit padding */
    uint32_t offset;         /* 8:  byte offset from file start */
    uint32_t size;           /* 12: asset data size in bytes */
} NtAssetEntry;
#pragma pack(pop)

_Static_assert(sizeof(NtPackHeader) == 24, "PackHeader must be 24 bytes");
_Static_assert(sizeof(NtAssetEntry) == 16, "AssetEntry must be 16 bytes");

#endif /* NT_PACK_FORMAT_H */
