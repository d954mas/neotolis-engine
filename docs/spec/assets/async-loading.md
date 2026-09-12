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
game code: nt_resource_mount(pack_id, priority)
  → nt_resource_load_url(pack_id, url)
  → HTTP wrapper starts fetch through its JS bridge

... N frames pass ...

JS completion callback
  → HTTP request stores the received WASM buffer and completion state

Next nt_resource_step():
  → polls the HTTP request and takes its completed buffer
  → first load: parses the manifest and registers assets (BLOB owners are READY)
  → reload after eviction: restores bytes for retained records without reparsing
  → pack state = READY

Asset activation (eager with a time budget):
  → visits eligible canonical owners in pack-index, then asset-index order
  → reads data from blob by offset/size
  → parses runtime format
  → calls the registered asset-type activator
  → owner state = READY on a nonzero handle, FAILED otherwise

Resolve/publication:
  → dirty slots run a resolve pass after activation / mount / unmount / priority change / invalidation
  → simple asset types publish immediately once the target winner is READY
  → aux-backed asset types run on_resolve to build per-slot user_data before publication
  → if the highest-priority target winner needs aux data but its blob is missing, the slot keeps the best usable fallback published or reports LOADING and schedules a reload
```

`nt_resource_load_url` takes a URL, `nt_resource_load_file` a filesystem path,
and `nt_resource_load_auto` picks per platform — it is the portable entry point.
There is no filesystem on web: `nt_resource_load_file` is not declared there and
the `NT_IO_FS` path is compiled out, so `load_auto` resolves to `nt_http`.

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
    uint8_t io_type;        /* NT_IO_NONE / NT_IO_FS (native only) / NT_IO_HTTP */
    uint16_t attempt_count; /* retry state */
    uint32_t retry_delay_ms;
    uint32_t retry_time_ms;
    uint32_t blob_last_access_ms;
    uint32_t blob_ttl_ms;
    uint32_t blob_pins; /* PIN_BLOB aggregate — published winners pinning this blob (O(1) Phase-C gate) */
    uint8_t blob_evict_skip_logged; /* edge-trigger for the AUTO-as-KEEP one-shot log */
    uint32_t activate_cursor; /* next registry index to inspect for this pack */
    char load_path[256];
} NtPackMeta;
```

`meta_data` is copied out of the pack blob at parse time so metadata queries survive blob eviction. `retry_*`, `io_type`, and `load_path` drive both normal retry/backoff and immediate aux-miss reloads. `blob_last_access_ms` + `blob_ttl_ms` implement `NT_BLOB_AUTO` eviction. `blob_pins` is the per-pack aggregate pin count that gates Phase-C eviction and unmount for zero-copy consumers — see [Resource System — blob pinning](resource.md) for the full lifecycle.

## HTTP requests — nt_http contract

`engine/http` is a general HTTP client (swappable: web `fetch()` / native libcurl
multi / stub). `nt_http_request(url)` is the GET shorthand;
`nt_http_request_ex(url, opts)` adds method, body (copied at call time),
request-header pairs, an optional `content_type` (defaulted to
`application/octet-stream` when a body is present and no Content-Type pair was
given), and `timeout_ms`. A body on GET/HEAD is asserted out, as are the
fetch()-forbidden methods CONNECT/TRACE/TRACK — backends would diverge
otherwise.

**Reference semantics: the web backend.** `nt_http` behaves like `fetch()`; the
native backend approximates that with libcurl and is NOT byte-identical. The
guarantees both backends share are the ones in this chapter (state semantics,
decoded bytes, transport truncation → FAILED, empty body → `take_data` NULL/0,
progress settling on the decoded size at DONE, method normalization,
copy-at-call ownership). Everything else follows fetch() only approximately;
the known divergences live in the table below. A newly found divergence is
added to the table and pinned by a test — it is not a bug unless it breaks a
shared guarantee, and it is not silently "fixed" toward either side unless
parity is cheap.

State semantics: **DONE = a full response arrived with ANY HTTP status** (a 404
body is data, not a transport error) — the caller checks `nt_http_status()`;
**FAILED = transport error, timeout or cancel** (a status may still be recorded).
`nt_http_response_headers()` returns lowercased `"name: value\n"` lines, valid
until `nt_http_free`/`nt_http_shutdown`. `nt_http_update()` pumps native
transfers (no-op on web/stub) — call it once per frame while requests are in
flight; `nt_resource_step()` calls it too, and the pump is global, so it
advances the game's own requests as well (see
[frame lifecycle](../runtime/frame-lifecycle.md)).

The pack loader treats a non-2xx status and a 2xx response with an empty body as
load failures (normal retry policy applies).

