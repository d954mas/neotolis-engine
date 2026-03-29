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
#define NT_PACK_VERSION 2
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
    NT_ASSET_BLOB = 4,        /* generic binary data (game-defined) */
    NT_ASSET_FONT = 5,        /* font glyph data (Slug format) */
} nt_asset_type_t;

/*
 * PackHeader: 32 bytes total (packed, 8-byte aligned for NtAssetEntry that follows).
 * magic(4) + meta_count(4) + version(2) + asset_count(2) +
 * header_size(4) + total_size(4) + checksum(4) + meta_offset(4) + _pad(4) = 32
 *
 * Fields ordered so every field sits on its natural alignment boundary
 * (uint32 at 4-byte, uint16 at 2-byte). This avoids slow unaligned
 * access when casting directly from a loaded blob pointer.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;       /* 0:  NT_PACK_MAGIC ("NPAK") */
    uint32_t meta_count;  /* 4:  number of NtMetaEntryHeader records (0 = no metadata) */
    uint16_t version;     /* 8:  NT_PACK_VERSION */
    uint16_t asset_count; /* 10: number of assets */
    uint32_t header_size; /* 12: offset where data region starts */
    uint32_t total_size;  /* 16: total file size in bytes */
    uint32_t checksum;    /* 20: CRC32 of data after header */
    uint32_t meta_offset; /* 24: byte offset from file start to meta section (0 = no meta) */
    uint32_t _pad;        /* 28: align to 32 bytes (NtAssetEntry requires 8-byte alignment) */
} NtPackHeader;
#pragma pack(pop)

/*
 * AssetEntry: 24 bytes total (packed).
 * resource_id(8) + offset(4) + size(4) + format_version(2) +
 * asset_type(1) + _pad(1) + meta_offset(4) = 24
 */
#pragma pack(push, 1)
typedef struct {
    uint64_t resource_id;    /* 0:  nt_hash64 of asset path */
    uint32_t offset;         /* 8:  byte offset from file start */
    uint32_t size;           /* 12: asset data size in bytes */
    uint16_t format_version; /* 16: per-asset-type format version */
    uint8_t asset_type;      /* 18: nt_asset_type_t */
    uint8_t _pad;            /* 19: explicit padding */
    uint32_t meta_offset;    /* 20: byte offset from file start to first meta entry for this asset (0 = no meta) */
} NtAssetEntry;
#pragma pack(pop)

_Static_assert(sizeof(NtPackHeader) == 32, "PackHeader must be 32 bytes");
_Static_assert(sizeof(NtAssetEntry) == 24, "AssetEntry must be 24 bytes");

/* MetaEntry header: variable-size, 20 bytes header + size bytes data.
 * Written contiguously per-asset in meta section. */
#pragma pack(push, 1)
typedef struct {
    uint64_t resource_id; /* 0:  which asset this metadata belongs to */
    uint64_t kind;        /* 8:  hash64 of metadata type name */
    uint32_t size;        /* 16: payload size in bytes (max 256 per D-12) */
    /* uint8_t data[size] follows immediately */
} NtMetaEntryHeader;
#pragma pack(pop)
_Static_assert(sizeof(NtMetaEntryHeader) == 20, "NtMetaEntryHeader must be 20 bytes");

#endif /* NT_PACK_FORMAT_H */
