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

Next nt_resource_step():
  → nt_http_update() delivers the completed response into a C buffer
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

The private `NtPackMeta` layout lives in
[`nt_resource_internal.h`](../../../engine/resource/nt_resource_internal.h).
It stores per-pack I/O progress (`bytes_received`, `bytes_total`), request linkage,
retry state, blob residency, and the activation cursor described below.

`meta_data` is copied out of the pack blob at parse time so metadata queries survive blob eviction. `retry_*`, `io_type`, and `load_path` drive both normal retry/backoff and immediate aux-miss reloads. `blob_last_access_ms` + `blob_ttl_ms` implement `NT_BLOB_AUTO` eviction. `blob_pins` is the per-pack aggregate pin count that gates eviction for zero-copy consumers. Explicit unmount overrides pins and clears their borrowed views before freeing the blob — see [Resource System — blob pinning](resource.md#blob-pinning) for the full lifecycle.

## HTTP requests — nt_http contract

`engine/http` is a general HTTP client (swappable: web `fetch()` / native libcurl
multi / stub). `nt_http_request(url)` is the GET shorthand;
`nt_http_request_ex(url, opts)` adds method, body (copied at call time),
request-header pairs, an optional `content_type` (defaulted to
`application/octet-stream` when a body is present and no Content-Type pair was
given), and `timeout_ms`. A body on GET/HEAD is asserted out, as are the
fetch()-forbidden methods CONNECT/TRACE/TRACK — backends would diverge
otherwise.

`timeout_ms` limits the whole request. `idle_timeout_ms` instead limits how long
`nt_http_update()` sees no new response-body bytes. Both are per request, default to 0
(disabled) and can be used together. Connection setup counts as idle, so no
separate connect timeout exists. Only
`nt_http_update()` judges idleness, after the backend has taken everything that
arrived: the clock starts on the first update after the request is issued and
restarts whenever the received byte count changes. Headers alone do not restart
it; upload and server think time count as idle. Whatever the backend has taken
by that update counts as progress, including bytes or a completion that arrived
during a long frame. Native reads the socket inside the update, so a slow frame
never discards bytes the server sent. Web sees only what the fetch() handlers
have already recorded, and after a long frame the browser may run the frame
before those handlers. A request with no new
bytes for the limit becomes FAILED in that update; the log includes the full URL,
received bytes and threshold. A frame gap longer than the limit still fails a
request that had nothing new by then, including after a suspended page resumes;
the caller's retry handles that. Progress counts the same bytes as
`nt_http_progress()`: wire bytes on native, decoded bytes on web.

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
**FAILED = transport error, timeout, idle stall or cancel** (a status may still be recorded).
`nt_http_response_headers()` returns lowercased `"name: value\n"` lines, valid
until `nt_http_free`/`nt_http_shutdown`. `nt_http_update()` advances
transfers on native and web (no-op on stub) — call it once per frame while requests are in
flight; `nt_resource_step()` calls it too, and the pump is global, so it
advances the game's own requests as well (see
[frame lifecycle](../runtime/frame-lifecycle.md)).

Connections are kept alive and reused across requests, as `fetch()` does. A
request sent on a reused connection that the server closed before any response
arrived may be sent again on a new connection, on both backends; an endpoint that
must not act twice needs its own request id.

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
| Connection pool | browser-managed (about 6 per host on HTTP/1.1, one on HTTP/2) | The vendored build uses HTTP/1.1. The connection cache retains up to `NT_HTTP_MAX_REQUESTS` connections across hosts after transfers complete, so a burst to one host can reuse the previous burst's connections. This is not a hard limit on all open connections: transfers to other hosts may temporarily exceed it. An idle connection is not reused past libcurl's idle limit (`CURLOPT_MAXAGE_CONN`) and closes on a later request or at shutdown. |

Progress numbers are transport-level best effort while DOWNLOADING on both
backends; at DONE both report `received == total ==` decoded size.

Web bridge (EM_JS in `engine/http/web/nt_http_web.c`, all called from C):