Both backends follow redirects (303 → GET on both; a GET/HEAD request keeps
its method) and negotiate compression,
handing the caller DECODED bytes — the browser's `fetch()` transparently, the
native backend via `CURLOPT_ACCEPT_ENCODING` with curl's gzip/deflate decoders
(vendored `deps/zlib`, native exe only).

### Web/native divergences (canonical list)

| Area | Web (reference) | Native (libcurl) |
| --- | --- | --- |
| 301/302 of a bodied non-POST (PUT/PATCH+body) | keeps method and body | re-issues as GET (`CURLFOLLOW_OBEYCODE`: anything sent via POSTFIELDS is curl POST mode) |
| Corrupt gzip that still satisfies Content-Length | fetch FAILs (`ERR_CONTENT_DECODING_FAILED`) | tolerated: DONE with partial decoded bytes (curl upstream behavior for broken servers) |
| Response header block shape | duplicates combined into one `", "`-joined line, names sorted, `Set-Cookie` hidden | wire order, one line per header |
| Request header validation | forbidden names (Host, Cookie, Origin, ...) silently dropped, CORS applies, CR/LF in a value throws → FAILED | sent verbatim, no validation |
| obs-fold continuation lines (legacy servers) | browser unfolds them | folded line is dropped or emitted as a garbage header line |
| Mid-transfer progress numbers | decoded stream bytes vs raw Content-Length | wire (possibly compressed) bytes |
| Relative URL (`"/path"`) | resolved against the page origin | no base URL — the request FAILs |

Progress numbers are transport-level best effort while DOWNLOADING on both
backends; at DONE both report `received == total ==` decoded size.

Web bridge (EM_JS in `engine/http/web/nt_http_web.c`):

```c
// Called from C → JS (request parameters read from the slot)
void nt_http_web_fetch(int slot, int generation, const char *url,
                       const char *method, const uint8_t *body, int body_size,
                       const char *headers, int headers_size, int timeout_ms);

// Called from JS → C (generation-checked against the slot)
EMSCRIPTEN_KEEPALIVE
void nt_http_web_on_progress(int slot, int generation, int received, int total);

EMSCRIPTEN_KEEPALIVE
void nt_http_web_on_complete(int slot, int generation, uint8_t *data, int size,
                             int status, char *resp_headers, int success);
```

The `(slot, generation)` pair is the single staleness mechanism: freeing a slot
bumps its generation, and `nt_http_shutdown` bumps and PRESERVES every
generation across shutdown/init, so a callback from a fetch started in a
previous lifecycle of the module always mismatches (its payload is freed on
rejection).

## Asset activation strategy

**Eager with a time budget:** `nt_resource_step()` visits packs by registry index,
then their canonical owners by asset index. Only READY packs with resident bytes
and REGISTERED owners with an activator are eligible. At least one eligible
activation is attempted per step; a zero budget is unlimited. Aliases consume no
activation attempts and read owner state directly, regardless of which names
the game requested. BLOB owners become READY during parse. Success and failure
both dirty resolve. Failed activation requires explicit type invalidation or
unmount/remount to retry; restoring evicted bytes alone does not reset its state.

Each pack retains an activation cursor into the asset registry. A budget stop
leaves it on the eligible owner not yet attempted; a completed scan leaves it at
the current asset high-water mark. Subsequent steps skip that completed prefix.
Invalidating file owners rewinds their packs, and registering a previously absent
activator rewinds all packs so skipped types become eligible. A new mount starts
at zero. Restoring an evicted blob preserves the cursor because it preserves asset
states. Growing the global registry may make completed packs scan its new suffix
once; a stable idle registry needs only the bounded pack walk, not asset scans.
Cursor movement alone does not dirty resolve or change the publication epoch.
Callbacks follow the [resource callback contract](resource.md#resource-callback-contract).

Any change that can affect publication (`mount`, `unmount`, `set_priority`, asset activation, virtual register/unregister, invalidation, placeholder change, or aux-miss reload scheduling) marks the registry dirty. Dirty frames run a resolve scan over assets to compute each slot's target winner and published winner. Clean frames stay on the O(1) fast path.

If `on_post_resolve` work creates new dependent slots (for example atlas page textures), `resource_step()` may execute additional resolve passes in the same frame. The total pass count is bounded to avoid infinite loops.

## Retry policy

Normal load failures use 1-2 retries with exponential backoff. After retries fail: PackState = FAILED, log error, game code decides response (show error, retry later).

Aux-miss reloads (target winner requires aux data but its blob was evicted) reuse the same I/O path, but schedule an immediate retry on the next `resource_step()` instead of waiting for backoff.

## Memory note

Peak memory during loading = 2x pack size (JS fetch buffer + WASM heap copy). For packs in the low megabytes range this is acceptable.
