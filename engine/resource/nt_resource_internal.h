#ifndef NT_RESOURCE_INTERNAL_H
#define NT_RESOURCE_INTERNAL_H

#include "resource/nt_resource.h"

#include <stdint.h>

/* ---- Asset state machine ----
 * Numeric order matters: resolve uses (state > scan_state) to pick the
 * best state across multiple assets for one slot.  READY > LOADING >
 * FAILED > REGISTERED — so a LOADING asset beats FAILED, etc.
 * Do not reorder without updating the comparison in resource_resolve_pass(). */

typedef enum {
    NT_ASSET_STATE_REGISTERED = 0, /* meta exists, data not loaded */
    NT_ASSET_STATE_FAILED,         /* error, permanent, no retry */
    NT_ASSET_STATE_LOADING,        /* being activated (GPU upload etc.) */
    NT_ASSET_STATE_READY,          /* runtime handle valid, usable */
} nt_asset_state_t;

_Static_assert(NT_ASSET_STATE_READY > NT_ASSET_STATE_LOADING && NT_ASSET_STATE_LOADING > NT_ASSET_STATE_FAILED && NT_ASSET_STATE_FAILED > NT_ASSET_STATE_REGISTERED,
               "nt_asset_state_t ordering is load-bearing — resolve uses numeric comparison");

/* ---- Pack type ---- */

typedef enum {
    NT_PACK_FILE = 0,    /* loaded from NTPACK file */
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
    nt_resolve_fn on_resolve;
    nt_cleanup_fn on_cleanup;
    nt_post_resolve_fn on_post_resolve;
    uint8_t behavior_flags;
} NtActivatorEntry;

/* ---- Per-asset metadata (one per asset from all packs) ---- */

typedef struct {
    uint64_t resource_id;    /* nt_hash64 value */
    uint32_t offset;         /* byte offset in pack blob */
    uint32_t size;           /* asset data size in bytes */
    uint32_t runtime_handle; /* GFX handle (uint32), 0 = none */
    uint16_t format_version; /* from NtAssetEntry */
    uint16_t pack_index;     /* index into packs[] array */
    uint8_t asset_type;      /* nt_asset_type_t (mesh/texture/shader) */
    uint8_t state;           /* nt_asset_state_t */
    uint8_t is_dedup;        /* 1 = shares data with another asset in same pack (same offset+size) */
    uint8_t _pad;
    uint32_t meta_offset; /* byte offset into pack's meta_data buffer (NT_NO_METADATA = absent) */
} NtAssetMeta;

#define NT_NO_METADATA UINT32_MAX

/* ---- Per-pack metadata ---- */

typedef struct {
    uint32_t pack_id;    /* nt_hash32 value of pack name/path */
    int16_t priority;    /* higher = wins on conflict, signed */
    uint8_t pack_type;   /* nt_pack_type_t */
    uint8_t mounted;     /* 1 if mounted, 0 if slot available */
    uint16_t mount_seq;  /* monotonic mount order for tiebreak */
    uint8_t pack_state;  /* nt_pack_state_t */
    uint8_t blob_policy; /* nt_blob_policy_t */
    const uint8_t *blob; /* loaded pack data */
    uint32_t blob_size;  /* size of loaded blob */
    /* Metadata section (resident, survives blob eviction) */
    uint8_t *meta_data;  /* heap-copied meta section (NULL if no metadata) */
    uint32_t meta_size;  /* total bytes in meta section */
    uint32_t meta_count; /* number of NtMetaEntryHeader records */
    /* Loading progress */
    uint32_t bytes_received;
    uint32_t bytes_total;
    /* I/O request linkage */
    uint32_t io_request_id; /* nt_http or nt_fs handle.id */
    uint8_t io_type;        /* nt_io_type_t */
    uint8_t _pad2;
    /* Retry state */
    uint16_t attempt_count;
    uint32_t retry_delay_ms;
    uint32_t retry_time_ms; /* wall clock ms of next retry */
    /* Blob eviction */
    uint32_t blob_last_access_ms;
    uint32_t blob_ttl_ms;
    /* Original load path for retry and re-download after invalidation */
    char load_path[256];
} NtPackMeta;

/* ---- Per unique ResourceId requested by game ---- */

/* Persistent per-slot state — survives across frames.
 * One entry per unique resource_id requested by the game.
 * Tracks the currently published winner and auxiliary data. */
typedef struct {
    uint64_t resource_id;            /* nt_hash64 value */
    uint32_t runtime_handle;         /* published winner's runtime handle (what game sees) */
    uint16_t generation;             /* stale-handle detection; incremented on slot reuse */
    int16_t resolve_prio;            /* priority of currently published winner */
    uint16_t resolve_seq;            /* mount_seq of published winner (tiebreak) */
    uint16_t resolve_asset_idx;      /* index into assets[] of published winner */
    uint16_t prev_resolve_asset_idx; /* previous published winner (change detection) */
    uint16_t user_data_asset_idx;    /* asset idx last used to build user_data (aux sync check) */
    uint32_t prev_runtime_handle;    /* previous published handle (detect re-activation) */
    uint8_t asset_type;              /* nt_asset_type_t */
    uint8_t state;                   /* nt_asset_state_t visible to game code */
    uint8_t _pad[2];
    void *user_data; /* per-slot auxiliary data (on_resolve/on_cleanup) */
} NtResourceSlot;

/* Transient per-slot state — only valid during resource_resolve_pass().
 * Reset at the start of each pass, consumed by the end.
 * Lives in a separate static array to keep NtResourceSlot small for
 * the common case (resolve runs only when needs_resolve is true). */
typedef struct {
    uint32_t target_runtime_handle;    /* best READY asset handle, even if blob is evicted */
    uint32_t candidate_runtime_handle; /* best READY asset handle that is publishable now */
    int16_t target_prio;               /* priority of target winner */
    int16_t candidate_prio;            /* priority of publishable candidate */
    uint16_t target_seq;               /* mount_seq of target winner */
    uint16_t candidate_seq;            /* mount_seq of publishable candidate */
    uint16_t target_asset_idx;         /* assets[] index of target winner */
    uint16_t candidate_asset_idx;      /* assets[] index of publishable candidate */
    uint8_t scan_state;                /* best nt_asset_state_t seen among all matching assets */
    uint8_t resolve_pending;           /* on_resolve should fire for the published winner */
    uint8_t post_resolve_pending;      /* on_post_resolve should fire after the pass */
    uint8_t _pad;
} NtResolveTemp;

#endif /* NT_RESOURCE_INTERNAL_H */
