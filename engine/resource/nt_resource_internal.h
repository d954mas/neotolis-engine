#ifndef NT_RESOURCE_INTERNAL_H
#define NT_RESOURCE_INTERNAL_H

#include <stdint.h>

/* ---- Asset state machine ---- */

typedef enum {
    NT_ASSET_STATE_REGISTERED = 0, /* meta exists, data not loaded */
    NT_ASSET_STATE_LOADING,        /* being activated (GPU upload etc.) */
    NT_ASSET_STATE_READY,          /* runtime handle valid, usable */
    NT_ASSET_STATE_FAILED,         /* error, permanent, no retry */
} nt_asset_state_t;

/* ---- Pack type ---- */

typedef enum {
    NT_PACK_FILE = 0,    /* loaded from NEOPAK file */
    NT_PACK_VIRTUAL = 1, /* runtime-created resources */
} nt_pack_type_t;

/* ---- Per-asset metadata (one per asset from all packs) ---- */

typedef struct {
    uint32_t resource_id;    /* FNV-1a hash */
    uint8_t asset_type;      /* nt_asset_type_t (mesh/texture/shader) */
    uint8_t state;           /* nt_asset_state_t */
    uint16_t format_version; /* from NtAssetEntry */
    uint16_t pack_index;     /* index into packs[] array */
    uint16_t _pad;
    uint32_t offset;         /* byte offset in pack blob */
    uint32_t size;           /* asset data size in bytes */
    uint32_t runtime_handle; /* GFX handle (uint32), 0 = none */
} NtAssetMeta;

/* ---- Per-pack metadata ---- */

typedef struct {
    uint32_t pack_id;     /* FNV-1a hash of pack name/path */
    int16_t priority;     /* higher = wins on conflict, signed */
    uint8_t pack_type;    /* nt_pack_type_t */
    uint8_t mounted;      /* 1 if mounted, 0 if slot available */
    uint16_t asset_start; /* first index in assets[] array */
    uint16_t asset_count; /* number of AssetMeta entries from this pack */
    const uint8_t *blob;  /* loaded pack data (NULL until Phase 25) */
    uint32_t blob_size;   /* size of loaded blob */
} NtPackMeta;

/* ---- Per unique ResourceId requested by game ---- */

typedef struct {
    uint32_t resource_id; /* FNV-1a hash */
    uint32_t gfx_handle;  /* current best GFX handle */
    uint16_t generation;  /* for stale detection */
    uint8_t asset_type;   /* nt_asset_type_t */
    uint8_t state;        /* nt_asset_state_t of resolved entry */
} NtResourceSlot;

#endif /* NT_RESOURCE_INTERNAL_H */
