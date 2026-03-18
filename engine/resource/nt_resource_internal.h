#ifndef NT_RESOURCE_INTERNAL_H
#define NT_RESOURCE_INTERNAL_H

#include "resource/nt_resource.h"

#include <stdint.h>

/* ---- Asset state machine ---- */

typedef enum {
    NT_ASSET_STATE_REGISTERED = 0, /* meta exists, data not loaded */
    NT_ASSET_STATE_FAILED,         /* error, permanent, no retry */
    NT_ASSET_STATE_LOADING,        /* being activated (GPU upload etc.) */
    NT_ASSET_STATE_READY,          /* runtime handle valid, usable */
} nt_asset_state_t;

/* ---- Pack type ---- */

typedef enum {
    NT_PACK_FILE = 0,    /* loaded from NEOPAK file */
    NT_PACK_VIRTUAL = 1, /* runtime-created resources */
} nt_pack_type_t;

/* ---- Blob retention policy ---- */

typedef enum {
    NT_BLOB_KEEP = 0, /* blob lives as long as pack is mounted */
    NT_BLOB_AUTO = 1, /* auto-evict after TTL since last access */
} nt_blob_policy_t;

/* ---- I/O request type ---- */

typedef enum {
    NT_IO_NONE = 0,
    NT_IO_FS = 1,
    NT_IO_HTTP = 2,
} nt_io_type_t;

/* ---- Activator callbacks ---- */

#define NT_RESOURCE_MAX_ASSET_TYPES 8

typedef struct {
    nt_activate_fn activate;
    nt_deactivate_fn deactivate;
} NtActivatorEntry;

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
    uint32_t pack_id;    /* FNV-1a hash of pack name/path */
    int16_t priority;    /* higher = wins on conflict, signed */
    uint8_t pack_type;   /* nt_pack_type_t */
    uint8_t mounted;     /* 1 if mounted, 0 if slot available */
    uint16_t mount_seq;  /* monotonic mount order for tiebreak */
    uint8_t pack_state;  /* nt_pack_state_t */
    uint8_t blob_policy; /* nt_blob_policy_t */
    const uint8_t *blob; /* loaded pack data */
    uint32_t blob_size;  /* size of loaded blob */
    /* Loading progress */
    uint32_t bytes_received;
    uint32_t bytes_total;
    /* I/O request linkage */
    uint32_t io_request_id; /* nt_http or nt_fs handle.id */
    uint8_t io_type;        /* nt_io_type_t */
    /* Retry state */
    uint8_t attempt_count;
    uint16_t _pad2;
    uint32_t retry_delay_ms;
    uint32_t retry_time_ms; /* wall clock ms of next retry */
    /* Blob eviction */
    uint32_t blob_last_access_ms;
    uint32_t blob_ttl_ms;
    /* Original load path for retry and re-download after invalidation */
    char load_path[256];
} NtPackMeta;

/* ---- Per unique ResourceId requested by game ---- */

typedef struct {
    uint32_t resource_id;    /* FNV-1a hash */
    uint32_t runtime_handle; /* current best resolved handle */
    uint16_t generation;     /* for stale detection */
    int16_t resolve_prio;    /* priority of current winner; Phase 25: use for O(1) activation */
    uint8_t asset_type;      /* nt_asset_type_t */
    uint8_t state;           /* nt_asset_state_t of resolved entry */
    uint16_t resolve_seq;    /* mount_seq of current winner; tiebreak + Phase 25 O(1) activation */
} NtResourceSlot;

#endif /* NT_RESOURCE_INTERNAL_H */
