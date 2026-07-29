# Async Loading System

All loading is potentially async on every platform (the web cannot block).
Defines the pack and asset state machines, the fetch-bridge loading flow, eager
rate-limited asset activation, dirty-slot resolve passes, retry with backoff,
and the `NtPackMeta` fields including blob eviction and pin counts.

Related: [Resource System](resource.md), [Pack Format](ntpack.md), [Platform Architecture](../runtime/platform.md)

## Overview

On the web, all data loading is asynchronous. `fetch()` returns a Promise. The main thread cannot be blocked. Loading must be non-blocking and integrated into the frame loop.

The same async contract applies to all platforms for consistency — desktop implementations may complete instantly but the API contract remains "potentially async."

## Pack state machine

```c
typedef enum {
    NT_PACK_STATE_NONE = 0,    /* not loaded */
    NT_PACK_STATE_REQUESTED,   /* I/O request issued */
    NT_PACK_STATE_DOWNLOADING, /* receiving data (progress available) */
    NT_PACK_STATE_LOADED,      /* data received, not yet parsed */
    NT_PACK_STATE_READY,       /* parsed, assets registered */
    NT_PACK_STATE_FAILED,      /* load failed (may retry) */
} nt_pack_state_t;
```

## Asset state machine

```c
typedef enum {
        NT_ASSET_STATE_REGISTERED = 0, /* meta exists, data not loaded */
        NT_ASSET_STATE_FAILED,         /* error, permanent, no retry */
        NT_ASSET_STATE_LOADING,        /* being activated; slot state may also wait for publication */
        NT_ASSET_STATE_READY,          /* runtime handle valid; for slots this means published winner fully usable */
    } nt_asset_state_t;
```

## Pack loading flow

```text
game code: pack_request_load("world.pak")
  → PackMeta.state = REQUESTED
  → platform_web calls fetch() via JS bridge

... N frames pass ...

JS callback → WASM: platform_on_fetch_complete(request_id, blob_ptr, blob_size, success)
  → PackMeta.state = LOADED
  → PackMeta.blob = blob_ptr

Next resource_step():
  → sees LOADED pack
  → parses header/manifest (NTPACK format, direct struct read)
  → registers AssetMeta entries (state = REGISTERED)
  → PackMeta.state = READY

Asset activation (eager with rate-limit):
  → resource_step() processes up to N assets per frame
  → reads data from blob by offset/size
  → parses runtime format
  → creates GPU resources / decodes audio
  → AssetState = READY

Resolve/publication:
  → dirty slots run a resolve pass after activation / mount / unmount / priority change / invalidation
  → simple asset types publish immediately once the target winner is READY
  → aux-backed asset types run on_resolve to build per-slot user_data before publication
  → if the highest-priority target winner needs aux data but its blob is missing, the slot keeps the best usable fallback published or reports LOADING and schedules a reload
```

## Loading progress

Current `NtPackMeta`:

```c
typedef struct {
    uint32_t pack_id;    /* nt_hash32 value */
    int16_t priority;    /* higher = wins on conflict */
    uint8_t pack_type;   /* NT_PACK_FILE or NT_PACK_VIRTUAL */
    uint8_t mounted;     /* 1 if slot occupied */
    uint32_t mount_seq;  /* monotonic mount order tiebreak (runtime-only, not serialized) */
    uint8_t pack_state;  /* nt_pack_state_t */
    uint8_t blob_policy; /* NT_BLOB_KEEP or NT_BLOB_AUTO */
    const uint8_t *blob; /* loaded pack bytes, may be NULL after eviction */
    uint32_t blob_size;  /* original blob size */
    uint8_t *meta_data;  /* resident metadata copy (survives blob eviction) */
    uint32_t meta_size;
    uint32_t meta_count;
    uint32_t bytes_received; /* async progress */
    uint32_t bytes_total;
    uint32_t io_request_id;
    uint8_t io_type;        /* NT_IO_NONE / NT_IO_FS / NT_IO_HTTP */
    uint16_t attempt_count; /* retry state */
    uint32_t retry_delay_ms;
    uint32_t retry_time_ms;
    uint32_t blob_last_access_ms;
    uint32_t blob_ttl_ms;
    uint32_t blob_pins; /* PIN_BLOB aggregate — published winners pinning this blob (O(1) Phase-C gate) */
    uint8_t blob_evict_skip_logged; /* edge-trigger for the AUTO-as-KEEP one-shot log */
    char load_path[256];
} NtPackMeta;
```

`meta_data` is copied out of the pack blob at parse time so metadata queries survive blob eviction. `retry_*`, `io_type`, and `load_path` drive both normal retry/backoff and immediate aux-miss reloads. `blob_last_access_ms` + `blob_ttl_ms` implement `NT_BLOB_AUTO` eviction. `blob_pins` is the per-pack aggregate pin count that gates Phase-C eviction and unmount for zero-copy consumers — see [Resource System — blob pinning](resource.md) for the full lifecycle.

## JS bridge — fetch contract

C exports:

```c
// Called from C → JS
void platform_request_fetch(uint32_t request_id, const char *url);

// Called from JS → C
EMSCRIPTEN_KEEPALIVE
void platform_on_fetch_progress(uint32_t request_id, uint32_t received, uint32_t total);

EMSCRIPTEN_KEEPALIVE
void platform_on_fetch_complete(uint32_t request_id, uint8_t *data, uint32_t size, uint32_t success);
```

## Asset activation strategy

**Eager with rate-limit**: when a pack becomes READY, `resource_step()` processes up to N assets per frame from the ready queue. This prevents frame spikes while ensuring assets become available quickly.

Any change that can affect publication (`mount`, `unmount`, `set_priority`, asset activation, virtual register/unregister, invalidation, placeholder change, or aux-miss reload scheduling) marks the registry dirty. Dirty frames run a resolve scan over assets to compute each slot's target winner and published winner. Clean frames stay on the O(1) fast path.

If `on_post_resolve` work creates new dependent slots (for example atlas page textures), `resource_step()` may execute additional resolve passes in the same frame. The total pass count is bounded to avoid infinite loops.

## Retry policy

Normal load failures use 1-2 retries with exponential backoff. After retries fail: PackState = FAILED, log error, game code decides response (show error, retry later).

Aux-miss reloads (target winner requires aux data but its blob was evicted) reuse the same I/O path, but schedule an immediate retry on the next `resource_step()` instead of waiting for backoff.

## Memory note

Peak memory during loading = 2x pack size (JS fetch buffer + WASM heap copy). For packs in the low megabytes range this is acceptable.