```c
// Starts fetch(); request parameters read from the slot
void nt_http_web_fetch(int slot, const char *url, const char *method,
                       const uint8_t *body, int body_size, const char *headers,
                       int headers_size, int timeout_ms);
// out: received, total, status, header block size with its NUL (0 = none yet);
// returns the nt_http_state_t
int nt_http_web_poll(int slot, uint32_t *out);
// Copies the body and header block into C buffers (NULL drops), drops the record
void nt_http_web_take(int slot, uint8_t *data, char *hdrs);
void nt_http_web_cancel(int slot);
```

Promise handlers only fill a JS record per slot; wasm is never entered from
them. `nt_http_update()` polls the in-flight slots, allocates the body and
header buffers in C and takes the JS record. A C fault while delivering
therefore happens inside the game's frame: it escapes the requestAnimationFrame
callback, which stops the frame loop like any other crash, and reaches the
window `error` handler, where `nt_crash` records it once initialized. A JS
record lives exactly while its C slot is in flight (take, cancel and shutdown
drop it), so late handlers of a cancelled fetch write only to the dropped
record. The handle generation rejects stale game handles across slot reuse and
shutdown/init.

## Asset activation strategy

**Eager with a time budget:** `nt_resource_step()` visits packs by registry index,
then their canonical owners by asset index. Only READY packs with resident bytes
and REGISTERED owners are eligible. Parsing requires an activator for every
non-BLOB file type, configured before the first mount. At least one eligible
activation is attempted per step; a zero budget is unlimited. Aliases consume no
activation attempts and read owner state directly, regardless of which names
the game requested. BLOB owners become READY during parse. Success and failure
both dirty resolve. Failed activation requires explicit type invalidation or
unmount/remount to retry; restoring evicted bytes alone does not reset its state.

Each pack retains an activation cursor into the asset registry. A budget stop
leaves it on the eligible owner not yet attempted; a completed scan leaves it at
the current asset high-water mark. Subsequent steps skip that completed prefix.
Invalidating file owners rewinds their packs; a new mount starts at zero.
Restoring an evicted blob preserves the cursor because it preserves asset states.
Growing the global registry may make completed packs scan its new suffix once.
The activation phase of a stable idle registry needs only the bounded pack walk.
Cursor movement alone does not dirty resolve or change the publication epoch.
Callbacks follow the [resource callback contract](resource.md#resource-callback-contract).

An expired, unpinned AUTO blob remains resident while its activation cursor is
below the asset high-water mark. A budget stop leaves the cursor before the
pending owner, so eviction cannot remove bytes needed on a later step. Types
cannot gain activators after mounting, so a completed cursor cannot conceal
owners waiting for late registration. READY and FAILED owners do not themselves
hold the blob. The cursor check may conservatively retain a completed pack while
other registry growth remains unscanned. Explicit unmount still removes a pack
with pending activation. There is no additional owner scan or pending counter.

Any change that can affect publication (`mount`, `unmount`, `set_priority`, asset activation, virtual register/unregister, invalidation, placeholder change, or aux-miss reload scheduling) marks the registry dirty. Dirty frames run a resolve scan over assets to compute each slot's target winner and published winner. Clean frames stay on the O(1) fast path.

If `on_post_resolve` work creates new dependent slots (for example atlas page textures), `resource_step()` may execute additional resolve passes in the same frame. The total pass count is bounded to avoid infinite loops.

## Retry policy

Normal load failures use 1-2 retries with exponential backoff. After retries fail: PackState = FAILED, log error, game code decides response (show error, retry later).

**Known spec/code divergence:** the policy above is bounded, but `nt_resource_init`
currently sets `retry_max_attempts = 0`, which means unlimited attempts.
`nt_resource_set_retry_policy` can set a finite total attempt limit, including the
initial attempt; `1` disables retries. The default policy still needs a separate
decision; callers needing bounded retries must configure that limit explicitly.

`nt_resource_set_http_idle_timeout(ms)` sets the `idle_timeout_ms` of every HTTP
pack request issued afterwards, retries included (default 0, disabled). A stalled
pack request then fails like any other load failure and follows this policy, so a
stalled pack retries without limit unless the game also bounds `max_attempts`.

Aux-miss reloads (target winner requires aux data but its blob was evicted) reuse the same I/O path, but schedule an immediate retry on the next `resource_step()` instead of waiting for backoff.

## Memory note

Peak memory during loading = 2x pack size (JS fetch buffer + WASM heap copy). For packs in the low megabytes range this is acceptable.
