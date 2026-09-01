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

Both backends follow redirects (303 → GET on both) and negotiate compression,
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

Progress numbers are transport-level best effort while DOWNLOADING on both
backends; at DONE both report `received == total ==` decoded size.

Web bridge (EM_JS in `engine/http/web/nt_http_web.c`):

```c
// Called from C → JS (request parameters read from the slot)
void nt_http_web_fetch(int slot, int generation, int epoch, const char *url,
                       const char *method, const uint8_t *body, int body_size,
                       const char *headers, int headers_size, int timeout_ms);

// Called from JS → C (generation- AND epoch-checked against the slot)
EMSCRIPTEN_KEEPALIVE
void nt_http_web_on_progress(int slot, int generation, int epoch, int received, int total);

EMSCRIPTEN_KEEPALIVE
void nt_http_web_on_complete(int slot, int generation, int epoch, uint8_t *data, int size,
                             int status, char *resp_headers, int success);
```

`epoch` is bumped on every module init: slot generations restart after a
shutdown/init cycle, so `(slot, generation)` alone cannot reject a callback from
a fetch started in a previous lifecycle of the module — the epoch check does.

## Asset activation strategy

**Eager with rate-limit**: when a pack becomes READY, `resource_step()` processes up to N assets per frame from the ready queue. This prevents frame spikes while ensuring assets become available quickly.

Any change that can affect publication (`mount`, `unmount`, `set_priority`, asset activation, virtual register/unregister, invalidation, placeholder change, or aux-miss reload scheduling) marks the registry dirty. Dirty frames run a resolve scan over assets to compute each slot's target winner and published winner. Clean frames stay on the O(1) fast path.

If `on_post_resolve` work creates new dependent slots (for example atlas page textures), `resource_step()` may execute additional resolve passes in the same frame. The total pass count is bounded to avoid infinite loops.

## Retry policy

Normal load failures use 1-2 retries with exponential backoff. After retries fail: PackState = FAILED, log error, game code decides response (show error, retry later).

Aux-miss reloads (target winner requires aux data but its blob was evicted) reuse the same I/O path, but schedule an immediate retry on the next `resource_step()` instead of waiting for backoff.

## Memory note

Peak memory during loading = 2x pack size (JS fetch buffer + WASM heap copy). For packs in the low megabytes range this is acceptable.
